#include "snobol_vm.h"      /* MUST come before snobol_compiler.h to get CHARCLASS_BITMAP_BYTES */
#include "snobol_compiler.h"

#ifndef STANDALONE_BUILD
#include "php.h"
#endif

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

#ifdef STANDALONE_BUILD
#define emalloc malloc
#define efree free
#define erealloc realloc
#endif

/* Minimal dynamic code buffer */
typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
} CodeBuf;

static void cb_init(CodeBuf *c) {
    c->cap = 4096;
    c->buf = emalloc(c->cap);
    c->len = 0;
}
static void cb_free(CodeBuf *c) {
    if (c->buf) {
        efree(c->buf);
        c->buf = NULL;
    }
    c->cap = c->len = 0;
}
static void cb_ensure(CodeBuf *c, size_t need) {
    if (c->len + need <= c->cap) return;
    size_t newcap = c->cap ? c->cap * 2 : 4096;
    while (c->len + need > newcap) newcap *= 2;
    c->buf = erealloc(c->buf, newcap);
    c->cap = newcap;
}
static size_t cb_pos(CodeBuf *c) { return c->len; }
static void cb_emit_u8(CodeBuf *c, uint8_t v) { cb_ensure(c,1); c->buf[c->len++] = v; }
static void cb_emit_u16(CodeBuf *c, uint16_t v) { cb_ensure(c,2); c->buf[c->len++] = (v >> 8) & 0xff; c->buf[c->len++] = v & 0xff; }
static void cb_emit_u32(CodeBuf *c, uint32_t v) { cb_ensure(c,4); c->buf[c->len++] = (v >> 24) & 0xff; c->buf[c->len++] = (v >> 16) & 0xff; c->buf[c->len++] = (v >> 8) & 0xff; c->buf[c->len++] = v & 0xff; }
static void cb_emit_bytes(CodeBuf *c, const uint8_t *b, size_t n) { if (n==0) return; cb_ensure(c,n); memcpy(c->buf + c->len, b, n); c->len += n; }

/* Charclass table handling */
typedef struct cc_entry {
    uint8_t bitmap[CHARCLASS_BITMAP_BYTES];
    struct cc_entry *next;
} CCEntry;
static CCEntry *charclass_head = NULL;
static uint32_t charclass_count = 0;
static uint8_t next_loop_id = 0;

static void free_charclass_list(void) {
    CCEntry *e = charclass_head;
    while (e) {
        CCEntry *next = e->next;
        efree(e);
        e = next;
    }
    charclass_head = NULL;
    charclass_count = 0;
}

static int add_or_get_charclass(const char *s, size_t len) {
    uint8_t bm[CHARCLASS_BITMAP_BYTES];
    memset(bm, 0, CHARCLASS_BITMAP_BYTES);
    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)s[i];
        if (ch > 127) continue;
        unsigned idx = ch >> 3;
        unsigned bit = ch & 7;
        bm[idx] |= (1 << bit);
    }
    CCEntry *e = charclass_head;
    int id = 1;
    while (e) {
        if (memcmp(e->bitmap, bm, CHARCLASS_BITMAP_BYTES) == 0) return id;
        id++; e = e->next;
    }
    CCEntry *ne = emalloc(sizeof(*ne));
    memcpy(ne->bitmap, bm, CHARCLASS_BITMAP_BYTES);
    ne->next = charclass_head;
    charclass_head = ne;
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
    size_t loop_start = cb_pos(c);
    cb_emit_u8(c, OP_SPLIT);
    size_t where_body = cb_pos(c); cb_emit_u32(c, 0);
    size_t where_done = cb_pos(c); cb_emit_u32(c, 0);

    size_t body_start = cb_pos(c);
    if (emit_node(sub, c) != 0) return -1;
    cb_emit_u8(c, OP_JMP);
    cb_emit_u32(c, (uint32_t)loop_start);

    size_t done = cb_pos(c);
    uint32_t v_body = (uint32_t)body_start;
    c->buf[where_body+0] = (v_body >> 24) & 0xff; c->buf[where_body+1] = (v_body >> 16) & 0xff; c->buf[where_body+2] = (v_body >> 8) & 0xff; c->buf[where_body+3] = v_body & 0xff;
    uint32_t v_done = (uint32_t)done;
    c->buf[where_done+0] = (v_done >> 24) & 0xff; c->buf[where_done+1] = (v_done >> 16) & 0xff; c->buf[where_done+2] = (v_done >> 8) & 0xff; c->buf[where_done+3] = v_done & 0xff;

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
        cb_emit_u8(c, OP_EMIT_LIT);
        cb_emit_u32(c, (uint32_t)off_of_payload);
        cb_emit_u32(c, (uint32_t)ZSTR_LEN(zs));
        cb_emit_bytes(c, (const uint8_t*)ZSTR_VAL(zs), ZSTR_LEN(zs));
        return 0;
    } else if (reg && Z_TYPE_P(reg) == IS_LONG) {
        cb_emit_u8(c, OP_EMIT_REF);
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
    return -1;
}

int compile_ast_to_bytecode(zval *ast, uint8_t **out_bc, size_t *out_len) {
    SNOBOL_LOG("compile_ast_to_bytecode START");
    free_charclass_list();
    next_loop_id = 0;
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

    for (CCEntry *it = charclass_head; it != NULL; it = it->next) {
        cb_emit_bytes(&cb, it->bitmap, CHARCLASS_BITMAP_BYTES);
    }
    cb_emit_u32(&cb, charclass_count);

    uint8_t *out = emalloc(cb.len);
    if (!out) {
        SNOBOL_LOG("compile_ast_to_bytecode FAILED to emalloc final bc");
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
#endif

void compiler_free(uint8_t *bc) {
    if (bc) {
        SNOBOL_LOG("compiler_free bc=%p", (void*)bc);
        efree(bc);
    }
}