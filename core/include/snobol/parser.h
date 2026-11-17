#pragma once

#ifdef __cplusplus
extern "C" {
#endif


#include "snobol/ast.h"
#include "snobol/lexer.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * @file parser.h
 * @brief Recursive descent parser for SNOBOL pattern syntax
 *
 * Consumes tokens from lexer and produces AST.
 * Opaque handle pattern - implementation details hidden from callers.
 */

/**
 * Maximum error message length
 */
#define SNOBOL_PARSER_ERROR_MAX 256

/**
 * Maximum structural nesting depth of a parsed pattern.
 *
 * Each parenthesised group, label prefix, capture and function-call argument
 * consumes one recursive-descent level.  The parser rejects patterns that
 * exceed this budget with a parse error instead of letting a few kilobytes
 * of repeated '(' exhaust the C stack.
 *
 * The level count is a secondary bound: the productions have frames of very
 * different sizes, so the stack budget below is what guarantees the descent
 * fits the smallest default thread stack.  256 is a backstop; the byte
 * budget rejects the larger productions first.
 *
 * Builder-composed ASTs (snobol_pattern_build_compile) bypass the parser and
 * are not subject to this limit.
 */
enum { SNOBOL_PARSER_MAX_DEPTH = 256 };

/**
 * Maximum parser stack the recursive descent may consume, in bytes.
 *
 * A level count alone cannot bound the stack: the productions re-enter the
 * descent with frames of very different sizes (a nested ARBNO call costs
 * about 1.5x a parenthesised group, EVAL and table updates up to 2.5x), so
 * the same level count can overflow a thread whose stack is large enough
 * for the cheapest shape.  depth_enter() measures the stack consumed since
 * snobol_parser_parse() was entered and rejects the pattern once this
 * budget is spent, whatever mixture of productions the input uses.
 *
 * 256 KiB leaves half of the smallest default thread stack (512 KiB on
 * macOS) for the caller and the frames above the parse entry.
 */
enum { SNOBOL_PARSER_MAX_STACK_BYTES = 256 * 1024 };

/**
 * Maximum height of the AST the parser hands to the compiler.
 *
 * Flat alternation branches and match-naming operators extend the tree
 * without recursing the descent, so they get a larger budget than
 * SNOBOL_PARSER_MAX_DEPTH.  It still bounds the recursive walkers
 * (emit_node_c/emit_alt_c, ast_max_reg_c, snobol_ast_free).  Those are
 * cheaper per level than the descent: a 1000-branch alternation compiles
 * inside a 128 KiB thread stack in a Release build and needs about 384 KiB
 * in a Debug build, which still fits the 512 KiB target with margin.
 */
enum { SNOBOL_PARSER_MAX_AST_DEPTH = 1000 };

/**
 * Parser error information
 */
typedef struct {
  bool has_error;
  char message[SNOBOL_PARSER_ERROR_MAX];
  size_t line;
  size_t column;
} parser_error_t;

/**
 * Opaque parser state
 */
typedef struct snobol_parser snobol_parser_t;

/**
 * Create a new parser
 * @return New parser (caller owns, must free with snobol_parser_destroy)
 */
snobol_parser_t *snobol_parser_create(void);

/**
 * Parse a complete pattern from tokens
 * @param parser Parser instance
 * @param lexer Lexer instance (must be created with same source)
 * @return AST root node (caller owns), or NULL on error
 */
ast_node_t *snobol_parser_parse(snobol_parser_t *parser, snobol_lexer_t *lexer);

/**
 * Check if parser has an error
 * @param parser Parser instance
 * @return true if error occurred
 */
bool snobol_parser_has_error(snobol_parser_t *parser);

/**
 * Get parser error message
 * @param parser Parser instance
 * @return Error message (valid until parser is destroyed), or NULL if no error
 */
const char *snobol_parser_get_error(snobol_parser_t *parser);

/**
 * Get error location
 * @param parser Parser instance
 * @param line Output: line number (1-based)
 * @param column Output: column number (1-based)
 */
void snobol_parser_get_error_location(snobol_parser_t *parser, size_t *line,
                                      size_t *column);

/**
 * Clear parser error state (for reuse)
 * @param parser Parser instance
 */
void snobol_parser_clear_error(snobol_parser_t *parser);

/**
 * Destroy parser and free resources
 * @param parser Parser to destroy (NULL is safe)
 */
void snobol_parser_destroy(snobol_parser_t *parser);

#ifdef __cplusplus
}
#endif
