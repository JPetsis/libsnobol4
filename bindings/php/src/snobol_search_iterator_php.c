#include "php.h"
#include "php_snobol.h"
#include "zend_exceptions.h"
#include "Zend/zend_interfaces.h"
#include "snobol/snobol.h"
#include "snobol/search.h"

#define SNOBOL_LOG(fmt, ...) ((void)0)

zend_class_entry *snobol_search_iterator_ce;
extern zend_class_entry *snobol_pattern_ce;

static zend_object_handlers snobol_search_iterator_handlers;

/* Internal struct: stores state for the "lazy iterator" semantics */
typedef struct {
    snobol_pattern_t *pattern;
    snobol_pattern_search_state_t *state;
    const char *subject;
    size_t subject_len;
    /* Current iteration state */
    zend_long key;
    bool started;
    bool valid;
    zval current_match;    /* owned match array (or IS_UNDEF/NULL) */
    zend_object std;
} snobol_search_iterator_t;

static inline snobol_search_iterator_t* php_si_fetch(zend_object *obj) {
    return (snobol_search_iterator_t *)((char *)(obj) - XtOffsetOf(snobol_search_iterator_t, std));
}

static void si_dtor(zend_object *object) {
    snobol_search_iterator_t *iter = php_si_fetch(object);
    zval_ptr_dtor(&iter->current_match);
    if (iter->state) {
        snobol_pattern_search_state_destroy(iter->state);
        iter->state = NULL;
    }
    zend_object_std_dtor(object);
}

static zend_object *si_create(zend_class_entry *ce) {
    snobol_search_iterator_t *iter = zend_object_alloc(sizeof(snobol_search_iterator_t), ce);
    zend_object_std_init(&iter->std, ce);
    object_properties_init(&iter->std, ce);
    iter->std.handlers = &snobol_search_iterator_handlers;
    ZVAL_UNDEF(&iter->current_match);
    return &iter->std;
}

/* ---- Internal: fetch next match and store in iter->current_match ---- */
static bool si_fetch_next(snobol_search_iterator_t *iter, size_t search_offset) {
    if (!iter->state || search_offset > iter->subject_len) return false;

    snobol_match_t *m = snobol_pattern_search_ex(iter->state, iter->subject,
                                                  iter->subject_len, search_offset);
    if (!m || !snobol_match_success(m)) return false;

    size_t ms = snobol_match_get_position(m);
    size_t me = ms + snobol_match_get_length(m);

    zval_ptr_dtor(&iter->current_match);
    array_init(&iter->current_match);
    for (size_t i = 0; i < m->var_count; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "v%u", (unsigned)i);
        size_t vlen = 0;
        const char *vval = snobol_match_get_variable(m, key, &vlen);
        if (vval && vlen > 0) {
            add_assoc_stringl(&iter->current_match, key, vval, vlen);
        } else {
            add_assoc_null(&iter->current_match, key);
        }
    }
    add_assoc_long(&iter->current_match, "_match_len", (zend_long)(me - ms));
    add_assoc_long(&iter->current_match, "_match_start", (zend_long)ms);
    if (m->output && m->output_len > 0) {
        add_assoc_stringl(&iter->current_match, "_output", m->output, m->output_len);
    } else {
        add_assoc_string(&iter->current_match, "_output", "");
    }
    return true;
}

/* ---- Iterator methods implemented on the class itself ---- */

PHP_METHOD(Snobol_SearchIterator, current) {
    snobol_search_iterator_t *iter = php_si_fetch(Z_OBJ_P(ZEND_THIS));
    if (Z_TYPE(iter->current_match) == IS_ARRAY) {
        RETURN_ZVAL(&iter->current_match, 1, 0);
    }
    RETURN_NULL();
}

PHP_METHOD(Snobol_SearchIterator, key) {
    snobol_search_iterator_t *iter = php_si_fetch(Z_OBJ_P(ZEND_THIS));
    RETURN_LONG(iter->key);
}

PHP_METHOD(Snobol_SearchIterator, next) {
    snobol_search_iterator_t *iter = php_si_fetch(Z_OBJ_P(ZEND_THIS));
    iter->key++;

    if (!iter->started || !iter->valid) {
        iter->valid = false;
        return;
    }

    /* Find the end of the current match to advance past it */
    zend_long match_start = 0, match_len = 0;
    if (Z_TYPE(iter->current_match) == IS_ARRAY) {
        zval *ms = zend_hash_str_find(Z_ARRVAL(iter->current_match), "_match_start", 12);
        if (ms) match_start = Z_LVAL_P(ms);
        zval *ml = zend_hash_str_find(Z_ARRVAL(iter->current_match), "_match_len", 10);
        if (ml) match_len = Z_LVAL_P(ml);
    }

    size_t search_offset = (size_t)(match_start + (match_len ? match_len : 1));
    iter->valid = si_fetch_next(iter, search_offset);
}

PHP_METHOD(Snobol_SearchIterator, valid) {
    snobol_search_iterator_t *iter = php_si_fetch(Z_OBJ_P(ZEND_THIS));
    RETURN_BOOL(iter->valid);
}

PHP_METHOD(Snobol_SearchIterator, rewind) {
    snobol_search_iterator_t *iter = php_si_fetch(Z_OBJ_P(ZEND_THIS));
    iter->key = 0;
    iter->started = true;

    if (!iter->state || iter->subject_len == 0) {
        iter->valid = false;
        return;
    }

    /* Re-create search state (API has no reset). */
    snobol_pattern_search_state_destroy(iter->state);
    iter->state = snobol_pattern_search_state_create(
        iter->pattern->bc, iter->pattern->bc_len);
    if (!iter->state) {
        iter->valid = false;
        return;
    }

    iter->valid = si_fetch_next(iter, 0);
}

/* ---- Public API: create SearchIterator ---- */

void php_snobol_create_search_iterator(zval *return_value,
                                        snobol_pattern_t *pattern,
                                        const char *subject,
                                        size_t subject_len) {
    if (!snobol_search_iterator_ce) {
        zend_throw_exception(zend_ce_exception,
            "SearchIterator class not registered", 0);
        return;
    }
    if (object_init_ex(return_value, snobol_search_iterator_ce) != SUCCESS) {
        zend_throw_exception(zend_ce_exception,
            "Failed to create SearchIterator", 0);
        return;
    }
    snobol_search_iterator_t *iter = php_si_fetch(Z_OBJ_P(return_value));
    iter->pattern = pattern;
    iter->subject = subject;
    iter->subject_len = subject_len;
    iter->state = snobol_pattern_search_state_create(pattern->bc, pattern->bc_len);
    iter->key = 0;
    iter->started = false;
    iter->valid = false;
    ZVAL_UNDEF(&iter->current_match);
    if (!iter->state) {
        zend_throw_exception(zend_ce_exception,
            "Failed to create search state", 0);
    }
}

PHP_METHOD(Snobol_SearchIterator, fromPattern) {
    zval *pattern_zv;
    zend_string *subject;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_OBJECT_OF_CLASS(pattern_zv, snobol_pattern_ce)
        Z_PARAM_STR(subject)
    ZEND_PARSE_PARAMETERS_END();

    snobol_pattern_t *pat = php_snobol_fetch(Z_OBJ_P(pattern_zv));
    if (!pat->bc || pat->bc_len == 0) {
        zend_throw_exception(zend_ce_exception, "Pattern not compiled", 0);
        RETURN_NULL();
    }
    php_snobol_create_search_iterator(return_value, pat,
                                       ZSTR_VAL(subject), ZSTR_LEN(subject));
}

ZEND_BEGIN_ARG_INFO_EX(ai_si_fromPattern, 0, 0, 2)
    ZEND_ARG_OBJ_INFO(0, pattern, Snobol\\Pattern, 0)
    ZEND_ARG_TYPE_INFO(0, subject, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_si_current, 0, 0, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_si_key, 0, 0, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_si_next, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_si_valid, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_si_rewind, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry snobol_search_iterator_methods[] = {
    PHP_ME(Snobol_SearchIterator, fromPattern, ai_si_fromPattern, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_SearchIterator, current,   ai_si_current, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_SearchIterator, key,       ai_si_key,     ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_SearchIterator, next,      ai_si_next,    ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_SearchIterator, valid,     ai_si_valid,   ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_SearchIterator, rewind,    ai_si_rewind,  ZEND_ACC_PUBLIC)
    PHP_FE_END
};

void snobol_search_iterator_minit(void) {
    memcpy(&snobol_search_iterator_handlers, zend_get_std_object_handlers(),
           sizeof(zend_object_handlers));
    snobol_search_iterator_handlers.offset = XtOffsetOf(snobol_search_iterator_t, std);
    snobol_search_iterator_handlers.free_obj = si_dtor;

    zend_class_entry ce;
    INIT_CLASS_ENTRY(ce, "Snobol\\SearchIterator", snobol_search_iterator_methods);
    snobol_search_iterator_ce = zend_register_internal_class(&ce);
    snobol_search_iterator_ce->create_object = si_create;
    /* Implement Iterator interface — PHP will call the methods directly,
     * bypassing the buggy zend_object_iterator API. */
    zend_class_implements(snobol_search_iterator_ce, 1, zend_ce_iterator);
}
