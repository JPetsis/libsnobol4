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
        c->buf = nullptr;
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
static CCEntry *charclass_head = nullptr;
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
    charclass_head = nullptr;
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
        uint32_t cp; int cp_bytes;
        if (!utf8_peek_next(s, len, pos, &cp, &cp_bytes)) break;

        /* Check for range notation: X-Y where X < Y */
        uint32_t dash_cp; int dash_bytes;
        uint32_t end_cp; int end_bytes;
        if (utf8_peek_next(s, len, pos + cp_bytes, &dash_cp, &dash_bytes) && dash_cp == '-' &&
            utf8_peek_next(s, len, pos + cp_bytes + dash_bytes, &end_cp, &end_bytes) && end_cp > cp) {
            /* Expand X-Y range */
            add_range(ne, cp, end_cp);
            if (compiler_case_insensitive) {
                /* Add case-folded partner range for ASCII alpha ranges */
                if (cp >= 'A' && end_cp <= 'Z') {
                    add_range(ne, cp + 32, end_cp + 32);     /* A-Z -> a-z */
                } else if (cp >= 'a' && end_cp <= 'z') {
                    add_range(ne, cp - 32, end_cp - 32);     /* a-z -> A-Z */
                }
            }
            pos += cp_bytes + dash_bytes + end_bytes;
            continue;
        }

        /* Single codepoint */
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

        pos += cp_bytes;
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
    
    ne->next = nullptr;
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

/* ---------------------------------------------------------------------------
 * SPLIT/ANY Fusion Pass
 *
 * After all bytecode ops are emitted (but before charclass data is appended),
 * scan for eligible OP_SPLIT patterns and fuse them into a single OP_ANY that
 * matches the union of both arm character sets.
 *
 * Eligible pattern:
 *   OP_SPLIT(a, b)
 *   @ a:  <single-char-op>  OP_JMP(merge)     [arm-a, followed by JMP]
 *   @ b:  <single-char-op>  [arm-b, falls through to merge]
 *   Both arms must be OP_LIT(len=1) or OP_ANY; not mixed with OP_NOTANY.
 *   NOPs (from a previous fusion) are skipped when searching for the JMP.
 *
 * Rewrite:
 *   bc[split_pos]   = OP_ANY
 *   bc[split_pos+1] = merged_cc >> 8
 *   bc[split_pos+2] = merged_cc & 0xff
 *   bc[split_pos+3 .. merge-1] = OP_NOP
 *
 * The pass runs right-to-left so nested SPLITs (N-arm chains) are fused
 * innermost-first, enabling each outer SPLIT to see an already-fused OP_ANY.
 * ---------------------------------------------------------------------------*/

static uint32_t fuse_read_u32(const uint8_t *bc, size_t off) {
    return ((uint32_t)bc[off] << 24) | ((uint32_t)bc[off+1] << 16) |
           ((uint32_t)bc[off+2] << 8)  | (uint32_t)bc[off+3];
}
static uint16_t fuse_read_u16(const uint8_t *bc, size_t off) {
    return (uint16_t)(((uint16_t)bc[off] << 8) | bc[off+1]);
}

/* Get CCEntry for 1-based id (order of add_or_get_charclass calls). */
static CCEntry *get_cc_entry(uint32_t id) {
    CCEntry *e = charclass_head;
    uint32_t i = 1;
    while (e && i < id) { e = e->next; i++; }
    return (e && i == id) ? e : nullptr;
}

/*
 * Register a new charclass that is the union of two CCEntry ranges.
 * ea / eb may be nullptr; in that case cp_a / cp_b is a single codepoint.
 */
static int fuse_add_union_cc(CCEntry *ea, uint32_t cp_a,
                              CCEntry *eb, uint32_t cp_b,
                              uint8_t ci) {
    uint16_t na = ea ? ea->range_count : 1;
    uint16_t nb = eb ? eb->range_count : 1;
    CCEntry *ne = snobol_malloc(sizeof(*ne));
    memset(ne, 0, sizeof(*ne));
    ne->case_insensitive = ci;
    ne->range_cap = (uint16_t)(na + nb);
    ne->ranges = snobol_malloc(ne->range_cap * sizeof(CpRange));
    if (ea) {
        memcpy(ne->ranges, ea->ranges, na * sizeof(CpRange));
        ne->range_count = na;
    } else {
        ne->ranges[0].start = cp_a; ne->ranges[0].end = cp_a;
        ne->range_count = 1;
    }
    if (eb) {
        memcpy(ne->ranges + ne->range_count, eb->ranges, nb * sizeof(CpRange));
        ne->range_count = (uint16_t)(ne->range_count + nb);
    } else {
        ne->ranges[ne->range_count].start = cp_b;
        ne->ranges[ne->range_count].end   = cp_b;
        ne->range_count++;
    }
    normalize_ranges(ne);

    /* Dedup: check for existing identical entry */
    CCEntry *e = charclass_head;
    int id = 1;
    while (e) {
        if (e->range_count == ne->range_count &&
            e->case_insensitive == ne->case_insensitive &&
            memcmp(e->ranges, ne->ranges, ne->range_count * sizeof(CpRange)) == 0) {
            if (ne->ranges) snobol_free(ne->ranges);
            snobol_free(ne);
            return id;
        }
        id++; e = e->next;
    }
    ne->next = nullptr;
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

/*
 * Describe a single-char arm.
 * type: 1=LIT, 2=ANY; 0=ineligible
 */
typedef struct {
    int      type;       /* 1=LIT, 2=ANY */
    uint32_t cp;         /* codepoint (for LIT) */
    uint16_t cc_id;      /* charclass id (for ANY) */
    size_t   merge;      /* merge point (first byte after this arm's contribution) */
} ArmInfo;

/* Parse arm-a: single-char-op followed (after optional NOPs) by OP_JMP */
static ArmInfo parse_arm_a(const uint8_t *bc, size_t bc_len, size_t a) {
    ArmInfo r = {0};
    if (a >= bc_len) return r;
    uint8_t op = bc[a];
    size_t after_op;
    if (op == OP_LIT) {
        if (a + 9 > bc_len) return r;
        uint32_t len = fuse_read_u32(bc, a + 5);
        if (len != 1) return r;           /* multi-char LIT: not eligible */
        if (a + 9 >= bc_len) return r;
        r.cp = (uint32_t)bc[a + 9];      /* single ASCII byte */
        after_op = a + 10;
        r.type = 1;
    } else if (op == OP_ANY) {
        if (a + 3 > bc_len) return r;
        r.cc_id = fuse_read_u16(bc, a + 1);
        after_op = a + 3;
        r.type = 2;
    } else {
        return r;  /* ineligible opcode */
    }
    /* Skip NOPs between op and the required JMP */
    while (after_op < bc_len && bc[after_op] == OP_NOP) after_op++;
    if (after_op + 5 > bc_len) { r.type = 0; return r; }
    if (bc[after_op] != OP_JMP) { r.type = 0; return r; }
    r.merge = (size_t)fuse_read_u32(bc, after_op + 1);
    return r;
}

/* Parse arm-b: single-char-op that falls through to merge (no trailing JMP) */
static ArmInfo parse_arm_b(const uint8_t *bc, size_t bc_len, size_t b) {
    ArmInfo r = {0};
    if (b >= bc_len) return r;
    uint8_t op = bc[b];
    size_t after_op;
    if (op == OP_LIT) {
        if (b + 9 > bc_len) return r;
        uint32_t len = fuse_read_u32(bc, b + 5);
        if (len != 1) return r;
        if (b + 9 >= bc_len) return r;
        r.cp = (uint32_t)bc[b + 9];
        after_op = b + 10;
        r.type = 1;
    } else if (op == OP_ANY) {
        if (b + 3 > bc_len) return r;
        r.cc_id = fuse_read_u16(bc, b + 1);
        after_op = b + 3;
        r.type = 2;
    } else {
        return r;
    }
    /* Skip NOPs after the arm-b op to find the merge point */
    while (after_op < bc_len && bc[after_op] == OP_NOP) after_op++;
    r.merge = after_op;
    return r;
}

static void snobol_bc_fuse_split_any(CodeBuf *cb) {
    uint8_t *bc = cb->buf;
    size_t bc_len = cb->len;

    /* Right-to-left scan so inner SPLITs are fused before outer ones */
    size_t pos = bc_len;
    while (pos > 0) {
        pos--;
        if (bc[pos] != OP_SPLIT) continue;
        if (pos + 9 > bc_len) continue;

        size_t a = (size_t)fuse_read_u32(bc, pos + 1);
        size_t b = (size_t)fuse_read_u32(bc, pos + 5);

        /* Both arms must be strictly forward */
        if (a <= pos || b <= pos || a >= bc_len || b >= bc_len) continue;

        ArmInfo arm_a = parse_arm_a(bc, bc_len, a);
        ArmInfo arm_b = parse_arm_b(bc, bc_len, b);

        if (arm_a.type == 0 || arm_b.type == 0) continue;
        /* Don't mix NOTANY — only LIT and ANY are fused */
        /* arm_b and arm_a are type 1 or 2: compatible */
        if (arm_a.merge != arm_b.merge) continue;

        size_t merge = arm_a.merge;

        /* Determine union charclass */
        CCEntry *ea = (arm_a.type == 2) ? get_cc_entry(arm_a.cc_id) : nullptr;
        CCEntry *eb = (arm_b.type == 2) ? get_cc_entry(arm_b.cc_id) : nullptr;
        uint32_t cp_a = (arm_a.type == 1) ? arm_a.cp : 0;
        uint32_t cp_b = (arm_b.type == 1) ? arm_b.cp : 0;
        uint8_t ci = (ea && ea->case_insensitive) || (eb && eb->case_insensitive) ? 1 : 0;

        int merged_id = fuse_add_union_cc(ea, cp_a, eb, cp_b, ci);
        if (merged_id <= 0 || merged_id > 0xFFFF) continue;

        /* Rewrite: OP_ANY merged_id at pos, NOPs for pos+3 .. merge-1 */
        bc[pos]     = (uint8_t)OP_ANY;
        bc[pos + 1] = (uint8_t)(((uint16_t)merged_id >> 8) & 0xFF);
        bc[pos + 2] = (uint8_t)((uint16_t)merged_id & 0xFF);
        for (size_t i = pos + 3; i < merge; i++) bc[i] = (uint8_t)OP_NOP;
    }
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

/* New primitive emit functions */
static int emit_breakx(zval *set_str, CodeBuf *c) {
    if (!set_str || Z_TYPE_P(set_str) != IS_STRING) return -1;
    zend_string *zs = Z_STR_P(set_str);
    int setid = add_or_get_charclass(ZSTR_VAL(zs), ZSTR_LEN(zs));
    cb_emit_u8(c, OP_BREAKX); cb_emit_u16(c, (uint16_t)setid);
    return 0;
}

/* Decode first UTF-8 codepoint from a PHP string zval; return -1 on failure */
static int32_t first_codepoint(zval *str_zv) {
    if (!str_zv || Z_TYPE_P(str_zv) != IS_STRING) return -1;
    zend_string *zs = Z_STR_P(str_zv);
    if (ZSTR_LEN(zs) == 0) return -1;
    uint32_t cp = 0; int bytes = 0;
    if (!utf8_peek_next(ZSTR_VAL(zs), ZSTR_LEN(zs), 0, &cp, &bytes)) return -1;
    return (int32_t)cp;
}

static int emit_bal(zval *open_zv, zval *close_zv, CodeBuf *c) {
    int32_t open_cp  = first_codepoint(open_zv);
    int32_t close_cp = first_codepoint(close_zv);
    if (open_cp < 0 || close_cp < 0) return -1;
    cb_emit_u8(c, OP_BAL);
    cb_emit_u32(c, (uint32_t)open_cp);
    cb_emit_u32(c, (uint32_t)close_cp);
    return 0;
}

static int emit_fence(CodeBuf *c) {
    cb_emit_u8(c, OP_FENCE);
    return 0;
}

static int emit_rem(CodeBuf *c) {
    cb_emit_u8(c, OP_REM);
    return 0;
}

static int emit_rpos(zval *n_zv, CodeBuf *c) {
    if (!n_zv || Z_TYPE_P(n_zv) != IS_LONG) return -1;
    cb_emit_u8(c, OP_RPOS);
    cb_emit_u32(c, (uint32_t)(long)Z_LVAL_P(n_zv));
    return 0;
}

static int emit_rtab(zval *n_zv, CodeBuf *c) {
    if (!n_zv || Z_TYPE_P(n_zv) != IS_LONG) return -1;
    cb_emit_u8(c, OP_RTAB);
    cb_emit_u32(c, (uint32_t)(long)Z_LVAL_P(n_zv));
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
    /* Pattern primitives: breakx, bal, fence, rem, rpos, rtab */
    if (zend_string_equals_literal(type, "breakx")) {
        zval *set = zend_hash_str_find(Z_ARRVAL_P(node), "set", sizeof("set")-1);
        return emit_breakx(set, c);
    }
    if (zend_string_equals_literal(type, "bal")) {
        zval *open  = zend_hash_str_find(Z_ARRVAL_P(node), "open",  sizeof("open")-1);
        zval *close = zend_hash_str_find(Z_ARRVAL_P(node), "close", sizeof("close")-1);
        return emit_bal(open, close, c);
    }
    if (zend_string_equals_literal(type, "fence")) {
        return emit_fence(c);
    }
    if (zend_string_equals_literal(type, "rem")) {
        return emit_rem(c);
    }
    if (zend_string_equals_literal(type, "rpos")) {
        zval *n = zend_hash_str_find(Z_ARRVAL_P(node), "n", sizeof("n")-1);
        return emit_rpos(n, c);
    }
    if (zend_string_equals_literal(type, "rtab")) {
        zval *n = zend_hash_str_find(Z_ARRVAL_P(node), "n", sizeof("n")-1);
        return emit_rtab(n, c);
    }
    if (zend_string_equals_literal(type, "dynamic_eval")) {
        /* dynamic_eval: compile pattern for runtime caching and execution.
         * Canonical approach: Store both the compiled bytecode AND the source text.
         * - Source text: Used as cache key for reuse across repeated EVAL(...)
         * - Bytecode: Used for efficient execution in the VM
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

    /* Fusion pass: fuse eligible SPLIT/LIT|ANY pairs into a single OP_ANY */
    snobol_bc_fuse_split_any(&cb);

    CCEntry *rev = nullptr;
    for (CCEntry *it = charclass_head; it != nullptr; ) {
        CCEntry *next = it->next;
        it->next = rev;
        rev = it;
        it = next;
    }
    charclass_head = rev;

    size_t *offsets = charclass_count > 0 ? snobol_malloc(charclass_count * sizeof(size_t)) : nullptr;
    int idx = 0;
    for (CCEntry *it = charclass_head; it != nullptr; it = it->next) {
        if (offsets) offsets[idx++] = cb_pos(&cb);
        cb_emit_u16(&cb, it->range_count);
        cb_emit_u16(&cb, it->case_insensitive);
        for (size_t i = 0; i < it->range_count; ++i) {
            cb_emit_u32(&cb, it->ranges[i].start);
            cb_emit_u32(&cb, it->ranges[i].end);
        }
    }
    
    if (offsets) {
        /* Emit offsets in reverse order so that set_id=1 maps to CC1's data.
         * The list was reversed before serialisation (last-added first), so
         * offsets[0] holds the position of CCN, offsets[N-1] holds CC1.
         * get_ranges_ptr uses (set_id-1) as the index, so we emit in
         * original-id order: CC1 at index 0, CC2 at index 1, … */
        for (int i = (int)charclass_count - 1; i >= 0; i--) {
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

#endif /* !STANDALONE_BUILD — PHP-specific AST compilation above only */

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

                uint8_t fmt_type = 0;
                uint16_t fmt_width = 0;
                uint8_t  fmt_fill  = ' ';
                bool     fmt_has_width = false;
                if (braced) {
                    if (i < len && tpl[i] == '.') {
                        i++;
                        if (len - i >= 7 && memcmp(tpl + i, "upper()", 7) == 0) {
                            fmt_type = SNBL_FMT_UPPER; i += 7;
                        } else if (len - i >= 7 && memcmp(tpl + i, "lower()", 7) == 0) {
                            fmt_type = SNBL_FMT_LOWER; i += 7;
                        } else if (len - i >= 8 && memcmp(tpl + i, "length()", 8) == 0) {
                            fmt_type = SNBL_FMT_LENGTH; i += 8;
                        } else if (len - i >= 4 &&
                                   (memcmp(tpl + i, "lpad", 4) == 0 ||
                                    memcmp(tpl + i, "rpad", 4) == 0)) {
                            uint8_t pad_type = (tpl[i] == 'l') ? SNBL_FMT_LPAD : SNBL_FMT_RPAD;
                            i += 4;
                            if (i < len && tpl[i] == '(') {
                                i++;
                                /* parse mandatory width integer */
                                if (i < len && tpl[i] >= '1' && tpl[i] <= '9') {
                                    uint16_t w = 0;
                                    while (i < len && tpl[i] >= '0' && tpl[i] <= '9') {
                                        w = (uint16_t)(w * 10 + (tpl[i] - '0'));
                                        i++;
                                    }
                                    /* optional fill char: ,'c' */
                                    if (i < len && tpl[i] == ',') {
                                        i++;
                                        if (i < len && tpl[i] == '\'') {
                                            i++;
                                            if (i < len) {
                                                fmt_fill = (uint8_t)tpl[i];
                                                i++;
                                                if (i < len && tpl[i] == '\'') i++;
                                            }
                                        }
                                    }
                                    if (i < len && tpl[i] == ')') {
                                        i++;
                                        fmt_type = pad_type;
                                        fmt_width = w;
                                        fmt_has_width = true;
                                    }
                                }
                            }
                        }
                    }
                    if (i < len && tpl[i] == '}') {
                        i++;
                        if (fmt_type == 0) {
                            cb_emit_u8(&cb, OP_EMIT_CAPTURE); cb_emit_u8(&cb, reg);
                        } else {
                            cb_emit_u8(&cb, OP_EMIT_FORMAT);
                            cb_emit_u8(&cb, reg);
                            cb_emit_u8(&cb, fmt_type);
                            if (fmt_has_width) {
                                cb_emit_u16(&cb, fmt_width); /* big-endian width */
                                cb_emit_u8(&cb, fmt_fill);   /* fill char */
                            }
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

                    /* Reject overlong table names (name_len field is 1 byte) */
                    if (table_name_len > 255) {
                        cb_free(&cb);
                        return -1;
                    }

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
                        if (i < len && tpl[i] == ']') i++; /* consume outer ']' if present */

                        /* Emit OP_EMIT_TABLE with name encoded and literal key
                         * Format: opcode u8, table_id u16 (0xFFFF=unbound),
                         *         key_type u8 (0=literal),
                         *         name_len u8, name_bytes[name_len],
                         *         key_len u16, key_bytes[key_len] */
                        cb_emit_u8(&cb, OP_EMIT_TABLE);
                        cb_emit_u16(&cb, (uint16_t)SNBL_TABLE_ID_UNBOUND);
                        cb_emit_u8(&cb, 0);  /* key_type: 0 = literal key */
                        cb_emit_u8(&cb, (uint8_t)table_name_len);
                        cb_emit_bytes(&cb, (const uint8_t*)table_name, table_name_len);
                        cb_emit_u16(&cb, (uint16_t)key_len); /* literal key length */
                        cb_emit_bytes(&cb, (const uint8_t*)(tpl + key_start), key_len);
                    } else {
                        /* Capture-derived key: accept vN or bare digit(s) */
                        bool has_v_prefix = (i < len && tpl[i] == 'v');
                        if (has_v_prefix) i++; /* skip optional 'v' */
                        size_t key_reg_start = i;
                        while (i < len && tpl[i] >= '0' && tpl[i] <= '9') {
                            i++;
                        }
                        if (i == key_reg_start || i >= len || tpl[i] != ']') {
                            i = start_of_dollar + 1;
                            cb_emit_u8(&cb, OP_EMIT_LITERAL);
                            size_t off = cb_pos(&cb) + 4 + 4;
                            cb_emit_u32(&cb, (uint32_t)off); cb_emit_u32(&cb, 1); cb_emit_u8(&cb, '$');
                            continue;
                        }
                        size_t key_reg = 0;
                        for (size_t ki = key_reg_start; ki < i; ki++) {
                            key_reg = key_reg * 10 + (uint8_t)(tpl[ki] - '0');
                        }
                        i++; /* skip inner ']' */
                        if (i < len && tpl[i] == ']') i++; /* consume outer ']' if present */

                        /* Emit OP_EMIT_TABLE with name encoded and capture key
                         * Format: opcode u8, table_id u16 (0xFFFF=unbound),
                         *         key_type u8 (1=capture),
                         *         name_len u8, name_bytes[name_len],
                         *         key_reg u8 */
                        cb_emit_u8(&cb, OP_EMIT_TABLE);
                        cb_emit_u16(&cb, (uint16_t)SNBL_TABLE_ID_UNBOUND);
                        cb_emit_u8(&cb, 1);  /* key_type: 1 = capture-derived key */
                        cb_emit_u8(&cb, (uint8_t)table_name_len);
                        cb_emit_bytes(&cb, (const uint8_t*)table_name, table_name_len);
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

int snobol_template_bind_tables(uint8_t *bc, size_t bc_len,
                                 const char **names, const uint16_t *ids,
                                 size_t n) {
    if (!bc || bc_len == 0) return 0;
    /* Allow n==0: still scan so we can detect and report any unbound table IDs */

    int result = 0;
    size_t ip = 0;

    while (ip < bc_len) {
        uint8_t op = bc[ip++];

        switch (op) {
            case OP_ACCEPT:
                return result; /* end of template bytecode */

            case OP_EMIT_LITERAL: {
                /* off:u32(4) + len:u32(4) + data[len] */
                if (ip + 8 > bc_len) return result;
                uint32_t lit_len = ((uint32_t)bc[ip+4] << 24) | ((uint32_t)bc[ip+5] << 16)
                                 | ((uint32_t)bc[ip+6] << 8)  | (uint32_t)bc[ip+7];
                ip += 8 + lit_len;
                break;
            }

            case OP_EMIT_CAPTURE:
                ip += 1; /* reg:u8 */
                break;

            case OP_EMIT_EXPR:
                ip += 2; /* reg:u8, expr_type:u8 (legacy) */
                break;

            case OP_EMIT_FORMAT: {
                /* reg:u8, format_type:u8 [+ width:u16, fill:u8 for LPAD/RPAD] */
                if (ip + 2 > bc_len) return result;
                uint8_t fmt = bc[ip + 1];
                ip += 2;
                if (fmt == SNBL_FMT_LPAD || fmt == SNBL_FMT_RPAD) {
                    ip += 3; /* width:u16 + fill:u8 */
                }
                break;
            }

            case OP_EMIT_TABLE: {
                /* table_id:u16, key_type:u8, name_len:u8, name_bytes[name_len], <key payload> */
                if (ip + 4 > bc_len) return result;
                uint16_t tid      = ((uint16_t)bc[ip] << 8) | bc[ip+1];
                uint8_t  key_type = bc[ip+2];
                uint8_t  nm_len   = bc[ip+3];

                if (ip + 4 + nm_len > bc_len) return result;

                if (tid == (uint16_t)SNBL_TABLE_ID_UNBOUND) {
                    const char *name_ptr = (const char *)bc + ip + 4;
                    bool resolved = false;
                    for (size_t k = 0; k < n; k++) {
                        if (names[k] && strlen(names[k]) == nm_len &&
                            memcmp(names[k], name_ptr, nm_len) == 0) {
                            bc[ip]   = (uint8_t)((ids[k] >> 8) & 0xFF);
                            bc[ip+1] = (uint8_t)(ids[k] & 0xFF);
                            resolved = true;
                            break;
                        }
                    }
                    if (!resolved) result = -1;
                }

                ip += 2 + 1 + 1 + nm_len; /* table_id + key_type + name_len + name_bytes */

                /* skip key payload */
                if (key_type == 0) {
                    /* literal key: key_len:u16, key_bytes[key_len] */
                    if (ip + 2 > bc_len) return result;
                    uint16_t key_len = ((uint16_t)bc[ip] << 8) | bc[ip+1];
                    ip += 2 + key_len;
                } else if (key_type == 1) {
                    /* capture key: key_reg:u8 */
                    ip += 1;
                }
                break;
            }

            default:
                return result; /* unknown op — stop walking safely */
        }
    }

    return result;
}

void compiler_free(uint8_t *bc) {
    if (bc) {
        SNOBOL_LOG("compiler_free bc=%p", (void*)bc);
        snobol_free(bc);
    }
}

/* ---------------------------------------------------------------------------
 * Label tracking for C-AST compilation
 * ---------------------------------------------------------------------------
 * Labels are assigned sequential numeric IDs (0, 1, 2, ...).
 * The label offset table is appended to the end of the bytecode after the
 * charclass section:
 *   [label offsets: u32 * label_count] [label_count: u32]
 * vm_exec reads this table and pre-registers labels before running.
 * ---------------------------------------------------------------------------*/
typedef struct {
    char *name;       /* Owned: label name string */
    uint16_t id;      /* Numeric ID (== index in table) */
    uint32_t offset;  /* Bytecode offset immediately after OP_LABEL+id */
    bool defined;     /* Was this label defined (not just referenced)? */
    bool referenced;  /* Was this label referenced by a goto? */
} LabelEntry;

static LabelEntry *label_table_c = nullptr;
static uint16_t label_table_count_c = 0;
static uint16_t label_table_capacity_c = 0;
static char label_error_c[256];
static bool label_has_error_c = false;

static void free_label_table_c(void) {
    for (uint16_t i = 0; i < label_table_count_c; i++) {
        if (label_table_c[i].name) snobol_free(label_table_c[i].name);
    }
    if (label_table_c) snobol_free(label_table_c);
    label_table_c = nullptr;
    label_table_count_c = 0;
    label_table_capacity_c = 0;
    label_has_error_c = false;
    label_error_c[0] = '\0';
}

/* Find label by name. Returns index (==id), or -1 if not found. */
static int find_label_c(const char *name) {
    for (uint16_t i = 0; i < label_table_count_c; i++) {
        if (label_table_c[i].name && strcmp(label_table_c[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* Get or create label entry. Returns index (==id), or -1 on error. */
static int get_or_create_label_c(const char *name) {
    int idx = find_label_c(name);
    if (idx >= 0) return idx;

    if (label_table_count_c >= label_table_capacity_c) {
        uint16_t new_cap = label_table_capacity_c ? (uint16_t)(label_table_capacity_c * 2) : 8;
        LabelEntry *new_tbl = snobol_realloc(label_table_c, new_cap * sizeof(LabelEntry));
        if (!new_tbl) return -1;
        label_table_c = new_tbl;
        label_table_capacity_c = new_cap;
    }
    uint16_t new_idx = label_table_count_c++;
    label_table_c[new_idx].name = snobol_malloc(strlen(name) + 1);
    if (!label_table_c[new_idx].name) { label_table_count_c--; return -1; }
    strcpy(label_table_c[new_idx].name, name);
    label_table_c[new_idx].id = new_idx;
    label_table_c[new_idx].offset = 0;
    label_table_c[new_idx].defined = false;
    label_table_c[new_idx].referenced = false;
    return (int)new_idx;
}

/* Magic sentinel that marks a bytecode as having the label-table extension.
 * "SNBL" in ASCII: 0x534E424C
 * get_ranges_ptr and vm_exec check for this value at bc_len-4 to distinguish
 * compiler-produced bytecodes (new format) from hand-built test bytecodes
 * (old format: charclass_count lives at bc_len-4, no label table present).
 */
/* Guard prevents a redefinition error in the amalgam build (single TU)
 * where vm.c—included after compiler.c—declares the same constexpr. */
#ifndef SNOBOL_LABEL_TABLE_MAGIC_DEFINED
#define SNOBOL_LABEL_TABLE_MAGIC_DEFINED
constexpr uint32_t SNOBOL_LABEL_TABLE_MAGIC = 0x534E424Cu;
#endif

/* Emit label offset table at end of bytecode (after charclass section).
 * Format: [offset_0 u32] ... [offset_{N-1} u32] [label_count u32] [MAGIC u32]
 * The MAGIC at bc_len-4 lets readers distinguish this new format from the
 * old format where charclass_count was the last u32. */
static void emit_label_table(CodeBuf *c) {
    for (uint16_t i = 0; i < label_table_count_c; i++) {
        cb_emit_u32(c, label_table_c[i].defined ? label_table_c[i].offset : 0);
    }
    cb_emit_u32(c, (uint32_t)label_table_count_c);
    cb_emit_u32(c, SNOBOL_LABEL_TABLE_MAGIC);
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
    if (text != nullptr) {
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

/* Pattern primitive emit helpers (C-AST path) */
static int emit_breakx_c(const char *set, size_t len, CodeBuf *c) {
    int setid = add_or_get_charclass(set, len);
    cb_emit_u8(c, OP_BREAKX);
    cb_emit_u16(c, (uint16_t)setid);
    return 0;
}

static int emit_bal_c(uint32_t open_cp, uint32_t close_cp, CodeBuf *c) {
    cb_emit_u8(c, OP_BAL);
    cb_emit_u32(c, open_cp);
    cb_emit_u32(c, close_cp);
    return 0;
}

static int emit_fence_c(CodeBuf *c) {
    cb_emit_u8(c, OP_FENCE);
    return 0;
}

static int emit_rem_c(CodeBuf *c) {
    cb_emit_u8(c, OP_REM);
    return 0;
}

static int emit_rpos_c(int32_t n, CodeBuf *c) {
    cb_emit_u8(c, OP_RPOS);
    cb_emit_u32(c, (uint32_t)n);
    return 0;
}

static int emit_rtab_c(int32_t n, CodeBuf *c) {
    cb_emit_u8(c, OP_RTAB);
    cb_emit_u32(c, (uint32_t)n);
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
    free_label_table_c();
    next_loop_id = 0;
    compiler_case_insensitive = case_insensitive;

    CodeBuf cb;
    cb_init(&cb);

    if (emit_node_c(ast, &cb) != 0) {
        SNOBOL_LOG("compile_ast_to_bytecode_c FAILED at emit_node_c");
        cb_free(&cb);
        free_charclass_list();
        free_label_table_c();
        return -1;
    }

    /* Validate unknown labels - all referenced labels must be defined */
    for (uint16_t i = 0; i < label_table_count_c; i++) {
        if (label_table_c[i].referenced && !label_table_c[i].defined) {
            SNOBOL_LOG("compile_ast_to_bytecode_c FAILED: undefined label '%s'", label_table_c[i].name);
            cb_free(&cb);
            free_charclass_list();
            free_label_table_c();
            return -1;
        }
    }

    cb_emit_u8(&cb, OP_ACCEPT);

    /* Fusion pass: fuse eligible SPLIT/LIT|ANY pairs into a single OP_ANY */
    snobol_bc_fuse_split_any(&cb);

    CCEntry *rev = nullptr;
    for (CCEntry *it = charclass_head; it != nullptr; ) {
        CCEntry *next = it->next;
        it->next = rev;
        rev = it;
        it = next;
    }
    charclass_head = rev;

    size_t *offsets = charclass_count > 0 ? snobol_malloc(charclass_count * sizeof(size_t)) : nullptr;
    int idx = 0;
    for (CCEntry *it = charclass_head; it != nullptr; it = it->next) {
        if (offsets) offsets[idx++] = cb_pos(&cb);
        cb_emit_u16(&cb, it->range_count);
        cb_emit_u16(&cb, it->case_insensitive);
        for (size_t i = 0; i < it->range_count; ++i) {
            cb_emit_u32(&cb, it->ranges[i].start);
            cb_emit_u32(&cb, it->ranges[i].end);
        }
    }

    if (offsets) {
        /* Emit offsets in original-id order (CC1 at index 0, CC2 at index 1, …)
         * so get_ranges_ptr(vm, k) correctly resolves to CCk's data.
         * The list was reversed before serialisation so offsets[0]=CCN; we
         * emit in reverse to restore the original-id mapping. */
        for (int i = (int)charclass_count - 1; i >= 0; i--) {
            cb_emit_u32(&cb, (uint32_t)offsets[i]);
        }
        snobol_free(offsets);
    }

    cb_emit_u32(&cb, charclass_count);

    /* Emit label offset table (always, even if empty - label_count=0 is valid) */
    emit_label_table(&cb);

    uint8_t *out = snobol_malloc(cb.len);
    if (!out) {
        SNOBOL_LOG("compile_ast_to_bytecode_c FAILED to allocate final bc");
        cb_free(&cb);
        free_label_table_c();
        return -1;
    }
    memcpy(out, cb.buf, cb.len);
    *out_bc = out;
    *out_len = cb.len;

    cb_free(&cb);
    free_charclass_list();
    free_label_table_c();
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

        case AST_BREAKX:
            return emit_breakx_c(node->data.breakx.set, node->data.breakx.len, c);

        case AST_BAL:
            return emit_bal_c(node->data.bal.open_cp, node->data.bal.close_cp, c);

        case AST_FENCE:
            return emit_fence_c(c);

        case AST_REM:
            return emit_rem_c(c);

        case AST_RPOS:
            return emit_rpos_c(node->data.rpos_rtab.n, c);

        case AST_RTAB:
            return emit_rtab_c(node->data.rpos_rtab.n, c);

        case AST_LABEL: {
            /* Emit OP_LABEL opcode, register offset, detect duplicates */
            const char *name = node->data.label.name;
            if (!name) return -1;

            /* Duplicate label check */
            int existing = find_label_c(name);
            if (existing >= 0 && label_table_c[existing].defined) {
                snprintf(label_error_c, sizeof(label_error_c), "Duplicate label: '%s'", name);
                label_has_error_c = true;
                SNOBOL_LOG("emit_node_c: duplicate label '%s'", name);
                return -1;
            }

            int idx = get_or_create_label_c(name);
            if (idx < 0) return -1;

            /* Emit OP_LABEL with numeric label ID */
            cb_emit_u8(c, OP_LABEL);
            cb_emit_u16(c, (uint16_t)idx);

            /* Record the offset AFTER OP_LABEL+id - this is where OP_GOTO will transfer to */
            label_table_c[idx].offset = (uint32_t)cb_pos(c);
            label_table_c[idx].defined = true;

            /* Compile the target pattern (code that runs at/after this label) */
            if (node->data.label.target) {
                return emit_node_c(node->data.label.target, c);
            }
            return 0;
        }

        case AST_GOTO: {
            /* Emit OP_GOTO for goto statement */
            const char *label_name = node->data.goto_stmt.label;
            if (!label_name) return -1;

            int idx = get_or_create_label_c(label_name);
            if (idx < 0) return -1;

            label_table_c[idx].referenced = true;

            cb_emit_u8(c, OP_GOTO);
            cb_emit_u16(c, (uint16_t)idx);
            return 0;
        }

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
