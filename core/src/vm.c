#include "snobol/snobol_internal.h"
#include "snobol/vm.h"
#include "snobol/table.h"
#include "snobol/dynamic_pattern.h"
#include "snobol/string_fn.h"
#include "snobol/type_fn.h"
#ifdef SNOBOL_JIT
#include "snobol/jit.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

/* Minimal code buffer for dynamic pattern compilation in VM */
typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
} VmCodeBuf;

static void vm_cb_init(VmCodeBuf *c) {
    c->cap = 4096;
    c->buf = snobol_malloc(c->cap);
    c->len = 0;
}
static void vm_cb_free(VmCodeBuf *c) {
    if (c->buf) {
        snobol_free(c->buf);
        c->buf = nullptr;
    }
    c->cap = c->len = 0;
}
static void vm_cb_ensure(VmCodeBuf *c, size_t need) {
    if (c->len + need <= c->cap) return;
    size_t newcap = c->cap ? c->cap * 2 : 4096;
    while (c->len + need > newcap) newcap *= 2;
    c->buf = snobol_realloc(c->buf, newcap);
    c->cap = newcap;
}
static void vm_cb_emit_u8(VmCodeBuf *c, uint8_t v) { vm_cb_ensure(c,1); c->buf[c->len++] = v; }
static void vm_cb_emit_u32(VmCodeBuf *c, uint32_t v) { vm_cb_ensure(c,4); c->buf[c->len++] = (v >> 24) & 0xff; c->buf[c->len++] = (v >> 16) & 0xff; c->buf[c->len++] = (v >> 8) & 0xff; c->buf[c->len++] = v & 0xff; }
static void vm_cb_emit_bytes(VmCodeBuf *c, const uint8_t *b, size_t n) { if (n==0) return; vm_cb_ensure(c,n); memcpy(c->buf + c->len, b, n); c->len += n; }

/* Emit literal inline for VM-compiled patterns */
static int vm_emit_lit_bytes(VmCodeBuf *c, const char *s, size_t len) {
    size_t off_of_payload = c->len + 1 + 4 + 4;
    vm_cb_emit_u8(c, OP_LIT);
    vm_cb_emit_u32(c, (uint32_t)off_of_payload);
    vm_cb_emit_u32(c, (uint32_t)len);
    vm_cb_emit_bytes(c, (const uint8_t*)s, len);
    return 0;
}

/* Magic that compiler appends after the label table (last 4 bytes of new-format bc).
 * Old-format bytecodes have charclass_count at bc_len-4 (typically < 65536).
 * 0x534E424C = "SNBL" — safely outside any realistic charclass_count range.
 * Guard prevents a redefinition error in the amalgam build where compiler.c
 * (included before vm.c) already declares this constant. */
#ifndef SNOBOL_LABEL_TABLE_MAGIC_DEFINED
#define SNOBOL_LABEL_TABLE_MAGIC_DEFINED
constexpr uint32_t SNOBOL_LABEL_TABLE_MAGIC = 0x534E424Cu;
#endif

const uint8_t *get_ranges_ptr(const VM *vm, uint16_t set_id, uint16_t *out_count, uint16_t *out_case) {
    if (set_id == 0) return nullptr;
    size_t tail_ip = vm->bc_len;
    if (tail_ip < 4) return nullptr;

    /* Detect bytecode format:
     *   NEW format (compiler-produced): last 4 bytes == SNOBOL_LABEL_TABLE_MAGIC
     *     layout (from end): MAGIC | label_count | label_offsets[] | charclass_count | ...
     *   OLD format (hand-built tests): last 4 bytes == charclass_count
     *
     * For charclass lookup we only need to find charclass_count; in new format
     * we skip past the label table first. */
    uint32_t last4 = ((uint32_t)vm->bc[tail_ip-4] << 24) | ((uint32_t)vm->bc[tail_ip-3] << 16) |
                     ((uint32_t)vm->bc[tail_ip-2] << 8) | (uint32_t)vm->bc[tail_ip-1];

    size_t cc_tail; /* position just after charclass_count field */
    if (last4 == SNOBOL_LABEL_TABLE_MAGIC) {
        /* New format: read label_count from the u32 before the MAGIC */
        if (tail_ip < 8) return nullptr;
        uint32_t label_count = ((uint32_t)vm->bc[tail_ip-8] << 24) | ((uint32_t)vm->bc[tail_ip-7] << 16) |
                               ((uint32_t)vm->bc[tail_ip-6] << 8) | (uint32_t)vm->bc[tail_ip-5];
        /* charclass_count sits just before: [label_offsets…][label_count][MAGIC] */
        size_t skip = 8 + (size_t)label_count * 4; /* 4 (label_count) + 4 (MAGIC) + N*4 */
        if (tail_ip < skip + 4) return nullptr;
        cc_tail = tail_ip - skip;
    } else {
        /* Old format: charclass_count IS at bc_len-4 */
        cc_tail = tail_ip;
    }

    if (cc_tail < 4) return nullptr;
    uint32_t class_count = ((uint32_t)vm->bc[cc_tail-4] << 24) | ((uint32_t)vm->bc[cc_tail-3] << 16) |
                           ((uint32_t)vm->bc[cc_tail-2] << 8) | (uint32_t)vm->bc[cc_tail-1];
    if (set_id > class_count) return nullptr;
    size_t table_size = (size_t)class_count * 4;
    if (cc_tail < 4 + table_size) return nullptr;
    size_t table_start = cc_tail - 4 - table_size;
    size_t offset_pos = table_start + (size_t)(set_id - 1) * 4;
    uint32_t offset = ((uint32_t)vm->bc[offset_pos+0] << 24) | ((uint32_t)vm->bc[offset_pos+1] << 16) | ((uint32_t)vm->bc[offset_pos+2] << 8) | (uint32_t)vm->bc[offset_pos+3];
    if (offset >= vm->bc_len) return nullptr;
    size_t ip = (size_t)offset;
    *out_count = read_u16(vm->bc, vm->bc_len, &ip);
    *out_case = read_u16(vm->bc, vm->bc_len, &ip);
    return vm->bc + ip;
}

bool ranges_to_ascii_bitmap(const uint8_t *ranges_ptr, size_t count, uint64_t map[2]) {
    map[0] = map[1] = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t offset = i * 8;
        uint32_t start = ((uint32_t)ranges_ptr[offset+0] << 24) | ((uint32_t)ranges_ptr[offset+1] << 16) | ((uint32_t)ranges_ptr[offset+2] << 8) | (uint32_t)ranges_ptr[offset+3];
        uint32_t end   = ((uint32_t)ranges_ptr[offset+4] << 24) | ((uint32_t)ranges_ptr[offset+5] << 16) | ((uint32_t)ranges_ptr[offset+6] << 8) | (uint32_t)ranges_ptr[offset+7];
        if (start > 127 || end > 127) return false;
        for (uint32_t c = start; c <= end; ++c) {
            if (c < 64) map[0] |= (1ULL << c);
            else map[1] |= (1ULL << (c - 64));
        }
    }
    return true;
}

bool range_contains(const uint8_t *ranges_ptr, size_t count, uint32_t cp) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        size_t offset = mid * 8;
        uint32_t start = ((uint32_t)ranges_ptr[offset+0] << 24) | ((uint32_t)ranges_ptr[offset+1] << 16) | ((uint32_t)ranges_ptr[offset+2] << 8) | (uint32_t)ranges_ptr[offset+3];
        uint32_t end   = ((uint32_t)ranges_ptr[offset+4] << 24) | ((uint32_t)ranges_ptr[offset+5] << 16) | ((uint32_t)ranges_ptr[offset+6] << 8) | (uint32_t)ranges_ptr[offset+7];
        if (cp < start) hi = mid;
        else if (cp > end) lo = mid + 1;
        else return true;
    }
    return false;
}

/* ========== Write-log management for compact choice stack ========== */

void vm_write_log_init(VM *vm) {
    vm->write_log = nullptr;
    vm->write_log_cap = 0;
    vm->write_log_next = 0;
    vm->write_log_bitmap = 0;
    vm->write_log_compressed_count = 0;
    vm->write_log_dirty = false;
}

void vm_write_log_free(VM *vm) {
    if (vm->write_log) {
        snobol_free(vm->write_log);
        vm->write_log = nullptr;
    }
    vm->write_log_cap = 0;
    vm->write_log_next = 0;
    vm->write_log_bitmap = 0;
    vm->write_log_compressed_count = 0;
    vm->write_log_dirty = false;
}

void vm_write_log_clear(VM *vm) {
    vm->write_log_next = 0;
    vm->write_log_bitmap = 0;
    vm->write_log_dirty = false;
}

void vm_write_log_track_cap_start(VM *vm, uint8_t cap, size_t old_start) {
    if (!vm->use_compact_choice || !vm->write_log) return;
    /* Look for existing entry for this cap (most recent first) */
    size_t slot = (vm->write_log_next > 0) ? vm->write_log_next - 1 : vm->write_log_cap - 1;
    for (size_t i = 0; i < vm->write_log_cap; i++) {
        if (vm->write_log_bitmap & (1ULL << slot)) {
            if (vm->write_log[slot].cap_index == cap) {
                vm->write_log[slot].old_start = old_start;
                return;
            }
        }
        if (slot == 0) slot = vm->write_log_cap - 1;
        else slot--;
    }
    /* No existing entry: create new */
    slot = vm->write_log_next++;
    if (slot >= vm->write_log_cap) {
        vm->write_log_next = 0;
        slot = 0;
    }
    vm->write_log[slot].cap_index = cap;
    vm->write_log[slot].old_start = old_start;
    vm->write_log[slot].old_end = (size_t)-1;  /* sentinel: end not changed */
    vm->write_log_bitmap |= (1ULL << slot);
    vm->write_log_dirty = true;
}

void vm_write_log_track_cap_end(VM *vm, uint8_t cap, size_t old_end) {
    if (!vm->use_compact_choice || !vm->write_log) return;
    /* Look for existing entry for this cap (most recent first) */
    size_t slot = (vm->write_log_next > 0) ? vm->write_log_next - 1 : vm->write_log_cap - 1;
    for (size_t i = 0; i < vm->write_log_cap; i++) {
        if (vm->write_log_bitmap & (1ULL << slot)) {
            if (vm->write_log[slot].cap_index == cap) {
                vm->write_log[slot].old_end = old_end;
                return;
            }
        }
        if (slot == 0) slot = vm->write_log_cap - 1;
        else slot--;
    }
    /* No existing entry: create new */
    slot = vm->write_log_next++;
    if (slot >= vm->write_log_cap) {
        vm->write_log_next = 0;
        slot = 0;
    }
    vm->write_log[slot].cap_index = cap;
    vm->write_log[slot].old_start = (size_t)-1;  /* sentinel: start not changed */
    vm->write_log[slot].old_end = old_end;
    vm->write_log_bitmap |= (1ULL << slot);
    vm->write_log_dirty = true;
}

size_t vm_write_log_count_entries(const VM *vm) {
    size_t count = 0;
    for (size_t i = 0; i < vm->write_log_cap; i++) {
        if (vm->write_log_bitmap & (1ULL << i)) {
            const WriteLogEntry *e = &vm->write_log[i];
            if (e->old_start != (size_t)-1 || e->old_end != (size_t)-1) {
                count++;
            }
        }
    }
    return count;
}

void vm_write_log_copy_entries(const VM *vm, WriteLogEntry *dst, size_t dst_cap) {
    size_t copied = 0;
    for (size_t i = 0; i < vm->write_log_cap; i++) {
        if (vm->write_log_bitmap & (1ULL << i)) {
            const WriteLogEntry *e = &vm->write_log[i];
            if (e->old_start != (size_t)-1 || e->old_end != (size_t)-1) {
                if (copied < dst_cap) {
                    dst[copied] = *e;
                    copied++;
                }
            }
        }
    }
}

void vm_write_log_restore(VM *vm, const CompactChoiceHeader *hdr) {
    const uint8_t *p = (const uint8_t *)(hdr + 1);
    if (hdr->max_counter_used > 0) {
        p += hdr->max_counter_used * (sizeof(uint32_t) + sizeof(size_t));
    }
    for (uint8_t i = 0; i < hdr->write_log_count; i++) {
        const WriteLogEntry *e = (const WriteLogEntry *)p;
        if (e->cap_index < MAX_CAPS) {
            vm->cap_start[e->cap_index] = e->old_start;
            vm->cap_end[e->cap_index] = e->old_end;
        }
        p += sizeof(WriteLogEntry);
    }
}

size_t vm_compact_choice_record_size(const CompactChoiceHeader *hdr) {
    size_t sz = sizeof(CompactChoiceHeader);
    if (hdr->max_counter_used > 0) {
        sz += hdr->max_counter_used * (sizeof(uint32_t) + sizeof(size_t));
    }
    sz += hdr->write_log_count * sizeof(WriteLogEntry);
    return sz;
}

/* Choice stack statistics */
size_t vm_choice_stack_memory_usage(VM *vm) {
    return vm->choices_top;
}

size_t vm_choice_stack_depth(VM *vm) {
    /* Depth = number of choice records on stack.
     * We can estimate by dividing total bytes by average record size,
     * or we can walk the stack counting records. Walking is O(n) but accurate.
     * For simplicity, we walk the stack.
     */
    size_t depth = 0;
    size_t pos = 0;
    while (pos < vm->choices_top) {
        CompactChoiceHeader *h = (CompactChoiceHeader *)((uint8_t *)vm->choices + pos);
        size_t rec_size = h->total_size;
        pos += rec_size;
        depth++;
    }
    return depth;
}

size_t vm_choice_record_average_size(VM *vm) {
    if (vm->choice_push_count > 0) {
        return vm->choice_allocated / vm->choice_push_count;
    }
#ifdef SNOBOL_JIT
    if (vm->jit.stats && vm->jit.stats->choice_push_total > 0) {
        return vm->jit.stats->choice_bytes_total / vm->jit.stats->choice_push_total;
    }
#endif
    return 0;
}

void vm_push_choice(VM *vm, size_t ip, size_t pos) {
    if (!vm->choices) return;
#ifdef SNOBOL_PROFILE
    vm->profile.push_count++;
#endif
    if (vm->use_compact_choice) {
        uint8_t num_counters = vm->max_counter_used;
        size_t wl_entries = vm_write_log_count_entries(vm);
        size_t data_size = sizeof(CompactChoiceHeader);
        if (num_counters > 0) {
            data_size += num_counters * (sizeof(uint32_t) + sizeof(size_t));
        }
        data_size += wl_entries * sizeof(WriteLogEntry);
        uint32_t record_size = (uint32_t)(data_size + sizeof(uint32_t));
        record_size = (record_size + 7) & ~7;
        if (vm->choices_top + record_size >= vm->choices_cap) {
            size_t new_cap = vm->choices_cap ? vm->choices_cap * 2 : 4096;
            while (vm->choices_top + record_size >= new_cap) new_cap *= 2;
            void *new_choices = snobol_realloc(vm->choices, new_cap);
            if (!new_choices) return;
            vm->choices = new_choices; vm->choices_cap = new_cap;
        }
        CompactChoiceHeader *h = (CompactChoiceHeader *)((uint8_t *)vm->choices + vm->choices_top);
        h->total_size = record_size;
        h->ip = ip;
        h->pos = pos;
        h->var_count = vm->var_count;
        h->max_cap_used = vm->max_cap_used;
        h->max_counter_used = num_counters;
        h->write_log_count = (uint8_t)wl_entries;
        h->pad = 0;
        uint8_t *p = (uint8_t *)(h + 1);
        if (num_counters > 0) {
            memcpy(p, vm->counters, num_counters * sizeof(uint32_t)); p += num_counters * sizeof(uint32_t);
            memcpy(p, vm->loop_last_pos, num_counters * sizeof(size_t)); p += num_counters * sizeof(size_t);
        }
        vm_write_log_copy_entries(vm, (WriteLogEntry *)p, wl_entries);
        uint32_t *size_ptr = (uint32_t *)((uint8_t *)h + record_size - sizeof(uint32_t));
        *size_ptr = record_size;
        vm->choices_top += record_size;
        vm->choice_allocated += record_size;
        vm->choice_push_count++;
        vm->choice_live_depth++;
        /* Track peak values */
        if (vm->choices_top > vm->choice_peak_memory) vm->choice_peak_memory = vm->choices_top;
        if (vm->choice_live_depth > vm->choice_peak_depth) vm->choice_peak_depth = vm->choice_live_depth;
        vm_write_log_clear(vm);
#ifdef SNOBOL_JIT
        if (vm->jit.stats) { vm->jit.stats->choice_push_total++; vm->jit.stats->choice_bytes_total += record_size; }
#endif
    } else {
        size_t record_size = sizeof(struct choice);
        if (vm->choices_top + record_size >= vm->choices_cap) {
            size_t new_cap = vm->choices_cap ? vm->choices_cap * 2 : (128 * sizeof(struct choice));
            void *new_choices = snobol_realloc(vm->choices, new_cap);
            if (!new_choices) return;
            vm->choices = new_choices; vm->choices_cap = new_cap;
        }
        struct choice *c = (struct choice *)((uint8_t *)vm->choices + vm->choices_top);
        c->ip = ip; c->pos = pos; c->var_count_snapshot = vm->var_count;
        c->max_cap_used_snapshot = vm->max_cap_used; c->max_counter_used_snapshot = vm->max_counter_used;
        if (vm->max_cap_used > 0) {
            size_t cap_size = vm->max_cap_used * sizeof(size_t);
            memcpy(c->cap_start_snapshot, vm->cap_start, cap_size);
            memcpy(c->cap_end_snapshot, vm->cap_end, cap_size);
        }
        if (vm->max_counter_used > 0) {
            memcpy(c->counters_snapshot, vm->counters, vm->max_counter_used * sizeof(uint32_t));
            memcpy(c->loop_last_pos_snapshot, vm->loop_last_pos, vm->max_counter_used * sizeof(size_t));
        }
        vm->choices_top += record_size;
        vm->choice_allocated += record_size;
        vm->choice_push_count++;
        vm->choice_live_depth++;
        /* Track peak values */
        if (vm->choices_top > vm->choice_peak_memory) vm->choice_peak_memory = vm->choices_top;
        if (vm->choice_live_depth > vm->choice_peak_depth) vm->choice_peak_depth = vm->choice_live_depth;
#ifdef SNOBOL_JIT
        if (vm->jit.stats) { vm->jit.stats->choice_push_total++; vm->jit.stats->choice_bytes_total += record_size; }
#endif
    }
}

bool vm_pop_choice(VM *vm) {
    if (!vm->choices || vm->choices_top == 0) return false;
#ifdef SNOBOL_PROFILE
    vm->profile.pop_count++;
#endif
#ifdef SNOBOL_JIT
    if (vm->jit.stats) vm->jit.stats->choice_pop_total++;
#endif
    if (vm->use_compact_choice) {
        uint32_t last_size;
        memcpy(&last_size, (uint8_t *)vm->choices + vm->choices_top - sizeof(uint32_t), sizeof(uint32_t));
        vm->choices_top -= last_size;
        CompactChoiceHeader *h = (CompactChoiceHeader *)((uint8_t *)vm->choices + vm->choices_top);
        vm->ip = h->ip; vm->pos = h->pos; vm->var_count = h->var_count;
        vm->max_cap_used = h->max_cap_used; vm->max_counter_used = h->max_counter_used;
        uint8_t *p = (uint8_t *)(h + 1);
        if (h->max_counter_used > 0) {
            memcpy(vm->counters, p, h->max_counter_used * sizeof(uint32_t)); p += h->max_counter_used * sizeof(uint32_t);
            memcpy(vm->loop_last_pos, p, h->max_counter_used * sizeof(size_t)); p += h->max_counter_used * sizeof(size_t);
        }
        /* Restore captures from write-log entries (delta) */
        for (uint8_t i = 0; i < h->write_log_count; i++) {
            const WriteLogEntry *e = (const WriteLogEntry *)p;
            if (e->cap_index < MAX_CAPS) {
                vm->cap_start[e->cap_index] = e->old_start;
                vm->cap_end[e->cap_index] = e->old_end;
            }
            p += sizeof(WriteLogEntry);
        }
    } else {
        vm->choices_top -= sizeof(struct choice);
        struct choice *c = (struct choice *)((uint8_t *)vm->choices + vm->choices_top);
        vm->ip = c->ip; vm->pos = c->pos; vm->var_count = c->var_count_snapshot;
        vm->max_cap_used = c->max_cap_used_snapshot; vm->max_counter_used = c->max_counter_used_snapshot;
        if (vm->max_cap_used > 0) {
            size_t cap_bytes = vm->max_cap_used * sizeof(size_t);
            memcpy(vm->cap_start, c->cap_start_snapshot, cap_bytes);
            memcpy(vm->cap_end, c->cap_end_snapshot, cap_bytes);
        }
        if (vm->max_counter_used > 0) {
            memcpy(vm->counters, c->counters_snapshot, vm->max_counter_used * sizeof(uint32_t));
            memcpy(vm->loop_last_pos, c->loop_last_pos_snapshot, vm->max_counter_used * sizeof(size_t));
        }
    }
    if (vm->choice_live_depth > 0) vm->choice_live_depth--;
    return true;
}

void snobol_buf_init(snobol_buf *b) { b->cap = 1024; b->data = snobol_malloc(b->cap); b->len = 0; }
void snobol_buf_append(snobol_buf *b, const char *data, size_t len) {
    if (len == 0) return;
    if (b->len + len >= b->cap) {
        size_t newcap = b->cap ? b->cap * 2 : 1024;
        while (b->len + len >= newcap) newcap *= 2;
        b->data = snobol_realloc(b->data, newcap); b->cap = newcap;
    }
    memcpy(b->data + b->len, data, len); b->len += len; b->data[b->len] = '\0';
}
void snobol_buf_clear(snobol_buf *b) { b->len = 0; if (b->data) b->data[0] = '\0'; }
void snobol_buf_free(snobol_buf *b) { if (b->data) { snobol_free(b->data); b->data = nullptr; } b->len = b->cap = 0; }

bool vm_run(VM *vm) {
    size_t initial_cap = 4096;
    vm->choices = snobol_malloc(initial_cap);
    if (!vm->choices) return false;
    vm->choices_cap = initial_cap; vm->choices_top = 0;
    vm->use_compact_choice = (getenv("SNOBOL_LEGACY_CHOICE") == nullptr);
    if (vm->use_compact_choice) {
        vm_write_log_init(vm);
        vm->write_log_cap = MAX_CAPS;
        vm->write_log = snobol_malloc(vm->write_log_cap * sizeof(WriteLogEntry));
        if (!vm->write_log) {
            snobol_free(vm->choices);
            vm->choices = nullptr;
            return false;
        }
    }

    while (1) {
#ifdef SNOBOL_PROFILE
        vm->profile.dispatch_count++;
#endif
        if (vm->ip >= vm->bc_len) { if (!vm_pop_choice(vm)) goto fail_ret; continue; }
#ifdef SNOBOL_JIT
        {
        size_t current_ip = vm->ip;
        const SnobolJitConfig *jit_cfg = snobol_jit_get_config();

        if (vm->jit.ctx && vm->jit.ctx->stop_compiling &&
            (!vm->jit.traces || vm->jit.traces[current_ip] == nullptr)) {
            /* Profitability gate permanently disabled JIT for this pattern and
             * there is no trace at the current IP.  Fall straight through to
             * the interpreter without paying profiling overhead each dispatch. */
        } else if (vm->jit.enabled && vm->jit.traces && vm->jit.traces[current_ip]) {
            /* ---- Execute compiled trace ---- */
            if (vm->jit.stats) {
                vm->jit.stats->entries_total++;
                if (vm->jit.search_mode) vm->jit.stats->search_entries_total++;
            }
            if (vm->jit.ctx)   vm->jit.ctx->ctx_entries++;

            uint64_t t_exec = (vm->jit.stats) ? snobol_jit_now_ns() : 0;
            jit_trace_fn fn = (jit_trace_fn)vm->jit.traces[current_ip];
            fn(vm);
            if (vm->jit.stats) {
                uint64_t exec_ns = snobol_jit_now_ns() - t_exec;
                vm->jit.stats->exec_time_ns_total += exec_ns;
                vm->jit.stats->time_ns_total      += exec_ns; /* legacy */
                vm->jit.stats->exits_total++;
                if (vm->ip == current_ip) {
                    vm->jit.stats->bailouts_total++;
                    vm->jit.stats->bailout_match_fail_total++;
                    /* Attribute quick first-op mismatches in search mode
                     * to expected candidate rejection so they are distinguishable
                     * from unsupported control flow or safety fallbacks. */
                    if (vm->jit.search_mode) {
                        vm->jit.stats->bailout_search_candidate_total++;
                        /* search_candidate_rejects is the canonical per-exit
                         * counter for expected candidate rejections only.
                         * It SHALL NOT be incremented for unsupported-op or
                         * left-region bailouts (those land in bailout_partial). */
                        vm->jit.stats->search_candidate_rejects++;
                    }
                } else {
                    vm->jit.stats->bailout_partial_total++;
                }
            }

            /* Per-pattern early-exit rule */
            if (vm->jit.ctx) {
                vm->jit.ctx->ctx_exits++;
                /* Separately track search-mode exits for attribution */
                if (vm->jit.search_mode)
                    vm->jit.ctx->search_ctx_exits++;
                if (!vm->jit.ctx->stop_compiling &&
                    vm->jit.ctx->ctx_entries > 100 &&
                    jit_cfg->max_exit_rate_pct < 100) {
                    uint64_t entries = vm->jit.ctx->ctx_entries;
                    uint64_t exits   = vm->jit.ctx->ctx_exits;
                    if (exits * 100 / entries > (uint64_t)jit_cfg->max_exit_rate_pct) {
                        vm->jit.ctx->stop_compiling = true;
                        if (vm->jit.stats) vm->jit.stats->skipped_exit_rate_total++;
                    }
                }
            }

            if (vm->ip != current_ip) continue;
            /* Fell through (bailout): let interpreter handle current ip */

        } else if (vm->jit.ip_counts && current_ip < vm->bc_len) {
            uint64_t count = ++vm->jit.ip_counts[current_ip];

            /* in search mode, use a lower hotness threshold so
             * single-op hot search patterns become JIT-eligible sooner. */
            uint64_t hot_threshold = vm->jit.search_mode
                ? jit_cfg->search_hotness_threshold
                : jit_cfg->hotness_threshold;

            bool should_try = (count == hot_threshold) &&
                              vm->jit.traces &&
                              vm->jit.traces[current_ip] == nullptr &&
                              !(vm->jit.ctx && vm->jit.ctx->stop_compiling);

            if (should_try) {
                /* Profitability gate: use search_mode flag */
                if (!snobol_jit_should_compile(vm, current_ip, jit_cfg, vm->jit.search_mode)) {
                    if (vm->jit.ctx) vm->jit.ctx->stop_compiling = true;
                    /* attribute cold skip to search-mode counter when appropriate */
                    if (vm->jit.stats) {
                        if (vm->jit.search_mode)
                            vm->jit.stats->skipped_search_cold_total++;
                        else
                            vm->jit.stats->skipped_cold_total++;
                    }
                } else {
                    /* Compile budget check */
                    uint64_t budget_used = vm->jit.ctx ? vm->jit.ctx->compile_time_ns : 0;
                    if (budget_used >= jit_cfg->compile_budget_ns) {
                        if (vm->jit.ctx) vm->jit.ctx->stop_compiling = true;
                        if (vm->jit.stats) vm->jit.stats->skipped_budget_total++;
                    } else {
                        if (vm->jit.stats) vm->jit.stats->compilations_total++;
                        uint64_t t_compile = (vm->jit.stats) ? snobol_jit_now_ns() : 0;
                        size_t code_sz = 0;
                        void *trace = (void *)snobol_jit_compile(vm, current_ip, &code_sz);
                        if (t_compile && vm->jit.stats) {
                            uint64_t compile_ns = snobol_jit_now_ns() - t_compile;
                            vm->jit.stats->compile_time_ns_total += compile_ns;
                            if (vm->jit.ctx) vm->jit.ctx->compile_time_ns += compile_ns;
                        }
                        if (trace) {
                            vm->jit.traces[current_ip] = trace;
                            if (vm->jit.ctx && vm->jit.ctx->trace_sizes)
                                vm->jit.ctx->trace_sizes[current_ip] = code_sz;
                        } else {
                            if (vm->jit.stats) vm->jit.stats->bailouts_total++;
                        }
                    }
                }
            }
        }
        } /* end SNOBOL_JIT block */
#endif
        uint8_t op = vm->bc[vm->ip++];
        switch (op) {
            case OP_NOP: break; /* fusion filler — skip one byte */
            case OP_ACCEPT:
                if (vm->choices) { snobol_free(vm->choices); vm->choices = nullptr; }
                if (vm->use_compact_choice && vm->write_log) { vm_write_log_free(vm); }
                return true;
            case OP_FAIL: if (!vm_pop_choice(vm)) goto fail_ret; break;
            case OP_JMP: { uint32_t tgt = read_u32(vm->bc, vm->bc_len, &vm->ip); vm->ip = (size_t)tgt; break; }
            case OP_SPLIT: { uint32_t a = read_u32(vm->bc, vm->bc_len, &vm->ip); uint32_t b = read_u32(vm->bc, vm->bc_len, &vm->ip);
                vm_push_choice(vm, (size_t)b, vm->pos); vm->ip = (size_t)a; break; }
            case OP_LIT: {
                uint32_t off = read_u32(vm->bc, vm->bc_len, &vm->ip); uint32_t len = read_u32(vm->bc, vm->bc_len, &vm->ip);
                if (off == vm->ip) vm->ip += len;
                if (len <= vm->len - vm->pos && memcmp(vm->s + vm->pos, vm->bc + off, len) == 0) { vm->pos += len; }
                else if (!vm_pop_choice(vm)) goto fail_ret;
                break;
            }
            case OP_ANY: {
                uint16_t set_id = read_u16(vm->bc, vm->bc_len, &vm->ip); uint16_t count, ci;
                const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count, &ci);
                uint64_t map[2]; bool is_ascii = ranges && ranges_to_ascii_bitmap(ranges, count, map);
                if (is_ascii) {
                    if (vm->pos < vm->len && bitmap_test(map, (uint8_t)vm->s[vm->pos])) vm->pos++;
                    else if (!vm_pop_choice(vm)) goto fail_ret;
                } else {
                    uint32_t cp; int bytes;
                    if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes)) { if (!vm_pop_choice(vm)) goto fail_ret; }
                    else if (ranges && range_contains(ranges, count, cp)) vm->pos += bytes;
                    else if (!vm_pop_choice(vm)) goto fail_ret;
                }
                break;
            }
            case OP_NOTANY: {
                uint16_t set_id = read_u16(vm->bc, vm->bc_len, &vm->ip); uint16_t count, ci;
                const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count, &ci);
                uint64_t map[2]; bool is_ascii = ranges && ranges_to_ascii_bitmap(ranges, count, map);
                if (is_ascii) {
                    if (vm->pos < vm->len && !bitmap_test(map, (uint8_t)vm->s[vm->pos])) vm->pos++;
                    else if (!vm_pop_choice(vm)) goto fail_ret;
                } else {
                    uint32_t cp; int bytes;
                    if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes)) { if (!vm_pop_choice(vm)) goto fail_ret; }
                    else if (ranges && range_contains(ranges, count, cp)) { if (!vm_pop_choice(vm)) goto fail_ret; }
                    else vm->pos += bytes;
                }
                break;
            }
            case OP_SPAN: {
                uint16_t set_id = read_u16(vm->bc, vm->bc_len, &vm->ip); uint16_t count, ci;
                const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count, &ci);
                uint64_t map[2]; bool is_ascii = ranges && ranges_to_ascii_bitmap(ranges, count, map);
                if (is_ascii) {
                    if (vm->pos < vm->len && bitmap_test(map, (uint8_t)vm->s[vm->pos])) {
                        vm->pos++; while (vm->pos < vm->len && bitmap_test(map, (uint8_t)vm->s[vm->pos])) vm->pos++;
                    } else if (!vm_pop_choice(vm)) goto fail_ret;
                } else {
                    uint32_t cp; int bytes;
                    if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes) || !ranges || !range_contains(ranges, count, cp)) {
                        if (!vm_pop_choice(vm)) goto fail_ret;
                    } else {
                        vm->pos += bytes;
                        while (utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes) && range_contains(ranges, count, cp)) vm->pos += bytes;
                    }
                }
                break;
            }
            case OP_BREAK: {
                uint16_t set_id = read_u16(vm->bc, vm->bc_len, &vm->ip); uint16_t count, ci;
                const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count, &ci);
                uint64_t map[2]; bool is_ascii = ranges && ranges_to_ascii_bitmap(ranges, count, map);
                if (is_ascii) { while (vm->pos < vm->len && !bitmap_test(map, (uint8_t)vm->s[vm->pos])) vm->pos++; }
                else { uint32_t cp; int bytes; while (utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes) && (!ranges || !range_contains(ranges, count, cp))) vm->pos += bytes; }
                break;
            }
            case OP_CAP_START: {
                uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip);
                if (r < MAX_CAPS) {
                    if (vm->use_compact_choice) {
                        vm_write_log_track_cap_start(vm, r, vm->cap_start[r]);
                    }
                    vm->cap_start[r] = vm->pos;
                    if (r >= vm->max_cap_used) vm->max_cap_used = r + 1;
                }
                break;
            }
            case OP_CAP_END: {
                uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip);
                if (r < MAX_CAPS) {
                    if (vm->use_compact_choice) {
                        vm_write_log_track_cap_end(vm, r, vm->cap_end[r]);
                    }
                    vm->cap_end[r] = vm->pos;
                    if (r >= vm->max_cap_used) vm->max_cap_used = r + 1;
                }
                break;
            }
            case OP_ASSIGN: { uint16_t var = read_u16(vm->bc, vm->bc_len, &vm->ip); uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip);
                if (var < MAX_VARS && r < MAX_CAPS) { if (var >= vm->var_count) vm->var_count = (size_t)var + 1; vm->var_start[var] = vm->cap_start[r]; vm->var_end[var] = vm->cap_end[r]; } break; }
            case OP_LEN: {
                uint32_t n = read_u32(vm->bc, vm->bc_len, &vm->ip); size_t p = vm->pos; uint32_t i;
                for (i = 0; i < n; ++i) { uint32_t cp; int bytes; if (!utf8_peek_next(vm->s, vm->len, p, &cp, &bytes)) break; p += bytes; }
                if (i != n) { if (!vm_pop_choice(vm)) goto fail_ret; } else vm->pos = p; break; }
            case OP_EVAL: {
                /* Built-in dispatch table.
                 * If fn_id is a known built-in (< SNOBOL_FN_MAX), dispatch
                 * directly to the C function.  Otherwise fall back to the
                 * host eval_fn callback to preserve backward compatibility. */
                uint16_t fn = read_u16(vm->bc, vm->bc_len, &vm->ip);
                uint8_t r   = read_u8 (vm->bc, vm->bc_len, &vm->ip);
                if (r >= MAX_CAPS) { if (!vm_pop_choice(vm)) goto fail_ret; break; }

                /* Validate capture bounds */
                bool cap_ok = (vm->cap_end[r] >= vm->cap_start[r]) &&
                              (vm->cap_end[r] <= vm->len);
                if (!cap_ok) { if (!vm_pop_choice(vm)) goto fail_ret; break; }

                const char *cap_s = vm->s + vm->cap_start[r];
                size_t      cap_l = vm->cap_end[r] - vm->cap_start[r];
                bool ok = true;

                SNOBOL_LOG("OP_EVAL: fn=%u reg=%u cap_len=%zu", fn, r, cap_l);

                if (fn > SNOBOL_FN_NONE && fn < SNOBOL_FN_MAX) {
                    /* Direct C dispatch (no host callback) */
                    snobol_buf tmp_out = {0};
                    snobol_buf_init(&tmp_out);

                    switch ((snobol_builtin_fn_t)fn) {
                        /* --- string transformations (output to vm->out if set) --- */
                        case SNOBOL_FN_SIZE: {
                            /* SIZE: always succeeds, useful as predicate */
                            (void)snobol_size(cap_s, cap_l);
                            ok = true;
                            break;
                        }
                        case SNOBOL_FN_TRIM: {
                            ok = snobol_trim(cap_s, cap_l, &tmp_out);
                            if (ok && vm->out) snobol_buf_append(vm->out, tmp_out.data, tmp_out.len);
                            if (ok && vm->emit_fn) vm->emit_fn(tmp_out.data, tmp_out.len, vm->emit_udata);
                            break;
                        }
                        case SNOBOL_FN_REVERSE: {
                            ok = snobol_reverse(cap_s, cap_l, &tmp_out);
                            if (ok && vm->out) snobol_buf_append(vm->out, tmp_out.data, tmp_out.len);
                            if (ok && vm->emit_fn) vm->emit_fn(tmp_out.data, tmp_out.len, vm->emit_udata);
                            break;
                        }
                        case SNOBOL_FN_UPPER: {
                            ok = snobol_upper(cap_s, cap_l, &tmp_out);
                            if (ok && vm->out) snobol_buf_append(vm->out, tmp_out.data, tmp_out.len);
                            if (ok && vm->emit_fn) vm->emit_fn(tmp_out.data, tmp_out.len, vm->emit_udata);
                            break;
                        }
                        case SNOBOL_FN_LOWER: {
                            ok = snobol_lower(cap_s, cap_l, &tmp_out);
                            if (ok && vm->out) snobol_buf_append(vm->out, tmp_out.data, tmp_out.len);
                            if (ok && vm->emit_fn) vm->emit_fn(tmp_out.data, tmp_out.len, vm->emit_udata);
                            break;
                        }
                        /* --- predicates (succeed or fail, no output) --- */
                        case SNOBOL_FN_INTEGER:   ok = snobol_integer(cap_s, cap_l);     break;
                        case SNOBOL_FN_REAL:       ok = snobol_real(cap_s, cap_l);        break;
                        case SNOBOL_FN_NUMERIC:    ok = snobol_numeric(cap_s, cap_l);     break;
                        /* Multi-arg functions (IDENT, DIFFER, LEX*, REPLACE, DUPL,
                         * SUBSTR, LPAD, RPAD, CHAR, ORD) require additional context
                         * not available via single-register OP_EVAL.  Fall back to
                         * host callback when fn_id reaches these cases. */
                        default:
                            /* Fallback to host eval_fn */
                            ok = !vm->eval_fn ||
                                 vm->eval_fn((int)fn, vm->s,
                                             vm->cap_start[r], vm->cap_end[r],
                                             vm->eval_udata);
                            break;
                    }
                    snobol_buf_free(&tmp_out);
                } else {
                    /* Host callback fallback (fn == 0 or unknown >= SNOBOL_FN_MAX) */
                    ok = !vm->eval_fn ||
                         vm->eval_fn((int)fn, vm->s,
                                     vm->cap_start[r], vm->cap_end[r],
                                     vm->eval_udata);
                }
                if (!ok) { if (!vm_pop_choice(vm)) goto fail_ret; }
                break;
            }
            case OP_ANCHOR: { uint8_t type = read_u8(vm->bc, vm->bc_len, &vm->ip); bool ok = (type == 0) ? (vm->pos == 0) : (vm->pos == vm->len);
                if (!ok) { if (!vm_pop_choice(vm)) goto fail_ret; } break; }
            case OP_REPEAT_INIT: {
                uint8_t loop_id = read_u8(vm->bc, vm->bc_len, &vm->ip); uint32_t min = read_u32(vm->bc, vm->bc_len, &vm->ip); uint32_t max = read_u32(vm->bc, vm->bc_len, &vm->ip); uint32_t skip = read_u32(vm->bc, vm->bc_len, &vm->ip);
                if (loop_id < MAX_LOOPS) {
                    vm->counters[loop_id] = 0; vm->loop_min[loop_id] = min; vm->loop_max[loop_id] = max; vm->loop_last_pos[loop_id] = vm->pos;
                    if (loop_id + 1 > vm->max_counter_used) vm->max_counter_used = loop_id + 1;
                    if (min == 0) vm_push_choice(vm, (size_t)skip, vm->pos);
                } break;
            }
            case OP_REPEAT_STEP: {
                uint8_t loop_id = read_u8(vm->bc, vm->bc_len, &vm->ip); uint32_t target = read_u32(vm->bc, vm->bc_len, &vm->ip);
                if (loop_id < MAX_LOOPS) {
                    vm->counters[loop_id]++; uint32_t count = vm->counters[loop_id];
                    if (count < vm->loop_min[loop_id]) { vm->loop_last_pos[loop_id] = vm->pos; vm->ip = (size_t)target; }
                    else if (vm->loop_max[loop_id] == (uint32_t)-1 || count < vm->loop_max[loop_id]) {
                        if (vm->loop_max[loop_id] == (uint32_t)-1 && vm->pos == vm->loop_last_pos[loop_id]) { }
                        else { vm_push_choice(vm, vm->ip, vm->pos); vm->loop_last_pos[loop_id] = vm->pos; vm->ip = (size_t)target; }
                    }
                } break;
            }
            case OP_EMIT_LITERAL: {
                uint32_t off = read_u32(vm->bc, vm->bc_len, &vm->ip); uint32_t len = read_u32(vm->bc, vm->bc_len, &vm->ip);
                if (off == vm->ip) vm->ip += len;
                if (vm->out) snobol_buf_append(vm->out, (const char *)vm->bc + off, (size_t)len);
                if (vm->emit_fn) vm->emit_fn((const char *)vm->bc + off, (size_t)len, vm->emit_udata); break;
            }
            case OP_EMIT_CAPTURE: {
                uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip);
                if (r < MAX_CAPS && vm->cap_end[r] >= vm->cap_start[r] && vm->cap_end[r] <= vm->len) {
                    if (vm->out) snobol_buf_append(vm->out, vm->s + vm->cap_start[r], vm->cap_end[r] - vm->cap_start[r]);
                    if (vm->emit_fn) vm->emit_fn(vm->s + vm->cap_start[r], vm->cap_end[r] - vm->cap_start[r], vm->emit_udata);
                } break;
            }
            case OP_EMIT_EXPR: {
                /* LEGACY alias: old discriminants 1=upper, 2=length.
                 * Map to SNBL_FMT_* and fall through to the same logic
                 * as OP_EMIT_FORMAT. */
                uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip);
                uint8_t expr_type = read_u8(vm->bc, vm->bc_len, &vm->ip);
                uint8_t mapped_type;
                if (expr_type == 1)      mapped_type = SNBL_FMT_UPPER;
                else if (expr_type == 2) mapped_type = SNBL_FMT_LENGTH;
                else                     mapped_type = 0; /* unknown → emit raw */

                if (r < MAX_CAPS && vm->cap_end[r] >= vm->cap_start[r] && vm->cap_end[r] <= vm->len) {
                    const char *data = vm->s + vm->cap_start[r];
                    size_t len = vm->cap_end[r] - vm->cap_start[r];
                    if (mapped_type == SNBL_FMT_UPPER) {
                        char *tmp = snobol_malloc(len + 1);
                        for (size_t i = 0; i < len; ++i) tmp[i] = (data[i] >= 'a' && data[i] <= 'z') ? data[i] - 32 : data[i];
                        if (vm->out) snobol_buf_append(vm->out, tmp, len);
                        if (vm->emit_fn) vm->emit_fn(tmp, len, vm->emit_udata);
                        snobol_free(tmp);
                    } else if (mapped_type == SNBL_FMT_LENGTH) {
                        char tmp[32]; int n = snprintf(tmp, sizeof(tmp), "%zu", len);
                        if (vm->out) snobol_buf_append(vm->out, tmp, (size_t)n);
                        if (vm->emit_fn) vm->emit_fn(tmp, (size_t)n, vm->emit_udata);
                    } else {
                        if (vm->out) snobol_buf_append(vm->out, data, len);
                        if (vm->emit_fn) vm->emit_fn(data, len, vm->emit_udata);
                    }
                } break;
            }
            
            /* Table-backed and formatted replacement opcodes */
            case OP_EMIT_TABLE: {
                /* Lookup table[key] and emit the value
                 * Format: table_id u16 (SNBL_TABLE_ID_UNBOUND=unbound),
                 *         key_type u8,
                 *         name_len u8, name_bytes[name_len]  ← skip at runtime
                 *   then key payload:
                 *   - key_type=0 (literal): key_len u16, key_bytes[key_len]
                 *   - key_type=1 (capture): key_reg u8
                 * If key not found, emit empty string (graceful degradation) */
                uint16_t table_id = read_u16(vm->bc, vm->bc_len, &vm->ip);
                uint8_t key_type = read_u8(vm->bc, vm->bc_len, &vm->ip);
                /* skip embedded table name (resolved at bind time) */
                uint8_t name_len = read_u8(vm->bc, vm->bc_len, &vm->ip);
                vm->ip += name_len;

#ifdef SNOBOL_DYNAMIC_PATTERN
                snobol_table_t *table = vm_get_table(vm, table_id);
                const char *value = nullptr;

                if (key_type == 0) {
                    /* Literal key: read key_len and key_bytes from bytecode */
                    uint16_t key_len = read_u16(vm->bc, vm->bc_len, &vm->ip);
                    if (key_len > 0 && key_len < 1024) {
                        char *key = (char *)snobol_malloc(key_len + 1);
                        if (key) {
                            memcpy(key, vm->bc + vm->ip, key_len);
                            key[key_len] = '\0';
                            vm->ip += key_len;

                            /* Lookup and emit */
                            if (table) {
                                value = table_get(table, key);
                            }
                            snobol_free(key);
                        }
                    }
                } else if (key_type == 1) {
                    /* Capture-derived key: read key_reg */
                    uint8_t key_reg = read_u8(vm->bc, vm->bc_len, &vm->ip);

                    if (table && key_reg < MAX_CAPS &&
                        vm->cap_end[key_reg] >= vm->cap_start[key_reg] &&
                        vm->cap_end[key_reg] <= vm->len) {

                        /* Extract key */
                        size_t key_len = vm->cap_end[key_reg] - vm->cap_start[key_reg];
                        char *key = (char *)snobol_malloc(key_len + 1);
                        if (key) {
                            memcpy(key, vm->s + vm->cap_start[key_reg], key_len);
                            key[key_len] = '\0';

                            /* Lookup */
                            if (table) {
                                value = table_get(table, key);
                            }
                            snobol_free(key);
                        }
                    }
                }

                /* Emit the value (or nothing if not found - graceful degradation) */
                if (value) {
                    size_t val_len = strlen(value);
                    if (vm->out) snobol_buf_append(vm->out, value, val_len);
                    if (vm->emit_fn) vm->emit_fn(value, val_len, vm->emit_udata);
                }
#else
                (void)table_id; (void)key_type; (void)name_len; /* Table support not enabled */
                /* Still need to advance ip past key payload to avoid misalignment */
                if (key_type == 0) {
                    uint16_t skip_len = read_u16(vm->bc, vm->bc_len, &vm->ip);
                    vm->ip += skip_len;
                } else if (key_type == 1) {
                    vm->ip += 1;
                }
#endif
                break;
            }
            case OP_EMIT_FORMAT: {
                /* Format capture with specified format type
                 * Format: reg u8, format_type u8 (SNBL_FMT_*)
                 * SNBL_FMT_LPAD / SNBL_FMT_RPAD also read: width u16, fill_char u8 */
                uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip);
                uint8_t format_type = read_u8(vm->bc, vm->bc_len, &vm->ip);
                
                if (r < MAX_CAPS && vm->cap_end[r] >= vm->cap_start[r] && vm->cap_end[r] <= vm->len) {
                    const char *data = vm->s + vm->cap_start[r];
                    size_t len = vm->cap_end[r] - vm->cap_start[r];
                    
                    if (format_type == SNBL_FMT_UPPER) {
                        /* Uppercase */
                        char *tmp = (char *)snobol_malloc(len + 1);
                        for (size_t i = 0; i < len; ++i) {
                            tmp[i] = (data[i] >= 'a' && data[i] <= 'z') ? data[i] - 32 : data[i];
                        }
                        tmp[len] = '\0';
                        if (vm->out) snobol_buf_append(vm->out, tmp, len);
                        if (vm->emit_fn) vm->emit_fn(tmp, len, vm->emit_udata);
                        snobol_free(tmp);
                    } else if (format_type == SNBL_FMT_LOWER) {
                        /* Lowercase */
                        char *tmp = (char *)snobol_malloc(len + 1);
                        for (size_t i = 0; i < len; ++i) {
                            tmp[i] = (data[i] >= 'A' && data[i] <= 'Z') ? data[i] + 32 : data[i];
                        }
                        tmp[len] = '\0';
                        if (vm->out) snobol_buf_append(vm->out, tmp, len);
                        if (vm->emit_fn) vm->emit_fn(tmp, len, vm->emit_udata);
                        snobol_free(tmp);
                    } else if (format_type == SNBL_FMT_LENGTH) {
                        /* Length as string */
                        char tmp[32];
                        int n = snprintf(tmp, sizeof(tmp), "%zu", len);
                        if (vm->out) snobol_buf_append(vm->out, tmp, (size_t)n);
                        if (vm->emit_fn) vm->emit_fn(tmp, (size_t)n, vm->emit_udata);
                    } else if (format_type == SNBL_FMT_LPAD || format_type == SNBL_FMT_RPAD) {
                        /* Padding: read width (u16, big-endian) and fill_char (u8) */
                        uint16_t width = read_u16(vm->bc, vm->bc_len, &vm->ip);
                        uint8_t  fill  = read_u8(vm->bc, vm->bc_len, &vm->ip);
                        if (width > 1024) width = 1024; /* cap */
                        if (len >= width) {
                            /* No padding needed - emit as-is */
                            if (vm->out) snobol_buf_append(vm->out, data, len);
                            if (vm->emit_fn) vm->emit_fn(data, len, vm->emit_udata);
                        } else {
                            size_t pad = width - len;
                            size_t total = (size_t)width;
                            char *buf = (char *)snobol_malloc(total);
                            if (buf) {
                                if (format_type == SNBL_FMT_LPAD) {
                                    memset(buf, fill, pad);
                                    memcpy(buf + pad, data, len);
                                } else {
                                    memcpy(buf, data, len);
                                    memset(buf + len, fill, pad);
                                }
                                if (vm->out) snobol_buf_append(vm->out, buf, total);
                                if (vm->emit_fn) vm->emit_fn(buf, total, vm->emit_udata);
                                snobol_free(buf);
                            }
                        }
                    } else {
                        /* Unknown format - emit raw */
                        if (vm->out) snobol_buf_append(vm->out, data, len);
                        if (vm->emit_fn) vm->emit_fn(data, len, vm->emit_udata);
                    }
                } else {
                    /* Missing capture or LPAD/RPAD: read and discard extra operands */
                    if (format_type == SNBL_FMT_LPAD || format_type == SNBL_FMT_RPAD) {
                        read_u16(vm->bc, vm->bc_len, &vm->ip); /* width */
                        read_u8(vm->bc, vm->bc_len, &vm->ip);  /* fill */
                    }
                    /* emit empty (graceful degradation) */
                }
                break;
            }
            
            /* Control flow opcodes */
            case OP_LABEL: {
                /* Define a label target - just skip during execution
                 * Labels are resolved at compile time, OP_LABEL is a no-op at runtime */
                uint16_t label_id = read_u16(vm->bc, vm->bc_len, &vm->ip);
                (void)label_id;
                break;
            }
            case OP_GOTO: {
                /* Unconditional transfer to label
                 * CRITICAL: GOTO does NOT restore backtracking state
                 * This distinguishes explicit control flow from ordinary backtracking */
                uint16_t label_id = read_u16(vm->bc, vm->bc_len, &vm->ip);
                uint32_t target = vm_get_label_offset(vm, label_id);
                
                if (target == 0 && label_id != 0) {
                    /* Invalid label - fail without restoring backtracking state */
                    vm->in_goto_fail = true;
                    if (!vm_pop_choice(vm)) goto fail_ret;
                } else {
                    /* Transfer control to label target
                     * Note: We do NOT pop any choice here - GOTO is explicit flow,
                     * not backtracking. The choice stack remains for future backtracking. */
                    vm->ip = target;
                }
                break;
            }
            case OP_GOTO_F: {
                /* Transfer to label if last match failed
                 * This is used for conditional control flow after a match attempt */
                uint16_t label_id = read_u16(vm->bc, vm->bc_len, &vm->ip);
                
                if (vm->in_goto_fail) {
                    uint32_t target = vm_get_label_offset(vm, label_id);
                    if (target == 0 && label_id != 0) {
                        if (!vm_pop_choice(vm)) goto fail_ret;
                    } else {
                        vm->ip = target;
                        vm->in_goto_fail = false;
                    }
                } else {
                    /* Continue normally - no failure occurred */
                    break;
                }
                break;
            }
            
#ifdef SNOBOL_DYNAMIC_PATTERN
            /* Table operations */
            case OP_TABLE_GET: {
                /* Lookup table[key] and store result in dest register
                 * Format: table_id u16, key_reg u8, dest_reg u8
                 * If key not found, fail (trigger backtracking) */
                uint16_t table_id = read_u16(vm->bc, vm->bc_len, &vm->ip);
                uint8_t key_reg = read_u8(vm->bc, vm->bc_len, &vm->ip);
                uint8_t dest_reg = read_u8(vm->bc, vm->bc_len, &vm->ip);
                
                snobol_table_t *table = vm_get_table(vm, table_id);
                if (!table) {
                    /* Invalid table - fail */
                    if (!vm_pop_choice(vm)) goto fail_ret;
                    break;
                }
                
                /* Get key from capture register */
                if (key_reg >= MAX_CAPS || vm->cap_end[key_reg] <= vm->cap_start[key_reg]) {
                    /* Invalid key register - fail */
                    if (!vm_pop_choice(vm)) goto fail_ret;
                    break;
                }
                
                /* Extract key string */
                size_t key_start = vm->cap_start[key_reg];
                size_t key_end = vm->cap_end[key_reg];
                size_t key_len = key_end - key_start;
                
                /* Allocate and copy key */
                char *key = (char *)snobol_malloc(key_len + 1);
                if (!key) {
                    if (!vm_pop_choice(vm)) goto fail_ret;
                    break;
                }
                memcpy(key, vm->s + key_start, key_len);
                key[key_len] = '\0';
                
                /* Lookup in table */
                const char *value = table_get(table, key);
                snobol_free(key);
                
                if (!value) {
                    /* Key not found - fail (trigger backtracking) */
                    if (!vm_pop_choice(vm)) goto fail_ret;
                    break;
                }
                
                /* Store result: update dest_reg to match the value
                 * For now, we just note the lookup succeeded
                 * Full implementation would store value in a temp buffer */
                (void)dest_reg; /* Result available via table */
                break;
            }
            case OP_TABLE_SET: {
                /* Set table[key] = value
                 * Format: table_id u16, key_reg u8, value_reg u8
                 * Always succeeds (table operations don't fail) */
                uint16_t table_id = read_u16(vm->bc, vm->bc_len, &vm->ip);
                uint8_t key_reg = read_u8(vm->bc, vm->bc_len, &vm->ip);
                uint8_t value_reg = read_u8(vm->bc, vm->bc_len, &vm->ip);
                
                snobol_table_t *table = vm_get_table(vm, table_id);
                if (!table) {
                    /* Invalid table - fail */
                    if (!vm_pop_choice(vm)) goto fail_ret;
                    break;
                }
                
                /* Get key from capture register */
                if (key_reg >= MAX_CAPS || vm->cap_end[key_reg] <= vm->cap_start[key_reg]) {
                    if (!vm_pop_choice(vm)) goto fail_ret;
                    break;
                }
                
                /* Get value from capture register */
                if (value_reg >= MAX_CAPS || vm->cap_end[value_reg] <= vm->cap_start[value_reg]) {
                    if (!vm_pop_choice(vm)) goto fail_ret;
                    break;
                }
                
                size_t key_start = vm->cap_start[key_reg];
                size_t key_end = vm->cap_end[key_reg];
                size_t key_len = key_end - key_start;
                
                size_t val_start = vm->cap_start[value_reg];
                size_t val_end = vm->cap_end[value_reg];
                size_t val_len = val_end - val_start;
                
                /* Allocate and copy key */
                char *key = (char *)snobol_malloc(key_len + 1);
                if (!key) {
                    if (!vm_pop_choice(vm)) goto fail_ret;
                    break;
                }
                memcpy(key, vm->s + key_start, key_len);
                key[key_len] = '\0';
                
                /* Allocate and copy value */
                char *value = (char *)snobol_malloc(val_len + 1);
                if (!value) {
                    snobol_free(key);
                    if (!vm_pop_choice(vm)) goto fail_ret;
                    break;
                }
                memcpy(value, vm->s + val_start, val_len);
                value[val_len] = '\0';
                
                /* Set in table */
                (void)table_set(table, key, value);

                snobol_free(key);
                snobol_free(value);
                break;
            }
            case OP_DYNAMIC_DEF: {
                /* Define dynamic pattern source and bytecode for runtime caching
                 * Format: source_len(u32) + source_text + bc_len(u32) + bytecode
                 * 
                 * Canonical approach: Store both source text and compiled bytecode.
                 * - Source text: Used as cache key for reuse across repeated EVAL(...)
                 * - Bytecode: Used for efficient execution in the VM
                 */
                uint32_t source_len = read_u32(vm->bc, vm->bc_len, &vm->ip);

                /* Copy the source text for cache keying */
                char *source_copy = (char *)snobol_malloc(source_len + 1);
                if (source_copy) {
                    memcpy(source_copy, vm->bc + vm->ip, source_len);
                    source_copy[source_len] = '\0';
                }
                vm->ip += source_len;

                /* Copy the bytecode for execution */
                uint32_t bc_len = read_u32(vm->bc, vm->bc_len, &vm->ip);
                uint8_t *bc_copy = (uint8_t *)snobol_malloc(bc_len);
                if (bc_copy) {
                    memcpy(bc_copy, vm->bc + vm->ip, bc_len);
                }
                vm->ip += bc_len;

                /* Store for OP_DYNAMIC to use */
#ifdef SNOBOL_DYNAMIC_PATTERN
                /* Free any previously pending data */
                if (vm->dyn_pending_source) {
                    snobol_free(vm->dyn_pending_source);
                }
                if (vm->dyn_pending_bc) {
                    snobol_free(vm->dyn_pending_bc);
                }
                
                vm->dyn_pending_source = source_copy;
                vm->dyn_pending_source_len = source_len;
                vm->dyn_pending_bc = bc_copy;
                vm->dyn_pending_bc_len = bc_len;
#else
                if (source_copy) snobol_free(source_copy);
                if (bc_copy) snobol_free(bc_copy);
#endif
                break;
            }
            case OP_DYNAMIC: {
                /* Evaluate dynamic pattern using cached compilation
                 * Format: (no additional operands - uses data from OP_DYNAMIC_DEF)
                 *
                 * Canonical implementation:
                 * 1. Look up pattern in dynamic pattern cache by source text
                 * 2. On hit: use cached pattern (retains reference)
                 * 3. On miss: create pattern from bytecode, cache it
                 * 4. Execute the pattern with full VM semantics
                 * 5. Handle success/failure with proper backtracking
                 *
                 * This enables:
                 * - Cache reuse across repeated EVAL(...) with same source
                 * - Full pattern semantics: alternation, backtracking, nested EVAL(...)
                 * - Proper retain/release ownership through runtime cache
                 */

#ifdef SNOBOL_DYNAMIC_PATTERN
                /* Verify we have both source and bytecode */
                if (!vm->dyn_pending_source || !vm->dyn_pending_bc) {
                    SNOBOL_LOG("OP_DYNAMIC: missing source or bytecode");
                    goto fail_ret;
                }

                SNOBOL_LOG("OP_DYNAMIC: looking up source '%.*s' (len=%zu) in cache",
                           (int)vm->dyn_pending_source_len, vm->dyn_pending_source,
                           vm->dyn_pending_source_len);

                /* Look up in dynamic pattern cache by source text */
                dynamic_pattern_t *pattern = dynamic_pattern_cache_get(
                    vm->dyn_cache,
                    vm->dyn_pending_source,
                    (int)vm->dyn_pending_source_len
                );

                if (!pattern) {
                    /* Cache miss - create pattern from pre-compiled bytecode */
                    SNOBOL_LOG("OP_DYNAMIC: cache miss, creating pattern from bytecode (bc_len=%zu)",
                               vm->dyn_pending_bc_len);

                    /* Copy bytecode for the pattern object (pattern owns its copy) */
                    uint8_t *bc_copy = (uint8_t *)snobol_malloc(vm->dyn_pending_bc_len);
                    if (!bc_copy) {
                        SNOBOL_LOG("OP_DYNAMIC: failed to allocate bytecode copy");
                        goto fail_ret;
                    }
                    memcpy(bc_copy, vm->dyn_pending_bc, vm->dyn_pending_bc_len);

                    /* Create pattern object with the bytecode */
                    pattern = dynamic_pattern_create(vm->dyn_pending_source, bc_copy, vm->dyn_pending_bc_len);
                    if (!pattern || !pattern->is_valid) {
                        SNOBOL_LOG("OP_DYNAMIC: failed to create pattern");
                        if (pattern) dynamic_pattern_release(pattern);
                        if (bc_copy) snobol_free(bc_copy);
                        goto fail_ret;
                    }

                    /* Cache the compiled pattern */
                    if (!dynamic_pattern_cache_put(vm->dyn_cache, vm->dyn_pending_source, pattern)) {
                        SNOBOL_LOG("OP_DYNAMIC: failed to cache pattern (continuing anyway)");
                    }

                    SNOBOL_LOG("OP_DYNAMIC: created and cached pattern");
                } else {
                    SNOBOL_LOG("OP_DYNAMIC: cache hit");
                }

                /* Execute the dynamic pattern with full VM semantics
                 * Save/restore IP and position for proper backtracking */
                size_t saved_ip = vm->ip;
                size_t saved_pos = vm->pos;
                size_t saved_cap_start[MAX_CAPS], saved_cap_end[MAX_CAPS];
                memcpy(saved_cap_start, vm->cap_start, sizeof(saved_cap_start));
                memcpy(saved_cap_end, vm->cap_end, sizeof(saved_cap_end));

                /* Execute dynamic pattern bytecode through the VM */
                const uint8_t *saved_bc = vm->bc;
                size_t saved_bc_len = vm->bc_len;
                vm->bc = pattern->bc;
                vm->bc_len = pattern->bc_len;
                vm->ip = 0;
                vm->pos = saved_pos;

                /* Run the dynamic pattern bytecode through vm_run for full semantics
                 * This enables alternation, backtracking, nested EVAL(...), etc. */
                int dynamic_result = vm_run(vm);

                /* Restore VM state */
                vm->bc = saved_bc;
                vm->bc_len = saved_bc_len;
                vm->ip = saved_ip;

                if (!dynamic_result) {
                    /* Dynamic pattern failed - restore captures and position */
                    SNOBOL_LOG("OP_DYNAMIC: pattern failed, restoring state");
                    vm->pos = saved_pos;
                    memcpy(vm->cap_start, saved_cap_start, sizeof(saved_cap_start));
                    memcpy(vm->cap_end, saved_cap_end, sizeof(saved_cap_end));
                    dynamic_pattern_release(pattern);
                    goto fail_ret;
                }

                /* Success - pattern matched */
                SNOBOL_LOG("OP_DYNAMIC: pattern matched, pos=%zu", vm->pos);

                /* Release our reference to the pattern (cache retains its own) */
                dynamic_pattern_release(pattern);
#else
                /* Dynamic pattern support not enabled */
                SNOBOL_LOG("OP_DYNAMIC: support not enabled");
#endif
                break;
            }
#endif /* SNOBOL_DYNAMIC_PATTERN */
            
            /* ------------------------------------------------------------------
             * Pattern primitives
             * ------------------------------------------------------------------ */

            case OP_BREAKX: {
                /* BREAKX pre-scan optimization.
                 *
                 * Semantics: like BREAK (advance past non-break chars), but also
                 * push a retry choice point.  When the choice is popped, pos
                 * advances past the break char so matching resumes after it.
                 * This is equivalent to BREAK + ARBNO(LEN(1) BREAK) but as a
                 * single opcode with O(n) complexity (the pre-scan visits each
                 * subject byte at most twice).
                 */
                size_t breakx_ip = vm->ip - 1; /* points to OP_BREAKX opcode */
                uint16_t set_id = read_u16(vm->bc, vm->bc_len, &vm->ip);
                uint16_t bx_count, bx_ci;
                const uint8_t *bx_ranges = get_ranges_ptr(vm, set_id, &bx_count, &bx_ci);
                uint64_t bx_map[2];
                bool bx_ascii = bx_ranges && ranges_to_ascii_bitmap(bx_ranges, bx_count, bx_map);

                /* Advance past non-break characters (like OP_BREAK) */
                if (bx_ascii) {
                    while (vm->pos < vm->len && !bitmap_test(bx_map, (uint8_t)vm->s[vm->pos]))
                        vm->pos++;
                } else {
                    uint32_t bx_cp; int bx_bytes;
                    while (utf8_peek_next(vm->s, vm->len, vm->pos, &bx_cp, &bx_bytes) &&
                           (!bx_ranges || !range_contains(bx_ranges, bx_count, bx_cp)))
                        vm->pos += bx_bytes;
                }

                /* If we stopped at a break char, push a retry choice that
                 * re-executes BREAKX from the position AFTER the break char */
                if (vm->pos < vm->len) {
                    uint32_t bx_cp2; int bx_skip = 1;
                    if (utf8_peek_next(vm->s, vm->len, vm->pos, &bx_cp2, &bx_skip))
                        ; /* bx_skip now holds byte count of break char */
                    vm_push_choice(vm, breakx_ip, vm->pos + (size_t)bx_skip);
                }
                break;
            }

            case OP_BAL: {
                /* BAL: match balanced structure.
                 * Operands: open_cp u32, close_cp u32
                 * Fails if the subject at vm->pos does not start with open_cp,
                 * or if the string ends before depth returns to zero.
                 */
                uint32_t bal_open  = read_u32(vm->bc, vm->bc_len, &vm->ip);
                uint32_t bal_close = read_u32(vm->bc, vm->bc_len, &vm->ip);
                size_t   bal_pos   = vm->pos;
                int      bal_depth = 0;
                bool     bal_ok    = false;

                /* Subject must start with the open delimiter */
                uint32_t bal_first; int bal_fb;
                if (!utf8_peek_next(vm->s, vm->len, bal_pos, &bal_first, &bal_fb) ||
                    bal_first != bal_open) {
                    if (!vm_pop_choice(vm)) goto fail_ret;
                    break;
                }

                while (bal_pos < vm->len) {
                    uint32_t bal_cp; int bal_cb;
                    if (!utf8_peek_next(vm->s, vm->len, bal_pos, &bal_cp, &bal_cb)) break;
                    if (bal_cp == bal_open)  { bal_depth++; }
                    else if (bal_cp == bal_close) {
                        bal_depth--;
                        if (bal_depth == 0) { bal_pos += (size_t)bal_cb; bal_ok = true; break; }
                    }
                    bal_pos += (size_t)bal_cb;
                }

                if (!bal_ok) { if (!vm_pop_choice(vm)) goto fail_ret; }
                else vm->pos = bal_pos;
                break;
            }

            case OP_FENCE: {
                /* FENCE: cut the choice stack.
                 * Drops all choice points pushed since execution started,
                 * preventing backtracking past this point.
                 * This implements "possessive" / atomic behaviour.
                 */
                vm->choices_top = 0;
                break;
            }

            case OP_REM: {
                /* REM: match remainder of subject.
                 * Advances pos to end of string unconditionally.
                 */
                vm->pos = vm->len;
                break;
            }

            case OP_RPOS: {
                /* REM: match remainder of subject.
                 * codepoints from the end of the subject string.
                 *
                 * Operand: n u32
                 * Algorithm: walk [n] codepoints backwards from vm->len to
                 * determine the target byte offset; fail if vm->pos != target.
                 */
                uint32_t rpos_n = read_u32(vm->bc, vm->bc_len, &vm->ip);
                size_t rpos_target = vm->len;
                for (uint32_t rpos_i = 0; rpos_i < rpos_n && rpos_target > 0; rpos_i++) {
                    rpos_target--;
                    /* Skip past UTF-8 continuation bytes to find codepoint start */
                    while (rpos_target > 0 &&
                           ((unsigned char)vm->s[rpos_target] & 0xC0) == 0x80)
                        rpos_target--;
                }
                if (vm->pos != rpos_target) { if (!vm_pop_choice(vm)) goto fail_ret; }
                break;
            }

            case OP_RTAB: {
                /* RTAB(n): advance cursor to the position that is
                 * n codepoints from the end of the subject string.
                 * RTAB(0) is equivalent to REM (advance to end).
                 *
                 * Operand: n u32
                 * Fails if vm->pos is already past the target position.
                 */
                uint32_t rtab_n = read_u32(vm->bc, vm->bc_len, &vm->ip);
                size_t rtab_target = vm->len;
                for (uint32_t rtab_i = 0; rtab_i < rtab_n && rtab_target > 0; rtab_i++) {
                    rtab_target--;
                    while (rtab_target > 0 &&
                           ((unsigned char)vm->s[rtab_target] & 0xC0) == 0x80)
                        rtab_target--;
                }
                if (vm->pos > rtab_target) { if (!vm_pop_choice(vm)) goto fail_ret; }
                else vm->pos = rtab_target;
                break;
            }

            /* Note: ARB is not a separate opcode.
             * It is implemented at compile time as SPLIT + REPEAT
             * (equivalent to REPEAT_INIT(0,-1) + REPEAT_STEP on LEN(1)),
             * matching the spec description "split + LEN(1) loop".
             * Helper: snobol_emit_arb() in pattern_build.c builds the bytecode. */

            default: if (!vm_pop_choice(vm)) goto fail_ret; break;
        }
    }
    if (vm->choices) { snobol_free(vm->choices); vm->choices = nullptr; }
    if (vm->use_compact_choice && vm->write_log) { vm_write_log_free(vm); }
    return false;
 fail_ret:
    if (vm->choices) { snobol_free(vm->choices); vm->choices = nullptr; }
    if (vm->use_compact_choice && vm->write_log) { vm_write_log_free(vm); }
    return false;
}

bool vm_exec(VM *vm) {
    vm->ip = 0; vm->pos = 0; vm->max_cap_used = 0; vm->max_counter_used = 0;
    memset(vm->counters, 0, sizeof(vm->counters));
    
    /* Initialize control flow state */
    vm_init_labels(vm);

    /* Pre-register labels from bytecode tail (compiler-produced bytecodes only).
     * Detection: last 4 bytes == SNOBOL_LABEL_TABLE_MAGIC (0x534E424C = "SNBL").
     * Layout (from end): MAGIC | label_count | label_offsets[N] | ...charclass...
     * Hand-built test bytecodes have charclass_count there instead → no labels. */
    if (vm->bc_len >= 8) {
        uint32_t last4 = ((uint32_t)vm->bc[vm->bc_len-4] << 24) | ((uint32_t)vm->bc[vm->bc_len-3] << 16) |
                         ((uint32_t)vm->bc[vm->bc_len-2] << 8) | (uint32_t)vm->bc[vm->bc_len-1];
        if (last4 == SNOBOL_LABEL_TABLE_MAGIC) {
            uint32_t lc = ((uint32_t)vm->bc[vm->bc_len-8] << 24) | ((uint32_t)vm->bc[vm->bc_len-7] << 16) |
                          ((uint32_t)vm->bc[vm->bc_len-6] << 8) | (uint32_t)vm->bc[vm->bc_len-5];
            if (lc > 0 && vm->bc_len >= 8 + (size_t)lc * 4) {
                size_t table_base = vm->bc_len - 8 - (size_t)lc * 4;
                for (uint32_t i = 0; i < lc; i++) {
                    size_t op = table_base + (size_t)i * 4;
                    uint32_t offset = ((uint32_t)vm->bc[op] << 24) | ((uint32_t)vm->bc[op+1] << 16) |
                                      ((uint32_t)vm->bc[op+2] << 8) | (uint32_t)vm->bc[op+3];
                    vm_register_label(vm, (uint16_t)i, offset);
                }
            }
        }
    }

#ifdef SNOBOL_DYNAMIC_PATTERN
    /* Initialize table registry */
    vm_init_tables(vm);
#endif
    
#ifdef SNOBOL_PROFILE
    memset(&vm->profile, 0, sizeof(vm->profile));
#endif
    bool res = vm_run(vm);
    
    /* Cleanup */
    vm_free_labels(vm);
#ifdef SNOBOL_DYNAMIC_PATTERN
    vm_free_tables(vm);
#endif
    
#ifdef SNOBOL_PROFILE
#endif
    return res;
}

/* Control flow initialization and cleanup */

void vm_init_labels(VM *vm) {
    vm->label_offsets = nullptr;
    vm->label_count = 0;
    vm->label_capacity = 0;
    vm->current_label = 0;
    vm->in_goto_fail = false;
}

void vm_free_labels(VM *vm) {
    if (vm->label_offsets) {
        snobol_free(vm->label_offsets);
        vm->label_offsets = nullptr;
    }
    vm->label_count = 0;
    vm->label_capacity = 0;
}

bool vm_register_label(VM *vm, uint16_t label_id, uint32_t offset) {
    /* Ensure capacity */
    if (label_id >= vm->label_capacity) {
        size_t new_cap = (label_id + 1) * 2;
        uint16_t *new_offsets = (uint16_t *)snobol_realloc(vm->label_offsets, new_cap * sizeof(uint16_t));
        if (!new_offsets) return false;
        vm->label_offsets = new_offsets;
        vm->label_capacity = new_cap;
    }
    
    vm->label_offsets[label_id] = (uint16_t)offset;
    if (label_id >= vm->label_count) {
        vm->label_count = label_id + 1;
    }
    return true;
}

uint32_t vm_get_label_offset(VM *vm, uint16_t label_id) {
    if (label_id < vm->label_count && vm->label_offsets) {
        return vm->label_offsets[label_id];
    }
    return 0; /* Invalid label - will cause fail */
}

#ifdef SNOBOL_DYNAMIC_PATTERN
/* Table registry functions */

void vm_init_tables(VM *vm) {
    vm->tables = nullptr;
    vm->table_count = 0;
    vm->table_capacity = 0;
}

void vm_free_tables(VM *vm) {
    if (vm->tables) {
        for (size_t i = 0; i < vm->table_count; i++) {
            if (vm->tables[i]) {
                table_release(vm->tables[i]);
            }
        }
        snobol_free(vm->tables);
        vm->tables = nullptr;
    }
    vm->table_count = 0;
    vm->table_capacity = 0;
}

bool vm_register_table(VM *vm, snobol_table_t *table, uint16_t *out_id) {
    if (vm->table_count >= vm->table_capacity) {
        size_t new_cap = (vm->table_capacity == 0) ? 16 : vm->table_capacity * 2;
        snobol_table_t **new_tables = (snobol_table_t **)snobol_realloc(vm->tables, new_cap * sizeof(snobol_table_t *));
        if (!new_tables) return false;
        vm->tables = new_tables;
        vm->table_capacity = new_cap;
    }
    
    *out_id = (uint16_t)vm->table_count;
    vm->tables[vm->table_count++] = table_retain(table);
    return true;
}

snobol_table_t *vm_get_table(VM *vm, uint16_t table_id) {
    if (table_id < vm->table_count && vm->tables) {
        return vm->tables[table_id];
    }
    return nullptr;
}
#endif
