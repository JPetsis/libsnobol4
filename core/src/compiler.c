#include "snobol/snobol_internal.h"
#include "snobol/vm.h"      /* MUST come before snobol/compiler.h to get CHARCLASS_BITMAP_BYTES */
#include "snobol/compiler.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

/* Minimal dynamic code buffer */
typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
} CodeBuf;

static void cb_init(CodeBuf *c) {
    c->cap = 4096;
    c->buf = snobol_malloc(c->cap);
    c->len = 0;
}
static void cb_free(CodeBuf *c) {
    if (c->buf) {
        snobol_free(c->buf);
        c->buf = NULL;
    }
    c->cap = c->len = 0;
}
static void cb_ensure(CodeBuf *c, size_t need) {
    if (c->len + need <= c->cap) return;
    size_t newcap = c->cap ? c->cap * 2 : 4096;
    while (c->len + need > newcap) newcap *= 2;
    c->buf = snobol_realloc(c->buf, newcap);
    c->cap = newcap;
}
static size_t cb_pos(CodeBuf *c) { return c->len; }
static void cb_emit_u8(CodeBuf *c, uint8_t v) { cb_ensure(c,1); c->buf[c->len++] = v; }
static void cb_emit_u16(CodeBuf *c, uint16_t v) { cb_ensure(c,2); c->buf[c->len++] = (v >> 8) & 0xff; c->buf[c->len++] = v & 0xff; }
static void cb_emit_u32(CodeBuf *c, uint32_t v) { cb_ensure(c,4); c->buf[c->len++] = (v >> 24) & 0xff; c->buf[c->len++] = (v >> 16) & 0xff; c->buf[c->len++] = (v >> 8) & 0xff; c->buf[c->len++] = v & 0xff; }
static void cb_emit_bytes(CodeBuf *c, const uint8_t *b, size_t n) { if (n==0) return; cb_ensure(c,n); memcpy(c->buf + c->len, b, n); c->len += n; }

/* Charclass table handling */
typedef struct cc_entry {
    CpRange *ranges;
    uint16_t range_count;
    uint16_t range_cap;
    uint16_t case_insensitive;
    struct cc_entry *next;
} CCEntry;
static CCEntry *charclass_head = NULL;
static uint32_t charclass_count = 0;
static uint8_t next_loop_id = 0;
static bool compiler_case_insensitive = false;

static void free_charclass_list(void) {
    CCEntry *e = charclass_head;
    while (e) {
        CCEntry *next = e->next;
        if (e->ranges) snobol_free(e->ranges);
        snobol_free(e);
        e = next;
    }
    charclass_head = NULL;
    charclass_count = 0;
    compiler_case_insensitive = false;
}

static void add_range(CCEntry *e, uint32_t start, uint32_t end) {
    if (e->range_count == e->range_cap) {
        e->range_cap = e->range_cap ? e->range_cap * 2 : 4;
        e->ranges = snobol_realloc(e->ranges, e->range_cap * sizeof(CpRange));
    }
    e->ranges[e->range_count].start = start;
    e->ranges[e->range_count].end = end;
    e->range_count++;
}

static int compare_ranges(const void *a, const void *b) {
    const CpRange *ra = (const CpRange*)a;
    const CpRange *rb = (const CpRange*)b;
    if (ra->start < rb->start) return -1;
    if (ra->start > rb->start) return 1;
    return 0;
}

static void normalize_ranges(CCEntry *e) {
    if (e->range_count == 0) return;
    qsort(e->ranges, e->range_count, sizeof(CpRange), compare_ranges);
    
    size_t write = 0;
    for (size_t read = 1; read < e->range_count; ++read) {
        if (e->ranges[read].start <= e->ranges[write].end + 1) {
            if (e->ranges[read].end > e->ranges[write].end) {
                e->ranges[write].end = e->ranges[read].end;
            }
        } else {
            write++;
            e->ranges[write] = e->ranges[read];
        }
    }
    e->range_count = (uint16_t)(write + 1);
}

static int add_or_get_charclass(const char *s, size_t len) {
    CCEntry *ne = snobol_malloc(sizeof(*ne));
    memset(ne, 0, sizeof(*ne));
    ne->case_insensitive = compiler_case_insensitive ? 1 : 0;
    
    size_t pos = 0;
    while (pos < len) {
        uint32_t cp; int bytes;
        if (!utf8_peek_next(s, len, pos, &cp, &bytes)) break;
        add_range(ne, cp, cp);
        
        if (compiler_case_insensitive) {
            if (cp >= 'A' && cp <= 'Z') {
                add_range(ne, cp + 32, cp + 32);
            } else if (cp >= 'a' && cp <= 'z') {
                add_range(ne, cp - 32, cp - 32);
            } else if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) {
                 add_range(ne, cp + 0x20, cp + 0x20);
            } else if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) {
                 add_range(ne, cp - 0x20, cp - 0x20);
            }
        }
        
        pos += bytes;
    }
    normalize_ranges(ne);
    
    CCEntry *e = charclass_head;
    int id = 1;
    while (e) {
        if (e->range_count == ne->range_count &&
            e->case_insensitive == ne->case_insensitive &&
            memcmp(e->ranges, ne->ranges, e->range_count * sizeof(CpRange)) == 0) {
            if (ne->ranges) snobol_free(ne->ranges);
            snobol_free(ne);
            return id;
        }
        id++; e = e->next;
    }
    
    ne->next = NULL;
    if (!charclass_head) {
        charclass_head = ne;
    } else {
        CCEntry *tail = charclass_head;
        while (tail->next) tail = tail->next;
        tail->next = ne;
    }
    charclass_count++;
    return (int)charclass_count;
}

/* Emit literal inline: OP_LIT offset len bytes... (offset points to payload location) */
static int emit_lit_bytes(CodeBuf *c, const char *s, size_t len) {
    size_t off_of_payload = cb_pos(c) + 1 + 4 + 4;
    cb_emit_u8(c, OP_LIT);
    cb_emit_u32(c, (uint32_t)off_of_payload);
    cb_emit_u32(c, (uint32_t)len);
    cb_emit_bytes(c, (const uint8_t*)s, len);
    return 0;
}

#ifndef STANDALONE_BUILD
/* Forward */
static int emit_node(zval *node, CodeBuf *c);

/* concat */
static int emit_concat(zval *parts, CodeBuf *c) {
    if (!parts || Z_TYPE_P(parts) != IS_ARRAY) return -1;
    zval *entry;
    int result = 0;
    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(parts), entry) {
        if (!entry) continue;
        if (emit_node(entry, c) != 0) {
            result = -1;
            break;
        }
    } ZEND_HASH_FOREACH_END();
    return result;
}

/* alt */
static int emit_alt(zval *left, zval *right, CodeBuf *c) {
    size_t where_split = cb_pos(c);
    cb_emit_u8(c, OP_SPLIT);
    size_t where_a = cb_pos(c); cb_emit_u32(c, 0);
    size_t where_b = cb_pos(c); cb_emit_u32(c, 0);

    size_t left_start = cb_pos(c);
    if (emit_node(left, c) != 0) return -1;

    size_t jmp_where = cb_pos(c);
    cb_emit_u8(c, OP_JMP);
    size_t jmp_target_pos = cb_pos(c); cb_emit_u32(c, 0);

    size_t right_start = cb_pos(c);
    if (emit_node(right, c) != 0) return -1;

    size_t end_pos = cb_pos(c);

    uint32_t v1 = (uint32_t)left_start;
    c->buf[where_a+0] = (v1 >> 24) & 0xff; c->buf[where_a+1] = (v1 >> 16) & 0xff; c->buf[where_a+2] = (v1 >> 8) & 0xff; c->buf[where_a+3] = v1 & 0xff;
    uint32_t v2 = (uint32_t)right_start;
    c->buf[where_b+0] = (v2 >> 24) & 0xff; c->buf[where_b+1] = (v2 >> 16) & 0xff; c->buf[where_b+2] = (v2 >> 8) & 0xff; c->buf[where_b+3] = v2 & 0xff;
    uint32_t vj = (uint32_t)end_pos;
    c->buf[jmp_target_pos+0] = (vj >> 24) & 0xff; c->buf[jmp_target_pos+1] = (vj >> 16) & 0xff; c->buf[jmp_target_pos+2] = (vj >> 8) & 0xff; c->buf[jmp_target_pos+3] = vj & 0xff;

    return 0;
}

/* arbno (zero or more) */
static int emit_arbno(zval *sub, CodeBuf *c) {
    // Optimization: Unwrap nested ARBNOs.
    while (sub && Z_TYPE_P(sub) == IS_ARRAY) {
        zval *type_zv = zend_hash_str_find(Z_ARRVAL_P(sub), "type", sizeof("type")-1);
        if (!type_zv || Z_TYPE_P(type_zv) != IS_STRING) break;
        zend_string *type = Z_STR_P(type_zv);
        
        if (zend_string_equals_literal(type, "arbno")) {
            zval *inner = zend_hash_str_find(Z_ARRVAL_P(sub), "sub", sizeof("sub")-1);
            if (inner) {
                sub = inner;
                continue;
            }
        } else if (zend_string_equals_literal(type, "repeat")) {
            // Check for repeat(x, 0, -1) which is ARBNO
            zval *min = zend_hash_str_find(Z_ARRVAL_P(sub), "min", sizeof("min")-1);
            zval *max = zend_hash_str_find(Z_ARRVAL_P(sub), "max", sizeof("max")-1);
            if (min && max && Z_TYPE_P(min) == IS_LONG && Z_TYPE_P(max) == IS_LONG) {
                if (Z_LVAL_P(min) == 0 && Z_LVAL_P(max) == -1) {
                    zval *inner = zend_hash_str_find(Z_ARRVAL_P(sub), "sub", sizeof("sub")-1);
                    if (inner) {
                        sub = inner;
                        continue;
                    }
                }
            }
        }
        break;
    }

    if (next_loop_id >= MAX_LOOPS) return -1;
    uint8_t loop_id = next_loop_id++;
    uint32_t min = 0;
    uint32_t max = (uint32_t)-1;

    size_t init_pos = cb_pos(c);
    cb_emit_u8(c, OP_REPEAT_INIT);
    cb_emit_u8(c, loop_id);
    cb_emit_u32(c, min);
    cb_emit_u32(c, max);
    size_t skip_target_off = cb_pos(c);
    cb_emit_u32(c, 0); // placeholder for skip_target

    size_t body_start = cb_pos(c);
    if (emit_node(sub, c) != 0) return -1;

    cb_emit_u8(c, OP_REPEAT_STEP);
    cb_emit_u8(c, loop_id);
    cb_emit_u32(c, (uint32_t)body_start);

    size_t done_pos = cb_pos(c);
    // Fill skip_target
    uint32_t v_done = (uint32_t)done_pos;
    c->buf[skip_target_off+0] = (v_done >> 24) & 0xff;
    c->buf[skip_target_off+1] = (v_done >> 16) & 0xff;
    c->buf[skip_target_off+2] = (v_done >> 8) & 0xff;
    c->buf[skip_target_off+3] = v_done & 0xff;

    return 0;
}

/* span/break/any/notany */
static int emit_span(zval *set_str, CodeBuf *c) {
    if (!set_str || Z_TYPE_P(set_str) != IS_STRING) return -1;
    zend_string *zs = Z_STR_P(set_str);
    int setid = add_or_get_charclass(ZSTR_VAL(zs), ZSTR_LEN(zs));
    cb_emit_u8(c, OP_SPAN); cb_emit_u16(c, (uint16_t)setid);
    return 0;
}
static int emit_break(zval *set_str, CodeBuf *c) {
    if (Z_TYPE_P(set_str) != IS_STRING) return -1;
    zend_string *zs = Z_STR_P(set_str);
    int setid = add_or_get_charclass(ZSTR_VAL(zs), ZSTR_LEN(zs));
    cb_emit_u8(c, OP_BREAK); cb_emit_u16(c, (uint16_t)setid);
    return 0;
}
static int emit_any(zval *set_str, CodeBuf *c) {
    uint16_t setid = 0;
    if (set_str && Z_TYPE_P(set_str) == IS_STRING) {
        zend_string *zs = Z_STR_P(set_str);
        setid = (uint16_t)add_or_get_charclass(ZSTR_VAL(zs), ZSTR_LEN(zs));
    }
    cb_emit_u8(c, OP_ANY); cb_emit_u16(c, setid);
    return 0;
}
static int emit_notany(zval *set_str, CodeBuf *c) {
    if (Z_TYPE_P(set_str) != IS_STRING) return -1;
    zend_string *zs = Z_STR_P(set_str);
    int setid = add_or_get_charclass(ZSTR_VAL(zs), ZSTR_LEN(zs));
    cb_emit_u8(c, OP_NOTANY); cb_emit_u16(c, (uint16_t)setid);
    return 0;
}

/* cap/assign/len/eval */
static int emit_cap(zval *reg_zv, zval *sub, CodeBuf *c) {
    if (!reg_zv || Z_TYPE_P(reg_zv) != IS_LONG) return -1;
    long reg = Z_LVAL_P(reg_zv);
    cb_emit_u8(c, OP_CAP_START); cb_emit_u8(c, (uint8_t)reg);
    if (emit_node(sub, c) != 0) return -1;
    cb_emit_u8(c, OP_CAP_END); cb_emit_u8(c, (uint8_t)reg);
    return 0;
}
static int emit_assign(zval *var_zv, zval *reg_zv, CodeBuf *c) {
    if (Z_TYPE_P(var_zv) != IS_LONG || Z_TYPE_P(reg_zv) != IS_LONG) return -1;
    long var = Z_LVAL_P(var_zv);
    long reg = Z_LVAL_P(reg_zv);
    cb_emit_u8(c, OP_ASSIGN); cb_emit_u16(c, (uint16_t)var); cb_emit_u8(c, (uint8_t)reg);
    return 0;
}
static int emit_len(zval *n_zv, CodeBuf *c) {
    if (Z_TYPE_P(n_zv) != IS_LONG) return -1;
    long n = Z_LVAL_P(n_zv);
    cb_emit_u8(c, OP_LEN); cb_emit_u32(c, (uint32_t)n);
    return 0;
}
static int emit_eval(zval *fn_zv, zval *reg_zv, CodeBuf *c) {
    if (Z_TYPE_P(fn_zv) != IS_LONG || Z_TYPE_P(reg_zv) != IS_LONG) return -1;
    long fn = Z_LVAL_P(fn_zv);
    long reg = Z_LVAL_P(reg_zv);
    cb_emit_u8(c, OP_EVAL); cb_emit_u16(c, (uint16_t)fn); cb_emit_u8(c, (uint8_t)reg);
    return 0;
}

static int emit_anchor(zval *type_zv, CodeBuf *c) {
    if (!type_zv || Z_TYPE_P(type_zv) != IS_STRING) return -1;
    uint8_t t = 0;
    if (zend_string_equals_literal(Z_STR_P(type_zv), "start")) t = 0;
    else if (zend_string_equals_literal(Z_STR_P(type_zv), "end")) t = 1;
    else return -1;
    cb_emit_u8(c, OP_ANCHOR); cb_emit_u8(c, t);
    return 0;
}

static int emit_repeat(zval *sub, zval *min_zv, zval *max_zv, CodeBuf *c) {
    if (next_loop_id >= MAX_LOOPS) return -1;
    uint8_t loop_id = next_loop_id++;
    uint32_t min = (uint32_t)Z_LVAL_P(min_zv);
    uint32_t max = (uint32_t)Z_LVAL_P(max_zv);

    // Flatten logic for repeat(0, -1) same as ARBNO
    if (min == 0 && max == (uint32_t)-1) {
        while (sub && Z_TYPE_P(sub) == IS_ARRAY) {
            zval *type_zv = zend_hash_str_find(Z_ARRVAL_P(sub), "type", sizeof("type")-1);
            if (!type_zv || Z_TYPE_P(type_zv) != IS_STRING) break;
            zend_string *type = Z_STR_P(type_zv);
            
            if (zend_string_equals_literal(type, "arbno")) {
                zval *inner = zend_hash_str_find(Z_ARRVAL_P(sub), "sub", sizeof("sub")-1);
                if (inner) {
                    sub = inner;
                    continue;
                }
            } else if (zend_string_equals_literal(type, "repeat")) {
                zval *imin = zend_hash_str_find(Z_ARRVAL_P(sub), "min", sizeof("min")-1);
                zval *imax = zend_hash_str_find(Z_ARRVAL_P(sub), "max", sizeof("max")-1);
                if (imin && imax && Z_TYPE_P(imin) == IS_LONG && Z_TYPE_P(imax) == IS_LONG) {
                    if (Z_LVAL_P(imin) == 0 && Z_LVAL_P(imax) == -1) {
                        zval *inner = zend_hash_str_find(Z_ARRVAL_P(sub), "sub", sizeof("sub")-1);
                        if (inner) {
                            sub = inner;
                            continue;
                        }
                    }
                }
            }
            break;
        }
    }

    size_t init_pos = cb_pos(c);
    cb_emit_u8(c, OP_REPEAT_INIT);
    cb_emit_u8(c, loop_id);
    cb_emit_u32(c, min);
    cb_emit_u32(c, max);
    size_t skip_target_off = cb_pos(c);
    cb_emit_u32(c, 0); // placeholder for skip_target

    size_t body_start = cb_pos(c);
    if (emit_node(sub, c) != 0) return -1;

    cb_emit_u8(c, OP_REPEAT_STEP);
    cb_emit_u8(c, loop_id);
    cb_emit_u32(c, (uint32_t)body_start);

    size_t done_pos = cb_pos(c);
    // Fill skip_target
    uint32_t v_done = (uint32_t)done_pos;
    c->buf[skip_target_off+0] = (v_done >> 24) & 0xff;
    c->buf[skip_target_off+1] = (v_done >> 16) & 0xff;
    c->buf[skip_target_off+2] = (v_done >> 8) & 0xff;
    c->buf[skip_target_off+3] = v_done & 0xff;

    return 0;
}

static int emit_emit(zval *node, CodeBuf *c) {
    zval *text = zend_hash_str_find(Z_ARRVAL_P(node), "text", sizeof("text")-1);
    zval *reg = zend_hash_str_find(Z_ARRVAL_P(node), "reg", sizeof("reg")-1);
    if (text && Z_TYPE_P(text) == IS_STRING) {
        zend_string *zs = Z_STR_P(text);
        size_t off_of_payload = cb_pos(c) + 1 + 4 + 4;
        cb_emit_u8(c, OP_EMIT_LITERAL);
        cb_emit_u32(c, (uint32_t)off_of_payload);
        cb_emit_u32(c, (uint32_t)ZSTR_LEN(zs));
        cb_emit_bytes(c, (const uint8_t*)ZSTR_VAL(zs), ZSTR_LEN(zs));
        return 0;
    } else if (reg && Z_TYPE_P(reg) == IS_LONG) {
        cb_emit_u8(c, OP_EMIT_CAPTURE);
        cb_emit_u8(c, (uint8_t)Z_LVAL_P(reg));
        return 0;
    }
    return -1;
}

static int emit_node(zval *node, CodeBuf *c) {
    if (Z_TYPE_P(node) != IS_ARRAY) return -1;
    zval *type_zv = zend_hash_str_find(Z_ARRVAL_P(node), "type", sizeof("type")-1);
    if (!type_zv || Z_TYPE_P(type_zv) != IS_STRING) return -1;
    zend_string *type = Z_STR_P(type_zv);

    if (zend_string_equals_literal(type, "lit")) {
        zval *text = zend_hash_str_find(Z_ARRVAL_P(node), "text", sizeof("text")-1);
        if (!text || Z_TYPE_P(text) != IS_STRING) return -1;
        zend_string *zs = Z_STR_P(text);
        return emit_lit_bytes(c, ZSTR_VAL(zs), ZSTR_LEN(zs));
    }
    if (zend_string_equals_literal(type, "concat")) {
        zval *parts = zend_hash_str_find(Z_ARRVAL_P(node), "parts", sizeof("parts")-1);
        return emit_concat(parts, c);
    }
    if (zend_string_equals_literal(type, "alt")) {
        zval *left = zend_hash_str_find(Z_ARRVAL_P(node), "left", sizeof("left")-1);
        zval *right = zend_hash_str_find(Z_ARRVAL_P(node), "right", sizeof("right")-1);
        if (!left || !right) return -1;
        return emit_alt(left, right, c);
    }
    if (zend_string_equals_literal(type, "span")) {
        zval *set = zend_hash_str_find(Z_ARRVAL_P(node), "set", sizeof("set")-1);
        return emit_span(set, c);
    }
    if (zend_string_equals_literal(type, "break")) {
        zval *set = zend_hash_str_find(Z_ARRVAL_P(node), "set", sizeof("set")-1);
        return emit_break(set, c);
    }
    if (zend_string_equals_literal(type, "any")) {
        zval *set = zend_hash_str_find(Z_ARRVAL_P(node), "set", sizeof("set")-1);
        return emit_any(set, c);
    }
    if (zend_string_equals_literal(type, "notany")) {
        zval *set = zend_hash_str_find(Z_ARRVAL_P(node), "set", sizeof("set")-1);
        return emit_notany(set, c);
    }
    if (zend_string_equals_literal(type, "arbno")) {
        zval *sub = zend_hash_str_find(Z_ARRVAL_P(node), "sub", sizeof("sub")-1);
        return emit_arbno(sub, c);
    }
    if (zend_string_equals_literal(type, "cap")) {
        zval *reg = zend_hash_str_find(Z_ARRVAL_P(node), "reg", sizeof("reg")-1);
        zval *sub = zend_hash_str_find(Z_ARRVAL_P(node), "sub", sizeof("sub")-1);
        return emit_cap(reg, sub, c);
    }
    if (zend_string_equals_literal(type, "assign")) {
        zval *var = zend_hash_str_find(Z_ARRVAL_P(node), "var", sizeof("var")-1);
        zval *reg = zend_hash_str_find(Z_ARRVAL_P(node), "reg", sizeof("reg")-1);
        return emit_assign(var, reg, c);
    }
    if (zend_string_equals_literal(type, "len")) {
        zval *n = zend_hash_str_find(Z_ARRVAL_P(node), "n", sizeof("n")-1);
        return emit_len(n, c);
    }
    if (zend_string_equals_literal(type, "eval")) {
        zval *fn = zend_hash_str_find(Z_ARRVAL_P(node), "fn", sizeof("fn")-1);
        zval *reg = zend_hash_str_find(Z_ARRVAL_P(node), "reg", sizeof("reg")-1);
        return emit_eval(fn, reg, c);
    }
    if (zend_string_equals_literal(type, "anchor")) {
        zval *atype = zend_hash_str_find(Z_ARRVAL_P(node), "atype", sizeof("atype")-1);
        return emit_anchor(atype, c);
    }
    if (zend_string_equals_literal(type, "repeat")) {
        zval *sub = zend_hash_str_find(Z_ARRVAL_P(node), "sub", sizeof("sub")-1);
        zval *min = zend_hash_str_find(Z_ARRVAL_P(node), "min", sizeof("min")-1);
        zval *max = zend_hash_str_find(Z_ARRVAL_P(node), "max", sizeof("max")-1);
        if (!sub || !min || !max) return -1;
        return emit_repeat(sub, min, max, c);
    }
    if (zend_string_equals_literal(type, "emit")) {
        return emit_emit(node, c);
    }
    if (zend_string_equals_literal(type, "dynamic_eval")) {
        /* dynamic_eval: compile pattern for runtime caching and execution
         * 
         * Canonical approach: Store both the compiled bytecode AND the source text.
         * - Source text: Used as cache key for reuse across repeated EVAL(...)
         * - Bytecode: Used for efficient execution in the VM
         * 
         * This enables:
         * - Cache reuse across repeated EVAL(...) calls with the same source
         * - Full pattern semantics including alternation, backtracking, nested EVAL(...)
         * - Proper retain/release ownership through the runtime cache
         */
        zval *expr = zend_hash_str_find(Z_ARRVAL_P(node), "expr", sizeof("expr")-1);
        zval *source_text = zend_hash_str_find(Z_ARRVAL_P(node), "source", sizeof("source")-1);
        
        if (!expr) return -1;

        /* Compile the expression AST to bytecode */
        CodeBuf dynamic_cb;
        cb_init(&dynamic_cb);

        if (emit_node(expr, &dynamic_cb) != 0) {
            cb_free(&dynamic_cb);
            return -1;
        }
        cb_emit_u8(&dynamic_cb, OP_ACCEPT);

        /* Emit the compiled bytecode with source text metadata
         * Format: OP_DYNAMIC_DEF + source_len(u32) + source_text + bc_len(u32) + bytecode
         * The VM will use source for cache keying and bytecode for execution */
        cb_emit_u8(c, OP_DYNAMIC_DEF);
        
        /* Emit source length and source text (for cache keying) */
        if (source_text && Z_TYPE_P(source_text) == IS_STRING) {
            zend_string *source_zs = Z_STR_P(source_text);
            cb_emit_u32(c, (uint32_t)ZSTR_LEN(source_zs));
            cb_emit_bytes(c, (const uint8_t*)ZSTR_VAL(source_zs), ZSTR_LEN(source_zs));
        } else {
            /* Fallback: use bytecode as source (no cache reuse) */
            cb_emit_u32(c, (uint32_t)dynamic_cb.len);
            cb_emit_bytes(c, dynamic_cb.buf, dynamic_cb.len);
        }
        
        /* Emit bytecode length and bytecode (for execution) */
        cb_emit_u32(c, (uint32_t)dynamic_cb.len);
        cb_emit_bytes(c, dynamic_cb.buf, dynamic_cb.len);

        /* Emit OP_DYNAMIC to trigger execution */
        cb_emit_u8(c, OP_DYNAMIC);

        cb_free(&dynamic_cb);
        return 0;
    }
    return -1;
}

int compile_ast_to_bytecode(zval *ast, zval *options, uint8_t **out_bc, size_t *out_len) {
    SNOBOL_LOG("compile_ast_to_bytecode START");
    free_charclass_list();
    next_loop_id = 0;
    compiler_case_insensitive = false;
    
    if (options && Z_TYPE_P(options) == IS_ARRAY) {
        zval *ci = zend_hash_str_find(Z_ARRVAL_P(options), "caseInsensitive", sizeof("caseInsensitive")-1);
        if (ci && (Z_TYPE_P(ci) == IS_TRUE || (Z_TYPE_P(ci) == IS_LONG && Z_LVAL_P(ci)))) {
            compiler_case_insensitive = true;
        }
    }

    CodeBuf cb;
    cb_init(&cb);
    if (emit_node(ast, &cb) != 0) {
        SNOBOL_LOG("compile_ast_to_bytecode FAILED at emit_node");
        cb_free(&cb);
        free_charclass_list();
        return -1;
    }
    cb_emit_u8(&cb, OP_ACCEPT);
    
    CCEntry *rev = NULL;
    for (CCEntry *it = charclass_head; it != NULL; ) {
        CCEntry *next = it->next;
        it->next = rev;
        rev = it;
        it = next;
    }
    charclass_head = rev;

    size_t *offsets = charclass_count > 0 ? snobol_malloc(charclass_count * sizeof(size_t)) : NULL;
    int idx = 0;
    for (CCEntry *it = charclass_head; it != NULL; it = it->next) {
        if (offsets) offsets[idx++] = cb_pos(&cb);
        cb_emit_u16(&cb, it->range_count);
        cb_emit_u16(&cb, it->case_insensitive);
        for (size_t i = 0; i < it->range_count; ++i) {
            cb_emit_u32(&cb, it->ranges[i].start);
            cb_emit_u32(&cb, it->ranges[i].end);
        }
    }
    
    if (offsets) {
        for (uint32_t i = 0; i < charclass_count; ++i) {
            cb_emit_u32(&cb, (uint32_t)offsets[i]);
        }
        snobol_free(offsets);
    }

    cb_emit_u32(&cb, charclass_count);

    uint8_t *out = snobol_malloc(cb.len);
    if (!out) {
        SNOBOL_LOG("compile_ast_to_bytecode FAILED to allocate final bc");
        cb_free(&cb); 
        return -1; 
    }
    memcpy(out, cb.buf, cb.len);
    *out_bc = out;
    *out_len = cb.len;

    SNOBOL_LOG("compile_ast_to_bytecode SUCCESS, bc=%p, len=%zu", (void*)out, cb.len);

    cb_free(&cb);
    free_charclass_list();
    return 0;
}

int compile_template_to_bytecode(const char *tpl, size_t len, uint8_t **out_bc, size_t *out_len) {
    SNOBOL_LOG("compile_template_to_bytecode START: tpl='%.*s'", (int)len, tpl);
    CodeBuf cb;
    cb_init(&cb);

    size_t i = 0;
    while (i < len) {
        if (tpl[i] == '$') {
            size_t start_of_dollar = i;
            i++;
            if (i >= len) {
                cb_emit_u8(&cb, OP_EMIT_LITERAL);
                size_t off = cb_pos(&cb) + 4 + 4;
                cb_emit_u32(&cb, (uint32_t)off); cb_emit_u32(&cb, 1); cb_emit_u8(&cb, '$');
                break;
            }
            
            bool braced = (tpl[i] == '{');
            if (braced) i++;

            if (i < len && tpl[i] == 'v') {
                i++;
                uint8_t reg = 0;
                bool has_digits = false;
                while (i < len && tpl[i] >= '0' && tpl[i] <= '9') {
                    reg = reg * 10 + (tpl[i] - '0');
                    i++;
                    has_digits = true;
                }
                
                if (!has_digits) {
                    i = start_of_dollar + 1;
                    cb_emit_u8(&cb, OP_EMIT_LITERAL);
                    size_t off = cb_pos(&cb) + 4 + 4;
                    cb_emit_u32(&cb, (uint32_t)off); cb_emit_u32(&cb, 1); cb_emit_u8(&cb, '$');
                    continue;
                }

                uint8_t expr_type = 0;
                if (braced) {
                    if (i < len && tpl[i] == '.') {
                        i++;
                        if (len - i >= 7 && memcmp(tpl + i, "upper()", 7) == 0) {
                            expr_type = 1; i += 7;
                        } else if (len - i >= 8 && memcmp(tpl + i, "length()", 8) == 0) {
                            expr_type = 2; i += 8;
                        }
                    }
                    if (i < len && tpl[i] == '}') {
                        i++;
                        if (expr_type == 0) {
                            cb_emit_u8(&cb, OP_EMIT_CAPTURE); cb_emit_u8(&cb, reg);
                        } else {
                            cb_emit_u8(&cb, OP_EMIT_EXPR); cb_emit_u8(&cb, reg); cb_emit_u8(&cb, expr_type);
                        }
                    } else {
                        i = start_of_dollar + 1;
                        cb_emit_u8(&cb, OP_EMIT_LITERAL);
                        size_t off = cb_pos(&cb) + 4 + 4;
                        cb_emit_u32(&cb, (uint32_t)off); cb_emit_u32(&cb, 1); cb_emit_u8(&cb, '$');
                    }
                } else if (i < len && tpl[i] == '[') {
                    /* Table-backed replacement: $TABLE[key]
                     * Parse TABLE name and key, emit OP_EMIT_TABLE */
                    i++; /* skip '[' */
                    
                    /* Parse table name (identifier until '.' or '[') */
                    size_t table_name_start = i;
                    while (i < len && tpl[i] != '.' && tpl[i] != '[' && tpl[i] != ']') {
                        i++;
                    }
                    size_t table_name_len = i - table_name_start;
                    
                    if (table_name_len == 0 || i >= len || tpl[i] != '[') {
                        /* Invalid syntax, emit as literal '$' */
                        i = start_of_dollar + 1;
                        cb_emit_u8(&cb, OP_EMIT_LITERAL);
                        size_t off = cb_pos(&cb) + 4 + 4;
                        cb_emit_u32(&cb, (uint32_t)off); cb_emit_u32(&cb, 1); cb_emit_u8(&cb, '$');
                        continue;
                    }
                    
                    /* For now, table name must be a literal identifier */
                    /* Extract table name */
                    const char *table_name = tpl + table_name_start;
                    
                    /* Skip '[' and parse key */
                    i++; /* skip '[' */
                    size_t key_start = i;
                    
                    /* Key can be: quoted literal or identifier */
                    bool quoted = (i < len && tpl[i] == '\'');
                    if (quoted) {
                        i++; /* skip opening quote */
                        key_start = i;
                        while (i < len && tpl[i] != '\'') {
                            i++;
                        }
                        if (i >= len) {
                            /* Unclosed quote, emit as literal '$' */
                            i = start_of_dollar + 1;
                            cb_emit_u8(&cb, OP_EMIT_LITERAL);
                            size_t off = cb_pos(&cb) + 4 + 4;
                            cb_emit_u32(&cb, (uint32_t)off); cb_emit_u32(&cb, 1); cb_emit_u8(&cb, '$');
                            continue;
                        }
                        /* Key is from key_start to i (exclusive of closing quote) */
                        size_t key_len = i - key_start;
                        i++; /* skip closing quote */

                        /* Check for closing ']' */
                        if (i >= len || tpl[i] != ']') {
                            i = start_of_dollar + 1;
                            cb_emit_u8(&cb, OP_EMIT_LITERAL);
                            size_t off = cb_pos(&cb) + 4 + 4;
                            cb_emit_u32(&cb, (uint32_t)off); cb_emit_u32(&cb, 1); cb_emit_u8(&cb, '$');
                            continue;
                        }
                        i++; /* skip ']' */

                        /* Emit OP_EMIT_TABLE with literal key stored in bytecode
                         * Format: opcode u8, table_id u16, key_type u8 (0=literal), key_len u16, key_bytes...
                         * Table ID 0 means "resolve by name at runtime" - for now use placeholder */
                        cb_emit_u8(&cb, OP_EMIT_TABLE);
                        cb_emit_u16(&cb, 0); /* table_id placeholder - resolved at runtime */
                        cb_emit_u8(&cb, 0);  /* key_type: 0 = literal key */
                        cb_emit_u16(&cb, (uint16_t)key_len); /* literal key length */
                        cb_emit_bytes(&cb, (const uint8_t*)(tpl + key_start), key_len);
                    } else {
                        /* Identifier key (capture-derived) */
                        while (i < len && tpl[i] >= '0' && tpl[i] <= '9') {
                            i++;
                        }
                        if (i >= len || tpl[i] != ']') {
                            i = start_of_dollar + 1;
                            cb_emit_u8(&cb, OP_EMIT_LITERAL);
                            size_t off = cb_pos(&cb) + 4 + 4;
                            cb_emit_u32(&cb, (uint32_t)off); cb_emit_u32(&cb, 1); cb_emit_u8(&cb, '$');
                            continue;
                        }
                        size_t key_reg = 0;
                        /* Parse register number from identifier like v0, v1, etc. */
                        if (key_start > 0 && tpl[key_start - 1] == 'v') {
                            const char *reg_start = tpl + key_start;
                            key_reg = (uint8_t)(reg_start[0] - '0');
                        }
                        i++; /* skip ']' */

                        /* Emit OP_EMIT_TABLE with capture-derived key
                         * Format: opcode u8, table_id u16, key_type u8 (1=capture), key_reg u8 */
                        cb_emit_u8(&cb, OP_EMIT_TABLE);
                        cb_emit_u16(&cb, 0); /* table_id placeholder - needs resolution */
                        cb_emit_u8(&cb, 1);  /* key_type: 1 = capture-derived key */
                        cb_emit_u8(&cb, (uint8_t)key_reg);
                    }
                } else {
                    cb_emit_u8(&cb, OP_EMIT_CAPTURE); cb_emit_u8(&cb, reg);
                }
            } else {
                i = start_of_dollar + 1;
                cb_emit_u8(&cb, OP_EMIT_LITERAL);
                size_t off = cb_pos(&cb) + 4 + 4;
                cb_emit_u32(&cb, (uint32_t)off); cb_emit_u32(&cb, 1); cb_emit_u8(&cb, '$');
            }
        } else {
            // scan literal segment
            size_t start = i;
            while (i < len && tpl[i] != '$') i++;
            size_t seglen = i - start;
            cb_emit_u8(&cb, OP_EMIT_LITERAL);
            size_t off = cb_pos(&cb) + 4 + 4;
            cb_emit_u32(&cb, (uint32_t)off);
            cb_emit_u32(&cb, (uint32_t)seglen);
            cb_emit_bytes(&cb, (const uint8_t*)tpl + start, seglen);
        }
    }

    cb_emit_u8(&cb, OP_ACCEPT);

    uint8_t *out = snobol_malloc(cb.len);
    if (!out) { cb_free(&cb); return -1; }
    memcpy(out, cb.buf, cb.len);
    *out_bc = out;
    *out_len = cb.len;

    cb_free(&cb);
    SNOBOL_LOG("compile_template_to_bytecode SUCCESS, len=%zu", *out_len);
    return 0;
}
#endif

void compiler_free(uint8_t *bc) {
    if (bc) {
        SNOBOL_LOG("compiler_free bc=%p", (void*)bc);
        snobol_free(bc);
    }
}

/* ============================================================================
 * C AST-based compilation
 * ============================================================================
 * Compiles C AST (ast_node_t*) to bytecode.
 * This is the new compilation path for the language-agnostic core.
 */

/* Forward declaration */
static int emit_node_c(ast_node_t* node, CodeBuf *c);
static int emit_alt_c(ast_node_t* left, ast_node_t* right, CodeBuf *c);
static int emit_arbno_c(ast_node_t* sub, CodeBuf *c);
static int emit_cap_c(ast_node_t* reg_node, ast_node_t* sub, CodeBuf *c);
static int emit_repeat_c(ast_node_t* sub, ast_node_t* min_node, ast_node_t* max_node, CodeBuf *c);

/* C-only helper implementations */
static int emit_span_c(const char *set, size_t len, CodeBuf *c) {
    int setid = add_or_get_charclass(set, len);
    cb_emit_u8(c, OP_SPAN);
    cb_emit_u16(c, (uint16_t)setid);
    return 0;
}

static int emit_break_c(const char *set, size_t len, CodeBuf *c) {
    int setid = add_or_get_charclass(set, len);
    cb_emit_u8(c, OP_BREAK);
    cb_emit_u16(c, (uint16_t)setid);
    return 0;
}

static int emit_any_c(const char *set, size_t len, CodeBuf *c) {
    uint16_t setid = 0;
    if (set && len > 0) {
        setid = (uint16_t)add_or_get_charclass(set, len);
    }
    cb_emit_u8(c, OP_ANY);
    cb_emit_u16(c, setid);
    return 0;
}

static int emit_notany_c(const char *set, size_t len, CodeBuf *c) {
    int setid = add_or_get_charclass(set, len);
    cb_emit_u8(c, OP_NOTANY);
    cb_emit_u16(c, (uint16_t)setid);
    return 0;
}

static int emit_assign_c(int var, int reg, CodeBuf *c) {
    cb_emit_u8(c, OP_ASSIGN);
    cb_emit_u16(c, (uint16_t)var);
    cb_emit_u8(c, (uint8_t)reg);
    return 0;
}

static int emit_len_c(int n, CodeBuf *c) {
    cb_emit_u8(c, OP_LEN);
    cb_emit_u32(c, (uint32_t)n);
    return 0;
}

static int emit_eval_c(int fn, int reg, CodeBuf *c) {
    cb_emit_u8(c, OP_EVAL);
    cb_emit_u8(c, (uint8_t)fn);
    cb_emit_u8(c, (uint8_t)reg);
    return 0;
}

static int emit_anchor_c(const char *type, CodeBuf *c) {
    if (strcmp(type, "start") == 0) {
        cb_emit_u8(c, OP_ANCHOR);
        cb_emit_u8(c, 0); /* ANCHOR_START */
    } else if (strcmp(type, "end") == 0) {
        cb_emit_u8(c, OP_ANCHOR);
        cb_emit_u8(c, 1); /* ANCHOR_END */
    }
    return 0;
}
static int emit_emit_c(const char *text, int reg, CodeBuf *c) {
    if (text != NULL) {
        /* Emit literal text */
        size_t len = strlen(text);
        size_t off_of_payload = cb_pos(c) + 1 + 4 + 4;
        cb_emit_u8(c, OP_EMIT_LITERAL);
        cb_emit_u32(c, (uint32_t)off_of_payload);
        cb_emit_u32(c, (uint32_t)len);
        cb_emit_bytes(c, (const uint8_t*)text, len);
    } else if (reg >= 0) {
        /* Emit capture reference */
        cb_emit_u8(c, OP_EMIT_CAPTURE);
        cb_emit_u8(c, (uint8_t)reg);
    }
    return 0;
}

/**
 * Compile C AST to bytecode
 * @param ast Root AST node
 * @param case_insensitive Enable case-insensitive matching
 * @param out_bc Output: bytecode buffer
 * @param out_len Output: bytecode length
 * @return 0 on success, -1 on failure
 */
int compile_ast_to_bytecode_c(ast_node_t* ast, bool case_insensitive, uint8_t **out_bc, size_t *out_len) {
    SNOBOL_LOG("compile_ast_to_bytecode_c START");
    free_charclass_list();
    next_loop_id = 0;
    compiler_case_insensitive = case_insensitive;

    CodeBuf cb;
    cb_init(&cb);

    if (emit_node_c(ast, &cb) != 0) {
        SNOBOL_LOG("compile_ast_to_bytecode_c FAILED at emit_node_c");
        cb_free(&cb);
        free_charclass_list();
        return -1;
    }
    cb_emit_u8(&cb, OP_ACCEPT);

    CCEntry *rev = NULL;
    for (CCEntry *it = charclass_head; it != NULL; ) {
        CCEntry *next = it->next;
        it->next = rev;
        rev = it;
        it = next;
    }
    charclass_head = rev;

    size_t *offsets = charclass_count > 0 ? snobol_malloc(charclass_count * sizeof(size_t)) : NULL;
    int idx = 0;
    for (CCEntry *it = charclass_head; it != NULL; it = it->next) {
        if (offsets) offsets[idx++] = cb_pos(&cb);
        cb_emit_u16(&cb, it->range_count);
        cb_emit_u16(&cb, it->case_insensitive);
        for (size_t i = 0; i < it->range_count; ++i) {
            cb_emit_u32(&cb, it->ranges[i].start);
            cb_emit_u32(&cb, it->ranges[i].end);
        }
    }

    if (offsets) {
        for (uint32_t i = 0; i < charclass_count; ++i) {
            cb_emit_u32(&cb, (uint32_t)offsets[i]);
        }
        snobol_free(offsets);
    }

    cb_emit_u32(&cb, charclass_count);

    uint8_t *out = snobol_malloc(cb.len);
    if (!out) {
        SNOBOL_LOG("compile_ast_to_bytecode_c FAILED to allocate final bc");
        cb_free(&cb);
        return -1;
    }
    memcpy(out, cb.buf, cb.len);
    *out_bc = out;
    *out_len = cb.len;

    cb_free(&cb);
    free_charclass_list();
    SNOBOL_LOG("compile_ast_to_bytecode_c SUCCESS, len=%zu", *out_len);
    return 0;
}

/* C AST emit helpers */
static int emit_alt_c(ast_node_t* left, ast_node_t* right, CodeBuf *c) {
    /* Emit SPLIT opcode to try both alternatives */
    size_t where_split = cb_pos(c);
    cb_emit_u8(c, OP_SPLIT);
    size_t where_a = cb_pos(c); cb_emit_u32(c, 0);  /* placeholder */
    size_t where_b = cb_pos(c); cb_emit_u32(c, 0);  /* placeholder */

    /* Left alternative */
    size_t left_start = cb_pos(c);
    if (emit_node_c(left, c) != 0) return -1;

    /* Jump past right alternative */
    size_t jmp_where = cb_pos(c);
    cb_emit_u8(c, OP_JMP);
    size_t jmp_target_pos = cb_pos(c); cb_emit_u32(c, 0);

    /* Right alternative */
    size_t right_start = cb_pos(c);
    if (emit_node_c(right, c) != 0) return -1;

    /* Fill in placeholders */
    size_t end_pos = cb_pos(c);
    uint32_t v1 = (uint32_t)left_start;
    c->buf[where_a+0] = (v1 >> 24) & 0xff; c->buf[where_a+1] = (v1 >> 16) & 0xff;
    c->buf[where_a+2] = (v1 >> 8) & 0xff; c->buf[where_a+3] = v1 & 0xff;
    
    uint32_t v2 = (uint32_t)right_start;
    c->buf[where_b+0] = (v2 >> 24) & 0xff; c->buf[where_b+1] = (v2 >> 16) & 0xff;
    c->buf[where_b+2] = (v2 >> 8) & 0xff; c->buf[where_b+3] = v2 & 0xff;
    
    uint32_t vj = (uint32_t)end_pos;
    c->buf[jmp_target_pos+0] = (vj >> 24) & 0xff; c->buf[jmp_target_pos+1] = (vj >> 16) & 0xff;
    c->buf[jmp_target_pos+2] = (vj >> 8) & 0xff; c->buf[jmp_target_pos+3] = vj & 0xff;

    return 0;
}

static int emit_arbno_c(ast_node_t* sub, CodeBuf *c) {
    /* Unwrap nested ARBNOs */
    while (sub && sub->type == AST_ARBNO) {
        sub = sub->data.arbno.sub;
    }
    
    /* Unwrap REPEAT(0, -1) which is equivalent to ARBNO */
    while (sub && sub->type == AST_REPETITION) {
        if (sub->data.repetition.min == 0 && sub->data.repetition.max == -1) {
            sub = sub->data.repetition.sub;
            continue;
        }
        break;
    }
    
    if (!sub) return -1;
    if (next_loop_id >= MAX_LOOPS) return -1;
    
    uint8_t loop_id = next_loop_id++;
    uint32_t min = 0;
    uint32_t max = (uint32_t)-1;

    size_t init_pos = cb_pos(c);
    cb_emit_u8(c, OP_REPEAT_INIT);
    cb_emit_u8(c, loop_id);
    cb_emit_u32(c, min);
    cb_emit_u32(c, max);
    size_t skip_target_off = cb_pos(c);
    cb_emit_u32(c, 0); /* placeholder for skip_target */

    size_t body_start = cb_pos(c);
    if (emit_node_c(sub, c) != 0) return -1;

    cb_emit_u8(c, OP_REPEAT_STEP);
    cb_emit_u8(c, loop_id);
    cb_emit_u32(c, (uint32_t)body_start);

    size_t done_pos = cb_pos(c);
    
    /* Fill skip_target placeholder */
    uint32_t v_done = (uint32_t)done_pos;
    c->buf[skip_target_off+0] = (v_done >> 24) & 0xff;
    c->buf[skip_target_off+1] = (v_done >> 16) & 0xff;
    c->buf[skip_target_off+2] = (v_done >> 8) & 0xff;
    c->buf[skip_target_off+3] = v_done & 0xff;

    return 0;
}

static int emit_cap_c(ast_node_t* reg_node, ast_node_t* sub, CodeBuf *c) {
    /* Emit capture start, sub-pattern, capture end */
    cb_emit_u8(c, OP_CAP_START);
    cb_emit_u8(c, (uint8_t)reg_node->data.len.n);

    if (emit_node_c(sub, c) != 0) return -1;

    cb_emit_u8(c, OP_CAP_END);
    cb_emit_u8(c, (uint8_t)reg_node->data.len.n);
    return 0;
}

static int emit_repeat_c(ast_node_t* sub, ast_node_t* min_node, ast_node_t* max_node, CodeBuf *c) {
    if (!sub) return -1;
    if (next_loop_id >= MAX_LOOPS) return -1;

    uint8_t loop_id = next_loop_id++;
    uint32_t min = (uint32_t)min_node->data.len.n;
    uint32_t max = (uint32_t)max_node->data.len.n;

    /* Flatten repeat(0, -1) as ARBNO */
    if (min == 0 && max == (uint32_t)-1) {
        /* Unwrap nested */
        while (sub && sub->type == AST_REPETITION) {
            if (sub->data.repetition.min == 0 && sub->data.repetition.max == -1) {
                sub = sub->data.repetition.sub;
                continue;
            }
            break;
        }
        /* Use arbno logic */
        return emit_arbno_c(sub, c);
    }

    size_t init_pos = cb_pos(c);
    cb_emit_u8(c, OP_REPEAT_INIT);
    cb_emit_u8(c, loop_id);
    cb_emit_u32(c, min);
    cb_emit_u32(c, max);
    size_t skip_target_off = cb_pos(c);
    cb_emit_u32(c, 0); /* placeholder */

    size_t body_start = cb_pos(c);
    if (emit_node_c(sub, c) != 0) return -1;

    cb_emit_u8(c, OP_REPEAT_STEP);
    cb_emit_u8(c, loop_id);
    cb_emit_u32(c, (uint32_t)body_start);

    size_t done_pos = cb_pos(c);
    
    /* Fill skip_target */
    uint32_t v_done = (uint32_t)done_pos;
    c->buf[skip_target_off+0] = (v_done >> 24) & 0xff;
    c->buf[skip_target_off+1] = (v_done >> 16) & 0xff;
    c->buf[skip_target_off+2] = (v_done >> 8) & 0xff;
    c->buf[skip_target_off+3] = v_done & 0xff;

    return 0;
}

/**
 * Emit bytecode for a C AST node
 */
static int emit_node_c(ast_node_t* node, CodeBuf *c) {
    if (!node) return -1;

    switch (node->type) {
        case AST_LITERAL:
            return emit_lit_bytes(c, node->data.literal.text, node->data.literal.len);

        case AST_CONCAT:
            for (size_t i = 0; i < node->data.concat.count; i++) {
                if (emit_node_c(node->data.concat.parts[i], c) != 0) {
                    return -1;
                }
            }
            return 0;

        case AST_ALT:
            return emit_alt_c(node->data.alt.left, node->data.alt.right, c);

        case AST_REPETITION: {
            /* Create stub AST nodes for min/max values */
            ast_node_t min_node = {.type = AST_LEN, .data.len.n = node->data.repetition.min};
            ast_node_t max_node = {.type = AST_LEN, .data.len.n = node->data.repetition.max};
            return emit_repeat_c(node->data.repetition.sub, &min_node, &max_node, c);
        }

        case AST_SPAN:
            return emit_span_c(node->data.charclass.set, node->data.charclass.len, c);

        case AST_BREAK:
            return emit_break_c(node->data.charclass.set, node->data.charclass.len, c);

        case AST_ANY:
            return emit_any_c(node->data.charclass.set, node->data.charclass.len, c);

        case AST_NOTANY:
            return emit_notany_c(node->data.charclass.set, node->data.charclass.len, c);

        case AST_ARBNO:
            return emit_arbno_c(node->data.arbno.sub, c);

        case AST_CAP: {
            /* Emit capture start, sub-pattern, capture end */
            cb_emit_u8(c, OP_CAP_START);
            cb_emit_u8(c, (uint8_t)node->data.cap.reg);

            if (emit_node_c(node->data.cap.sub, c) != 0) return -1;

            cb_emit_u8(c, OP_CAP_END);
            cb_emit_u8(c, (uint8_t)node->data.cap.reg);
            return 0;
        }

        case AST_ASSIGN:
            return emit_assign_c(node->data.assign.var, node->data.assign.reg, c);

        case AST_LEN:
            return emit_len_c(node->data.len.n, c);

        case AST_EVAL:
            return emit_eval_c(node->data.eval.fn, node->data.eval.reg, c);

        case AST_DYNAMIC_EVAL:
            /* Dynamic eval: compile inner expression and emit as dynamic pattern */
            return emit_node_c(node->data.dynamic_eval.expr, c);

        case AST_ANCHOR:
            return emit_anchor_c(node->data.anchor.atype == ANCHOR_START ? "start" : "end", c);

        case AST_EMIT:
            return emit_emit_c(node->data.emit.text, node->data.emit.reg, c);

        case AST_LABEL:
        case AST_GOTO:
        case AST_TABLE_ACCESS:
        case AST_TABLE_UPDATE:
            /* These are handled at statement level, not pattern compilation */
            SNOBOL_LOG("emit_node_c: unhandled node type %d", node->type);
            return -1;

        default:
            SNOBOL_LOG("emit_node_c: unknown node type %d", node->type);
            return -1;
    }
}
