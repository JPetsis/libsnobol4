#ifndef SNOBOL_PARSER_H
#define SNOBOL_PARSER_H

#include "snobol_ast.h"
#include "snobol_lexer.h"
#include <stddef.h>
#include <stdbool.h>

/**
 * @file snobol_parser.h
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
snobol_parser_t* snobol_parser_create(void);

/**
 * Parse a complete pattern from tokens
 * @param parser Parser instance
 * @param lexer Lexer instance (must be created with same source)
 * @return AST root node (caller owns), or NULL on error
 */
ast_node_t* snobol_parser_parse(snobol_parser_t* parser, snobol_lexer_t* lexer);

/**
 * Check if parser has an error
 * @param parser Parser instance
 * @return true if error occurred
 */
bool snobol_parser_has_error(snobol_parser_t* parser);

/**
 * Get parser error message
 * @param parser Parser instance
 * @return Error message (valid until parser is destroyed), or NULL if no error
 */
const char* snobol_parser_get_error(snobol_parser_t* parser);

/**
 * Get error location
 * @param parser Parser instance
 * @param line Output: line number (1-based)
 * @param column Output: column number (1-based)
 */
void snobol_parser_get_error_location(snobol_parser_t* parser, size_t* line, size_t* column);

/**
 * Clear parser error state (for reuse)
 * @param parser Parser instance
 */
void snobol_parser_clear_error(snobol_parser_t* parser);

/**
 * Destroy parser and free resources
 * @param parser Parser to destroy (NULL is safe)
 */
void snobol_parser_destroy(snobol_parser_t* parser);

#endif /* SNOBOL_PARSER_H */
