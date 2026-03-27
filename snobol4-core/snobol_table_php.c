/**
 * @file snobol_table_php.c
 * @brief PHP bindings for SNOBOL4 runtime tables
 * 
 * Provides thin PHP wrappers over the C core table implementation.
 * All semantics live in the C core - PHP is just a façade.
 */

#include "php.h"
#include "php_snobol.h"
#include "snobol_table.h"
#include "zend_exceptions.h"

#include <stdio.h>
#include <string.h>

/* Disable logging for now */
#define SNOBOL_LOG(fmt, ...) ((void)0)

/* Standard PHP Custom Object Pattern: zend_object at the END */
typedef struct {
    snobol_table_t *table;  /* Owned: the C table object */
    zend_object std;
} snobol_table_php_t;

extern zend_class_entry *snobol_table_ce;
static zend_object_handlers snobol_table_object_handlers;

static inline snobol_table_php_t* php_snobol_table_fetch(zend_object *obj) {
    return (snobol_table_php_t *)((char *)(obj) - XtOffsetOf(snobol_table_php_t, std));
}

static void snobol_table_php_free(zend_object *object) {
    snobol_table_php_t *intern = php_snobol_table_fetch(object);
    SNOBOL_LOG("snobol_table_php_free: intern=%p table=%p", (void*)intern, (void*)intern->table);

    if (intern->table) {
        table_release(intern->table);
        intern->table = NULL;
    }

    zend_object_std_dtor(object);
    SNOBOL_LOG("snobol_table_php_free: done");
}

static zend_object *snobol_table_php_create(zend_class_entry *ce) {
    snobol_table_php_t *intern = zend_object_alloc(sizeof(snobol_table_php_t), ce);
    SNOBOL_LOG("snobol_table_php_create: intern=%p", (void*)intern);

    intern->table = NULL;

    zend_object_std_init(&intern->std, ce);
    object_properties_init(&intern->std, ce);
    intern->std.handlers = &snobol_table_object_handlers;

    return &intern->std;
}

/* Argument info */
ZEND_BEGIN_ARG_INFO_EX(ai_table_construct, 0, 0, 0)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_table_get, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_table_set, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, value, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_table_has, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_table_delete, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_table_clear, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_table_size, 0, 0, 0)
ZEND_END_ARG_INFO()

/* PHP Methods */

PHP_METHOD(Snobol_Table, __construct) {
    char *name = NULL;
    size_t name_len = 0;
    
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();

    snobol_table_php_t *intern = php_snobol_table_fetch(Z_OBJ_P(getThis()));
    
    const char *table_name = name ? name : NULL;
    intern->table = table_create(table_name);
    
    if (!intern->table) {
        zend_throw_exception(zend_ce_exception, "Failed to create table", 0);
        RETURN_NULL();
    }
    
    SNOBOL_LOG("Snobol_Table::__construct: table=%p name=%s", 
               (void*)intern->table, table_name ? table_name : "(unnamed)");
}

PHP_METHOD(Snobol_Table, get) {
    char *key;
    size_t key_len;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(key, key_len)
    ZEND_PARSE_PARAMETERS_END();

    snobol_table_php_t *intern = php_snobol_table_fetch(Z_OBJ_P(getThis()));
    
    if (!intern->table) {
        zend_throw_exception(zend_ce_exception, "Table not initialized", 0);
        RETURN_NULL();
    }
    
    const char *value = table_get(intern->table, key);
    
    if (!value) {
        RETURN_NULL();
    }
    
    RETVAL_STRING(value);
}

PHP_METHOD(Snobol_Table, set) {
    char *key;
    size_t key_len;
    zval *value_zv;
    
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_ZVAL(value_zv)
    ZEND_PARSE_PARAMETERS_END();

    snobol_table_php_t *intern = php_snobol_table_fetch(Z_OBJ_P(getThis()));
    
    if (!intern->table) {
        zend_throw_exception(zend_ce_exception, "Table not initialized", 0);
        RETURN_NULL();
    }
    
    const char *val_to_set = NULL;
    if (Z_TYPE_P(value_zv) == IS_NULL) {
        val_to_set = NULL;
    } else if (Z_TYPE_P(value_zv) == IS_STRING) {
        val_to_set = Z_STRVAL_P(value_zv);
    } else {
        zend_throw_exception(zend_ce_exception, "Value must be string or null", 0);
        RETURN_FALSE;
    }
    
    bool result = table_set(intern->table, key, val_to_set);
    
    if (!result) {
        zend_throw_exception(zend_ce_exception, "Failed to set table value", 0);
        RETURN_FALSE;
    }
    
    RETURN_TRUE;
}

PHP_METHOD(Snobol_Table, has) {
    char *key;
    size_t key_len;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(key, key_len)
    ZEND_PARSE_PARAMETERS_END();

    snobol_table_php_t *intern = php_snobol_table_fetch(Z_OBJ_P(getThis()));
    
    if (!intern->table) {
        zend_throw_exception(zend_ce_exception, "Table not initialized", 0);
        RETURN_FALSE;
    }
    
    bool result = table_has(intern->table, key);
    RETURN_BOOL(result);
}

PHP_METHOD(Snobol_Table, delete) {
    char *key;
    size_t key_len;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(key, key_len)
    ZEND_PARSE_PARAMETERS_END();

    snobol_table_php_t *intern = php_snobol_table_fetch(Z_OBJ_P(getThis()));
    
    if (!intern->table) {
        zend_throw_exception(zend_ce_exception, "Table not initialized", 0);
        RETURN_FALSE;
    }
    
    bool result = table_delete(intern->table, key);
    RETURN_BOOL(result);
}

PHP_METHOD(Snobol_Table, clear) {
    ZEND_PARSE_PARAMETERS_NONE();

    snobol_table_php_t *intern = php_snobol_table_fetch(Z_OBJ_P(getThis()));
    
    if (!intern->table) {
        zend_throw_exception(zend_ce_exception, "Table not initialized", 0);
        RETURN_NULL();
    }
    
    table_clear(intern->table);
}

PHP_METHOD(Snobol_Table, size) {
    ZEND_PARSE_PARAMETERS_NONE();

    snobol_table_php_t *intern = php_snobol_table_fetch(Z_OBJ_P(getThis()));
    
    if (!intern->table) {
        zend_throw_exception(zend_ce_exception, "Table not initialized", 0);
        RETURN_LONG(0);
    }
    
    size_t size = table_size(intern->table);
    RETURN_LONG((zend_long)size);
}

/* Method table */
static const zend_function_entry snobol_table_methods[] = {
    PHP_ME(Snobol_Table, __construct, ai_table_construct, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Table, get, ai_table_get, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Table, set, ai_table_set, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Table, has, ai_table_has, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Table, delete, ai_table_delete, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Table, clear, ai_table_clear, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Table, size, ai_table_size, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

zend_class_entry *snobol_table_ce;

void snobol_table_php_minit(void) {
    SNOBOL_LOG("snobol_table_php_minit: START");
    zend_class_entry ce;

    memcpy(&snobol_table_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    snobol_table_object_handlers.offset = XtOffsetOf(snobol_table_php_t, std);
    snobol_table_object_handlers.free_obj = snobol_table_php_free;

    INIT_CLASS_ENTRY(ce, "Snobol\\Table", snobol_table_methods);
    snobol_table_ce = zend_register_internal_class(&ce);
    snobol_table_ce->create_object = snobol_table_php_create;
    
    SNOBOL_LOG("snobol_table_php_minit: DONE");
}
