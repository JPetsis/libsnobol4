#include "php.h"
#include "php_snobol.h"
#include "zend_exceptions.h"
#include "snobol/snobol.h"

#define SNOBOL_LOG(fmt, ...) ((void)0)

zend_class_entry *snobol_builder_ce;

ZEND_BEGIN_ARG_INFO_EX(ai_builder_lit, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, s, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_span, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, set, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_brk, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, set, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_any, 0, 0, 0)
    ZEND_ARG_TYPE_INFO(0, set, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_notany, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, set, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_len, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, n, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_arbno, 0, 0, 1)
    ZEND_ARG_ARRAY_INFO(0, sub, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_cap, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, reg, IS_LONG, 0)
    ZEND_ARG_ARRAY_INFO(0, sub, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_assign, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, var, IS_LONG, 0)
    ZEND_ARG_TYPE_INFO(0, reg, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_concat, 0, 0, 1)
    ZEND_ARG_ARRAY_INFO(0, parts, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_alt, 0, 0, 2)
    ZEND_ARG_INFO(0, left)
    ZEND_ARG_INFO(0, right)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_eval, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, fn, IS_LONG, 0)
    ZEND_ARG_TYPE_INFO(0, reg, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_anchor, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, atype, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_repeat, 0, 0, 2)
    ZEND_ARG_ARRAY_INFO(0, sub, 0)
    ZEND_ARG_TYPE_INFO(0, min, IS_LONG, 0)
    ZEND_ARG_TYPE_INFO(0, max, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_emit, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, text, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_emitRef, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, reg, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_label, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
    ZEND_ARG_ARRAY_INFO(0, target, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_goto, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, label, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_dynamicEval, 0, 0, 1)
    ZEND_ARG_ARRAY_INFO(0, expr, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_tableAccess, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, tableName, IS_STRING, 0)
    ZEND_ARG_ARRAY_INFO(0, keyExpr, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_tableUpdate, 0, 0, 3)
    ZEND_ARG_TYPE_INFO(0, tableName, IS_STRING, 0)
    ZEND_ARG_ARRAY_INFO(0, keyExpr, 0)
    ZEND_ARG_ARRAY_INFO(0, valueExpr, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_arrayAccess, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, arrayName, IS_STRING, 0)
    ZEND_ARG_ARRAY_INFO(0, indexExpr, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_arrayUpdate, 0, 0, 3)
    ZEND_ARG_TYPE_INFO(0, arrayName, IS_STRING, 0)
    ZEND_ARG_ARRAY_INFO(0, indexExpr, 0)
    ZEND_ARG_ARRAY_INFO(0, valueExpr, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_arrayCreate, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, arrayName, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, size, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_breakx, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, set, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_bal, 0, 0, 0)
    ZEND_ARG_TYPE_INFO(0, open, IS_STRING, 1)
    ZEND_ARG_TYPE_INFO(0, close, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_pos, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, n, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_tab, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, n, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_rpos, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, n, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_fence, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_rem, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_arb, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_abort, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_fail, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_succeed, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_builder_rtab, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, n, IS_LONG, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Snobol_Builder, lit) {
    char *s; size_t s_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(s, s_len)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "lit", 3);
    add_assoc_stringl(return_value, "text", s, s_len);
}

PHP_METHOD(Snobol_Builder, span) {
    char *set; size_t set_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(set, set_len)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "span", 4);
    add_assoc_stringl(return_value, "set", set, set_len);
}

PHP_METHOD(Snobol_Builder, brk) {
    char *set; size_t set_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(set, set_len)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "break", 5);
    add_assoc_stringl(return_value, "set", set, set_len);
}

PHP_METHOD(Snobol_Builder, any) {
    char *set = NULL; size_t set_len = 0;
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING_OR_NULL(set, set_len)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "any", 3);
    if (set) {
        add_assoc_stringl(return_value, "set", set, set_len);
    }
}

PHP_METHOD(Snobol_Builder, notany) {
    char *set; size_t set_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(set, set_len)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "notany", 6);
    add_assoc_stringl(return_value, "set", set, set_len);
}

PHP_METHOD(Snobol_Builder, len) {
    zend_long n;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(n)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "len", 3);
    add_assoc_long(return_value, "n", n);
}

PHP_METHOD(Snobol_Builder, arbno) {
    zval *sub;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ARRAY(sub)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "arbno", 5);
    snobol_assoc_zval(return_value, "sub", 3, sub);
}

PHP_METHOD(Snobol_Builder, cap) {
    zend_long reg; zval *sub;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_LONG(reg)
        Z_PARAM_ARRAY(sub)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "cap", 3);
    add_assoc_long(return_value, "reg", reg);
    snobol_assoc_zval(return_value, "sub", 3, sub);
}

PHP_METHOD(Snobol_Builder, assign) {
    zend_long var, reg;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_LONG(var)
        Z_PARAM_LONG(reg)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "assign", 6);
    add_assoc_long(return_value, "var", var);
    add_assoc_long(return_value, "reg", reg);
}

PHP_METHOD(Snobol_Builder, concat) {
    zval *parts;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ARRAY(parts)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "concat", 6);
    {
        zval copy;
        ZVAL_COPY(&copy, parts);
        zend_hash_str_update(Z_ARRVAL_P(return_value), "parts", 5, &copy);
    }
}

PHP_METHOD(Snobol_Builder, alt) {
    zval *left, *right;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_ZVAL(left)
        Z_PARAM_ZVAL(right)
    ZEND_PARSE_PARAMETERS_END();

    array_init(return_value);
    add_assoc_stringl(return_value, "type", "alt", 3);

    if (Z_TYPE_P(left) == IS_STRING) {
        zval l;
        array_init(&l);
        add_assoc_stringl(&l, "type", "lit", 3);
        add_assoc_stringl(&l, "text", Z_STRVAL_P(left), Z_STRLEN_P(left));
        snobol_assoc_zval(return_value, "left", 4, &l);
        zval_ptr_dtor(&l);
    } else {
        snobol_assoc_zval(return_value, "left", 4, left);
    }

    if (Z_TYPE_P(right) == IS_STRING) {
        zval r;
        array_init(&r);
        add_assoc_stringl(&r, "type", "lit", 3);
        add_assoc_stringl(&r, "text", Z_STRVAL_P(right), Z_STRLEN_P(right));
        snobol_assoc_zval(return_value, "right", 5, &r);
        zval_ptr_dtor(&r);
    } else {
        snobol_assoc_zval(return_value, "right", 5, right);
    }
}

PHP_METHOD(Snobol_Builder, eval) {
    zend_long fn, reg;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_LONG(fn)
        Z_PARAM_LONG(reg)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "eval", 4);
    add_assoc_long(return_value, "fn", fn);
    add_assoc_long(return_value, "reg", reg);
}

PHP_METHOD(Snobol_Builder, anchor) {
    char *atype; size_t atype_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(atype, atype_len)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "anchor", 6);
    add_assoc_stringl(return_value, "atype", atype, atype_len);
}

PHP_METHOD(Snobol_Builder, repeat) {
    zval *sub; zend_long min; zend_long max = -1;
    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_ARRAY(sub)
        Z_PARAM_LONG(min)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(max)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "repeat", 6);
    snobol_assoc_zval(return_value, "sub", 3, sub);
    add_assoc_long(return_value, "min", min);
    add_assoc_long(return_value, "max", max);
}

PHP_METHOD(Snobol_Builder, emit) {
    char *text; size_t text_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(text, text_len)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "emit", 4);
    add_assoc_stringl(return_value, "text", text, text_len);
}

PHP_METHOD(Snobol_Builder, emitRef) {
    zend_long reg;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(reg)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "emit", 4);
    add_assoc_long(return_value, "reg", reg);
}

PHP_METHOD(Snobol_Builder, label) {
    char *name; size_t name_len; zval *target;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_ARRAY(target)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "label", 5);
    add_assoc_stringl(return_value, "name", name, name_len);
    snobol_assoc_zval(return_value, "target", 6, target);
}

PHP_METHOD(Snobol_Builder, goto) {
    char *label; size_t label_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(label, label_len)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "goto", 4);
    add_assoc_stringl(return_value, "label", label, label_len);
}

PHP_METHOD(Snobol_Builder, dynamicEval) {
    zval *expr;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ARRAY(expr)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "dynamic_eval", 12);
    snobol_assoc_zval(return_value, "expr", 4, expr);
}

PHP_METHOD(Snobol_Builder, tableAccess) {
    char *table; size_t table_len; zval *key;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(table, table_len)
        Z_PARAM_ARRAY(key)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "table_access", 12);
    add_assoc_stringl(return_value, "table", table, table_len);
    snobol_assoc_zval(return_value, "key", 3, key);
}

PHP_METHOD(Snobol_Builder, tableUpdate) {
    char *table; size_t table_len; zval *key, *value;
    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_STRING(table, table_len)
        Z_PARAM_ARRAY(key)
        Z_PARAM_ARRAY(value)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "table_update", 12);
    add_assoc_stringl(return_value, "table", table, table_len);
    snobol_assoc_zval(return_value, "key", 3, key);
    snobol_assoc_zval(return_value, "value", 5, value);
}

PHP_METHOD(Snobol_Builder, arrayAccess) {
    char *name; size_t name_len; zval *index;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_ARRAY(index)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "array_access", 12);
    add_assoc_stringl(return_value, "array", name, name_len);
    snobol_assoc_zval(return_value, "index", 5, index);
}

PHP_METHOD(Snobol_Builder, arrayUpdate) {
    char *name; size_t name_len; zval *index, *value;
    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_ARRAY(index)
        Z_PARAM_ARRAY(value)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "array_update", 12);
    add_assoc_stringl(return_value, "array", name, name_len);
    snobol_assoc_zval(return_value, "index", 5, index);
    snobol_assoc_zval(return_value, "value", 5, value);
}

PHP_METHOD(Snobol_Builder, arrayCreate) {
    char *name; size_t name_len; zend_long size = 0;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(name, name_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(size)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "array_create", 12);
    add_assoc_stringl(return_value, "array", name, name_len);
    add_assoc_long(return_value, "size", size);
}

PHP_METHOD(Snobol_Builder, breakx) {
    char *set; size_t set_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(set, set_len)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "breakx", 6);
    add_assoc_stringl(return_value, "set", set, set_len);
}

PHP_METHOD(Snobol_Builder, bal) {
    char *open = "(", *close = ")";
    size_t open_len = 1, close_len = 1;
    ZEND_PARSE_PARAMETERS_START(0, 2)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING_OR_NULL(open, open_len)
        Z_PARAM_STRING_OR_NULL(close, close_len)
    ZEND_PARSE_PARAMETERS_END();
    if (!open) { open = "("; open_len = 1; }
    if (!close) { close = ")"; close_len = 1; }
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "bal", 3);
    add_assoc_stringl(return_value, "open", open, open_len);
    add_assoc_stringl(return_value, "close", close, close_len);
}

PHP_METHOD(Snobol_Builder, fence) {
    ZEND_PARSE_PARAMETERS_NONE();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "fence", 5);
}

PHP_METHOD(Snobol_Builder, rem) {
    ZEND_PARSE_PARAMETERS_NONE();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "rem", 3);
}

PHP_METHOD(Snobol_Builder, rpos) {
    zend_long n;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(n)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "rpos", 4);
    add_assoc_long(return_value, "n", n);
}

PHP_METHOD(Snobol_Builder, rtab) {
    zend_long n;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(n)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "rtab", 4);
    add_assoc_long(return_value, "n", n);
}

PHP_METHOD(Snobol_Builder, arb) {
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "arbno", 5);
    {
        zval len1;
        array_init(&len1);
        add_assoc_stringl(&len1, "type", "len", 3);
        add_assoc_long(&len1, "n", 1);
        snobol_assoc_zval(return_value, "sub", 3, &len1);
        zval_ptr_dtor(&len1);
    }
}

PHP_METHOD(Snobol_Builder, pos) {
    zend_long n;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(n)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "pos", 3);
    add_assoc_long(return_value, "n", n);
}

PHP_METHOD(Snobol_Builder, tab) {
    zend_long n;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(n)
    ZEND_PARSE_PARAMETERS_END();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "tab", 3);
    add_assoc_long(return_value, "n", n);
}

PHP_METHOD(Snobol_Builder, abort) {
    ZEND_PARSE_PARAMETERS_NONE();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "abort", 5);
}

PHP_METHOD(Snobol_Builder, fail) {
    ZEND_PARSE_PARAMETERS_NONE();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "fail", 4);
}

PHP_METHOD(Snobol_Builder, succeed) {
    ZEND_PARSE_PARAMETERS_NONE();
    array_init(return_value);
    add_assoc_stringl(return_value, "type", "succeed", 7);
}

static const zend_function_entry snobol_builder_methods[] = {
    PHP_ME(Snobol_Builder, lit,         ai_builder_lit,         ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, span,        ai_builder_span,        ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, brk,         ai_builder_brk,         ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, any,         ai_builder_any,         ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, notany,      ai_builder_notany,      ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, len,         ai_builder_len,         ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, arbno,       ai_builder_arbno,       ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, cap,         ai_builder_cap,         ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, assign,      ai_builder_assign,      ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, concat,      ai_builder_concat,      ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, alt,         ai_builder_alt,         ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, eval,        ai_builder_eval,        ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, anchor,      ai_builder_anchor,      ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, repeat,      ai_builder_repeat,      ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, emit,        ai_builder_emit,        ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, emitRef,     ai_builder_emitRef,     ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, label,       ai_builder_label,       ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, goto,        ai_builder_goto,        ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, dynamicEval, ai_builder_dynamicEval, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, tableAccess, ai_builder_tableAccess, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, tableUpdate, ai_builder_tableUpdate, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, arrayAccess, ai_builder_arrayAccess, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, arrayUpdate, ai_builder_arrayUpdate, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, arrayCreate, ai_builder_arrayCreate, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, breakx,      ai_builder_breakx,      ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, bal,         ai_builder_bal,         ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, fence,       ai_builder_fence,       ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, rem,         ai_builder_rem,         ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, rpos,        ai_builder_rpos,        ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, rtab,        ai_builder_rtab,        ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, arb,         ai_builder_arb,         ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, pos,         ai_builder_pos,         ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, tab,         ai_builder_tab,         ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, abort,       ai_builder_abort,       ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, fail,        ai_builder_fail,        ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Builder, succeed,     ai_builder_succeed,     ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_FE_END
};

void snobol_builder_php_minit(void) {
    zend_class_entry ce;
    INIT_CLASS_ENTRY(ce, "Snobol\\Builder", snobol_builder_methods);
    snobol_builder_ce = zend_register_internal_class(&ce);
    snobol_builder_ce->ce_flags |= ZEND_ACC_FINAL;
}
