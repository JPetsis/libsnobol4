#pragma once

#ifdef __cplusplus
extern "C" {
#endif


#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
 * @param out_bc Output bytecode buffer (allocated, caller must free via
 * compiler_free())
 * @param out_len Output bytecode length
 * @return 0 on success, -1 on error
 */
int compile_ast_to_bytecode_c(ast_node_t *ast, bool case_insensitive,
                              uint8_t **out_bc, size_t *out_len);

/**
 * Compile template string to bytecode
 * @param tpl Template string
 * @param len Template length
 * @param out_bc Output bytecode buffer (allocated, caller must free via
 * compiler_free())
 * @param out_len Output bytecode length
 * @return 0 on success, -1 on error
 */
int compile_template_to_bytecode(const char *tpl, size_t len, uint8_t **out_bc,
                                 size_t *out_len);

/**
 * Free compiled bytecode
 * @param bc Bytecode buffer to free
 */
void compiler_free(uint8_t *bc);

/**
 * Resolve symbolic table names embedded in compiled template bytecode.
 *
 * Walks @p bc looking for OP_EMIT_TABLE instructions whose table_id equals
 * SNBL_TABLE_ID_UNBOUND (0xFFFF).  For each such instruction the embedded
 * name is compared against the @p names array; on a match the table_id field
 * is patched in-place with the corresponding entry from @p ids.
 *
 * The function patches as many entries as it can.  If any name cannot be
 * resolved it leaves the table_id at SNBL_TABLE_ID_UNBOUND and returns -1
 * after processing the entire bytecode.
 *
 * Binding is a one-shot operation: already-bound entries (table_id != 0xFFFF)
 * are left unchanged.  The caller is responsible for external synchronisation
 * if @p bc may be shared across threads.
 *
 * @param bc      Mutable compiled template bytecode buffer
 * @param bc_len  Length of @p bc in bytes
 * @param names   Array of @p n C-string table names (may be NULL entries)
 * @param ids     Array of @p n table IDs corresponding to @p names
 * @param n       Number of entries in @p names and @p ids
 * @return 0 on complete success, -1 if any name was unresolvable
 */
int snobol_template_bind_tables(uint8_t *bc, size_t bc_len, const char **names,
                                const uint16_t *ids, size_t n);

#ifdef __cplusplus
}
#endif
