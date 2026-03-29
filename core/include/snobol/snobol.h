#pragma once

/**
 * @file snobol.h
 * @brief Main API entry point for libsnobol4
 *
 * libsnobol4 is a high-performance C23 library implementing SNOBOL4-style
 * pattern matching. This header provides the main API entry point,
 * re-exporting common types and functions for convenient single-include usage.
 *
 * @version 0.1.0
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Version macros */
#define SNOBOL_VERSION_MAJOR 0
#define SNOBOL_VERSION_MINOR 1
#define SNOBOL_VERSION_PATCH 0
#define SNOBOL_VERSION_STRING "0.1.0"

/**
 * Get library version at runtime
 * @param major Output parameter for major version (can be NULL)
 * @param minor Output parameter for minor version (can be NULL)
 * @param patch Output parameter for patch version (can be NULL)
 */
void snobol_version(int* major, int* minor, int* patch);

/* Include common headers */
#include "snobol/ast.h"
#include "snobol/lexer.h"
#include "snobol/parser.h"
#include "snobol/compiler.h"
#include "snobol/vm.h"
#include "snobol/table.h"
#include "snobol/dynamic_pattern.h"
#include "snobol/jit.h"

/**
 * @brief Pattern matching context (opaque handle)
 *
 * The context owns all compiled patterns and associated resources.
 * Create with snobol_context_create(), destroy with snobol_context_destroy().
 */
typedef struct snobol_context snobol_context_t;

/**
 * @brief Compiled pattern (opaque handle)
 *
 * Represents a compiled pattern ready for matching.
 * Created via snobol_pattern_compile().
 */
typedef struct snobol_pattern snobol_pattern_t;

/**
 * @brief Match result (opaque handle)
 *
 * Contains the result of a successful pattern match,
 * including captured variables and output buffer.
 */
typedef struct snobol_match snobol_match_t;

/**
 * @brief Associative table (opaque handle)
 *
 * Runtime-owned string-keyed hash table for table-backed substitutions.
 */
typedef struct snobol_table snobol_table_t;

/* Context lifecycle */
snobol_context_t* snobol_context_create(void);
void snobol_context_destroy(snobol_context_t* ctx);

/* Pattern compilation */
snobol_pattern_t* snobol_pattern_compile(snobol_context_t* ctx, const char* source, size_t len, char** error);
void snobol_pattern_free(snobol_pattern_t* pattern);

/* Pattern matching */
snobol_match_t* snobol_pattern_match(snobol_pattern_t* pattern, const char* subject, size_t len);
void snobol_match_free(snobol_match_t* match);

/* Match result access */
bool snobol_match_success(snobol_match_t* match);
const char* snobol_match_get_output(snobol_match_t* match, size_t* len);
const char* snobol_match_get_variable(snobol_match_t* match, const char* name, size_t* len);

