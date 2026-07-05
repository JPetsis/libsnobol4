#pragma once

/**
 * @file snobol.h
 * @brief Main API entry point for libsnobol4
 *
 * libsnobol4 is a high-performance C library implementing SNOBOL4-style
 * pattern matching. This header provides the main API entry point,
 * re-exporting common types and functions for convenient single-include usage.
 *
 * @version 0.11.0
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -----------------------------------------------------------------------
 * Version macros
 * ----------------------------------------------------------------------- */
/** @brief Library major version number. */
#define SNOBOL_VERSION_MAJOR 0
/** @brief Library minor version number. */
#define SNOBOL_VERSION_MINOR 11
/** @brief Library patch version number. */
#define SNOBOL_VERSION_PATCH 0
/** @brief Library version as a string literal, e.g. @c "0.11.0". */
#define SNOBOL_VERSION_STRING "0.11.0"

/** @brief ABI version number (monotonically increasing).
 *
 *  Incremented on any breaking ABI change.  The initial value is 1.
 *  Bindings can check this at load time to detect incompatibility. */
#define SNOBOL_ABI_VERSION 1

/* -----------------------------------------------------------------------
 * Deprecation macros
 *
 * Use SNOBOL_DEPRECATED to mark public API functions that are deprecated
 * but kept for one minor version cycle before removal.  Deprecated
 * functions produce a compiler warning.
 *
 *   SNOBOL_DEPRECATED void snobol_old_func(void);
 * ----------------------------------------------------------------------- */
#if defined(__GNUC__) || defined(__clang__)
#define SNOBOL_DEPRECATED __attribute__((deprecated))
#elif defined(_MSC_VER)
#define SNOBOL_DEPRECATED __declspec(deprecated)
#else
#define SNOBOL_DEPRECATED
#endif

/**
 * Get library version at runtime (three separate integers)
 * @param major Output parameter for major version (can be NULL)
 * @param minor Output parameter for minor version (can be NULL)
 * @param patch Output parameter for patch version (can be NULL)
 */
void snobol_version(int *major, int *minor, int *patch);

/**
 * Get library API version as a single encoded integer.
 *
 * Encoding: (MAJOR << 16) | (MINOR << 8) | PATCH
 *
 * Bindings should compare `snobol_get_api_version() >> 16` against their
 * compile-time SNOBOL_VERSION_MAJOR to detect incompatible library upgrades.
 *
 * @return Encoded API version uint32_t.
 *         For v0.11.0 this returns 0x00000B00u.
 */
uint32_t snobol_get_api_version(void);

/**
 * Get the ABI version of the library.
 *
 * Returns a monotonically-increasing integer that is bumped whenever a
 * breaking change is made to the public ABI.  Bindings and dynamic
 * loaders can call this at initialisation to verify compatibility.
 *
 * The initial v0.11.0 value is 1.
 *
 * @return ABI version integer.
 */
uint32_t snobol_get_abi_version(void);

/* Include common headers */
#include "snobol/array.h"
#include "snobol/ast.h"
#include "snobol/compiler.h"
#include "snobol/dynamic_pattern.h"
#include "snobol/lexer.h"
#include "snobol/parser.h"
#include "snobol/search.h"
#include "snobol/string_fn.h"
#include "snobol/table.h"
#include "snobol/type_fn.h"
#include "snobol/vm.h"

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
 * @brief Maximum named variables returned from a match.
 */
#define SNOBOL_API_MAX_VARS 64

/**
 * @brief Match result object.
 *
 * Contains the result of a successful pattern match,
 * including captured variables and output buffer.
 * Use snobol_match_create() to allocate, snobol_match_free() to release.
 */
typedef struct snobol_match {
  bool success;

  /* Output buffer (from OP_EMIT_* instructions) */
  char *output;
  size_t output_len;

  /* Named capture variables: var_values[i] points into subject or is a
   * NUL-terminated copy; var_lens[i] is the byte length.
   * Variable names are stored as decimal strings "1", "2", …, "64".
   * Index 0 corresponds to variable name "1" (1-based). */
  char *var_values[SNOBOL_API_MAX_VARS];
  size_t var_lens[SNOBOL_API_MAX_VARS];
  int var_count;

  /* Match position within the subject. */
  size_t position;
  size_t length;
} snobol_match_t;

/**
 * @brief Associative table (opaque handle)
 *
 * Runtime-owned string-keyed hash table for table-backed substitutions.
 */
typedef struct snobol_table snobol_table_t;

typedef struct snobol_array snobol_array_t;

/* Context lifecycle */
/**
 * @brief Create a new pattern matching context.
 *
 * The context owns all compiled patterns and associated resources.
 * Must be destroyed with snobol_context_destroy() when no longer needed.
 *
 * @return New context, or NULL on allocation failure.
 */
snobol_context_t *snobol_context_create(void);

/**
 * @brief Destroy a pattern matching context and free all associated resources.
 *
 * All patterns compiled under this context are invalidated.
 * Passing NULL is safe (no-op).
 *
 * @param[in] ctx Context to destroy.
 */
void snobol_context_destroy(snobol_context_t *ctx);

/* -----------------------------------------------------------------------
 * Pattern compilation flags
 * ----------------------------------------------------------------------- */

/**
 * Make literal and character-class matching case-insensitive for ASCII and
 * Latin-1 Supplement (U+0041–U+005A / U+0061–U+007A; U+00C0–U+00FF pairs).
 * Captured text preserves the original subject case.
 *
 * Pass to snobol_pattern_compile_ex() via the flags bitmask.
 */
#define SNOBOL_FLAG_CASE_INSENSITIVE 0x0001u

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
snobol_pattern_t *snobol_pattern_compile(snobol_context_t *ctx,
                                         const char *source, size_t len,
                                         char **error);

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
snobol_pattern_t *snobol_pattern_compile_ex(snobol_context_t *ctx,
                                            const char *source, size_t len,
                                            uint32_t flags, char **error);

/**
 * @brief Get the bytecode pointer from a compiled pattern.
 * @param[in] pattern Compiled pattern.
 * @return Read-only pointer to the bytecode, or NULL if pattern is NULL.
 */
const uint8_t *snobol_pattern_get_bc(const snobol_pattern_t *pattern);

/**
 * @brief Get the bytecode length from a compiled pattern.
 * @param[in] pattern Compiled pattern.
 * @return Byte length, or 0 if pattern is NULL.
 */
size_t snobol_pattern_get_bc_len(const snobol_pattern_t *pattern);

/**
 * @brief Free a compiled pattern.
 *
 * @param[in] pattern Pattern to free.  NULL is safe.
 */
void snobol_pattern_free(snobol_pattern_t *pattern);

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
snobol_match_t *snobol_pattern_match(snobol_pattern_t *pattern,
                                     const char *subject, size_t len);

/**
 * @brief Lightweight result from snobol_pattern_match_literal().
 *
 * Zero heap allocations — returned by value.  Only valid for patterns
 * whose bytecode is a single anchored literal (e.g. "'abc'").
 */
typedef struct snobol_literal_match {
  bool success;      /**< Whether the literal matched at position 0. */
  size_t position;   /**< Match start position (always 0 on success). */
  size_t length;     /**< Byte length of the matched literal. */
} snobol_literal_match_t;

/**
 * @brief Lightweight anchored literal match for literal-only patterns.
 *
 * If the compiled pattern's bytecode is a single anchored literal (e.g.
 * @c "'abc'"), this function performs a @c memcmp at position 0 with
 * zero heap allocations.  No VM setup, emit buffer, or capture tracking
 * is performed — the result is returned by value.
 *
 * For non-literal patterns or when the subject does not start with the
 * pattern literal, @c success is set to @c false.
 *
 * @param[in] pattern Compiled pattern to execute.
 * @param[in] subject Subject string (UTF-8).
 * @param[in] len     Byte length of @p subject.
 * @return Lightweight result struct (zero heap allocations).
 */
snobol_literal_match_t snobol_pattern_match_literal(
    snobol_pattern_t *pattern, const char *subject, size_t len);

/**
 * @brief Execute a compiled pattern in search (un-anchored) mode.
 *
 * Uses the search tier dispatch (memchr/memmem/bitmap fast paths) before
 * falling back to the interpreter VM when no tier matches. Finds the
 * first occurrence of the pattern anywhere in the subject string (like
 * SNOBOL4's @c ? or @c POS(0) match).
 *
 * @param[in] pattern Compiled pattern to execute.
 * @param[in] subject Subject string (UTF-8).
 * @param[in] len Byte length of @p subject.
 * @return New match result that the caller must free with snobol_match_free().
 *         Always returns a valid pointer; call snobol_match_success() to
 *         determine whether the match succeeded.
 */
snobol_match_t *snobol_pattern_search(snobol_pattern_t *pattern,
                                      const char *subject, size_t len);

/**
 * @brief Opaque state for snobol_pattern_search_ex().
 *
 * Holds the cached search metadata, a reusable VM, an output buffer,
 * and a reusable match result.  Created once per pattern, reused across
 * all matches in a search loop (e.g. PHP Pattern::searchSplit) to
 * avoid per-call allocation churn.
 *
 * Pointer ownership: created by snobol_pattern_search_state_create(),
 * destroyed by snobol_pattern_search_state_destroy(). The state holds
 * a reference to the pattern but does not own it; the caller must keep
 * the pattern alive until the state is destroyed.
 */
typedef struct snobol_pattern_search_state snobol_pattern_search_state_t;

/**
 * @brief Create a state object for snobol_pattern_search_ex().
 *
 * @param[in] bc     Compiled bytecode buffer. Must outlive the state.
 * @param[in] bc_len Byte length of @p bc.
 * @return Newly allocated state, or NULL on allocation failure.
 *         Free with snobol_pattern_search_state_destroy().
 */
snobol_pattern_search_state_t *
snobol_pattern_search_state_create(const uint8_t *bc, size_t bc_len);

/**
 * @brief Destroy a search state object. NULL-safe.
 *
 * Releases the cached VM, output buffer, match result, and the
 * reference to the pattern's bytecode. Does NOT free the pattern
 * itself.
 */
void snobol_pattern_search_state_destroy(snobol_pattern_search_state_t *state);

/**
 * @brief Stateful search that reuses VM, output buffer, and match result
 * across calls.
 *
 * Functionally equivalent to repeated calls to
 * snobol_pattern_search() but amortises the per-call allocation cost.
 * Intended for hot loops (e.g. PHP Pattern::searchSplit, which performs
 * 1000+ matches per call).
 *
 * @param[in,out] state       Search state created by
 *                            snobol_pattern_search_state_create(). The
 *                            state's internal match result is
 *                            overwritten on each call.
 * @param[in]     subject     Subject string (UTF-8).
 * @param[in]     subject_len Byte length of @p subject.
 * @param[in]     start_offset Byte offset to start searching from
 *                            (0 = beginning).
 * @return Pointer to the internal match result owned by @p state, valid
 *         until the next call on the same state or state destruction.
 *         The caller must NOT free this pointer.
 */
snobol_match_t *snobol_pattern_search_ex(snobol_pattern_search_state_t *state,
                                         const char *subject,
                                         size_t subject_len,
                                         size_t start_offset);

/**
 * @brief Get the absolute offset where the match started within the subject.
 *
 * Returns the position of the first byte of the match in the subject
 * string (not the position within the search window). Returns 0 on
 * failure or if match is NULL.
 *
 * @param[in] match Match result from snobol_pattern_search() or
 *                  snobol_pattern_search_ex().
 * @return Byte offset of the match start in the original subject.
 */
size_t snobol_match_get_position(const snobol_match_t *match);

/**
 * @brief Get the length of the match in bytes.
 *
 * @param[in] match Match result from snobol_pattern_search() or
 *                  snobol_pattern_search_ex().
 * @return Number of bytes in the match. Returns 0 on failure or NULL.
 */
size_t snobol_match_get_length(const snobol_match_t *match);

/**
 * @brief Free a match result.
 *
 * @param[in] match Match result to free.  NULL is safe.
 */
void snobol_match_free(snobol_match_t *match);

/**
 * @brief Create a new match result object.
 *
 * Allocates and zero-initializes a snobol_match_t. The caller owns the
 * returned pointer and must free it with snobol_match_free() or reuse it
 * with snobol_pattern_search_reuse().
 *
 * @return Newly allocated match object, or NULL on allocation failure.
 */
snobol_match_t *snobol_match_create(void);

/**
 * @brief Reset a match result for reuse without freeing.
 *
 * Clears all fields and frees any allocated capture strings.
 * The match object remains allocated and ready for reuse.
 * Safe to call on a zeroed or freshly allocated match object.
 *
 * @param[in,out] match Match result to reset.  NULL is safe (no-op).
 */
void snobol_match_reset(snobol_match_t *match);

/**
 * @brief Search using a caller-allocated, reusable match object.
 *
 * Functionally equivalent to snobol_pattern_search() but uses a
 * caller-provided match object instead of allocating one per call.
 * Eliminates per-call malloc/free overhead (~30 ns).
 *
 * The caller owns the match object and must call snobol_match_reset()
 * or snobol_match_free() when done.  The match object is overwritten
 * on each call.
 *
 * @param[in]     pattern    Compiled pattern to search.
 * @param[in]     subject    Subject string (UTF-8).
 * @param[in]     len        Byte length of @p subject.
 * @param[in,out] match_out  Caller-allocated match object (reusable).
 * @return true if match succeeded, false otherwise.
 *         Results are written to *match_out.
 */
bool snobol_pattern_search_reuse(snobol_pattern_t *pattern,
                                 const char *subject, size_t len,
                                 snobol_match_t *match_out);

/* Match result access */
/**
 * @brief Return whether a match result represents a successful match.
 *
 * @param[in] match Match result from snobol_pattern_match().
 * @return true if the pattern matched; false otherwise.
 */
bool snobol_match_success(snobol_match_t *match);

/**
 * @brief Retrieve the output buffer produced by a successful match.
 *
 * The output contains any text emitted by @c OP_EMIT_LITERAL / @c
 * OP_EMIT_CAPTURE instructions during the match.
 *
 * @param[in]  match Match result from snobol_pattern_match().
 * @param[out] len   If non-NULL, set to the byte length of the returned string.
 * @return Pointer to the output string (owned by @p match, do not free),
 *         or NULL if there is no output.
 */
const char *snobol_match_get_output(snobol_match_t *match, size_t *len);

/**
 * @brief Retrieve the value of a named capture variable from a match result.
 *
 * @param[in]  match Match result from snobol_pattern_match().
 * @param[in]  name  Variable name (NUL-terminated).
 * @param[out] len   If non-NULL, set to the byte length of the returned string.
 * @return Pointer to the variable value (owned by @p match, do not free),
 *         or NULL if the variable was not set during the match.
 */
const char *snobol_match_get_variable(snobol_match_t *match, const char *name,
                                      size_t *len);

/* -----------------------------------------------------------------------
 * One-shot convenience match API
 *
 * snobol_match() bundles lex→parse→compile→VM execution into a single
 * call, freeing all intermediate resources internally.  Use this for
 * simple one-off matches; for repeated matching of the same pattern use
 * snobol_pattern_compile() + snobol_pattern_match() instead.
 * ----------------------------------------------------------------------- */

/**
 * @brief Result from the one-shot snobol_match() convenience API.
 *
 * Unlike the opaque snobol_match_t (which is used by the multi-step API),
 * this struct is non-opaque so callers can directly read the fields.
 */
typedef struct snobol_match_result {
  bool success;      /**< Whether the pattern matched */
  char *error;       /**< Malloc'd error message (NULL on success);
                          caller must free via snobol_match_result_free(). */
  char *output;      /**< Output buffer from OP_EMIT_* instructions
                          (owned, freed by snobol_match_result_free()). */
  size_t output_len; /**< Byte length of output (0 if no output). */

  /** Captured variable values (index 0 = variable "1", etc.).
   *  Each entry is a malloc'd NUL-terminated string (or NULL if not set).
   *  Freed by snobol_match_result_free(). */
  char **captures;
  size_t *capture_lens; /**< Byte lengths of each capture string. */
  int capture_count;    /**< Number of populated captures. */
} snobol_match_result_t;

/**
 * @brief One-shot convenience match: compile and run a pattern in one call.
 *
 * Bundles the full pipeline: lexer → parser → compiler → VM execution.
 * All intermediate objects are freed internally.
 *
 * @param[in]  pattern   Pattern source string (UTF-8).
 * @param[in]  pat_len   Byte length of @p pattern.
 * @param[in]  subject   Subject string (UTF-8).
 * @param[in]  sub_len   Byte length of @p subject.
 * @param[in]  flags     Bitmask of SNOBOL_FLAG_* constants (0 for default).
 * @return Newly allocated snobol_match_result_t.  Always valid; check
 *         the @c success and @c error fields.  Free with
 *         snobol_match_result_free().
 */
snobol_match_result_t *snobol_match(const char *pattern, size_t pat_len,
                                    const char *subject, size_t sub_len,
                                    uint32_t flags);

/**
 * @brief Free a snobol_match_result_t and all its owned resources.
 * @param[in] result Result to free.  NULL is safe.
 */
void snobol_match_result_free(snobol_match_result_t *result);

/* -----------------------------------------------------------------------
 * Builder API — programmatic AST construction
 *
 * Each snobol_pattern_build_*() function creates a single AST node of the
 * corresponding type.  Compound functions (concat, alt, cap, etc.) accept
 * child AST nodes and take ownership of them.  Use snobol_pattern_build_emit()
 * to finalize the root node, then compile with compile_ast_to_bytecode_c()
 * or snobol_pattern_compile() via the multi-step API.
 *
 * Example:
 *   snobol_pattern_build_t *b = snobol_pattern_build_create();
 *   ast_node_t *lit = snobol_pattern_build_lit(b, "hello", 5);
 *   ast_node_t *root = snobol_pattern_build_emit(b, lit);
 *   // root is ready for compile_ast_to_bytecode_c(root, ...)
 *   // Ownership of root transferred; b can be destroyed or reused.
 *   snobol_pattern_build_destroy(b);
 * ----------------------------------------------------------------------- */

/**
 * @brief Opaque builder state.
 */
typedef struct snobol_pattern_build snobol_pattern_build_t;

/** @brief Create a new builder. */
snobol_pattern_build_t *snobol_pattern_build_create(void);

/** @brief Destroy a builder. Does NOT free emitted AST nodes. */
void snobol_pattern_build_destroy(snobol_pattern_build_t *build);

/* --- Primitive nodes ---------------------------------------------------- */

/** @brief Create a LITERAL AST node. */
ast_node_t *snobol_pattern_build_lit(snobol_pattern_build_t *build,
                                     const char *text, size_t len);

/** @brief Create a SPAN AST node (match 1+ chars in set). */
ast_node_t *snobol_pattern_build_span(snobol_pattern_build_t *build,
                                      const char *set, size_t len);

/** @brief Create a BREAK AST node (consume until char in set). */
ast_node_t *snobol_pattern_build_brk(snobol_pattern_build_t *build,
                                     const char *set, size_t len);

/** @brief Create an ANY AST node (match single char in optional set). */
ast_node_t *snobol_pattern_build_any(snobol_pattern_build_t *build,
                                     const char *set, size_t len);

/** @brief Create a NOTANY AST node (match single char NOT in set). */
ast_node_t *snobol_pattern_build_notany(snobol_pattern_build_t *build,
                                        const char *set, size_t len);

/** @brief Create a LEN AST node (match exactly N codepoints). */
ast_node_t *snobol_pattern_build_len(snobol_pattern_build_t *build, int32_t n);

/** @brief Create an ARBNO AST node (zero or more repetitions). */
ast_node_t *snobol_pattern_build_arbno(snobol_pattern_build_t *build,
                                       ast_node_t *sub);

/* --- Capture / assignment ---------------------------------------------- */

/** @brief Create a CAP AST node (capture sub-pattern into register). */
ast_node_t *snobol_pattern_build_cap(snobol_pattern_build_t *build, int reg,
                                     ast_node_t *sub);

/** @brief Create an ASSIGN AST node (assign register to variable). */
ast_node_t *snobol_pattern_build_assign(snobol_pattern_build_t *build, int var,
                                        int reg);

/* --- Compound nodes ---------------------------------------------------- */

/** @brief Create a CONCAT AST node (sequence of parts).  Takes ownership
 *         of @p parts array and all child nodes. */
ast_node_t *snobol_pattern_build_concat(snobol_pattern_build_t *build,
                                        ast_node_t **parts, size_t count);

/** @brief Create an ALT AST node (alternation). */
ast_node_t *snobol_pattern_build_alt(snobol_pattern_build_t *build,
                                     ast_node_t *left, ast_node_t *right);

/* --- Control flow ------------------------------------------------------ */

/** @brief Create a LABEL AST node. */
ast_node_t *snobol_pattern_build_label(snobol_pattern_build_t *build,
                                       const char *name, ast_node_t *target);

/** @brief Create a GOTO AST node. */
ast_node_t *snobol_pattern_build_goto(snobol_pattern_build_t *build,
                                      const char *label);

/* --- Position primitives ----------------------------------------------- */

/** @brief Create a POS AST node. */
ast_node_t *snobol_pattern_build_pos(snobol_pattern_build_t *build, int32_t n);

/** @brief Create a TAB AST node. */
ast_node_t *snobol_pattern_build_tab(snobol_pattern_build_t *build, int32_t n);

/** @brief Create a RPOS AST node. */
ast_node_t *snobol_pattern_build_rpos(snobol_pattern_build_t *build, int32_t n);

/** @brief Create a RTAB AST node. */
ast_node_t *snobol_pattern_build_rtab(snobol_pattern_build_t *build, int32_t n);

/** @brief Create a BREAKX AST node. */
ast_node_t *snobol_pattern_build_breakx(snobol_pattern_build_t *build,
                                        const char *set, size_t len);

/** @brief Create a BAL AST node. */
ast_node_t *snobol_pattern_build_bal(snobol_pattern_build_t *build,
                                     uint32_t open_cp, uint32_t close_cp);

/** @brief Create a FENCE AST node. */
ast_node_t *snobol_pattern_build_fence(snobol_pattern_build_t *build);

/** @brief Create a REM AST node. */
ast_node_t *snobol_pattern_build_rem(snobol_pattern_build_t *build);

/** @brief Create an ABORT AST node. */
ast_node_t *snobol_pattern_build_abort(snobol_pattern_build_t *build);

/** @brief Create a FAIL AST node. */
ast_node_t *snobol_pattern_build_fail(snobol_pattern_build_t *build);

/** @brief Create a SUCCEED AST node. */
ast_node_t *snobol_pattern_build_succeed(snobol_pattern_build_t *build);

/**
 * @brief Finalize and emit an AST tree built with the builder.
 *
 * Takes ownership of @p root and returns it.  After this call the builder
 * may be reused to build a new tree, or destroyed.  The returned node
 * must eventually be freed via snobol_ast_free().
 *
 * @param[in] build Builder instance (may be NULL for stateless usage).
 * @param[in] root  Root AST node to emit (ownership transferred).
 * @return @p root (non-NULL if input non-NULL).
 */
ast_node_t *snobol_pattern_build_emit(snobol_pattern_build_t *build,
                                      ast_node_t *root);
