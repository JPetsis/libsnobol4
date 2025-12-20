#ifndef STANDALONE_BUILD
#include "php.h"
#endif

#include "snobol_vm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

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
#define SNOBOL_LOG(fmt, ...) snobol_log_impl(__FILE__, __LINE__, fmt, ##__VA_ARGS__)

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

static const uint8_t *get_bitmap(const VM *vm, uint16_t set_id) {
    size_t tail_ip = vm->bc_len;
    if (tail_ip < 4) return NULL;
    uint32_t count = ((uint32_t)vm->bc[tail_ip-4] << 24) | ((uint32_t)vm->bc[tail_ip-3] << 16) | ((uint32_t)vm->bc[tail_ip-2] << 8) | (uint32_t)vm->bc[tail_ip-1];
    size_t bitmaps_len = (size_t)count * CHARCLASS_BITMAP_BYTES;
    if (tail_ip < 4 + bitmaps_len) return NULL;
    size_t bitmaps_offset = tail_ip - 4 - bitmaps_len;
    if (set_id == 0 || set_id > count) return NULL;
    return vm->bc + bitmaps_offset + (size_t)(set_id - 1) * CHARCLASS_BITMAP_BYTES;
}

static inline bool bitmap_contains(const uint8_t *bm, unsigned char ch) {
    if (ch > 127) return false;
    unsigned idx = (unsigned)ch >> 3;
    unsigned bit = (unsigned)ch & 7;
    return (bm[idx] >> bit) & 1;
}

static inline int utf8_peek_next(const char *s, size_t len, size_t pos, uint32_t *out_cp, int *out_bytes) {
    if (pos >= len) return 0;
    unsigned char c = (unsigned char)s[pos];
    if (c < 0x80) {
        *out_cp = c; *out_bytes = 1; return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        if (pos + 1 >= len) return 0;
        *out_cp = ((c & 0x1F) << 6) | ((unsigned char)s[pos+1] & 0x3F);
        *out_bytes = 2; return 1;
    }
    if ((c & 0xF0) == 0xE0) {
        if (pos + 2 >= len) return 0;
        *out_cp = ((c & 0x0F) << 12) | (((unsigned char)s[pos+1] & 0x3F) << 6) | ((unsigned char)s[pos+2] & 0x3F);
        *out_bytes = 3; return 1;
    }
    if ((c & 0xF8) == 0xF0) {
        if (pos + 3 >= len) return 0;
        *out_cp = ((c & 0x07) << 18) | (((unsigned char)s[pos+1] & 0x3F) << 12) |
                  (((unsigned char)s[pos+2] & 0x3F) << 6) | ((unsigned char)s[pos+3] & 0x3F);
        *out_bytes = 4; return 1;
    }
    return 0;
}

#define MAX_CHOICES 128

void vm_push_choice(VM *vm, size_t ip, size_t pos) {
    if (!vm->choices || vm->choices_top >= MAX_CHOICES) return;
    struct choice *c = &vm->choices[vm->choices_top++];
    c->ip = ip;
    c->pos = pos;
    memcpy(c->cap_start_snapshot, vm->cap_start, sizeof(size_t) * MAX_CAPS);
    memcpy(c->cap_end_snapshot, vm->cap_end, sizeof(size_t) * MAX_CAPS);
    c->var_count_snapshot = vm->var_count;
    SNOBOL_LOG("vm_push_choice: top=%zu, ip=%zu, pos=%zu", vm->choices_top, ip, pos);
}

bool vm_pop_choice(VM *vm) {
    if (!vm->choices || vm->choices_top == 0) return false;
    vm->choices_top--;
    struct choice *c = &vm->choices[vm->choices_top];
    vm->ip = c->ip;
    vm->pos = c->pos;
    memcpy(vm->cap_start, c->cap_start_snapshot, sizeof(size_t) * MAX_CAPS);
    memcpy(vm->cap_end, c->cap_end_snapshot, sizeof(size_t) * MAX_CAPS);
    vm->var_count = c->var_count_snapshot;
    SNOBOL_LOG("vm_pop_choice: top=%zu, ip=%zu, pos=%zu", vm->choices_top, vm->ip, vm->pos);
    return true;
}

bool vm_exec(VM *vm) {
    vm->ip = 0;
    vm->pos = 0;
#ifdef STANDALONE_BUILD
    vm->choices = malloc(MAX_CHOICES * sizeof(struct choice));
#else
    vm->choices = emalloc(MAX_CHOICES * sizeof(struct choice));
#endif
    if (!vm->choices) {
        SNOBOL_LOG("vm_exec: FAILED to allocate choices");
        return false;
    }
    vm->choices_top = 0;
    SNOBOL_LOG("vm_exec: START, choices=%p", (void*)vm->choices);

    while (1) {
        if (vm->ip >= vm->bc_len) {
            if (!vm_pop_choice(vm)) {
                if (vm->choices) { 
#ifdef STANDALONE_BUILD
                    free(vm->choices); 
#else
                    efree(vm->choices);
#endif
                    vm->choices = NULL; 
                }
                SNOBOL_LOG("vm_exec: FAIL (end of bytecode)");
                return false;
            }
            continue;
        }
        
        size_t current_ip = vm->ip;
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
                SNOBOL_LOG("vm_exec: SUCCESS (OP_ACCEPT)");
                return true;

            case OP_FAIL:
                if (!vm_pop_choice(vm)) {
                    if (vm->choices) { 
#ifdef STANDALONE_BUILD
                        free(vm->choices); 
#else
                        efree(vm->choices);
#endif
                        vm->choices = NULL; 
                    }
                    SNOBOL_LOG("vm_exec: FAIL (OP_FAIL)");
                    return false;
                }
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
                vm->ip += len;
                if (len <= vm->len - vm->pos && memcmp(vm->s + vm->pos, vm->bc + off, len) == 0) {
                    vm->pos += len;
                } else {
                    if (!vm_pop_choice(vm)) {
                        if (vm->choices) { 
#ifdef STANDALONE_BUILD
                            free(vm->choices); 
#else
                            efree(vm->choices);
#endif
                            vm->choices = NULL; 
                        }
                        return false;
                    }
                }
                break;
            }

            case OP_ANY: {
                uint16_t set_id = read_u16(vm->bc, vm->bc_len, &vm->ip);
                uint32_t cp; int bytes;
                if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes)) {
                    if (!vm_pop_choice(vm)) {
                        if (vm->choices) { 
#ifdef STANDALONE_BUILD
                            free(vm->choices); 
#else
                            efree(vm->choices);
#endif
                            vm->choices = NULL; 
                        }
                        return false;
                    }
                } else {
                    if (set_id == 0) {
                        vm->pos += bytes;
                    } else if (cp <= 127) {
                        const uint8_t *bm = get_bitmap(vm, set_id);
                        if (bm && bitmap_contains(bm, (unsigned char)cp)) {
                            vm->pos += bytes;
                        } else {
                            if (!vm_pop_choice(vm)) {
                                if (vm->choices) { 
#ifdef STANDALONE_BUILD
                                    free(vm->choices); 
#else
                                    efree(vm->choices);
#endif
                                    vm->choices = NULL; 
                                }
                                return false;
                            }
                        }
                    } else {
                        if (!vm_pop_choice(vm)) {
                            if (vm->choices) { 
#ifdef STANDALONE_BUILD
                                free(vm->choices); 
#else
                                efree(vm->choices);
#endif
                                vm->choices = NULL; 
                            }
                            return false;
                        }
                    }
                }
                break;
            }

            case OP_NOTANY: {
                uint16_t set_id = read_u16(vm->bc, vm->bc_len, &vm->ip);
                uint32_t cp; int bytes;
                if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes)) {
                    if (!vm_pop_choice(vm)) {
                        if (vm->choices) { 
#ifdef STANDALONE_BUILD
                            free(vm->choices); 
#else
                            efree(vm->choices);
#endif
                            vm->choices = NULL; 
                        }
                        return false;
                    }
                } else {
                    if (cp <= 127) {
                        const uint8_t *bm = get_bitmap(vm, set_id);
                        if (bm && bitmap_contains(bm, (unsigned char)cp)) {
                            if (!vm_pop_choice(vm)) {
                                if (vm->choices) { 
#ifdef STANDALONE_BUILD
                                    free(vm->choices); 
#else
                                    efree(vm->choices);
#endif
                                    vm->choices = NULL; 
                                }
                                return false;
                            }
                        } else {
                            vm->pos += bytes;
                        }
                    } else {
                        vm->pos += bytes;
                    }
                }
                break;
            }

            case OP_SPAN: {
                uint16_t set_id = read_u16(vm->bc, vm->bc_len, &vm->ip);
                uint32_t cp; int bytes;
                if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes)) {
                    if (!vm_pop_choice(vm)) {
                        if (vm->choices) { 
#ifdef STANDALONE_BUILD
                            free(vm->choices); 
#else
                            efree(vm->choices);
#endif
                            vm->choices = NULL; 
                        }
                        return false;
                    }
                    break;
                }
                const uint8_t *bm = get_bitmap(vm, set_id);
                if (cp <= 127) {
                    if (!bm || !bitmap_contains(bm, (unsigned char)cp)) {
                        if (!vm_pop_choice(vm)) {
                            if (vm->choices) { 
#ifdef STANDALONE_BUILD
                                free(vm->choices); 
#else
                                efree(vm->choices);
#endif
                                vm->choices = NULL; 
                            }
                            return false;
                        }
                        break;
                    }
                }
                vm->pos += bytes;
                while (1) {
                    if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes)) break;
                    if (cp <= 127) {
                        const uint8_t *bm2 = get_bitmap(vm, set_id);
                        if (!bm2 || !bitmap_contains(bm2, (unsigned char)cp)) break;
                    }
                    vm->pos += bytes;
                }
                break;
            }

            case OP_BREAK: {
                uint16_t set_id = read_u16(vm->bc, vm->bc_len, &vm->ip);
                uint32_t cp; int bytes;
                const uint8_t *bm = get_bitmap(vm, set_id);
                while (1) {
                    if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes)) break;
                    if (cp <= 127) {
                        if (bm && bitmap_contains(bm, (unsigned char)cp)) break;
                    }
                    vm->pos += bytes;
                }
                break;
            }

            case OP_CAP_START: {
                uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip);
                if (r < MAX_CAPS) {
                    vm->cap_start[r] = vm->pos;
                    SNOBOL_LOG("OP_CAP_START r=%u, pos=%zu", r, vm->pos);
                }
                break;
            }

            case OP_CAP_END: {
                uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip);
                if (r < MAX_CAPS) {
                    vm->cap_end[r] = vm->pos;
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
                    if (!vm_pop_choice(vm)) {
                        if (vm->choices) { 
#ifdef STANDALONE_BUILD
                            free(vm->choices); 
#else
                            efree(vm->choices);
#endif
                            vm->choices = NULL; 
                        }
                        return false;
                    }
                } else {
                    vm->pos = p;
                }
                break;
            }

            case OP_EVAL: {
                uint16_t fn = read_u16(vm->bc, vm->bc_len, &vm->ip);
                uint8_t r = read_u8(vm->bc, vm->bc_len, &vm->ip);
                if (r >= MAX_CAPS) {
                    if (!vm_pop_choice(vm)) {
                        if (vm->choices) { 
#ifdef STANDALONE_BUILD
                                free(vm->choices); 
#else
                                efree(vm->choices);
#endif
                            vm->choices = NULL; 
                        }
                        return false;
                    }
                    break;
                }
                size_t a = vm->cap_start[r];
                size_t b = vm->cap_end[r];
                if (vm->eval_fn) {
                    if (!vm->eval_fn((int)fn, vm->s, a, b, vm->eval_udata)) {
                        if (!vm_pop_choice(vm)) {
                            if (vm->choices) { 
#ifdef STANDALONE_BUILD
                                free(vm->choices); 
#else
                                efree(vm->choices);
#endif
                                vm->choices = NULL; 
                            }
                            return false;
                        }
                    }
                }
                break;
            }

            default:
                if (!vm_pop_choice(vm)) {
                    if (vm->choices) { 
#ifdef STANDALONE_BUILD
                        free(vm->choices); 
#else
                        efree(vm->choices);
#endif
                        vm->choices = NULL; 
                    }
                    return false;
                }
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
}