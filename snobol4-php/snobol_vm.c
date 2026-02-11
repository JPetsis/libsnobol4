#include "snobol_internal.h"
#include "snobol_vm.h"
#include "snobol_jit.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

const uint8_t *get_ranges_ptr(const VM *vm, uint16_t set_id, uint16_t *out_count, uint16_t *out_case) {
    if (set_id == 0) return NULL;
    size_t tail_ip = vm->bc_len;
    if (tail_ip < 4) return NULL;
    uint32_t class_count = ((uint32_t)vm->bc[tail_ip-4] << 24) | ((uint32_t)vm->bc[tail_ip-3] << 16) | ((uint32_t)vm->bc[tail_ip-2] << 8) | (uint32_t)vm->bc[tail_ip-1];
    if (set_id > class_count) return NULL;
    size_t table_size = (size_t)class_count * 4;
    if (tail_ip < 4 + table_size) return NULL;
    size_t table_start = tail_ip - 4 - table_size;
    size_t offset_pos = table_start + (size_t)(set_id - 1) * 4;
    uint32_t offset = ((uint32_t)vm->bc[offset_pos+0] << 24) | ((uint32_t)vm->bc[offset_pos+1] << 16) | ((uint32_t)vm->bc[offset_pos+2] << 8) | (uint32_t)vm->bc[offset_pos+3];
    if (offset >= vm->bc_len) return NULL;
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

typedef struct {
    uint32_t total_size; size_t ip; size_t pos; size_t var_count;
    uint8_t num_caps; uint8_t num_counters; uint8_t max_cap_used; uint8_t max_counter_used;
} CompactChoiceHeader;

void vm_push_choice(VM *vm, size_t ip, size_t pos) {
    if (!vm->choices) return;
#ifdef SNOBOL_PROFILE
    vm->profile.push_count++;
#endif
    if (vm->use_compact_choice) {
        uint8_t num_caps = vm->max_cap_used; uint8_t num_counters = vm->max_counter_used;
        uint32_t data_size = sizeof(CompactChoiceHeader) + (num_caps * sizeof(size_t) * 2) + (num_counters * (sizeof(uint32_t) + sizeof(size_t)));
        uint32_t record_size = (data_size + sizeof(uint32_t) + 7) & ~7;
        if (vm->choices_top + record_size >= vm->choices_cap) {
            size_t new_cap = vm->choices_cap ? vm->choices_cap * 2 : 4096;
            while (vm->choices_top + record_size >= new_cap) new_cap *= 2;
            void *new_choices = snobol_realloc(vm->choices, new_cap);
            if (!new_choices) return;
            vm->choices = new_choices; vm->choices_cap = new_cap;
        }
        CompactChoiceHeader *h = (CompactChoiceHeader *)((uint8_t *)vm->choices + vm->choices_top);
        h->total_size = record_size; h->ip = ip; h->pos = pos; h->var_count = vm->var_count;
        h->num_caps = num_caps; h->num_counters = num_counters; h->max_cap_used = vm->max_cap_used; h->max_counter_used = vm->max_counter_used;
        uint8_t *p = (uint8_t *)(h + 1);
        if (num_caps > 0) {
            size_t cap_size = num_caps * sizeof(size_t);
            memcpy(p, vm->cap_start, cap_size); p += cap_size;
            memcpy(p, vm->cap_end, cap_size); p += cap_size;
        }
        if (num_counters > 0) {
            size_t pos_size = num_counters * sizeof(size_t);
            memcpy(p, vm->loop_last_pos, pos_size); p += pos_size;
            memcpy(p, vm->counters, num_counters * sizeof(uint32_t));
        }
        uint8_t *size_ptr = (uint8_t *)h + record_size - sizeof(uint32_t);
        memcpy(size_ptr, &record_size, sizeof(uint32_t));
        vm->choices_top += record_size;
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
        if (h->num_caps > 0) {
            size_t cap_size = h->num_caps * sizeof(size_t);
            memcpy(vm->cap_start, p, cap_size); p += cap_size;
            memcpy(vm->cap_end, p, cap_size); p += cap_size;
        }
        if (h->num_counters > 0) {
            size_t pos_size = h->num_counters * sizeof(size_t);
            memcpy(vm->loop_last_pos, p, pos_size); p += pos_size;
            memcpy(vm->counters, p, h->num_counters * sizeof(uint32_t));
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
void snobol_buf_free(snobol_buf *b) { if (b->data) { snobol_free(b->data); b->data = NULL; } b->len = b->cap = 0; }

bool vm_run(VM *vm) {
    size_t initial_cap = 4096;
    vm->choices = snobol_malloc(initial_cap);
    if (!vm->choices) return false;
    vm->choices_cap = initial_cap; vm->choices_top = 0;
    vm->use_compact_choice = (getenv("SNOBOL_LEGACY_CHOICE") == NULL);

    while (1) {
#ifdef SNOBOL_PROFILE
        vm->profile.dispatch_count++;
#endif
        if (vm->ip >= vm->bc_len) { if (!vm_pop_choice(vm)) goto fail_ret; continue; }
#ifdef SNOBOL_JIT
        size_t current_ip = vm->ip;
        if (vm->jit.enabled && vm->jit.traces && vm->jit.traces[current_ip]) {
            if (vm->jit.stats) { vm->jit.stats->entries_total++; vm->jit.stats->cache_hits_total++; }
            jit_trace_fn fn = (jit_trace_fn)vm->jit.traces[current_ip];
            fn(vm);
            if (vm->jit.stats) { vm->jit.stats->exits_total++; if (vm->ip == current_ip) vm->jit.stats->bailouts_total++; }
            if (vm->ip != current_ip) continue; 
        } else if (vm->jit.ip_counts && current_ip < vm->bc_len) {
            uint64_t count = ++vm->jit.ip_counts[current_ip];
            if (count == 50 && vm->jit.traces) {
                if (vm->jit.traces[current_ip] == NULL) {
                    if (vm->jit.stats) vm->jit.stats->compilations_total++;
                    void *trace = (void*)snobol_jit_compile(vm, current_ip);
                    if (trace) {
                        vm->jit.traces[current_ip] = trace;
                    } else {
                        if (vm->jit.stats) vm->jit.stats->bailouts_total++;
                    }
                }
            }
        }
#endif
        uint8_t op = vm->bc[vm->ip++];
        switch (op) {
            case OP_ACCEPT: if (vm->choices) { snobol_free(vm->choices); vm->choices = NULL; } return true;
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
            case OP_CAP_START: { uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip); if (r < MAX_CAPS) { vm->cap_start[r] = vm->pos; if (r >= vm->max_cap_used) vm->max_cap_used = r + 1; } break; }
            case OP_CAP_END: { uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip); if (r < MAX_CAPS) { vm->cap_end[r] = vm->pos; if (r >= vm->max_cap_used) vm->max_cap_used = r + 1; } break; }
            case OP_ASSIGN: { uint16_t var = read_u16(vm->bc, vm->bc_len, &vm->ip); uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip);
                if (var < MAX_VARS && r < MAX_CAPS) { if (var >= vm->var_count) vm->var_count = (size_t)var + 1; vm->var_start[var] = vm->cap_start[r]; vm->var_end[var] = vm->cap_end[r]; } break; }
            case OP_LEN: {
                uint32_t n = read_u32(vm->bc, vm->bc_len, &vm->ip); size_t p = vm->pos; uint32_t i;
                for (i = 0; i < n; ++i) { uint32_t cp; int bytes; if (!utf8_peek_next(vm->s, vm->len, p, &cp, &bytes)) break; p += bytes; }
                if (i != n) { if (!vm_pop_choice(vm)) goto fail_ret; } else vm->pos = p; break; }
            case OP_EVAL: { uint16_t fn = read_u16(vm->bc, vm->bc_len, &vm->ip); uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip);
                if (r >= MAX_CAPS) { if (!vm_pop_choice(vm)) goto fail_ret; break; }
                if (vm->eval_fn && !vm->eval_fn((int)fn, vm->s, vm->cap_start[r], vm->cap_end[r], vm->eval_udata)) { if (!vm_pop_choice(vm)) goto fail_ret; } break; }
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
                uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip); uint8_t expr_type = read_u8(vm->bc, vm->bc_len, &vm->ip);
                if (r < MAX_CAPS && vm->cap_end[r] >= vm->cap_start[r] && vm->cap_end[r] <= vm->len) {
                    const char *data = vm->s + vm->cap_start[r]; size_t len = vm->cap_end[r] - vm->cap_start[r];
                    if (expr_type == 1) { char *tmp = snobol_malloc(len + 1); for (size_t i = 0; i < len; ++i) tmp[i] = (data[i] >= 'a' && data[i] <= 'z') ? data[i] - 32 : data[i]; if (vm->out) snobol_buf_append(vm->out, tmp, len); if (vm->emit_fn) vm->emit_fn(tmp, len, vm->emit_udata); snobol_free(tmp); }
                    else if (expr_type == 2) { char tmp[32]; int n = snprintf(tmp, sizeof(tmp), "%zu", len); if (vm->out) snobol_buf_append(vm->out, tmp, (size_t)n); if (vm->emit_fn) vm->emit_fn(tmp, (size_t)n, vm->emit_udata); }
                    else { if (vm->out) snobol_buf_append(vm->out, data, len); if (vm->emit_fn) vm->emit_fn(data, len, vm->emit_udata); }
                } break;
            }
            default: if (!vm_pop_choice(vm)) goto fail_ret; break;
        }
    }
    if (vm->choices) { snobol_free(vm->choices); vm->choices = NULL; } return false;
 fail_ret: if (vm->choices) { snobol_free(vm->choices); vm->choices = NULL; } return false;
}

bool vm_exec(VM *vm) {
    vm->ip = 0; vm->pos = 0; vm->max_cap_used = 0; vm->max_counter_used = 0;
    memset(vm->counters, 0, sizeof(vm->counters));
#ifdef SNOBOL_PROFILE
    memset(&vm->profile, 0, sizeof(vm->profile));
#endif
    bool res = vm_run(vm);
#ifdef SNOBOL_PROFILE
#endif
    return res;
}
