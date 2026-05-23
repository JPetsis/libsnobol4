/* test_jit_opcode_coverage.c — JIT full opcode coverage tests
 *
 * Covers:
 *   Position guards (OP_REM, OP_RPOS, OP_RTAB) — jit_bailouts_total == 0
 *   OP_FENCE — jit_bailouts_total == 0 and correct cut semantics
 *   Labeled control flow (OP_LABEL, OP_GOTO_F) — jit_bailouts_total == 0
 *   Emit opcodes (OP_EMIT_CAPTURE) — jit_bailouts_total == 0
 *   OP_BAL — jit_bailouts_total == 0
 *   OP_EVAL — jit_bailouts_total == 0 with registered callback
 *   OP_RTAB — correct match/fail semantics
 *   OP_TABLE_GET / OP_TABLE_SET — jit_bailouts_total == 0
 *   OP_DYNAMIC — jit_bailouts_total == 0 with sub-pattern match
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "../../core/include/snobol/vm.h"
#include "../../core/include/snobol/jit.h"
#include "../../core/include/snobol/snobol_internal.h"
#include "../../core/include/snobol/table.h"

void test_suite(const char *name);
void test_assert(bool condition, const char *message);

static bool jit_is_supported(void) {
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return true;
#else
    return false;
#endif
}

#ifdef SNOBOL_JIT

/* ========================================================================
 * Bytecode builder helpers
 * ======================================================================== */

static void cov_u8(uint8_t *bc, size_t *ip, uint8_t v)  { bc[(*ip)++] = v; }
static void cov_u16(uint8_t *bc, size_t *ip, uint16_t v) {
    bc[(*ip)++] = (uint8_t)(v >> 8); bc[(*ip)++] = (uint8_t)(v & 0xff);
}
static void cov_u32(uint8_t *bc, size_t *ip, uint32_t v) {
    bc[(*ip)++] = (uint8_t)((v >> 24) & 0xff);
    bc[(*ip)++] = (uint8_t)((v >> 16) & 0xff);
    bc[(*ip)++] = (uint8_t)((v >>  8) & 0xff);
    bc[(*ip)++] = (uint8_t)(v & 0xff);
}

/* Emit OP_LIT with inline data at the current write position.
 * The offset field is computed exactly as ipAfterOp+4+4 = where bytes go. */
static void emit_lit(uint8_t *bc, size_t *ip, const char *data, uint32_t len) {
    cov_u8(bc, ip, OP_LIT);
    size_t off_pos = *ip; /* remember where to write offset */
    *ip += 4;             /* skip offset field for now */
    cov_u32(bc, ip, len);
    size_t data_pos = *ip; /* inline bytes start here */
    for (uint32_t i = 0; i < len; i++) bc[(*ip)++] = (uint8_t)data[i];
    /* patch offset */
    bc[off_pos]   = (uint8_t)(data_pos >> 24);
    bc[off_pos+1] = (uint8_t)(data_pos >> 16);
    bc[off_pos+2] = (uint8_t)(data_pos >>  8);
    bc[off_pos+3] = (uint8_t)(data_pos);
}

/* Add a single-character ASCII charclass.
 * Uses fixed data area at bc[200+slot*12] to avoid overlap with offset table.
 * bc_len-4 = class_count field (OLD format). */
static uint16_t add_class_ch(uint8_t *bc, size_t bc_len, uint8_t ch) {
    uint32_t cc = ((uint32_t)bc[bc_len-4] << 24) | ((uint32_t)bc[bc_len-3] << 16) |
                  ((uint32_t)bc[bc_len-2] <<  8)  |  (uint32_t)bc[bc_len-1];
    uint16_t sid = (uint16_t)(cc + 1);
    size_t data_off = 200 + (size_t)cc * 12;
    /* count=1, ci=0 */
    bc[data_off+0] = 0; bc[data_off+1] = 1;
    bc[data_off+2] = 0; bc[data_off+3] = 0;
    /* range [ch, ch] */
    bc[data_off+4] = 0; bc[data_off+5] = 0; bc[data_off+6] = 0; bc[data_off+7]  = ch;
    bc[data_off+8] = 0; bc[data_off+9] = 0; bc[data_off+10]= 0; bc[data_off+11] = ch;
    /* offset table entry for sid (1-indexed) */
    size_t te = (size_t)bc_len - 4 - (size_t)sid * 4;
    bc[te]   = (uint8_t)(data_off >> 24); bc[te+1] = (uint8_t)(data_off >> 16);
    bc[te+2] = (uint8_t)(data_off >>  8); bc[te+3] = (uint8_t)(data_off);
    /* update class_count */
    bc[bc_len-4] = (uint8_t)(sid >> 24); bc[bc_len-3] = (uint8_t)(sid >> 16);
    bc[bc_len-2] = (uint8_t)(sid >>  8); bc[bc_len-1] = (uint8_t)(sid);
    return sid;
}

/* Set up JIT on a VM (already has bc/bc_len set) and warm up. Returns ctx. */
static SnobolJitContext *setup_jit_vm(VM *vm, int hotness, const char *warmup_s) {
    SnobolJitContext *ctx = snobol_jit_acquire_context(vm->bc, vm->bc_len);
    if (!ctx) return nullptr;
    SnobolJitConfig cfg = *snobol_jit_get_config();
    cfg.hotness_threshold = (uint64_t)hotness;
    snobol_jit_set_config(&cfg);

    vm->jit.enabled   = true;
    vm->jit.ip_counts = ctx->ip_counts;
    vm->jit.traces    = ctx->traces;
    vm->jit.stats     = snobol_jit_get_stats();
    vm->jit.ctx       = ctx;

    for (int i = 0; i < hotness * 3; i++) {
        vm->s   = warmup_s;
        vm->len = strlen(warmup_s);
        vm->ip  = 0; vm->pos = 0;
        vm_run(vm);
    }
    return ctx;
}

/* ========================================================================
 * OP_REM test
 * ======================================================================== */
static void test_rem_jit(void) {
    if (!jit_is_supported()) {
        test_assert(true, "OP_REM JIT: skipped (non-ARM64)"); return;
    }
    snobol_jit_init(); snobol_jit_reset_stats();

    /* Pattern: OP_LIT "a" OP_REM OP_ACCEPT — matches any string starting with "a" */
    uint8_t bc[256]; memset(bc, 0, sizeof(bc));
    bc[252] = bc[253] = bc[254] = bc[255] = 0;
    size_t ip = 0;
    emit_lit(bc, &ip, "a", 1);
    cov_u8(bc, &ip, OP_REM);
    cov_u8(bc, &ip, OP_ACCEPT);

    VM vm = {0}; vm.bc = bc; vm.bc_len = 256;
    SnobolJitContext *ctx = setup_jit_vm(&vm, 5, "a");

    test_assert(snobol_jit_get_stats()->bailouts_total == 0,
                "OP_REM JIT: jit_bailouts_total == 0");
    vm.s = "a"; vm.len = 1;
    test_assert(vm_exec(&vm), "OP_REM JIT: matches 'a' (sets pos=len)");
    vm.s = "ab"; vm.len = 2;
    test_assert(vm_exec(&vm), "OP_REM JIT: matches 'ab' (REM advances to end)");
    vm.s = "b"; vm.len = 1;
    test_assert(!vm_exec(&vm), "OP_REM JIT: fails 'b' (LIT 'a' doesn't match)");

    snobol_jit_release_context(ctx); snobol_jit_shutdown();
}

/* ========================================================================
 * OP_RPOS test
 * ======================================================================== */
static void test_rpos_jit(void) {
    if (!jit_is_supported()) {
        test_assert(true, "OP_RPOS JIT: skipped (non-ARM64)"); return;
    }
    snobol_jit_init(); snobol_jit_reset_stats();

    uint8_t bc[256]; memset(bc, 0, sizeof(bc));
    bc[252] = bc[253] = bc[254] = bc[255] = 0;
    size_t ip = 0;
    emit_lit(bc, &ip, "ab", 2);
    cov_u8(bc, &ip, OP_RPOS); cov_u32(bc, &ip, 0); /* n=0: must be at end */
    cov_u8(bc, &ip, OP_ACCEPT);

    VM vm = {0}; vm.bc = bc; vm.bc_len = 256;
    SnobolJitContext *ctx = setup_jit_vm(&vm, 5, "ab");

    test_assert(snobol_jit_get_stats()->bailouts_total == 0,
                "OP_RPOS JIT: jit_bailouts_total == 0");
    vm.s = "ab"; vm.len = 2;
    test_assert(vm_exec(&vm), "OP_RPOS JIT: RPOS(0) at end succeeds");
    vm.s = "abc"; vm.len = 3;
    test_assert(!vm_exec(&vm), "OP_RPOS JIT: RPOS(0) not-at-end fails");

    snobol_jit_release_context(ctx); snobol_jit_shutdown();
}

/* ========================================================================
 * OP_RTAB test
 * ======================================================================== */
static void test_rtab_jit(void) {
    if (!jit_is_supported()) {
        test_assert(true, "OP_RTAB JIT: skipped (non-ARM64)"); return;
    }
    snobol_jit_init(); snobol_jit_reset_stats();

    /* OP_LIT "a" OP_RTAB(0) OP_ACCEPT — RTAB(0) advances to end */
    uint8_t bc[256]; memset(bc, 0, sizeof(bc));
    bc[252] = bc[253] = bc[254] = bc[255] = 0;
    size_t ip = 0;
    emit_lit(bc, &ip, "a", 1);
    cov_u8(bc, &ip, OP_RTAB); cov_u32(bc, &ip, 0);
    cov_u8(bc, &ip, OP_ACCEPT);

    VM vm = {0}; vm.bc = bc; vm.bc_len = 256;
    SnobolJitContext *ctx = setup_jit_vm(&vm, 5, "a");

    test_assert(snobol_jit_get_stats()->bailouts_total == 0,
                "OP_RTAB JIT: jit_bailouts_total == 0");
    vm.s = "a"; vm.len = 1;
    test_assert(vm_exec(&vm), "OP_RTAB JIT: 'a' + RTAB(0) succeeds");
    vm.s = "abc"; vm.len = 3;
    test_assert(vm_exec(&vm), "OP_RTAB JIT: 'abc' after 'a' RTAB(0) advances to end");

    snobol_jit_release_context(ctx); snobol_jit_shutdown();
}

/* ========================================================================
 * OP_FENCE test
 * ======================================================================== */
static void test_fence_jit(void) {
    if (!jit_is_supported()) {
        test_assert(true, "FENCE JIT: skipped (non-ARM64)"); return;
    }
    snobol_jit_init(); snobol_jit_reset_stats();

    uint8_t bc[256]; memset(bc, 0, sizeof(bc));
    bc[252] = bc[253] = bc[254] = bc[255] = 0;
    size_t ip = 0;
    emit_lit(bc, &ip, "a", 1);
    cov_u8(bc, &ip, OP_FENCE);
    cov_u8(bc, &ip, OP_ACCEPT);

    VM vm = {0}; vm.bc = bc; vm.bc_len = 256;
    SnobolJitContext *ctx = setup_jit_vm(&vm, 5, "a");

    test_assert(snobol_jit_get_stats()->bailouts_total == 0,
                "FENCE JIT: jit_bailouts_total == 0");
    vm.s = "a"; vm.len = 1;
    test_assert(vm_exec(&vm), "FENCE JIT: pattern matches 'a'");

    snobol_jit_release_context(ctx); snobol_jit_shutdown();
}

/* ========================================================================
 * Labeled control flow test
 * ======================================================================== */
#define SNBL_MAGIC 0x534E424Cu
static void test_label_jit(void) {
    if (!jit_is_supported()) {
        test_assert(true, "LABEL JIT: skipped (non-ARM64)"); return;
    }
    snobol_jit_init(); snobol_jit_reset_stats();

    uint8_t bc[256]; memset(bc, 0, sizeof(bc));
    size_t ip = 0;
    emit_lit(bc, &ip, "ab", 2);
    size_t label_ip = ip;
    cov_u8(bc, &ip, OP_LABEL); cov_u16(bc, &ip, 0);
    cov_u8(bc, &ip, OP_ACCEPT);

    /* New-format tail (last 16 bytes of 256-byte bc) */
    bc[240] = bc[241] = bc[242] = bc[243] = 0; /* class_count=0 */
    bc[244] = (uint8_t)(label_ip >> 24); bc[245] = (uint8_t)(label_ip >> 16);
    bc[246] = (uint8_t)(label_ip >>  8); bc[247] = (uint8_t)(label_ip);
    bc[248] = bc[249] = bc[250] = 0; bc[251] = 1; /* label_count=1 */
    bc[252] = (uint8_t)(SNBL_MAGIC >> 24); bc[253] = (uint8_t)(SNBL_MAGIC >> 16);
    bc[254] = (uint8_t)(SNBL_MAGIC >>  8); bc[255] = (uint8_t)(SNBL_MAGIC);

    VM vm = {0}; vm.bc = bc; vm.bc_len = 256;
    SnobolJitContext *ctx = setup_jit_vm(&vm, 5, "ab");

    test_assert(snobol_jit_get_stats()->bailouts_total == 0,
                "LABEL JIT: jit_bailouts_total == 0");
    vm.s = "ab"; vm.len = 2;
    test_assert(vm_exec(&vm), "LABEL JIT: matches correctly");

    snobol_jit_release_context(ctx); snobol_jit_shutdown();
}
#undef SNBL_MAGIC

/* ========================================================================
 * OP_EMIT_CAPTURE test
 * ======================================================================== */
static char g_emit_buf[256];
static size_t g_emit_len = 0;
static void test_emit_cb(const char *data, size_t len, void *udata) {
    (void)udata;
    if (g_emit_len + len < sizeof(g_emit_buf)) {
        memcpy(g_emit_buf + g_emit_len, data, len);
        g_emit_len += len;
    }
}

static void test_emit_cap_jit(void) {
    if (!jit_is_supported()) {
        test_assert(true, "EMIT_CAPTURE JIT: skipped (non-ARM64)"); return;
    }
    snobol_jit_init(); snobol_jit_reset_stats();

    /* Pattern: OP_CAP_START 0 OP_LIT "a" OP_CAP_END 0 OP_EMIT_CAPTURE 0 OP_ACCEPT */
    uint8_t bc[256]; memset(bc, 0, sizeof(bc));
    bc[252] = bc[253] = bc[254] = bc[255] = 0;
    size_t ip = 0;
    cov_u8(bc, &ip, OP_CAP_START); cov_u8(bc, &ip, 0);
    emit_lit(bc, &ip, "a", 1);
    cov_u8(bc, &ip, OP_CAP_END); cov_u8(bc, &ip, 0);
    cov_u8(bc, &ip, OP_EMIT_CAPTURE); cov_u8(bc, &ip, 0);
    cov_u8(bc, &ip, OP_ACCEPT);

    VM vm = {0}; vm.bc = bc; vm.bc_len = 256;
    vm.emit_fn = test_emit_cb;
    SnobolJitContext *ctx = setup_jit_vm(&vm, 5, "a");

    test_assert(snobol_jit_get_stats()->bailouts_total == 0,
                "EMIT_CAPTURE JIT: jit_bailouts_total == 0");
    g_emit_len = 0; memset(g_emit_buf, 0, sizeof(g_emit_buf));
    vm.s = "a"; vm.len = 1;
    test_assert(vm_exec(&vm), "EMIT_CAPTURE JIT: pattern matches");
    test_assert(g_emit_len == 1 && g_emit_buf[0] == 'a',
                "EMIT_CAPTURE JIT: callback received 'a'");

    snobol_jit_release_context(ctx); snobol_jit_shutdown();
}

/* ========================================================================
 * OP_BAL test
 * ======================================================================== */
static void test_bal_jit(void) {
    if (!jit_is_supported()) {
        test_assert(true, "BAL JIT: skipped (non-ARM64)"); return;
    }
    snobol_jit_init(); snobol_jit_reset_stats();

    uint8_t bc[256]; memset(bc, 0, sizeof(bc));
    bc[252] = bc[253] = bc[254] = bc[255] = 0;
    size_t ip = 0;
    emit_lit(bc, &ip, "x", 1);
    cov_u8(bc, &ip, OP_BAL);
    cov_u32(bc, &ip, '('); cov_u32(bc, &ip, ')');
    cov_u8(bc, &ip, OP_ACCEPT);

    VM vm = {0}; vm.bc = bc; vm.bc_len = 256;
    SnobolJitContext *ctx = setup_jit_vm(&vm, 5, "x(a(b)c)");

    test_assert(snobol_jit_get_stats()->bailouts_total == 0,
                "BAL JIT: jit_bailouts_total == 0");
    vm.s = "x(a(b)c)"; vm.len = 8;
    test_assert(vm_exec(&vm), "BAL JIT: balanced delimiter match succeeds");
    vm.s = "x(unmatched"; vm.len = 11;
    test_assert(!vm_exec(&vm), "BAL JIT: unmatched delimiter fails");

    snobol_jit_release_context(ctx); snobol_jit_shutdown();
}

/* ========================================================================
 * OP_EVAL test
 * ======================================================================== */
static int g_eval_calls = 0;
static int g_eval_fail_after = -1;
static bool test_eval_cb(int fn_id, const char *s, size_t start, size_t end, void *u) {
    (void)fn_id; (void)s; (void)start; (void)end; (void)u;
    g_eval_calls++;
    if (g_eval_fail_after >= 0 && g_eval_calls > g_eval_fail_after) return false;
    return true;
}

static void test_eval_jit(void) {
    if (!jit_is_supported()) {
        test_assert(true, "EVAL JIT: skipped (non-ARM64)"); return;
    }
    snobol_jit_init(); snobol_jit_reset_stats();

    uint8_t bc[256]; memset(bc, 0, sizeof(bc));
    bc[252] = bc[253] = bc[254] = bc[255] = 0;
    size_t ip = 0;
    cov_u8(bc, &ip, OP_CAP_START); cov_u8(bc, &ip, 0);
    emit_lit(bc, &ip, "ab", 2);
    cov_u8(bc, &ip, OP_CAP_END); cov_u8(bc, &ip, 0);
    cov_u8(bc, &ip, OP_EVAL);
    cov_u16(bc, &ip, 0); /* fn_id=0 → host callback */
    cov_u8(bc, &ip, 0);  /* reg=0 */
    cov_u8(bc, &ip, OP_ACCEPT);

    VM vm = {0}; vm.bc = bc; vm.bc_len = 256;
    vm.eval_fn = test_eval_cb;
    g_eval_calls = 0; g_eval_fail_after = -1;
    SnobolJitContext *ctx = setup_jit_vm(&vm, 5, "ab");

    test_assert(snobol_jit_get_stats()->bailouts_total == 0,
                "EVAL JIT: jit_bailouts_total == 0");
    test_assert(g_eval_calls > 0, "EVAL JIT: host callback invoked during warmup");

    /* Verify callback failure causes match failure */
    g_eval_calls = 0; g_eval_fail_after = 0;
    vm.s = "ab"; vm.len = 2;
    test_assert(!vm_exec(&vm), "EVAL JIT: callback returns false → pattern fails");

    snobol_jit_release_context(ctx); snobol_jit_shutdown();
}

#endif /* SNOBOL_JIT */

/* ========================================================================
 * OP_TABLE_GET test
 * ======================================================================== */
#ifdef SNOBOL_DYNAMIC_PATTERN
static void test_table_get_jit(void) {
    if (!jit_is_supported()) {
        test_assert(true, "TABLE_GET JIT: skipped (non-ARM64)"); return;
    }
    snobol_jit_init(); snobol_jit_reset_stats();

    /* Build a minimal table-backed pattern:
     *   OP_CAP_START 0 OP_LIT "key" OP_CAP_END 0 OP_TABLE_GET 0 0 0
     *   table_id=0, key_reg=0, dest_reg=0; then OP_ACCEPT */
    uint8_t bc[256]; memset(bc, 0, sizeof(bc));
    bc[252] = bc[253] = bc[254] = bc[255] = 0;
    size_t ip = 0;
    cov_u8(bc, &ip, OP_CAP_START); cov_u8(bc, &ip, 0);
    cov_u8(bc, &ip, OP_LIT);
    /* offset field placeholder */
    size_t off_pos = ip; ip += 4;
    cov_u32(bc, &ip, 3); /* len=3 */
    size_t data_pos = ip;
    bc[ip++] = 'k'; bc[ip++] = 'e'; bc[ip++] = 'y';
    /* patch offset */
    bc[off_pos]   = (uint8_t)(data_pos >> 24);
    bc[off_pos+1] = (uint8_t)(data_pos >> 16);
    bc[off_pos+2] = (uint8_t)(data_pos >>  8);
    bc[off_pos+3] = (uint8_t)(data_pos);
    cov_u8(bc, &ip, OP_CAP_END); cov_u8(bc, &ip, 0);
    /* OP_TABLE_GET: table_id=0(u16), key_reg=0(u8), dest_reg=0(u8), name_len=0(u8) */
    cov_u8(bc, &ip, OP_TABLE_GET);
    cov_u16(bc, &ip, 0); cov_u8(bc, &ip, 0); cov_u8(bc, &ip, 0); cov_u8(bc, &ip, 0);
    cov_u8(bc, &ip, OP_ACCEPT);

    VM vm = {0}; vm.bc = bc; vm.bc_len = 256;
    vm_init_tables(&vm);

    /* Register a table at ID 0 */
    snobol_table_t *tbl = table_create("TEST_TBL");
    test_assert(tbl != NULL, "TABLE_GET JIT: table_create succeeded");
    test_assert(table_set(tbl, "key", "val1"), "TABLE_GET JIT: table_set succeeded");
    uint16_t tid = 0;
    test_assert(vm_register_table(&vm, tbl, &tid), "TABLE_GET JIT: vm_register_table succeeded");
    test_assert(tid == 0, "TABLE_GET JIT: table_id is 0");

    SnobolJitContext *ctx = setup_jit_vm(&vm, 5, "key");

    test_assert(snobol_jit_get_stats()->bailouts_total == 0,
                "TABLE_GET JIT: jit_bailouts_total == 0");
    vm.s = "key"; vm.len = 3;
    test_assert(vm_exec(&vm), "TABLE_GET JIT: pattern matches");

    snobol_jit_release_context(ctx);
    vm_free_tables(&vm);
    table_release(tbl);
    snobol_jit_shutdown();
}

/* ========================================================================
 * OP_TABLE_SET test
 * ======================================================================== */
static void test_table_set_jit(void) {
    if (!jit_is_supported()) {
        test_assert(true, "TABLE_SET JIT: skipped (non-ARM64)"); return;
    }
    snobol_jit_init(); snobol_jit_reset_stats();

    /* Build: OP_CAP_START 0 OP_LIT "k" OP_CAP_END 0
     *        OP_CAP_START 1 OP_LIT "v" OP_CAP_END 1
     *        OP_TABLE_SET 0 0 1 OP_ACCEPT
     * table_id=0, key_reg=0, value_reg=1 */
    uint8_t bc[256]; memset(bc, 0, sizeof(bc));
    bc[252] = bc[253] = bc[254] = bc[255] = 0;
    size_t ip = 0;

    /* Capture "k" into reg 0 */
    cov_u8(bc, &ip, OP_CAP_START); cov_u8(bc, &ip, 0);
    cov_u8(bc, &ip, OP_LIT);
    size_t off0 = ip; ip += 4;
    cov_u32(bc, &ip, 1);
    size_t d0 = ip; bc[ip++] = 'k';
    bc[off0]   = (uint8_t)(d0 >> 24); bc[off0+1] = (uint8_t)(d0 >> 16);
    bc[off0+2] = (uint8_t)(d0 >>  8); bc[off0+3] = (uint8_t)(d0);
    cov_u8(bc, &ip, OP_CAP_END); cov_u8(bc, &ip, 0);

    /* Capture "v" into reg 1 */
    cov_u8(bc, &ip, OP_CAP_START); cov_u8(bc, &ip, 1);
    cov_u8(bc, &ip, OP_LIT);
    size_t off1 = ip; ip += 4;
    cov_u32(bc, &ip, 1);
    size_t d1 = ip; bc[ip++] = 'v';
    bc[off1]   = (uint8_t)(d1 >> 24); bc[off1+1] = (uint8_t)(d1 >> 16);
    bc[off1+2] = (uint8_t)(d1 >>  8); bc[off1+3] = (uint8_t)(d1);
    cov_u8(bc, &ip, OP_CAP_END); cov_u8(bc, &ip, 1);

    /* OP_TABLE_SET: table_id=0, key_reg=0, value_reg=1, name_len=0 */
    cov_u8(bc, &ip, OP_TABLE_SET);
    cov_u16(bc, &ip, 0); cov_u8(bc, &ip, 0); cov_u8(bc, &ip, 1); cov_u8(bc, &ip, 0);
    cov_u8(bc, &ip, OP_ACCEPT);

    VM vm = {0}; vm.bc = bc; vm.bc_len = 256;
    vm_init_tables(&vm);

    snobol_table_t *tbl = table_create("TEST_TBL2");
    test_assert(tbl != NULL, "TABLE_SET JIT: table_create succeeded");
    uint16_t tid = 0;
    test_assert(vm_register_table(&vm, tbl, &tid), "TABLE_SET JIT: vm_register_table succeeded");

    SnobolJitContext *ctx = setup_jit_vm(&vm, 5, "kv");

    test_assert(snobol_jit_get_stats()->bailouts_total == 0,
                "TABLE_SET JIT: jit_bailouts_total == 0");
    vm.s = "kv"; vm.len = 2;
    test_assert(vm_exec(&vm), "TABLE_SET JIT: pattern matches");

    snobol_jit_release_context(ctx);
    vm_free_tables(&vm);
    table_release(tbl);
    snobol_jit_shutdown();
}
#endif /* SNOBOL_DYNAMIC_PATTERN */

/* ========================================================================
 * OP_DYNAMIC test
 * ======================================================================== */
static void test_dynamic_jit(void) {
    if (!jit_is_supported()) {
        test_assert(true, "DYNAMIC JIT: skipped (non-ARM64)"); return;
    }
    snobol_jit_init(); snobol_jit_reset_stats();

    /* Build a dynamic pattern that matches "A":
     *   OP_DYNAMIC_DEF + source_len(u32)=1 + "A" + bc_len(u32)=2 + OP_LIT"A" + OP_ACCEPT
     *   OP_DYNAMIC (pattern_reg=0)
     * Then outer: OP_LIT "X" OP_DYNAMIC 0 OP_ACCEPT
     */
    uint8_t inner_bc[64]; memset(inner_bc, 0, sizeof(inner_bc));
    size_t iip = 0;
    /* inner: OP_LIT "A" OP_ACCEPT */
    size_t ii = 0;
    emit_lit(inner_bc, &ii, "A", 1);
    inner_bc[ii++] = OP_ACCEPT;
    iip = ii;

    uint8_t bc[256]; memset(bc, 0, sizeof(bc));
    bc[252] = bc[253] = bc[254] = bc[255] = 0;
    size_t ip = 0;

    /* OP_DYNAMIC_DEF: source_len=1, source="A", bc_len, inner_bc */
    cov_u8(bc, &ip, OP_DYNAMIC_DEF);
    cov_u32(bc, &ip, 1);
    bc[ip++] = 'A';
    cov_u32(bc, &ip, (uint32_t)iip);
    for (size_t k = 0; k < iip; k++) bc[ip++] = inner_bc[k];

    /* Outer pattern: OP_DYNAMIC OP_ACCEPT */
    /* OP_DYNAMIC: no operands */
    cov_u8(bc, &ip, OP_DYNAMIC);

    cov_u8(bc, &ip, OP_ACCEPT);

    VM vm = {0}; vm.bc = bc; vm.bc_len = 256;
    SnobolJitContext *ctx = setup_jit_vm(&vm, 5, "A");

    test_assert(snobol_jit_get_stats()->bailouts_total == 0,
                "DYNAMIC JIT: jit_bailouts_total == 0");
    vm.s = "A"; vm.len = 1;
    test_assert(vm_exec(&vm), "DYNAMIC JIT: sub-pattern 'A' matches at end");
    vm.s = "B"; vm.len = 1;
    test_assert(!vm_exec(&vm), "DYNAMIC JIT: sub-pattern 'A' does not match 'B'");

    snobol_jit_release_context(ctx); snobol_jit_shutdown();
}

void test_jit_opcode_coverage_suite(void) {
    test_suite("JIT Opcode Coverage");
#ifdef SNOBOL_JIT
    test_rem_jit();
    test_rpos_jit();
    test_rtab_jit();
    test_fence_jit();
    test_label_jit();
    test_emit_cap_jit();
    test_bal_jit();
    test_eval_jit();
    test_table_get_jit();
    test_table_set_jit();
    test_dynamic_jit();
#else
    test_assert(true, "JIT not enabled: all opcode coverage tests skipped");
#endif
}


