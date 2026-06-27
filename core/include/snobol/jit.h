#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef SNOBOL_JIT

/* Auto-detect the target platform if the build system did not supply an
 * explicit -DSNOBOL_JIT_PLATFORM_{LINUX,MACOS,WINDOWS}=1 flag (e.g. the PHP
 * phpize build). */
#if !defined(SNOBOL_JIT_PLATFORM_LINUX) &&                                     \
    !defined(SNOBOL_JIT_PLATFORM_MACOS) &&                                     \
    !defined(SNOBOL_JIT_PLATFORM_WINDOWS)
#if defined(__linux__)
#define SNOBOL_JIT_PLATFORM_LINUX 1
#elif defined(__APPLE__) && defined(__MACH__)
#define SNOBOL_JIT_PLATFORM_MACOS 1
#elif defined(_WIN32)
#define SNOBOL_JIT_PLATFORM_WINDOWS 1
#else
#warning                                                                       \
    "SNOBOL_JIT enabled on an untested platform — mmap-based code allocation may be broken"
#endif
#endif

/* VM type is defined in vm.h - include it */
#include "snobol/vm.h"

/* ---------------------------------------------------------------------------
 * Timing helper — uses C11 timespec_get (no POSIX feature-test macros needed).
 * Called only when stats != NULL, so overhead is bounded.
 * ---------------------------------------------------------------------------
 */
static inline uint64_t snobol_jit_now_ns(void) {
  struct timespec ts;
  timespec_get(&ts, TIME_UTC);
  return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

/* ---------------------------------------------------------------------------
 * JIT Configuration surface
 *
 * Method JIT (whole-pattern compilation via SLJIT) is the only JIT mode.
 * Tracing JIT (per-IP hot-trace compilation) has been retired.
 *
 * Overridable via environment variables (loaded on snobol_jit_init()):
 *  SNOBOL_JIT_METHOD_ENABLED   method_enabled    (default 1)
 *  SNOBOL_JIT_MAX_PATTERNS     max_compiled_patterns  (default 1024)
 *  SNOBOL_JIT_SCRATCH_SIZE     scratch_size       (default 256)
 * ---------------------------------------------------------------------------
 */
typedef struct SnobolJitConfig {
  bool method_enabled;        /**< Master switch: if true, search_exec prefers
                                  the method-JIT path (default true) */
  uint32_t max_compiled_patterns; /**< Hard cap on cached compiled patterns
                                      (default 1024) */
  uint32_t scratch_size;      /**< Bytes reserved for the choice-stack scratch
                                  buffer passed to compiled methods (default 256) */
} SnobolJitConfig;

void snobol_jit_set_config(const SnobolJitConfig *cfg);
const SnobolJitConfig *snobol_jit_get_config(void);
void snobol_jit_load_config_from_env(void);

/* ---------------------------------------------------------------------------
 * JIT event log
 *
 * Used to capture a chronological trace of JIT/VM events to a file for
 * post-mortem analysis of CI hangs and crashes.  Enable by setting the
 * SNOBOL_JIT_LOG_FILE environment variable to an output file path.  The
 * log is opened on first use and closed on shutdown.
 * ---------------------------------------------------------------------------
 */
void snobol_jit_log_open(void);
void snobol_jit_log_close(void);
void snobol_jit_log(const char *fmt, ...);

/* ---------------------------------------------------------------------------
 * JIT Statistics
 *
 * All counters are global (single instance per process).  Use
 * snobol_jit_reset_stats() for test isolation.
 *
 * Method JIT counters (whole-pattern compilation via SLJIT):
 *   attempts        compile calls
 *   successes       successfully compiled patterns
 *   fallbacks       patterns rejected (EVAL/BAL/DYNAMIC)
 *   evictions       cache evictions
 * ---------------------------------------------------------------------------
 */
typedef struct SnobolJitStats {
  uint64_t method_attempts_total;
  uint64_t method_successes_total;
  uint64_t method_fallbacks_total;
  uint64_t method_evictions_total;
} SnobolJitStats;

/* ---------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------------
 */

/** Return the global stats pointer (never NULL after snobol_jit_init). */
SnobolJitStats *snobol_jit_get_stats(void);

/** Reset global stats. */
void snobol_jit_reset_stats(void);

/** Opaque handle for a compiled native function. */
typedef void (*jit_trace_fn)(VM *vm);

/** Initialise JIT subsystem and load config from env vars. */
void snobol_jit_init(void);

/** Shutdown JIT subsystem: free all compiled code. */
void snobol_jit_shutdown(void);

/* Code memory management */
void *snobol_jit_alloc_code(size_t size);
void snobol_jit_seal_code(void *code, size_t size);
void snobol_jit_free_code(void *code, size_t size);

/* ---------------------------------------------------------------------------
 * Method JIT — whole-pattern compilation via SLJIT
 *
 * Compiles a trace starting at bytecode offset 0 (the whole pattern
 * entry point) and caches the result keyed by bytecode hash.  The
 * compiled function has the signature `void fn(VM *vm)`.
 *
 * The Tier 0 search path in search.c invokes this function by
 * synthesising a minimal VM struct with subject, bytecode, and captures
 * already populated.
 *
 * Patterns containing opcodes the SLJIT backend cannot compile
 * (EVAL, DYNAMIC, BAL) are rejected at compile time.
 *
 * Lifetime: the returned function pointer is owned by the JIT method
 * cache; callers MUST NOT free it.  It remains valid until the JIT
 * subsystem is shut down or the pattern is evicted.
 *
 * Return value convention: the compiled function modifies the VM's
 * `pos` and `ip` fields.  After return, ip == bc_len indicates a
 * successful match; otherwise the match failed and the caller should
 * fall through to the existing search tiers.
 * ---------------------------------------------------------------------------
 */

/** Compile the given bytecode into a native function.  Returns
 *  NULL if the pattern contains opcodes the SLJIT backend cannot handle
 *  (EVAL, DYNAMIC, BAL) or if SLJIT itself fails. */
jit_trace_fn snobol_jit_method_compile(const uint8_t *bc, size_t bc_len,
                                       size_t *out_code_size);

/** Query whether the method-JIT has a compiled function for the given
 *  bytecode.  Returns NULL if no compile has happened (or if the pattern is
 *  uncompilable). */
jit_trace_fn snobol_jit_method_query(const uint8_t *bc, size_t bc_len);

/** Free a compiled method function.  After this call the function
 *  pointer must not be invoked. */
void snobol_jit_method_free(jit_trace_fn fn);

#endif /* SNOBOL_JIT */
