#ifndef SNOBOL_COMPILER_H
#define SNOBOL_COMPILER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* AST node types for C-based compilation.
   See snobol_ast.h for the full AST node definitions.
   
   The compiler accepts both:
   - C AST (ast_node_t*) for new C-based parser
   - PHP zval* for backward compatibility during migration
*/

#include "php.h"
#include "snobol_ast.h"

/* New C AST-based compilation */
int compile_ast_to_bytecode_c(ast_node_t* ast, zval *options, uint8_t **out_bc, size_t *out_len);

/* Legacy PHP AST compilation (for migration) */
int compile_ast_to_bytecode(zval *ast, zval *options, uint8_t **out_bc, size_t *out_len);

int compile_template_to_bytecode(const char *tpl, size_t len, uint8_t **out_bc, size_t *out_len);

/* helper to free bc */
void compiler_free(uint8_t *bc);

#endif /* SNOBOL_COMPILER_H */