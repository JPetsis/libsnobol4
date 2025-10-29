#include "snobol_vm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* helpers to read u32/u16/u8 from bc */
static inline uint32_t read_u32(const uint8_t *bc, size_t *ip) {
    uint32_t v = (bc[*ip] << 24) | (bc[*ip+1] << 16) | (bc[*ip+2] << 8) | bc[*ip+3];
    *ip += 4;
    return v;
}
static inline uint16_t read_u16(const uint8_t *bc, size_t *ip) {
    uint16_t v = (bc[*ip] << 8) | bc[*ip+1];
    *ip += 2;
    return v;
}
static inline uint8_t read_u8(const uint8_t *bc, size_t *ip) {
    uint8_t v = bc[(*ip)++];
    return v;
}

/* A minimal ASCII charclass: for this package we support ASCII 0-127 via bitmap.
   For simplicity we store charclass tables in the bytecode blob externally (compiler provides set ids).
   In this implementation we'll assume 1..N charclasses and the vm will request the compiler to provide
   membership via a function pointer later. For now, we support only ASCII and simple sets handled by the compiler.
   To keep things contained, we will store charclass bitmaps just after bytecode (compiler packs them and sets
   bc_len appropriately). We'll provide a helper that given set_id returns pointer to 16-byte bitmap (128 bits).
*/

#define CHARCLASS_BITMAP_BYTES 16 /* 128 bits / 8 */

static const uint8_t *get_bitmap(const VM *vm, uint16_t set_id) {
    /* Simple encoding: after bc_len header, compiler appends (1-based) charclass bitmaps
       But vm only sees bc pointer and bc_len; compiler ensures that charclass bitmaps are at the end. */
    /* Layout enforced by compiler: bc contains: code bytes followed by a 4-byte count N, then N * 16 bytes of bitmaps.
       To find them, we read the 4 bytes located just before vm->bc_len - N*16 - 4; but bc_len is known only in caller.
       For simplicity, we assume compiler placed a 4-byte charclass_count at vm->bc + vm->bc_len - 4 - (N * 16)
       and then bitmaps start right after that header. */

    /* We'll decode: read last 4 bytes as uint32_t count, then compute offset */
    size_t tail_ip = vm->bc_len;
    if (tail_ip < 4) return NULL;
    uint32_t count = (vm->bc[tail_ip-4] << 24) | (vm->bc[tail_ip-3] << 16) | (vm->bc[tail_ip-2] << 8) | vm->bc[tail_ip-1];
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

/* UTF-8 next codepoint: returns bytes consumed (1..4) and updates pos; returns 0 if at end/invalid */
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

/* choice push/pop */
void vm_push_choice(VM *vm, size_t ip, size_t pos) {
    if (vm->choices_top == vm->choices_cap) {
        size_t new_cap = vm->choices_cap ? vm->choices_cap * 2 : 256;
        vm->choices = erealloc(vm->choices, new_cap * sizeof(*vm->choices));
        vm->choices_cap = new_cap;
    }
    struct choice *c = &vm->choices[vm->choices_top++];
    c->ip = ip;
    c->pos = pos;
    /* snapshot captures and var_count */
    memcpy(c->cap_start_snapshot, vm->cap_start, sizeof(vm->cap_start));
    memcpy(c->cap_end_snapshot, vm->cap_end, sizeof(vm->cap_end));
    c->var_count_snapshot = vm->var_count;
}

bool vm_pop_choice(VM *vm) {
    if (vm->choices_top == 0) return false;
    vm->choices_top--;
    struct choice *c = &vm->choices[vm->choices_top];
    vm->ip = c->ip;
    vm->pos = c->pos;
    memcpy(vm->cap_start, c->cap_start_snapshot, sizeof(vm->cap_start));
    memcpy(vm->cap_end, c->cap_end_snapshot, sizeof(vm->cap_end));
    vm->var_count = c->var_count_snapshot;
    return true;
}

/* execute VM */
bool vm_exec(VM *vm) {
    vm->ip = 0;
    vm->pos = 0;
    vm->choices = NULL;
    vm->choices_cap = 0;
    vm->choices_top = 0;

    while (1) {
        if (vm->ip >= vm->bc_len) {
            /* no more instructions => fail */
            if (!vm_pop_choice(vm)) {
                if (vm->choices) efree(vm->choices);
                return false;
            }
            continue;
        }
        uint8_t op = vm->bc[vm->ip++];

        switch (op) {
            case OP_ACCEPT:
                if (vm->choices) efree(vm->choices);
                return true;

            case OP_FAIL:
                if (!vm_pop_choice(vm)) {
                    if (vm->choices) efree(vm->choices);
                    return false;
                }
                break;

            case OP_JMP: {
                uint32_t tgt = read_u32(vm->bc, &vm->ip);
                vm->ip = (size_t)tgt;
                break;
            }

            case OP_SPLIT: {
                uint32_t a = read_u32(vm->bc, &vm->ip);
                uint32_t b = read_u32(vm->bc, &vm->ip);
                /* push continuation to b, continue at a */
                vm_push_choice(vm, (size_t)b, vm->pos);
                vm->ip = (size_t)a;
                break;
            }

            case OP_LIT: {
                uint32_t off = read_u32(vm->bc, &vm->ip);
                uint32_t len = read_u32(vm->bc, &vm->ip);

                /* Skip past the inline literal data */
                vm->ip += len;

                if (len <= vm->len - vm->pos &&
                    memcmp(vm->s + vm->pos, vm->bc + off, len) == 0) {
                    vm->pos += len;
                } else {
                    if (!vm_pop_choice(vm)) {
                        if (vm->choices) efree(vm->choices);
                        return false;
                    }
                }
                break;
            }

            case OP_ANY: {
                uint16_t set_id = read_u16(vm->bc, &vm->ip);
                uint32_t cp; int bytes;
                if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes)) {
                    if (!vm_pop_choice(vm)) {
                        if (vm->choices) efree(vm->choices);
                        return false;
                    }
                } else {
                    /* Only support ASCII membership via bitmaps for fast path.
                       If cp>127, treat as match (ANY means any codepoint) */
                    if (cp <= 127) {
                        const uint8_t *bm = get_bitmap(vm, set_id);
                        if (bm && bitmap_contains(bm, (unsigned char)cp)) {
                            vm->pos += bytes;
                        } else {
                            if (!vm_pop_choice(vm)) {
                                if (vm->choices) efree(vm->choices);
                                return false;
                            }
                        }
                    } else {
                        /* ANY: accept any codepoint */
                        vm->pos += bytes;
                    }
                }
                break;
            }

            case OP_NOTANY: {
                uint16_t set_id = read_u16(vm->bc, &vm->ip);
                uint32_t cp; int bytes;
                if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes)) {
                    if (!vm_pop_choice(vm)) {
                        if (vm->choices) efree(vm->choices);
                        return false;
                    }
                } else {
                    if (cp <= 127) {
                        const uint8_t *bm = get_bitmap(vm, set_id);
                        if (bm && bitmap_contains(bm, (unsigned char)cp)) {
                            if (!vm_pop_choice(vm)) {
                                if (vm->choices) efree(vm->choices);
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
                uint16_t set_id = read_u16(vm->bc, &vm->ip);
                /* require at least 1 char from set */
                uint32_t cp; int bytes;
                if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes)) {
                    if (!vm_pop_choice(vm)) {
                        if (vm->choices) efree(vm->choices);
                        return false;
                    }
                    break;
                }
                if (cp <= 127) {
                    const uint8_t *bm = get_bitmap(vm, set_id);
                    if (!bm || !bitmap_contains(bm, (unsigned char)cp)) {
                        if (!vm_pop_choice(vm)) {
                            if (vm->choices) efree(vm->choices);
                            return false;
                        }
                        break;
                    }
                }
                vm->pos += bytes;
                /* consume more greedily */
                while (1) {
                    if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes)) break;
                    if (cp <= 127) {
                        const uint8_t *bm = get_bitmap(vm, set_id);
                        if (!bm || !bitmap_contains(bm, (unsigned char)cp)) break;
                    }
                    vm->pos += bytes;
                }
                break;
            }

            case OP_BREAK: {
                uint16_t set_id = read_u16(vm->bc, &vm->ip);
                uint32_t cp; int bytes;
                while (1) {
                    if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes)) break;
                    if (cp <= 127) {
                        const uint8_t *bm = get_bitmap(vm, set_id);
                        if (bm && bitmap_contains(bm, (unsigned char)cp)) {
                            break; /* stop BEFORE this char (do not consume) */
                        }
                    }
                    vm->pos += bytes; /* consume */
                }
                break;
            }

            case OP_CAP_START: {
                uint8_t r = read_u8(vm->bc, &vm->ip);
                if (r < MAX_CAPS) vm->cap_start[r] = vm->pos;
                break;
            }

            case OP_CAP_END: {
                uint8_t r = read_u8(vm->bc, &vm->ip);
                if (r < MAX_CAPS) vm->cap_end[r] = vm->pos;
                break;
            }

            case OP_ASSIGN: {
                uint16_t var = read_u16(vm->bc, &vm->ip);
                uint8_t r = read_u8(vm->bc, &vm->ip);
                if (var < MAX_VARS && r < MAX_CAPS) {
                    if (var >= vm->var_count) vm->var_count = var + 1;
                    vm->var_start[var] = vm->cap_start[r];
                    vm->var_end[var] = vm->cap_end[r];
                }
                break;
            }

            case OP_LEN: {
                uint32_t n = read_u32(vm->bc, &vm->ip);
                size_t p = vm->pos;
                uint32_t i;
                for (i = 0; i < n; ++i) {
                    uint32_t cp; int bytes;
                    if (!utf8_peek_next(vm->s, vm->len, p, &cp, &bytes)) break;
                    p += bytes;
                }
                if (i != n) {
                    if (!vm_pop_choice(vm)) {
                        if (vm->choices) efree(vm->choices);
                        return false;
                    }
                } else {
                    vm->pos = p;
                }
                break;
            }

            case OP_EVAL: {
                uint16_t fn = read_u16(vm->bc, &vm->ip);
                uint8_t r = read_u8(vm->bc, &vm->ip);
                if (r >= MAX_CAPS) {
                    if (!vm_pop_choice(vm)) {
                        if (vm->choices) efree(vm->choices);
                        return false;
                    }
                    break;
                }
                size_t a = vm->cap_start[r];
                size_t b = vm->cap_end[r];
                bool ok = true;
                if (vm->eval_fn) ok = vm->eval_fn((int)fn, vm->s, a, b, vm->eval_udata);
                if (!ok) {
                    if (!vm_pop_choice(vm)) {
                        if (vm->choices) efree(vm->choices);
                        return false;
                    }
                }
                break;
            }

            default:
                /* unknown op */
                if (!vm_pop_choice(vm)) {
                    if (vm->choices) efree(vm->choices);
                    return false;
                }
                break;
        }
    }
    /* unreachable */
    if (vm->choices) efree(vm->choices);
    return false;
}
