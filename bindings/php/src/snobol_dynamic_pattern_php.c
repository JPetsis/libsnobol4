#include "php.h"
#include "php_snobol.h"
#include "zend_exceptions.h"
#include "Zend/zend_interfaces.h"
#include "snobol/snobol.h"

extern zend_class_entry *snobol_pattern_ce;

#define SNOBOL_LOG(fmt, ...) ((void)0)

#define DYN_CACHE_DEFAULT_CAPACITY 128

zend_class_entry *snobol_dynamic_pattern_cache_ce;
static zend_object_handlers snobol_dynamic_pattern_cache_object_handlers;

typedef struct {
    zval cache;          /* PHP array: pattern_source => Pattern */
    zval access_order;   /* PHP indexed array: keys in access order */
    zend_long capacity;
    zend_long compile_count;
    zend_long evaluate_count;
    zend_object std;
} snobol_dyn_cache_php_t;

static inline snobol_dyn_cache_php_t* php_dyncache_fetch(zend_object *obj) {
    return (snobol_dyn_cache_php_t *)((char *)(obj) - XtOffsetOf(snobol_dyn_cache_php_t, std));
}

static void php_dyncache_free(zend_object *object) {
    snobol_dyn_cache_php_t *intern = php_dyncache_fetch(object);
    zval_ptr_dtor(&intern->cache);
    zval_ptr_dtor(&intern->access_order);
    zend_object_std_dtor(object);
}

static zend_object *php_dyncache_create(zend_class_entry *ce) {
    snobol_dyn_cache_php_t *intern = zend_object_alloc(sizeof(snobol_dyn_cache_php_t), ce);
    zend_object_std_init(&intern->std, ce);
    object_properties_init(&intern->std, ce);
    intern->std.handlers = &snobol_dynamic_pattern_cache_object_handlers;
    array_init(&intern->cache);
    array_init(&intern->access_order);
    intern->capacity = DYN_CACHE_DEFAULT_CAPACITY;
    intern->compile_count = 0;
    intern->evaluate_count = 0;
    return &intern->std;
}

static void php_dyncache_evict_lru(snobol_dyn_cache_php_t *intern) {
    HashTable *ao = Z_ARRVAL_P(&intern->access_order);
    if (zend_hash_num_elements(ao) == 0) return;

    zval *lru_key_zv = zend_hash_index_find(ao, 0);
    if (!lru_key_zv) return;

    zend_hash_str_del(Z_ARRVAL_P(&intern->cache),
        Z_STRVAL_P(lru_key_zv), Z_STRLEN_P(lru_key_zv));
    zend_hash_index_del(ao, 0);

    uint32_t count = zend_hash_num_elements(ao);
    for (uint32_t i = 0; i < count; i++) {
        zval *zv = zend_hash_index_find(ao, i + 1);
        if (zv) {
            zval copy;
            ZVAL_COPY(&copy, zv);
            zend_hash_index_update(ao, i, &copy);
        }
    }
    if (count > 0) {
        zend_hash_index_del(ao, count);
    }
}

static void php_dyncache_touch(snobol_dyn_cache_php_t *intern, const char *key, size_t key_len) {
    HashTable *ao = Z_ARRVAL_P(&intern->access_order);
    uint32_t count = zend_hash_num_elements(ao);

    for (uint32_t i = 0; i < count; i++) {
        zval *zv = zend_hash_index_find(ao, i);
        if (zv && Z_STRLEN_P(zv) == key_len &&
            memcmp(Z_STRVAL_P(zv), key, key_len) == 0) {
            zval kv;
            ZVAL_STRINGL(&kv, key, key_len);
            Z_TRY_ADDREF_P(&kv);
            zend_hash_index_del(ao, i);
            count = zend_hash_num_elements(ao);
            for (uint32_t j = i; j < count; j++) {
                zval *next = zend_hash_index_find(ao, j + 1);
                if (next) {
                    zval copy;
                    ZVAL_COPY(&copy, next);
                    zend_hash_index_update(ao, j, &copy);
                }
            }
            zend_hash_index_del(ao, count);
            zend_hash_next_index_insert(ao, &kv);
            zval_ptr_dtor(&kv);
            return;
        }
    }
}

/* Argument info */
ZEND_BEGIN_ARG_INFO_EX(ai_dyn_cache_construct, 0, 0, 0)
    ZEND_ARG_TYPE_INFO(0, capacity, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_dyn_cache_compile, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, pattern_source, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_dyn_cache_evaluate, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, pattern_source, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, subject, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_dyn_cache_get, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, pattern_source, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_dyn_cache_clear, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_dyn_cache_stats, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Snobol_DynamicPatternCache, __construct) {
    zend_long capacity = DYN_CACHE_DEFAULT_CAPACITY;
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(capacity)
    ZEND_PARSE_PARAMETERS_END();

    if (capacity < 0) {
        zend_throw_exception(zend_ce_value_error,
            "Cache capacity must be non-negative", 0);
        RETURN_NULL();
    }

    snobol_dyn_cache_php_t *intern = php_dyncache_fetch(Z_OBJ_P(getThis()));
    intern->capacity = (capacity > 0) ? capacity : DYN_CACHE_DEFAULT_CAPACITY;
}

PHP_METHOD(Snobol_DynamicPatternCache, compile) {
    char *pattern_source; size_t pattern_source_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(pattern_source, pattern_source_len)
    ZEND_PARSE_PARAMETERS_END();

    snobol_dyn_cache_php_t *intern = php_dyncache_fetch(Z_OBJ_P(getThis()));
    intern->compile_count++;

    zval *cached = zend_hash_str_find(Z_ARRVAL_P(&intern->cache),
        pattern_source, pattern_source_len);
    if (cached) {
        php_dyncache_touch(intern, pattern_source, pattern_source_len);
        array_init(return_value);
        add_assoc_bool(return_value, "cached", 1);
        add_assoc_stringl(return_value, "pattern", pattern_source, pattern_source_len);
        add_assoc_bool(return_value, "compiled", 1);
        return;
    }

    zval args[2], retval;
    ZVAL_STRINGL(&args[0], pattern_source, pattern_source_len);
    array_init(&args[1]);

    zend_call_method(NULL, snobol_pattern_ce, NULL,
        "fromString", sizeof("fromString")-1, &retval, 2, &args[0], &args[1]);
    if (Z_TYPE(retval) == IS_OBJECT) {
        if (zend_hash_num_elements(Z_ARRVAL_P(&intern->cache)) >= (uint32_t)intern->capacity) {
            php_dyncache_evict_lru(intern);
        }
        zval copy;
        ZVAL_COPY(&copy, &retval);
        zend_hash_str_update(Z_ARRVAL_P(&intern->cache),
            pattern_source, pattern_source_len, &copy);

        zval kv;
        ZVAL_STRINGL(&kv, pattern_source, pattern_source_len);
        zend_hash_next_index_insert(Z_ARRVAL_P(&intern->access_order), &kv);

        array_init(return_value);
        add_assoc_bool(return_value, "cached", 0);
        add_assoc_stringl(return_value, "pattern", pattern_source, pattern_source_len);
        add_assoc_bool(return_value, "compiled", 1);
        zval_ptr_dtor(&retval);
    } else {
        /* fromString threw — clear the pending exception so we can return
           a structured error result instead of propagating the throw. */
        if (EG(exception)) zend_clear_exception();
        array_init(return_value);
        add_assoc_bool(return_value, "cached", 0);
        add_assoc_stringl(return_value, "pattern", pattern_source, pattern_source_len);
        add_assoc_bool(return_value, "compiled", 0);
        add_assoc_string(return_value, "error", "Pattern compilation failed");
    }
    zval_ptr_dtor(&args[0]);
    zval_ptr_dtor(&args[1]);
}

PHP_METHOD(Snobol_DynamicPatternCache, evaluate) {
    char *pattern_source, *subject;
    size_t pattern_source_len, subject_len;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(pattern_source, pattern_source_len)
        Z_PARAM_STRING(subject, subject_len)
    ZEND_PARSE_PARAMETERS_END();

    snobol_dyn_cache_php_t *intern = php_dyncache_fetch(Z_OBJ_P(getThis()));
    intern->evaluate_count++;

    zval *cached = zend_hash_str_find(Z_ARRVAL_P(&intern->cache),
        pattern_source, pattern_source_len);
    bool was_cached = (cached != NULL);
    zval pattern_zv;

    if (cached) {
        ZVAL_COPY(&pattern_zv, cached);
        php_dyncache_touch(intern, pattern_source, pattern_source_len);
    } else {
        zval args[2], retval;
        ZVAL_STRINGL(&args[0], pattern_source, pattern_source_len);
        array_init(&args[1]);

        zend_call_method(NULL, snobol_pattern_ce, NULL,
            "fromString", sizeof("fromString")-1, &retval, 2, &args[0], &args[1]);
        if (Z_TYPE(retval) != IS_OBJECT) {
            /* fromString threw — clear the pending exception so we can return
               a structured error result instead of propagating the throw. */
            if (EG(exception)) zend_clear_exception();
            array_init(return_value);
            add_assoc_bool(return_value, "cached", 0);
            add_assoc_bool(return_value, "evaluated", 0);
            add_assoc_string(return_value, "error", "Pattern compilation failed");
            zval_ptr_dtor(&args[0]);
            zval_ptr_dtor(&args[1]);
            return;
        }
        intern->compile_count++;

        ZVAL_COPY(&pattern_zv, &retval);

        if (zend_hash_num_elements(Z_ARRVAL_P(&intern->cache)) >= (uint32_t)intern->capacity) {
            php_dyncache_evict_lru(intern);
        }

        zval copy;
        ZVAL_COPY(&copy, &retval);
        zend_hash_str_update(Z_ARRVAL_P(&intern->cache),
            pattern_source, pattern_source_len, &copy);
        zval kv;
        ZVAL_STRINGL(&kv, pattern_source, pattern_source_len);
        zend_hash_next_index_insert(Z_ARRVAL_P(&intern->access_order), &kv);

        zval_ptr_dtor(&retval);
        zval_ptr_dtor(&args[0]);
        zval_ptr_dtor(&args[1]);
    }

    zval args[1], match_ret;
    ZVAL_STRINGL(&args[0], subject, subject_len);
    zend_call_method_with_1_params(Z_OBJ_P(&pattern_zv),
        Z_OBJCE_P(&pattern_zv), NULL, "match", &match_ret, &args[0]);

    bool matched = (Z_TYPE(match_ret) != IS_FALSE && Z_TYPE(match_ret) != IS_NULL);
    array_init(return_value);
    add_assoc_bool(return_value, "cached", was_cached ? 1 : 0);
    add_assoc_bool(return_value, "evaluated", matched);
    if (matched && Z_TYPE(match_ret) == IS_ARRAY) {
        snobol_assoc_zval(return_value, "matches", 7, &match_ret);
    } else {
        add_assoc_null(return_value, "matches");
    }
    zval_ptr_dtor(&match_ret);

    zval_ptr_dtor(&args[0]);
    zval_ptr_dtor(&pattern_zv);
}

PHP_METHOD(Snobol_DynamicPatternCache, get) {
    char *pattern_source; size_t pattern_source_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(pattern_source, pattern_source_len)
    ZEND_PARSE_PARAMETERS_END();

    snobol_dyn_cache_php_t *intern = php_dyncache_fetch(Z_OBJ_P(getThis()));

    zval *cached = zend_hash_str_find(Z_ARRVAL_P(&intern->cache),
        pattern_source, pattern_source_len);
    if (cached) {
        php_dyncache_touch(intern, pattern_source, pattern_source_len);
        array_init(return_value);
        add_assoc_bool(return_value, "found", 1);
        snobol_assoc_zval(return_value, "pattern", 7, cached);
    } else {
        array_init(return_value);
        add_assoc_bool(return_value, "found", 0);
        add_assoc_null(return_value, "pattern");
    }
}

PHP_METHOD(Snobol_DynamicPatternCache, clear) {
    ZEND_PARSE_PARAMETERS_NONE();
    snobol_dyn_cache_php_t *intern = php_dyncache_fetch(Z_OBJ_P(getThis()));
    zend_hash_clean(Z_ARRVAL_P(&intern->cache));
    zend_hash_clean(Z_ARRVAL_P(&intern->access_order));
}

PHP_METHOD(Snobol_DynamicPatternCache, stats) {
    ZEND_PARSE_PARAMETERS_NONE();
    snobol_dyn_cache_php_t *intern = php_dyncache_fetch(Z_OBJ_P(getThis()));
    array_init(return_value);
    add_assoc_long(return_value, "size",
        (zend_long)zend_hash_num_elements(Z_ARRVAL_P(&intern->cache)));
    add_assoc_long(return_value, "max_size", intern->capacity);
    add_assoc_long(return_value, "compile_count", intern->compile_count);
    add_assoc_long(return_value, "evaluate_count", intern->evaluate_count);
}

static const zend_function_entry snobol_dynamic_pattern_cache_methods[] = {
    PHP_ME(Snobol_DynamicPatternCache, __construct, ai_dyn_cache_construct, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_DynamicPatternCache, compile,     ai_dyn_cache_compile,   ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_DynamicPatternCache, evaluate,    ai_dyn_cache_evaluate,  ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_DynamicPatternCache, get,         ai_dyn_cache_get,       ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_DynamicPatternCache, clear,       ai_dyn_cache_clear,      ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_DynamicPatternCache, stats,       ai_dyn_cache_stats,      ZEND_ACC_PUBLIC)
    PHP_FE_END
};

void snobol_dynamic_pattern_cache_php_minit(void) {
    zend_class_entry ce;
    memcpy(&snobol_dynamic_pattern_cache_object_handlers, zend_get_std_object_handlers(),
        sizeof(zend_object_handlers));
    snobol_dynamic_pattern_cache_object_handlers.offset = XtOffsetOf(snobol_dyn_cache_php_t, std);
    snobol_dynamic_pattern_cache_object_handlers.free_obj = php_dyncache_free;
    INIT_CLASS_ENTRY(ce, "Snobol\\DynamicPatternCache", snobol_dynamic_pattern_cache_methods);
    snobol_dynamic_pattern_cache_ce = zend_register_internal_class(&ce);
    snobol_dynamic_pattern_cache_ce->create_object = php_dyncache_create;
}
