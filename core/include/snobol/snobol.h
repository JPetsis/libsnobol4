#pragma once

/**
 * @file snobol.h
 * @brief Main API entry point for libsnobol4
 *
 * libsnobol4 is a high-performance C library implementing SNOBOL4-style
 * pattern matching. This header provides the main API entry point,
 * re-exporting common types and functions for convenient single-include usage.
 *
 * @version 0.1.0
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Version macros */
/** @brief Library major version number. */
#define SNOBOL_VERSION_MAJOR 0
/** @brief Library minor version number. */
#define SNOBOL_VERSION_MINOR 8
/** @brief Library patch version number. */
#define SNOBOL_VERSION_PATCH 0
/** @brief Library version as a string literal, e.g. @c "0.8.0". */
#define SNOBOL_VERSION_STRING "0.8.0"

/**
 * Get library version at runtime (three separate integers)
 * @param major Output parameter for major version (can be NULL)
 * @param minor Output parameter for minor version (can be NULL)
 * @param patch Output parameter for patch version (can be NULL)
 */
void snobol_version(int* major, int* minor, int* patch);

/**
 * Get library API version as a single encoded integer.
 *
 * Encoding: (MAJOR << 16) | (MINOR << 8) | PATCH
 *
 * Bindings should compare `snobol_get_api_version() >> 16` against their
 * compile-time SNOBOL_VERSION_MAJOR to detect incompatible library upgrades.
 *
 * @return Encoded API version uint32_t.
 *         For v0.7.0 this returns 0x00000700u.
 */
uint32_t snobol_get_api_version(void);

/* Include common headers */
#include "snobol/ast.h"
#include "snobol/lexer.h"
#include "snobol/parser.h"
#include "snobol/compiler.h"
#include "snobol/vm.h"
#include "snobol/table.h"
#include "snobol/dynamic_pattern.h"
#include "snobol/jit.h"
#include "snobol/string_fn.h"
#include "snobol/type_fn.h"
#include "snobol/search.h"

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
/**
 * @brief Create a new pattern matching context.
 *
 * The context owns all compiled patterns and associated resources.
 * Must be destroyed with snobol_context_destroy() when no longer needed.
 *
 * @return New context, or NULL on allocation failure.
 */
snobol_context_t* snobol_context_create(void);

/**
 * @brief Destroy a pattern matching context and free all associated resources.
 *
 * All patterns compiled under this context are invalidated.
 * Passing NULL is safe (no-op).
 *
 * @param[in] ctx Context to destroy.
 */
void snobol_context_destroy(snobol_context_t* ctx);

/* -----------------------------------------------------------------------
 * Pattern compilation flags
 * ----------------------------------------------------------------------- */

/**
 * Make literal and character-class matching case-insensitive for ASCII and
 * Latin-1 Supplement (U+0041–U+005A / U+0061–U+007A; U+00C0–U+00FF pairs).
 * Captured text preserves the original subject case.
 *
 * JIT is disabled when this flag is set; patterns fall back to the interpreter.
 * Pass to snobol_pattern_compile_ex() via the flags bitmask.
 */
#define SNOBOL_FLAG_CASE_INSENSITIVE  0x0001u

/* Pattern compilation */
/**
 * @brief Compile a SNOBOL4 pattern string to a compiled pattern object.
 *
 * Equivalent to snobol_pattern_compile_ex() with @p flags = 0.
 *
 * @param[in]  ctx    Context that will own the returned pattern.
 * @param[in]  source Pattern source text (UTF-8).
 * @param[in]  len    Byte length of @p source.
 * @param[out] error  On failure, *error is set to a malloc'd error string that
 *                    the caller must free.  Set to NULL on success.
 * @return Compiled pattern owned by @p ctx, or NULL on parse/compile error.
 *         Free with snobol_pattern_free() before destroying the context.
 */
snobol_pattern_t* snobol_pattern_compile(snobol_context_t* ctx, const char* source, size_t len, char** error);

/**
 * Compile a pattern with option flags.
 *
 * @param ctx    Context that owns the returned pattern.
 * @param source Pattern source text (UTF-8).
 * @param len    Byte length of source.
 * @param flags  Bitmask of SNOBOL_FLAG_* constants (unknown bits are ignored).
 * @param error  On failure, *error is set to a malloc'd error string.
 *               On success, *error is set to NULL.  Caller must free on error.
 * @return Compiled pattern owned by ctx, or NULL on parse/compile error.
 *         Free with snobol_pattern_free() before destroying the context.
 */
snobol_pattern_t* snobol_pattern_compile_ex(snobol_context_t* ctx, const char* source, size_t len, uint32_t flags, char** error);

/**
 * @brief Free a compiled pattern.
 *
 * @param[in] pattern Pattern to free.  NULL is safe.
 */
void snobol_pattern_free(snobol_pattern_t* pattern);

/* Pattern matching */
/**
 * @brief Execute a compiled pattern against a subject string.
 *
 * @param[in] pattern Compiled pattern to execute.
 * @param[in] subject Subject string (UTF-8).
 * @param[in] len     Byte length of @p subject.
 * @return New match result that the caller must free with snobol_match_free().
 *         Always returns a valid pointer; call snobol_match_success() to
 *         determine whether the match succeeded.
 */
snobol_match_t* snobol_pattern_match(snobol_pattern_t* pattern, const char* subject, size_t len);

/**
 * @brief Free a match result.
 *
 * @param[in] match Match result to free.  NULL is safe.
 */
void snobol_match_free(snobol_match_t* match);

/* Match result access */
/**
 * @brief Return whether a match result represents a successful match.
 *
 * @param[in] match Match result from snobol_pattern_match().
 * @return true if the pattern matched; false otherwise.
 */
bool snobol_match_success(snobol_match_t* match);

/**
 * @brief Retrieve the output buffer produced by a successful match.
 *
 * The output contains any text emitted by @c OP_EMIT_LITERAL / @c OP_EMIT_CAPTURE
 * instructions during the match.
 *
 * @param[in]  match Match result from snobol_pattern_match().
 * @param[out] len   If non-NULL, set to the byte length of the returned string.
 * @return Pointer to the output string (owned by @p match, do not free),
 *         or NULL if there is no output.
 */
const char* snobol_match_get_output(snobol_match_t* match, size_t* len);

/**
 * @brief Retrieve the value of a named capture variable from a match result.
 *
 * @param[in]  match Match result from snobol_pattern_match().
 * @param[in]  name  Variable name (NUL-terminated).
 * @param[out] len   If non-NULL, set to the byte length of the returned string.
 * @return Pointer to the variable value (owned by @p match, do not free),
 *         or NULL if the variable was not set during the match.
 */
const char* snobol_match_get_variable(snobol_match_t* match, const char* name, size_t* len);

