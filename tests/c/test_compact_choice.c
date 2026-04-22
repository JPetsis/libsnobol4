/*
 * test_compact_choice.c - Compact choice stack and write-log mechanism tests
 *
 * Tests cover:
 *   - Write-log entry tracking and deduplication
 *   - Compact choice record size calculation
 *   - Mode consistency (legacy vs compact) for correctness
 *   - Memory footprint reduction for patterns with many choice points
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>  // for setenv/unsetenv

#include "snobol/vm.h"

/* External test framework functions */
extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

/* ── Minimal VM builder (mirrors test_backtracking.c) ────────────────────── */

static VM make_vm(const uint8_t *bc, size_t bc_len, const char *subject)
{
    VM vm;
    memset(&vm, 0, sizeof(vm));

    vm.bc = bc;
    vm.bc_len = bc_len;

    vm.s = subject;
    vm.len = strlen(subject);

    vm.ip = 0;
    vm.pos = 0;
    vm.var_count = 0;

    return vm;
}

static bool cap_equals(const VM *vm, uint8_t reg, const char *subject, const char *expected)
{
    if (reg >= MAX_CAPS) return false;
    size_t a = vm->cap_start[reg];
    size_t b = vm->cap_end[reg];

    if (expected == NULL) {
        return a == 0 && b == 0;
    }

    if (b < a) return false;
    size_t n = b - a;
    return strlen(expected) == n && memcmp(subject + a, expected, n) == 0;
}

/* ── Bytecode builder helpers ───────────────────────────────────────────── */

typedef struct {
    uint8_t bc[1024];
    size_t ip;
    size_t lit_pool;
} Bc;

static void bc_init(Bc *b)
{
    memset(b, 0, sizeof(*b));
    b->ip = 0;
    b->lit_pool = 256;
}

static uint32_t bc_add_lit(Bc *b, const char *bytes, size_t len)
{
    uint32_t off = (uint32_t)b->lit_pool;
    memcpy(b->bc + b->lit_pool, bytes, len);
    b->lit_pool += len;
    return off;
}

static void bc_emit_u8(Bc *b, uint8_t v)  { b->bc[b->ip++] = v; }
static void bc_emit_u16(Bc *b, uint16_t v) {
    b->bc[b->ip++] = (uint8_t)((v >> 8) & 0xFF);
    b->bc[b->ip++] = (uint8_t)(v & 0xFF);
}
static void bc_emit_u32(Bc *b, uint32_t v) {
    b->bc[b->ip++] = (uint8_t)((v >> 24) & 0xFF);
    b->bc[b->ip++] = (uint8_t)((v >> 16) & 0xFF);
    b->bc[b->ip++] = (uint8_t)((v >> 8) & 0xFF);
    b->bc[b->ip++] = (uint8_t)(v & 0xFF);
}

static void bc_emit_lit1(Bc *b, char c)
{
    uint32_t off = bc_add_lit(b, &c, 1);
    bc_emit_u8(b, OP_LIT);
    bc_emit_u32(b, off);
    bc_emit_u32(b, 1);
}

/* ── Write-log unit tests ───────────────────────────────────────────────── */

static void test_write_log_tracking(void)
{
    test_suite("Write-Log Tracking");

    VM vm;
    memset(&vm, 0, sizeof(vm));
    vm.use_compact_choice = true;
    vm_write_log_init(&vm);
    vm.write_log_cap = 64;
    vm.write_log = malloc(vm.write_log_cap * sizeof(WriteLogEntry));

    // Initially empty
    test_assert(vm_write_log_count_entries(&vm) == 0, "Write-log starts empty");

    // Track cap_start for cap 0
    vm_write_log_track_cap_start(&vm, 0, 100);
    test_assert(vm_write_log_count_entries(&vm) == 1, "One entry after cap_start");

    // Track cap_end for same cap 0 — should update existing entry, not add new
    vm_write_log_track_cap_end(&vm, 0, 200);
    test_assert(vm_write_log_count_entries(&vm) == 1, "Still one entry after cap_end on same cap");

    // Track cap_start for cap 1 — adds second entry
    vm_write_log_track_cap_start(&vm, 1, 300);
    test_assert(vm_write_log_count_entries(&vm) == 2, "Two entries after different cap");

    // Track cap_start for cap 0 again — should update existing entry (no new entry)
    vm_write_log_track_cap_start(&vm, 0, 400);
    test_assert(vm_write_log_count_entries(&vm) == 2, "No new entry when updating existing cap");

    // Verify final values by copying
    size_t count = vm_write_log_count_entries(&vm);
    test_assert(count == 2, "Final entry count is 2");

    WriteLogEntry entries[4];
    vm_write_log_copy_entries(&vm, entries, 4);
    // Entries order: cap0 then cap1 (based on insertion order in circular buffer)
    bool found0 = false, found1 = false;
    for (size_t i = 0; i < count; i++) {
        if (entries[i].cap_index == 0) {
            found0 = true;
            test_assert(entries[i].old_start == 400, "cap0 old_start updated");
            test_assert(entries[i].old_end == 200, "cap0 old_end preserved");
        } else if (entries[i].cap_index == 1) {
            found1 = true;
            test_assert(entries[i].old_start == 300, "cap1 old_start");
            test_assert(entries[i].old_end == (size_t)-1, "cap1 old_end unset");
        }
    }
    test_assert(found0 && found1, "Both entries found in copy");

    vm_write_log_free(&vm);
}

/* ── Compact choice record size tests ───────────────────────────────────── */

static void test_compact_choice_size_calculation(void)
{
    test_suite("Compact Choice Record Size");

    VM vm;
    memset(&vm, 0, sizeof(vm));
    vm.use_compact_choice = true;
    vm_write_log_init(&vm);
    vm.write_log_cap = 64;
    vm.write_log = malloc(vm.write_log_cap * sizeof(WriteLogEntry));
    vm.choices = malloc(4096);
    vm.choices_cap = 4096;
    vm.choices_top = 0;

    // Simulate: max_cap_used = 10, no counters, 1 write-log entry
    vm.max_cap_used = 10;
    // Track one modification
    vm_write_log_track_cap_start(&vm, 0, 0);
    vm_write_log_track_cap_end(&vm, 0, 5);

    // Compute expected data size: CompactChoiceHeader + 1 WriteLogEntry
    size_t expected_data = sizeof(CompactChoiceHeader) + sizeof(WriteLogEntry);
    // Total allocation includes trailing size and 8-byte alignment
    size_t expected_total = expected_data + sizeof(uint32_t);
    expected_total = (expected_total + 7) & ~7;

    // Simulate push by calling vm_push_choice (it will compress and allocate)
    uint8_t dummy_bc[1] = { OP_ACCEPT };
    vm.bc = dummy_bc;
    vm.bc_len = 1;
    vm.ip = 0;
    vm.pos = 0;
    vm.max_counter_used = 0;

    vm_push_choice(&vm, 0, 0);

    size_t allocated = vm.choice_allocated;
    printf("  [info] allocated=%zu expected=%zu\n", allocated, expected_total);
    test_assert(allocated == expected_total, "Compact record size matches calculation");

    // Cleanup
    free(vm.choices);
    vm_write_log_free(&vm);
}

/* ── Mode consistency test (legacy vs compact) ──────────────────────────── */

static void test_mode_consistency_capture_restore(void)
{
    test_suite("Mode Consistency: Capture Restore");

    Bc b;
    bc_init(&b);

    size_t split_ip = b.ip;
    bc_emit_u8(&b, OP_SPLIT);
    bc_emit_u32(&b, 0);
    bc_emit_u32(&b, 0);

    size_t a_ip = b.ip;
    bc_emit_u8(&b, OP_CAP_START); bc_emit_u8(&b, 0);
    bc_emit_lit1(&b, 'a');
    bc_emit_u8(&b, OP_CAP_END); bc_emit_u8(&b, 0);
    bc_emit_u8(&b, OP_FAIL);

    size_t b_ip = b.ip;
    bc_emit_u8(&b, OP_CAP_START); bc_emit_u8(&b, 0);
    bc_emit_lit1(&b, 'b');
    bc_emit_u8(&b, OP_CAP_END); bc_emit_u8(&b, 0);
    bc_emit_u8(&b, OP_ACCEPT);

    b.bc[split_ip + 1] = (uint8_t)((a_ip >> 24) & 0xFF);
    b.bc[split_ip + 2] = (uint8_t)((a_ip >> 16) & 0xFF);
    b.bc[split_ip + 3] = (uint8_t)((a_ip >> 8) & 0xFF);
    b.bc[split_ip + 4] = (uint8_t)(a_ip & 0xFF);
    b.bc[split_ip + 5] = (uint8_t)((b_ip >> 24) & 0xFF);
    b.bc[split_ip + 6] = (uint8_t)((b_ip >> 16) & 0xFF);
    b.bc[split_ip + 7] = (uint8_t)((b_ip >> 8) & 0xFF);
    b.bc[split_ip + 8] = (uint8_t)(b_ip & 0xFF);

    const char *subject = "b";

    // Legacy mode
    setenv("SNOBOL_LEGACY_CHOICE", "1", 1);
    VM vm_legacy = make_vm(b.bc, b.lit_pool, subject);
    bool ok_legacy = vm_run(&vm_legacy);
    bool cap0_legacy = cap_equals(&vm_legacy, 0, subject, "b");

    // Compact mode
    unsetenv("SNOBOL_LEGACY_CHOICE");
    VM vm_compact = make_vm(b.bc, b.lit_pool, subject);
    bool ok_compact = vm_run(&vm_compact);
    bool cap0_compact = cap_equals(&vm_compact, 0, subject, "b");

    test_assert(ok_legacy == ok_compact, "Both modes agree on success");
    test_assert(cap0_legacy == cap0_compact, "Both modes produce same capture");
    test_assert(ok_compact, "Match should succeed in compact mode");
    test_assert(cap0_compact, "cap0 should be 'b' in compact mode");
}

/* ── Memory reduction test ──────────────────────────────────────────────── */

static void test_memory_reduction_with_many_choices(void)
{
    test_suite("Memory Reduction: ≥10 Choice Points");

    const int N = 10;

    // Legacy mode
    VM vm_legacy;
    memset(&vm_legacy, 0, sizeof(vm_legacy));
    vm_legacy.use_compact_choice = false;
    vm_legacy.max_cap_used = 10;
    vm_legacy.choices = malloc(4096);
    vm_legacy.choices_cap = 4096;
    vm_legacy.choices_top = 0;
    vm_legacy.choice_allocated = 0;

    for (int i = 0; i < N; i++) {
        vm_push_choice(&vm_legacy, 0, 0);
    }
    size_t legacy_total = vm_legacy.choice_allocated;
    free(vm_legacy.choices);

    // Compact mode
    VM vm_compact;
    memset(&vm_compact, 0, sizeof(vm_compact));
    vm_compact.use_compact_choice = true;
    vm_compact.max_cap_used = 10;
    vm_write_log_init(&vm_compact);
    vm_compact.write_log_cap = 64;
    vm_compact.write_log = malloc(vm_compact.write_log_cap * sizeof(WriteLogEntry));
    vm_compact.choices = malloc(4096);
    vm_compact.choices_cap = 4096;
    vm_compact.choices_top = 0;
    vm_compact.choice_allocated = 0;

    for (int i = 0; i < N; i++) {
        vm_write_log_track_cap_start(&vm_compact, 0, 0);
        vm_write_log_track_cap_end(&vm_compact, 0, 5);
        vm_push_choice(&vm_compact, 0, 0);
    }
    size_t compact_total = vm_compact.choice_allocated;

    free(vm_compact.choices);
    vm_write_log_free(&vm_compact);

    // Compute expected sizes
    // Legacy records: sizeof(struct choice) per record, no trailing size, no alignment padding
    size_t legacy_per = sizeof(struct choice);
    size_t legacy_expected = N * legacy_per;
    // Compact records: header + 1 write-log entry + trailing uint32_t, aligned to 8 bytes
    size_t compact_per = sizeof(CompactChoiceHeader) + sizeof(WriteLogEntry);
    size_t compact_expected = N * (((compact_per + sizeof(uint32_t) + 7) & ~7));

    printf("  [info] legacy_total=%zu legacy_expected=%zu\n", legacy_total, legacy_expected);
    test_assert(legacy_total == legacy_expected, "Legacy total allocation matches expected");
    printf("  [info] compact_total=%zu compact_expected=%zu\n", compact_total, compact_expected);
    test_assert(compact_total == compact_expected, "Compact total allocation matches expected");

    double ratio = (double)compact_total / (double)legacy_total;
    printf("  [info] ratio=%.2f\n", ratio);
    test_assert(ratio <= 0.5, "Compact memory ≤ 50%% of legacy");
}

/* ── Statistics API test ─────────────────────────────────────────────────── */

static void test_choice_statistics_api(void)
{
    test_suite("Choice Statistics API");

    Bc b;
    bc_init(&b);
    size_t split_ip = b.ip;
    bc_emit_u8(&b, OP_SPLIT);
    bc_emit_u32(&b, 0);
    bc_emit_u32(&b, 0);
    size_t a_ip = b.ip;
    bc_emit_lit1(&b, 'a');
    bc_emit_u8(&b, OP_FAIL);
    size_t b_ip = b.ip;
    bc_emit_lit1(&b, 'b');
    bc_emit_u8(&b, OP_ACCEPT);
    b.bc[split_ip+1] = (uint8_t)((a_ip>>24)&0xFF);
    b.bc[split_ip+2] = (uint8_t)((a_ip>>16)&0xFF);
    b.bc[split_ip+3] = (uint8_t)((a_ip>>8)&0xFF);
    b.bc[split_ip+4] = (uint8_t)(a_ip&0xFF);
    b.bc[split_ip+5] = (uint8_t)((b_ip>>24)&0xFF);
    b.bc[split_ip+6] = (uint8_t)((b_ip>>16)&0xFF);
    b.bc[split_ip+7] = (uint8_t)((b_ip>>8)&0xFF);
    b.bc[split_ip+8] = (uint8_t)(b_ip&0xFF);

    VM vm = make_vm(b.bc, b.lit_pool, "b");
    bool ok = vm_run(&vm);
    test_assert(ok, "Pattern should match");

    /* After vm_run completes, choices_top is reset to 0 (choices consumed).
     * Use cumulative stats: choice_push_count and choice_allocated. */
    test_assert(vm.choice_push_count == 1, "One choice was pushed during execution");

    test_assert(vm.choice_allocated > 0, "Choice bytes were allocated during execution");

    size_t avg_size = vm_choice_record_average_size(&vm);
    test_assert(avg_size > 0, "Average record size should be > 0");
    size_t expected_compact = ((sizeof(CompactChoiceHeader) + sizeof(uint32_t) + 7) & ~7);
    printf("  [info] avg_size=%zu expected≈%zu\n", avg_size, expected_compact);
    test_assert(avg_size >= expected_compact - 1 && avg_size <= expected_compact + 1,
                "Average size matches compact header-only record");
}

/* ── Suite entry point ───────────────────────────────────────────────────── */

void test_compact_choice_suite(void)
{
    test_suite("Compact Choice Stack");

    test_write_log_tracking();
    test_compact_choice_size_calculation();
    test_mode_consistency_capture_restore();
    test_memory_reduction_with_many_choices();
    test_choice_statistics_api();
}
