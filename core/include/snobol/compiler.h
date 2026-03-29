#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @file compiler.h
 * @brief Compiler for SNOBOL pattern AST to bytecode
 *
 * Accepts C AST (ast_node_t*) from the C-based parser and compiles to bytecode.
 */

#include "snobol/ast.h"

/**
 * Compile AST to bytecode
 * @param ast AST node to compile
 * @param case_insensitive Enable case-insensitive matching
 * @param out_bc Output bytecode buffer (allocated, caller must free via compiler_free())
 * @param out_len Output bytecode length
 * @return 0 on success, -1 on error
 */
int compile_ast_to_bytecode_c(ast_node_t* ast, bool case_insensitive, uint8_t **out_bc, size_t *out_len);

/**
 * Compile template string to bytecode
 * @param tpl Template string
 * @param len Template length
 * @param out_bc Output bytecode buffer (allocated, caller must free via compiler_free())
 * @param out_len Output bytecode length
 * @return 0 on success, -1 on error
 */
int compile_template_to_bytecode(const char *tpl, size_t len, uint8_t **out_bc, size_t *out_len);

/**
 * Free compiled bytecode
 * @param bc Bytecode buffer to free
 */
void compiler_free(uint8_t *bc);

