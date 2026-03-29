/**
 * @file snobol_dynamic_pattern_php.c
 * @brief PHP bindings for SNOBOL4 dynamic pattern construction and evaluation
 * 
 * Provides thin PHP wrappers over the C core dynamic pattern implementation.
 * All semantics live in the C core - PHP is just a façade.
 */

#include "php.h"
#include "php_snobol.h"
#include "snobol/dynamic_pattern.h"
#include "zend_exceptions.h"

#include <stdio.h>
#include <string.h>

/* Disable logging for now */
#define SNOBOL_LOG(fmt, ...) ((void)0)

/* Standard PHP Custom Object Pattern: zend_object at the END */
typedef struct {
    dynamic_pattern_cache_t cache;  /* Owned: the C cache object */
    zend_object std;
} snobol_dynamic_pattern_cache_php_t;

extern zend_class_entry *snobol_dynamic_pattern_cache_ce;
static zend_object_handlers snobol_dynamic_pattern_cache_object_handlers;

static inline snobol_dynamic_pattern_cache_php_t* php_snobol_dyn_cache_fetch(zend_object *obj) {
    return (snobol_dynamic_pattern_cache_php_t *)((char *)(obj) - XtOffsetOf(snobol_dynamic_pattern_cache_php_t, std));
}

static void snobol_dynamic_pattern_cache_php_free(zend_object *object) {
    snobol_dynamic_pattern_cache_php_t *intern = php_snobol_dyn_cache_fetch(object);
    
    dynamic_pattern_cache_destroy(&intern->cache);
    
    zend_object_std_dtor(object);
}

static zend_object *snobol_dynamic_pattern_cache_php_create(zend_class_entry *ce) {
    snobol_dynamic_pattern_cache_php_t *intern = zend_object_alloc(sizeof(snobol_dynamic_pattern_cache_php_t), ce);

    intern->cache.buckets = NULL;
    intern->cache.bucket_count = 0;
    intern->cache.size = 0;
    intern->cache.max_size = 0;

    zend_object_std_init(&intern->std, ce);
    object_properties_init(&intern->std, ce);
    intern->std.handlers = &snobol_dynamic_pattern_cache_object_handlers;

    return &intern->std;
}

/* Argument info */
ZEND_BEGIN_ARG_INFO_EX(ai_dyn_cache_construct, 0, 0, 0)
    ZEND_ARG_TYPE_INFO(0, max_size, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_dyn_cache_compile, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, pattern_string, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_dyn_cache_evaluate, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, pattern_string, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, subject, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_dyn_cache_get, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, pattern_string, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_dyn_cache_clear, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_dyn_cache_stats, 0, 0, 0)
ZEND_END_ARG_INFO()

/* PHP Methods */

PHP_METHOD(Snobol_DynamicPatternCache, __construct) {
    zend_long max_size = 64;
    
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(max_size)
    ZEND_PARSE_PARAMETERS_END();

    snobol_dynamic_pattern_cache_php_t *intern = php_snobol_dyn_cache_fetch(Z_OBJ_P(getThis()));
    
    if (max_size <= 0) {
        max_size = 64;
    }
    
    bool result = dynamic_pattern_cache_init(&intern->cache, (size_t)max_size);
    
    if (!result) {
        zend_throw_exception(zend_ce_exception, "Failed to initialize dynamic pattern cache", 0);
        RETURN_NULL();
    }
}

PHP_METHOD(Snobol_DynamicPatternCache, compile) {
    /* This method is no longer used - DynamicPatternCache is implemented in PHP */
    /* Stub implementation for backward compatibility */
    array_init(return_value);
    add_assoc_bool(return_value, "cached", 0);
    add_assoc_string(return_value, "pattern", "stub");
    add_assoc_bool(return_value, "compiled", 0);
}

PHP_METHOD(Snobol_DynamicPatternCache, evaluate) {
    char *pattern_string;
    size_t pattern_string_len;
    char *subject;
    size_t subject_len;
    
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(pattern_string, pattern_string_len)
        Z_PARAM_STRING(subject, subject_len)
    ZEND_PARSE_PARAMETERS_END();

    snobol_dynamic_pattern_cache_php_t *intern = php_snobol_dyn_cache_fetch(Z_OBJ_P(getThis()));
    
    /* Try to get from cache */
    dynamic_pattern_t *pattern = dynamic_pattern_cache_get(&intern->cache, pattern_string, -1);
    
    if (pattern) {
        /* Pattern is cached - in full implementation, we would execute it here */
        /* For now, just indicate it was cached */
        dynamic_pattern_release(pattern);
        array_init(return_value);
        add_assoc_bool(return_value, "cached", 1);
        add_assoc_bool(return_value, "evaluated", 0);
        add_assoc_string(return_value, "message", "Dynamic pattern execution not yet fully implemented");
    } else {
        /* Not cached */
        array_init(return_value);
        add_assoc_bool(return_value, "cached", 0);
        add_assoc_bool(return_value, "evaluated", 0);
    }
}

PHP_METHOD(Snobol_DynamicPatternCache, get) {
    char *pattern_string;
    size_t pattern_string_len;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(pattern_string, pattern_string_len)
    ZEND_PARSE_PARAMETERS_END();

    snobol_dynamic_pattern_cache_php_t *intern = php_snobol_dyn_cache_fetch(Z_OBJ_P(getThis()));
    
    dynamic_pattern_t *pattern = dynamic_pattern_cache_get(&intern->cache, pattern_string, -1);
    
    if (pattern) {
        array_init(return_value);
        add_assoc_bool(return_value, "found", 1);
        add_assoc_long(return_value, "bc_len", (zend_long)pattern->bc_len);
        add_assoc_bool(return_value, "valid", pattern->is_valid);
        dynamic_pattern_release(pattern);
    } else {
        array_init(return_value);
        add_assoc_bool(return_value, "found", 0);
    }
}

PHP_METHOD(Snobol_DynamicPatternCache, clear) {
    ZEND_PARSE_PARAMETERS_NONE();

    snobol_dynamic_pattern_cache_php_t *intern = php_snobol_dyn_cache_fetch(Z_OBJ_P(getThis()));
    
    dynamic_pattern_cache_clear(&intern->cache);
}

PHP_METHOD(Snobol_DynamicPatternCache, stats) {
    ZEND_PARSE_PARAMETERS_NONE();

    snobol_dynamic_pattern_cache_php_t *intern = php_snobol_dyn_cache_fetch(Z_OBJ_P(getThis()));
    
    size_t size, max;
    dynamic_pattern_cache_stats(&intern->cache, &size, &max);
    
    array_init(return_value);
    add_assoc_long(return_value, "size", (zend_long)size);
    add_assoc_long(return_value, "max_size", (zend_long)max);
}

/* Method table */
static const zend_function_entry snobol_dynamic_pattern_cache_methods[] = {
    PHP_ME(Snobol_DynamicPatternCache, __construct, ai_dyn_cache_construct, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_DynamicPatternCache, compile, ai_dyn_cache_compile, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_DynamicPatternCache, evaluate, ai_dyn_cache_evaluate, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_DynamicPatternCache, get, ai_dyn_cache_get, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_DynamicPatternCache, clear, ai_dyn_cache_clear, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_DynamicPatternCache, stats, ai_dyn_cache_stats, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

zend_class_entry *snobol_dynamic_pattern_cache_ce;

void snobol_dynamic_pattern_cache_php_minit(void) {
    /* DynamicPatternCache is now implemented in PHP (php-src/DynamicPatternCache.php)
     * The C extension no longer registers this class to allow the PHP implementation
     * to provide truthful cache behavior with proper compile/evaluate/get methods.
     * 
     * Commented out class registration:
     * zend_class_entry ce;
     * memcpy(&snobol_dynamic_pattern_cache_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
     * snobol_dynamic_pattern_cache_object_handlers.offset = XtOffsetOf(snobol_dynamic_pattern_cache_php_t, std);
     * snobol_dynamic_pattern_cache_object_handlers.free_obj = snobol_dynamic_pattern_cache_php_free;
     * INIT_CLASS_ENTRY(ce, "Snobol\\DynamicPatternCache", snobol_dynamic_pattern_cache_methods);
     * snobol_dynamic_pattern_cache_ce = zend_register_internal_class(&ce);
     * snobol_dynamic_pattern_cache_ce->create_object = snobol_dynamic_pattern_cache_php_create;
     */
}
