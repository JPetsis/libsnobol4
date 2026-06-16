#include "php.h"
#include "php_snobol.h"
#include "snobol/array.h"
#include "zend_exceptions.h"

#include <stdio.h>
#include <string.h>

#define SNOBOL_LOG(fmt, ...) ((void)0)

typedef struct {
    snobol_array_t *array;
    zend_object std;
} snobol_array_php_t;

extern zend_class_entry *snobol_array_ce;
static zend_object_handlers snobol_array_object_handlers;

static inline snobol_array_php_t* php_snobol_array_fetch(zend_object *obj) {
    return (snobol_array_php_t *)((char *)(obj) - XtOffsetOf(snobol_array_php_t, std));
}

static snobol_array_t **global_php_arrays = NULL;
static size_t global_php_array_count = 0;
static size_t global_php_array_cap = 0;

static void php_snobol_array_register(snobol_array_t *arr) {
    if (global_php_array_count == global_php_array_cap) {
        global_php_array_cap = global_php_array_cap ? global_php_array_cap * 2 : 16;
        global_php_arrays = realloc(global_php_arrays, global_php_array_cap * sizeof(snobol_array_t *));
    }
    global_php_arrays[global_php_array_count++] = arr;
}

static void php_snobol_array_unregister(snobol_array_t *arr) {
    for (size_t i = 0; i < global_php_array_count; i++) {
        if (global_php_arrays[i] == arr) {
            memmove(global_php_arrays + i, global_php_arrays + i + 1, (global_php_array_count - i - 1) * sizeof(snobol_array_t *));
            global_php_array_count--;
            break;
        }
    }
}

size_t php_snobol_get_all_arrays(snobol_array_t ***out_arrays) {
    if (out_arrays) *out_arrays = global_php_arrays;
    return global_php_array_count;
}

static void snobol_array_php_free(zend_object *object) {
    snobol_array_php_t *intern = php_snobol_array_fetch(object);
    SNOBOL_LOG("snobol_array_php_free: intern=%p array=%p", (void*)intern, (void*)intern->array);

    if (intern->array) {
        php_snobol_array_unregister(intern->array);
        snobol_array_release(intern->array);
        intern->array = NULL;
    }

    zend_object_std_dtor(object);
}

static zend_object *snobol_array_php_create(zend_class_entry *ce) {
    snobol_array_php_t *intern = zend_object_alloc(sizeof(snobol_array_php_t), ce);
    intern->array = NULL;

    zend_object_std_init(&intern->std, ce);
    object_properties_init(&intern->std, ce);
    intern->std.handlers = &snobol_array_object_handlers;

    return &intern->std;
}

ZEND_BEGIN_ARG_INFO_EX(ai_array_construct, 0, 0, 0)
    ZEND_ARG_TYPE_INFO(0, size, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_array_get, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, key, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_array_set, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, key, IS_LONG, 0)
    ZEND_ARG_TYPE_INFO(0, value, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_array_has, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, key, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_array_delete, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, key, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_array_clear, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_array_size, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_array_keys, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_array_values, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Snobol_Array_, __construct) {
    zend_long size = 0;

    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(size)
    ZEND_PARSE_PARAMETERS_END();

    snobol_array_php_t *intern = php_snobol_array_fetch(Z_OBJ_P(getThis()));

    intern->array = snobol_array_create((int32_t)size);

    if (!intern->array) {
        zend_throw_exception(zend_ce_exception, "Failed to create array", 0);
        RETURN_NULL();
    }

    php_snobol_array_register(intern->array);
}

PHP_METHOD(Snobol_Array_, get) {
    zend_long key;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(key)
    ZEND_PARSE_PARAMETERS_END();

    snobol_array_php_t *intern = php_snobol_array_fetch(Z_OBJ_P(getThis()));

    if (!intern->array) {
        zend_throw_exception(zend_ce_exception, "Array not initialized", 0);
        RETURN_NULL();
    }

    const char *value = snobol_array_get(intern->array, (int32_t)key);

    if (!value) {
        RETURN_NULL();
    }

    RETVAL_STRING(value);
}

PHP_METHOD(Snobol_Array_, set) {
    zend_long key;
    char *value;
    size_t value_len;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_LONG(key)
        Z_PARAM_STRING(value, value_len)
    ZEND_PARSE_PARAMETERS_END();

    snobol_array_php_t *intern = php_snobol_array_fetch(Z_OBJ_P(getThis()));

    if (!intern->array) {
        zend_throw_exception(zend_ce_exception, "Array not initialized", 0);
        RETURN_FALSE;
    }

    bool result = snobol_array_set(intern->array, (int32_t)key, value);

    if (!result) {
        zend_throw_exception(zend_ce_exception, "Failed to set array value", 0);
        RETURN_FALSE;
    }

    RETURN_TRUE;
}

PHP_METHOD(Snobol_Array_, has) {
    zend_long key;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(key)
    ZEND_PARSE_PARAMETERS_END();

    snobol_array_php_t *intern = php_snobol_array_fetch(Z_OBJ_P(getThis()));

    if (!intern->array) {
        zend_throw_exception(zend_ce_exception, "Array not initialized", 0);
        RETURN_FALSE;
    }

    bool result = snobol_array_has(intern->array, (int32_t)key);
    RETURN_BOOL(result);
}

PHP_METHOD(Snobol_Array_, delete) {
    zend_long key;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(key)
    ZEND_PARSE_PARAMETERS_END();

    snobol_array_php_t *intern = php_snobol_array_fetch(Z_OBJ_P(getThis()));

    if (!intern->array) {
        zend_throw_exception(zend_ce_exception, "Array not initialized", 0);
        RETURN_FALSE;
    }

    bool result = snobol_array_delete(intern->array, (int32_t)key);
    RETURN_BOOL(result);
}

PHP_METHOD(Snobol_Array_, clear) {
    ZEND_PARSE_PARAMETERS_NONE();

    snobol_array_php_t *intern = php_snobol_array_fetch(Z_OBJ_P(getThis()));

    if (!intern->array) {
        zend_throw_exception(zend_ce_exception, "Array not initialized", 0);
        RETURN_NULL();
    }

    snobol_array_clear(intern->array);
}

PHP_METHOD(Snobol_Array_, size) {
    ZEND_PARSE_PARAMETERS_NONE();

    snobol_array_php_t *intern = php_snobol_array_fetch(Z_OBJ_P(getThis()));

    if (!intern->array) {
        zend_throw_exception(zend_ce_exception, "Array not initialized", 0);
        RETURN_LONG(0);
    }

    size_t size = snobol_array_size(intern->array);
    RETURN_LONG((zend_long)size);
}

PHP_METHOD(Snobol_Array_, keys) {
    ZEND_PARSE_PARAMETERS_NONE();

    snobol_array_php_t *intern = php_snobol_array_fetch(Z_OBJ_P(getThis()));

    if (!intern->array) {
        zend_throw_exception(zend_ce_exception, "Array not initialized", 0);
        RETURN_NULL();
    }

    size_t count;
    int32_t *keys = snobol_array_keys(intern->array, &count);

    array_init(return_value);
    for (size_t i = 0; i < count; i++) {
        add_next_index_long(return_value, (zend_long)keys[i]);
    }

    if (keys) {
        free(keys);
    }
}

PHP_METHOD(Snobol_Array_, values) {
    ZEND_PARSE_PARAMETERS_NONE();

    snobol_array_php_t *intern = php_snobol_array_fetch(Z_OBJ_P(getThis()));

    if (!intern->array) {
        zend_throw_exception(zend_ce_exception, "Array not initialized", 0);
        RETURN_NULL();
    }

    size_t count;
    char **values = snobol_array_values(intern->array, &count);

    array_init(return_value);
    for (size_t i = 0; i < count; i++) {
        if (values[i]) {
            add_next_index_string(return_value, values[i]);
            free(values[i]);
        }
    }

    if (values) {
        free(values);
    }
}

static const zend_function_entry snobol_array_methods[] = {
    PHP_ME(Snobol_Array_, __construct, ai_array_construct, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Array_, get, ai_array_get, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Array_, set, ai_array_set, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Array_, has, ai_array_has, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Array_, delete, ai_array_delete, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Array_, clear, ai_array_clear, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Array_, size, ai_array_size, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Array_, keys, ai_array_keys, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Array_, values, ai_array_values, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

zend_class_entry *snobol_array_ce;

void snobol_array_php_minit(void) {
    SNOBOL_LOG("snobol_array_php_minit: START");
    zend_class_entry ce;

    memcpy(&snobol_array_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    snobol_array_object_handlers.offset = XtOffsetOf(snobol_array_php_t, std);
    snobol_array_object_handlers.free_obj = snobol_array_php_free;

    INIT_CLASS_ENTRY(ce, "Snobol\\Array_", snobol_array_methods);
    snobol_array_ce = zend_register_internal_class(&ce);
    snobol_array_ce->create_object = snobol_array_php_create;

    SNOBOL_LOG("snobol_array_php_minit: DONE");
}
