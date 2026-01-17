#ifndef STANDALONE_BUILD
#include "php.h"
#endif

#include "snobol_vm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

/* DEBUG LOGGING DISABLED
static inline void snobol_log_impl(const char *file, int line, const char *fmt, ...) {
    FILE *f = fopen("/var/www/html/snobol_debug.log", "a");
    if (f) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);
        fprintf(f, "[%s] [%s:%d] ", ts, file, line);
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fprintf(f, "\n");
        fflush(f);
        fclose(f);
    }
}
*/
/* No-op macro to disable logging */
#define SNOBOL_LOG(fmt, ...) ((void)0)

/* helpers to read u32/u16/u8 from bc with bounds checking */
static inline uint32_t read_u32(const uint8_t *bc, size_t bc_len, size_t *ip) {
    if (*ip + 4 > bc_len) { *ip = bc_len; return 0; }
    uint32_t v = ((uint32_t)bc[*ip] << 24) | ((uint32_t)bc[*ip+1] << 16) | ((uint32_t)bc[*ip+2] << 8) | (uint32_t)bc[*ip+3];
    *ip += 4;
    return v;
}
static inline uint16_t read_u16(const uint8_t *bc, size_t bc_len, size_t *ip) {
    if (*ip + 2 > bc_len) { *ip = bc_len; return 0; }
    uint16_t v = ((uint16_t)bc[*ip] << 8) | ((uint16_t)bc[*ip+1]);
    *ip += 2;
    return v;
}
static inline uint8_t read_u8(const uint8_t *bc, size_t bc_len, size_t *ip) {
    if (*ip + 1 > bc_len) { *ip = bc_len; return 0; }
    uint8_t v = bc[(*ip)++];
    return v;
}

static const uint8_t *get_ranges_ptr(const VM *vm, uint16_t set_id, uint16_t *out_count, uint16_t *out_case) {
    if (set_id == 0) return NULL;
    size_t tail_ip = vm->bc_len;
    if (tail_ip < 4) return NULL;
    
    uint32_t class_count = ((uint32_t)vm->bc[tail_ip-4] << 24) | 
                           ((uint32_t)vm->bc[tail_ip-3] << 16) | 
                           ((uint32_t)vm->bc[tail_ip-2] << 8) | 
                           (uint32_t)vm->bc[tail_ip-1];
    
    if (set_id > class_count) return NULL;
    
    size_t table_size = (size_t)class_count * 4;
    if (tail_ip < 4 + table_size) return NULL;
    size_t table_start = tail_ip - 4 - table_size;
    
    size_t offset_pos = table_start + (size_t)(set_id - 1) * 4;
    uint32_t offset = ((uint32_t)vm->bc[offset_pos+0] << 24) | ((uint32_t)vm->bc[offset_pos+1] << 16) |
                      ((uint32_t)vm->bc[offset_pos+2] << 8) | (uint32_t)vm->bc[offset_pos+3];
                      
    if (offset >= vm->bc_len) return NULL;
    
    size_t ip = (size_t)offset;
    *out_count = read_u16(vm->bc, vm->bc_len, &ip);
    *out_case = read_u16(vm->bc, vm->bc_len, &ip);
    
    return vm->bc + ip;
}

static bool ranges_to_ascii_bitmap(const uint8_t *ranges_ptr, size_t count, uint64_t map[2]) {
    map[0] = map[1] = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t offset = i * 8;
        uint32_t start = ((uint32_t)ranges_ptr[offset+0] << 24) | ((uint32_t)ranges_ptr[offset+1] << 16) |
                         ((uint32_t)ranges_ptr[offset+2] << 8) | (uint32_t)ranges_ptr[offset+3];
        uint32_t end   = ((uint32_t)ranges_ptr[offset+4] << 24) | ((uint32_t)ranges_ptr[offset+5] << 16) |
                         ((uint32_t)ranges_ptr[offset+6] << 8) | (uint32_t)ranges_ptr[offset+7];
        
        if (start > 127) return false; // Non-ASCII
        if (end > 127) end = 127;      // Clamp to ASCII (though if start < 128 and end > 127, it's mixed, but we can just optimize the ASCII part? No, safe to reject)
        
        // Actually, if a range spans out of ASCII, we cannot use ASCII-only fast path for NEGATED logic (NOTANY) safely, 
        // but for ANY/SPAN/BREAK we might? 
        // Safer: strictly ASCII sets only.
        if (end > 127) return false;

        for (uint32_t c = start; c <= end; ++c) {
            if (c < 64) map[0] |= (1ULL << c);
            else map[1] |= (1ULL << (c - 64));
        }
    }
    return true;
}

static inline bool bitmap_test(const uint64_t map[2], uint8_t c) {
    if (c > 127) return false;
    if (c < 64) return (map[0] & (1ULL << c)) != 0;
    return (map[1] & (1ULL << (c - 64))) != 0;
}

static bool range_contains(const uint8_t *ranges_ptr, size_t count, uint32_t cp) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        size_t offset = mid * 8;
        uint32_t start = ((uint32_t)ranges_ptr[offset+0] << 24) | ((uint32_t)ranges_ptr[offset+1] << 16) |
                         ((uint32_t)ranges_ptr[offset+2] << 8) | (uint32_t)ranges_ptr[offset+3];
        uint32_t end   = ((uint32_t)ranges_ptr[offset+4] << 24) | ((uint32_t)ranges_ptr[offset+5] << 16) |
                         ((uint32_t)ranges_ptr[offset+6] << 8) | (uint32_t)ranges_ptr[offset+7];
        
        if (cp < start) {
            hi = mid;
        } else if (cp > end) {
            lo = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

#define MAX_CHOICES 128

void vm_push_choice(VM *vm, size_t ip, size_t pos) {
    if (!vm->choices) return;
    if (vm->choices_top >= vm->choices_cap) {
        size_t new_cap = vm->choices_cap ? vm->choices_cap * 2 : MAX_CHOICES;
        // Safety limit: don't grow beyond 10M to avoid total OOM freeze
        if (new_cap > 10000000) {
             SNOBOL_LOG("vm_push_choice: Hard limit reached (%zu)", new_cap);
             return;
        }
#ifdef STANDALONE_BUILD
        struct choice *new_choices = realloc(vm->choices, new_cap * sizeof(struct choice));
#else
        struct choice *new_choices = erealloc(vm->choices, new_cap * sizeof(struct choice));
#endif
        if (!new_choices) return;
        vm->choices = new_choices;
        vm->choices_cap = new_cap;
    }

    struct choice *c = &vm->choices[vm->choices_top++];
    c->ip = ip;
    c->pos = pos;
    c->var_count_snapshot = vm->var_count;

    // Only copy captures that are actually used
    // If max_cap_used is 0, we don't copy anything (no captures in use)
    size_t caps_to_copy = vm->max_cap_used;
    if (caps_to_copy > 0) {
        memcpy(c->cap_start_snapshot, vm->cap_start, caps_to_copy * sizeof(size_t));
        memcpy(c->cap_end_snapshot, vm->cap_end, caps_to_copy * sizeof(size_t));
    }

    // Only copy counters that are actually used
    size_t counters_to_copy = vm->max_counter_used;
    if (counters_to_copy > 0) {
        memcpy(c->counters_snapshot, vm->counters, counters_to_copy * sizeof(uint32_t));
        memcpy(c->loop_last_pos_snapshot, vm->loop_last_pos, counters_to_copy * sizeof(size_t));
    }

    SNOBOL_LOG("vm_push_choice: top=%zu, ip=%zu, pos=%zu, caps=%zu, counters=%zu",
               vm->choices_top, ip, pos, caps_to_copy, counters_to_copy);
#ifdef SNOBOL_PROFILE
    vm->profile.push_count++;
    if (vm->choices_top > vm->profile.max_depth) {
        vm->profile.max_depth = vm->choices_top;
    }
#endif
}

bool vm_pop_choice(VM *vm) {
    if (!vm->choices || vm->choices_top == 0) return false;
#ifdef SNOBOL_PROFILE
    vm->profile.pop_count++;
#endif
    vm->choices_top--;
    struct choice *c = &vm->choices[vm->choices_top];
    vm->ip = c->ip;
    vm->pos = c->pos;

    // Only restore captures that are actually used
    if (vm->max_cap_used > 0) {
        size_t cap_bytes = vm->max_cap_used * sizeof(size_t);
        memcpy(vm->cap_start, c->cap_start_snapshot, cap_bytes);
        memcpy(vm->cap_end, c->cap_end_snapshot, cap_bytes);
    }

    vm->var_count = c->var_count_snapshot;

    // Only restore counters that are actually used
    if (vm->max_counter_used > 0) {
        memcpy(vm->counters, c->counters_snapshot, vm->max_counter_used * sizeof(uint32_t));
        memcpy(vm->loop_last_pos, c->loop_last_pos_snapshot, vm->max_counter_used * sizeof(size_t));
    }

    SNOBOL_LOG("vm_pop_choice: top=%zu, ip=%zu, pos=%zu", vm->choices_top, vm->ip, vm->pos);
    return true;
}

void snobol_buf_init(snobol_buf *b) {
    b->cap = 1024;
#ifdef STANDALONE_BUILD
    b->data = malloc(b->cap);
#else
    b->data = emalloc(b->cap);
#endif
    b->len = 0;
}

void snobol_buf_append(snobol_buf *b, const char *data, size_t len) {
    if (len == 0) return;
    if (b->len + len >= b->cap) {
        size_t newcap = b->cap ? b->cap * 2 : 1024;
        while (b->len + len >= newcap) newcap *= 2;
#ifdef STANDALONE_BUILD
        b->data = realloc(b->data, newcap);
#else
        b->data = erealloc(b->data, newcap);
#endif
        b->cap = newcap;
    }
    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = '\0'; // keep it null terminated for convenience
}

void snobol_buf_clear(snobol_buf *b) {
    b->len = 0;
    if (b->data) b->data[0] = '\0';
}

void snobol_buf_free(snobol_buf *b) {
    if (b->data) {
#ifdef STANDALONE_BUILD
        free(b->data);
#else
        efree(b->data);
#endif
        b->data = NULL;
    }
    b->len = b->cap = 0;
}

bool vm_run(VM *vm) {
    size_t initial_cap = MAX_CHOICES;
#ifdef STANDALONE_BUILD
    vm->choices = malloc(initial_cap * sizeof(struct choice));
#else
    vm->choices = emalloc(initial_cap * sizeof(struct choice));
#endif
    if (!vm->choices) {
        SNOBOL_LOG("vm_run: FAILED to allocate choices");
        return false;
    }
    vm->choices_cap = initial_cap;
    vm->choices_top = 0;
    SNOBOL_LOG("vm_run: START, choices=%p, ip=%zu, pos=%zu", (void*)vm->choices, vm->ip, vm->pos);

    while (1) {
#ifdef SNOBOL_PROFILE
        vm->profile.dispatch_count++;
#endif
        if (vm->ip >= vm->bc_len) {
            if (!vm_pop_choice(vm)) goto fail_ret;
            continue;
        }
        
        uint8_t op = vm->bc[vm->ip++];
        
        /* SNOBOL_LOG("  TRACE ip=%zu, op=%u, pos=%zu", current_ip, (unsigned)op, vm->pos); */

        switch (op) {
            case OP_ACCEPT:
                if (vm->choices) { 
#ifdef STANDALONE_BUILD
                    free(vm->choices); 
#else
                    efree(vm->choices);
#endif
                    vm->choices = NULL; 
                }
                SNOBOL_LOG("vm_run: SUCCESS (OP_ACCEPT)");
                return true;

            case OP_FAIL:
                if (!vm_pop_choice(vm)) goto fail_ret;
                break;

            case OP_JMP: {
                uint32_t tgt = read_u32(vm->bc, vm->bc_len, &vm->ip);
                vm->ip = (size_t)tgt;
                break;
            }

            case OP_SPLIT: {
                uint32_t a = read_u32(vm->bc, vm->bc_len, &vm->ip);
                uint32_t b = read_u32(vm->bc, vm->bc_len, &vm->ip);
                vm_push_choice(vm, (size_t)b, vm->pos);
                vm->ip = (size_t)a;
                break;
            }

            case OP_LIT: {
                uint32_t off = read_u32(vm->bc, vm->bc_len, &vm->ip);
                uint32_t len = read_u32(vm->bc, vm->bc_len, &vm->ip);
                if (off == vm->ip) vm->ip += len;
                if (len <= vm->len - vm->pos && memcmp(vm->s + vm->pos, vm->bc + off, len) == 0) {
                    vm->pos += len;
                } else {
                    if (!vm_pop_choice(vm)) goto fail_ret;
                }
                break;
            }

            case OP_ANY: {
                uint16_t set_id = read_u16(vm->bc, vm->bc_len, &vm->ip);
                uint16_t count, ci;
                const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count, &ci);
                uint64_t ascii_map[2];
                bool is_ascii = ranges && ranges_to_ascii_bitmap(ranges, count, ascii_map);

                if (is_ascii) {
                    if (vm->pos < vm->len) {
                        uint8_t c = (uint8_t)vm->s[vm->pos];
                        if (bitmap_test(ascii_map, c)) {
                             vm->pos++;
                        } else {
                            if (!vm_pop_choice(vm)) goto fail_ret;
                        }
                    } else {
                        if (!vm_pop_choice(vm)) goto fail_ret;
                    }
                } else {
                    uint32_t cp; int bytes;
                    if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes)) {
                        if (!vm_pop_choice(vm)) goto fail_ret;
                    } else {
                        if (set_id == 0) {
                            vm->pos += bytes;
                        } else {
                            if (ranges && range_contains(ranges, count, cp)) {
                                vm->pos += bytes;
                            } else {
                                if (!vm_pop_choice(vm)) goto fail_ret;
                            }
                        }
                    }
                }
                break;
            }

            case OP_NOTANY: {
                uint16_t set_id = read_u16(vm->bc, vm->bc_len, &vm->ip);
                uint16_t count, ci;
                const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count, &ci);
                uint64_t ascii_map[2];
                bool is_ascii = ranges && ranges_to_ascii_bitmap(ranges, count, ascii_map);

                if (is_ascii) {
                    if (vm->pos < vm->len) {
                        uint8_t c = (uint8_t)vm->s[vm->pos];
                        if (bitmap_test(ascii_map, c)) {
                            // Found in set -> Fail
                            if (!vm_pop_choice(vm)) goto fail_ret;
                        } else {
                            // Not in set -> match
                            vm->pos++;
                        }
                    } else {
                        if (!vm_pop_choice(vm)) goto fail_ret;
                    }
                } else {
                    uint32_t cp; int bytes;
                    if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes)) {
                        if (!vm_pop_choice(vm)) goto fail_ret;
                    } else {
                        if (ranges && range_contains(ranges, count, cp)) {
                            if (!vm_pop_choice(vm)) goto fail_ret;
                        } else {
                            vm->pos += bytes;
                        }
                    }
                }
                break;
            }

            case OP_SPAN: {
                uint16_t set_id = read_u16(vm->bc, vm->bc_len, &vm->ip);
                uint16_t count, ci;
                const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count, &ci);
                uint64_t ascii_map[2];
                bool is_ascii = ranges && ranges_to_ascii_bitmap(ranges, count, ascii_map);

                if (is_ascii) {
                    if (vm->pos < vm->len && bitmap_test(ascii_map, (uint8_t)vm->s[vm->pos])) {
                        vm->pos++;
                        while (vm->pos < vm->len && bitmap_test(ascii_map, (uint8_t)vm->s[vm->pos])) {
                            vm->pos++;
                        }
                    } else {
                        if (!vm_pop_choice(vm)) goto fail_ret;
                    }
                } else {
                    uint32_t cp; int bytes;
                    if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes)) {
                        if (!vm_pop_choice(vm)) goto fail_ret;
                        break;
                    }
                    if (!ranges || !range_contains(ranges, count, cp)) {
                        if (!vm_pop_choice(vm)) goto fail_ret;
                        break;
                    }
                    vm->pos += bytes;
                    while (1) {
                        if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes)) break;
                        if (!range_contains(ranges, count, cp)) break;
                        vm->pos += bytes;
                    }
                }
                break;
            }

            case OP_BREAK: {
                uint16_t set_id = read_u16(vm->bc, vm->bc_len, &vm->ip);
                uint16_t count, ci;
                const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count, &ci);
                uint64_t ascii_map[2];
                bool is_ascii = ranges && ranges_to_ascii_bitmap(ranges, count, ascii_map);

                if (is_ascii) {
                    while (vm->pos < vm->len) {
                        uint8_t c = (uint8_t)vm->s[vm->pos];
                        if (bitmap_test(ascii_map, c)) break;
                        vm->pos++;
                    }
                } else {
                    uint32_t cp; int bytes;
                    while (1) {
                        if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes)) break;
                        if (ranges && range_contains(ranges, count, cp)) break;
                        vm->pos += bytes;
                    }
                }
                break;
            }

            case OP_CAP_START: {
                uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip);
                if (r < MAX_CAPS) {
                    vm->cap_start[r] = vm->pos;
                    if (r >= vm->max_cap_used) vm->max_cap_used = r + 1;
                    SNOBOL_LOG("OP_CAP_START r=%u, pos=%zu", r, vm->pos);
                }
                break;
            }

            case OP_CAP_END: {
                uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip);
                if (r < MAX_CAPS) {
                    vm->cap_end[r] = vm->pos;
                    if (r >= vm->max_cap_used) vm->max_cap_used = r + 1;
                    SNOBOL_LOG("OP_CAP_END r=%u, pos=%zu", r, vm->pos);
                }
                break;
            }

            case OP_ASSIGN: {
                uint16_t var = read_u16(vm->bc, vm->bc_len, &vm->ip);
                uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip);
                if (var < MAX_VARS && r < MAX_CAPS) {
                    if (var >= vm->var_count) vm->var_count = (size_t)var + 1;
                    vm->var_start[var] = vm->cap_start[r];
                    vm->var_end[var] = vm->cap_end[r];
                    SNOBOL_LOG("OP_ASSIGN var=%u, r=%u, [%zu, %zu]", var, r, vm->var_start[var], vm->var_end[var]);
                }
                break;
            }

            case OP_LEN: {
                uint32_t n = read_u32(vm->bc, vm->bc_len, &vm->ip);
                size_t p = vm->pos;
                uint32_t i;
                for (i = 0; i < n; ++i) {
                    uint32_t cp; int bytes;
                    if (!utf8_peek_next(vm->s, vm->len, p, &cp, &bytes)) break;
                    p += bytes;
                }
                if (i != n) {
                    if (!vm_pop_choice(vm)) goto fail_ret;
                } else {
                    vm->pos = p;
                }
                break;
            }

            case OP_EVAL: {
                uint16_t fn = read_u16(vm->bc, vm->bc_len, &vm->ip);
                uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip);
                if (r >= MAX_CAPS) {
                    if (!vm_pop_choice(vm)) goto fail_ret;
                    break;
                }
                size_t a = vm->cap_start[r];
                size_t b = vm->cap_end[r];
                if (vm->eval_fn) {
                    if (!vm->eval_fn((int)fn, vm->s, a, b, vm->eval_udata)) {
                        if (!vm_pop_choice(vm)) goto fail_ret;
                    }
                }
                break;
            }

            case OP_ANCHOR: {
                uint8_t type = read_u8(vm->bc, vm->bc_len, &vm->ip);
                bool ok = false;
                if (type == 0) { // start
                    if (vm->pos == 0) ok = true;
                } else if (type == 1) { // end
                    if (vm->pos == vm->len) ok = true;
                }
                if (!ok) {
                    if (!vm_pop_choice(vm)) goto fail_ret;
                }
                break;
            }

            case OP_REPEAT_INIT: {
                uint8_t loop_id = read_u8(vm->bc, vm->bc_len, &vm->ip);
                uint32_t min = read_u32(vm->bc, vm->bc_len, &vm->ip);
                uint32_t max = read_u32(vm->bc, vm->bc_len, &vm->ip);
                uint32_t skip = read_u32(vm->bc, vm->bc_len, &vm->ip);
                if (loop_id < MAX_LOOPS) {
                    vm->counters[loop_id] = 0;
                    vm->loop_min[loop_id] = min;
                    vm->loop_max[loop_id] = max;
                    vm->loop_last_pos[loop_id] = vm->pos;
                    // Track the highest counter index used (loop_id is 0-based, so +1 for count)
                    if (loop_id + 1 > vm->max_counter_used) vm->max_counter_used = loop_id + 1;
                    SNOBOL_LOG("OP_REPEAT_INIT id=%u, min=%u, max=%u, skip=%u", loop_id, min, max, skip);
                    if (min == 0) {
                        // Greedy: try body first, but can skip.
                        vm_push_choice(vm, (size_t)skip, vm->pos);
                    }
                }
                break;
            }

            case OP_REPEAT_STEP: {
                uint8_t loop_id = read_u8(vm->bc, vm->bc_len, &vm->ip);
                uint32_t target = read_u32(vm->bc, vm->bc_len, &vm->ip);
                if (loop_id < MAX_LOOPS) {
                    vm->counters[loop_id]++;
                    uint32_t count = vm->counters[loop_id];
                    uint32_t min = vm->loop_min[loop_id];
                    uint32_t max = vm->loop_max[loop_id];
                    SNOBOL_LOG("OP_REPEAT_STEP id=%u, count=%u, min=%u, max=%u", loop_id, count, min, max);
                    
                    if (count < min) {
                        // MUST repeat
                        vm->loop_last_pos[loop_id] = vm->pos;
                        vm->ip = (size_t)target;
                    } else if (max == (uint32_t)-1 || count < max) {
                        // Check for infinite loop on empty match
                        if (max == (uint32_t)-1 && vm->pos == vm->loop_last_pos[loop_id]) {
                            // Matched empty string in unbounded repeat -> STOP repeating
                        } else {
                            // CAN repeat (greedy)
                            vm_push_choice(vm, vm->ip, vm->pos);
                            vm->loop_last_pos[loop_id] = vm->pos;
                            vm->ip = (size_t)target;
                        }
                    } else {
                        // MUST stop
                        // continue (vm->ip is after STEP)
                    }
                }
                break;
            }

            case OP_EMIT_LITERAL: {
                uint32_t off = read_u32(vm->bc, vm->bc_len, &vm->ip);
                uint32_t len = read_u32(vm->bc, vm->bc_len, &vm->ip);
                if (off == vm->ip) vm->ip += len;
                if (vm->out) {
                    snobol_buf_append(vm->out, (const char *)vm->bc + off, (size_t)len);
                }
                if (vm->emit_fn) {
                    vm->emit_fn((const char *)vm->bc + off, (size_t)len, vm->emit_udata);
                }
                break;
            }

            case OP_EMIT_CAPTURE: {
                uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip);
                if (r < MAX_CAPS) {
                    size_t start = vm->cap_start[r];
                    size_t end = vm->cap_end[r];
                    if (end >= start && end <= vm->len) {
                        if (vm->out) {
                            snobol_buf_append(vm->out, vm->s + start, end - start);
                        }
                        if (vm->emit_fn) {
                            vm->emit_fn(vm->s + start, end - start, vm->emit_udata);
                        }
                    }
                }
                break;
            }

            case OP_EMIT_EXPR: {
                uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip);
                uint8_t expr_type = read_u8(vm->bc, vm->bc_len, &vm->ip);
                if (r < MAX_CAPS) {
                    size_t start = vm->cap_start[r];
                    size_t end = vm->cap_end[r];
                    if (end >= start && end <= vm->len) {
                        const char *data = vm->s + start;
                        size_t len = end - start;
                        
                        if (expr_type == 1) { // .upper()
#ifdef STANDALONE_BUILD
                            char *tmp = malloc(len + 1);
#else
                            char *tmp = emalloc(len + 1);
#endif
                            for (size_t i = 0; i < len; ++i) {
                                char c = data[i];
                                if (c >= 'a' && c <= 'z') tmp[i] = c - ('a' - 'A');
                                else tmp[i] = c;
                            }
                            if (vm->out) snobol_buf_append(vm->out, tmp, len);
                            if (vm->emit_fn) vm->emit_fn(tmp, len, vm->emit_udata);
#ifdef STANDALONE_BUILD
                            free(tmp);
#else
                            efree(tmp);
#endif
                        } else if (expr_type == 2) { // .length()
                            char tmp[32];
                            int n = snprintf(tmp, sizeof(tmp), "%zu", len);
                            if (vm->out) snobol_buf_append(vm->out, tmp, (size_t)n);
                            if (vm->emit_fn) vm->emit_fn(tmp, (size_t)n, vm->emit_udata);
                        } else {
                            // default: just emit
                            if (vm->out) snobol_buf_append(vm->out, data, len);
                            if (vm->emit_fn) vm->emit_fn(data, len, vm->emit_udata);
                        }
                    }
                }
                break;
            }

            default:
                if (!vm_pop_choice(vm)) goto fail_ret;
                break;
        }
    }
    if (vm->choices) { 
#ifdef STANDALONE_BUILD
        free(vm->choices); 
#else
        efree(vm->choices);
#endif
        vm->choices = NULL; 
    }
    return false;

fail_ret:
    if (vm->choices) { 
#ifdef STANDALONE_BUILD
        free(vm->choices); 
#else
        efree(vm->choices);
#endif
        vm->choices = NULL; 
    }
    SNOBOL_LOG("vm_run: FAIL");
    return false;
}

bool vm_exec(VM *vm) {
    vm->ip = 0;
    vm->pos = 0;
    vm->max_cap_used = 0;
    vm->max_counter_used = 0;
    memset(vm->counters, 0, sizeof(vm->counters));
#ifdef SNOBOL_PROFILE
    memset(&vm->profile, 0, sizeof(vm->profile));
#endif
    bool res = vm_run(vm);
#ifdef SNOBOL_PROFILE
    fprintf(stderr, "[SNOBOL PROFILE] dispatch=%llu push=%llu pop=%llu max_depth=%zu\n",
            (unsigned long long)vm->profile.dispatch_count,
            (unsigned long long)vm->profile.push_count,
            (unsigned long long)vm->profile.pop_count,
            vm->profile.max_depth);
#endif
    return res;
}