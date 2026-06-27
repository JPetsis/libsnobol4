#ifndef PHP_SNOBOL_H
#define PHP_SNOBOL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"

/* Forward declare AST type to avoid circular dependency */
typedef struct ast_node ast_node_t;

extern zend_module_entry snobol_module_entry;
#define phpext_snobol_ptr &snobol_module_entry

#define PHP_SNOBOL_VERSION "0.11.0"

PHP_MINIT_FUNCTION(snobol);

/* Forward declarations for AST compilation */
int compile_ast_to_bytecode(zval *ast, zval *options, uint8_t **out_bc, size_t *out_len);
int compile_ast_to_bytecode_wrapper(ast_node_t *ast, zval *options, uint8_t **out_bc, size_t *out_len);

/* Table registry access */
typedef struct snobol_table snobol_table_t;
size_t php_snobol_get_all_tables(snobol_table_t ***out_tables);

/* PHP 8.5 removed the refcount increment from add_assoc_zval. Use this helper
   instead when storing reference-counted zvals (arrays, objects). */
static zend_always_inline void snobol_assoc_zval(zval *arr, const char *key,
                                                   size_t key_len, zval *value) {
    zval copy;
    ZVAL_COPY(&copy, value);
    zend_hash_str_update(Z_ARRVAL_P(arr), key, key_len, &copy);
}

/* Core object struct: zend_object at the END */
typedef struct snobol_pattern {
    uint8_t *bc;
    size_t bc_len;
#ifdef SNOBOL_JIT
    bool jit_enabled;
#endif
    zend_object std;
} snobol_pattern_t;

static inline snobol_pattern_t* php_snobol_fetch(zend_object *obj) {
    return (snobol_pattern_t *)((char *)(obj) - XtOffsetOf(snobol_pattern_t, std));
}

/* Core search loop used by Pattern::searchAll and PatternHelper::matchAll */
void php_snobol_do_search_all(snobol_pattern_t *intern,
                               const char *subject_val, size_t subject_len,
                               zval *result);

#endif /* PHP_SNOBOL_H */
