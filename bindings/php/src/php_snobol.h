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

#define PHP_SNOBOL_VERSION "0.1.0"

PHP_MINIT_FUNCTION(snobol);

/* Forward declarations for AST compilation */
int compile_ast_to_bytecode(zval *ast, zval *options, uint8_t **out_bc, size_t *out_len);
int compile_ast_to_bytecode_wrapper(ast_node_t *ast, zval *options, uint8_t **out_bc, size_t *out_len);

#endif /* PHP_SNOBOL_H */
