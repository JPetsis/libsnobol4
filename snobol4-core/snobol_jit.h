#ifndef SNOBOL_JIT_H
#define SNOBOL_JIT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef SNOBOL_JIT

#include "snobol_vm.h"
#include <time.h>

/* ---------------------------------------------------------------------------
 * Timing helper — uses C11 timespec_get (no POSIX feature-test macros needed).
 * Called only when stats != NULL, so overhead is bounded.
 * --------------------------------------------------------------------------- */
static inline uint64_t snobol_jit_now_ns(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

/* ---------------------------------------------------------------------------
 * JIT Configuration surface
 *
 * All thresholds can be overridden via environment variables (loaded on
 * snobol_jit_init()) or by calling snobol_jit_set_config().
 *
 *  SNOBOL_JIT_HOTNESS       hotness_threshold    (default 50)
 *  SNOBOL_JIT_MAX_EXIT_PCT  max_exit_rate_pct    (default 80)
 *  SNOBOL_JIT_BUDGET_NS     compile_budget_ns    (default 500000)
 *  SNOBOL_JIT_CACHE_MAX     cache_max_entries    (default 128)
 *  SNOBOL_JIT_MIN_OPS       min_useful_ops       (default 2)
 *  SNOBOL_JIT_SKIP_BT       skip_backtrack_heavy (default 1)
 * --------------------------------------------------------------------------- */
typedef struct SnobolJitConfig {
    uint64_t hotness_threshold;     /**< IP hit count before compiling (default 50) */
    uint32_t max_exit_rate_pct;     /**< % exits/entries before stopping compilation (default 80) */
    uint64_t compile_budget_ns;     /**< Max compile time per context in ns (default 500 000) */
    uint32_t cache_max_entries;     /**< Max entries in LRU compiled-artifact cache (default 128) */
    uint32_t min_useful_ops;        /**< Min non-trivial ops before SPLIT/REPEAT; below → skip JIT (default 2) */
    bool     skip_backtrack_heavy;  /**< Skip JIT for SPLIT-dominated patterns with short prefix (default true) */
} SnobolJitConfig;

void                    snobol_jit_set_config(const SnobolJitConfig *cfg);
const SnobolJitConfig  *snobol_jit_get_config(void);
void                    snobol_jit_load_config_from_env(void);

/* ---------------------------------------------------------------------------
 * JIT Statistics
 *
 * All counters are global (single instance per process).  Use
 * snobol_jit_reset_stats() for test isolation.
 *
 * Timing (nanoseconds):
 *   compile_time_ns_total   wall time spent inside snobol_jit_compile()
 *   exec_time_ns_total      wall time executing compiled JIT traces
 *   interp_time_ns_total    wall time in interpreter dispatch while JIT is enabled
 *
 * Profitability / skip counters:
 *   skipped_cold_total      regions skipped: below hotness or min_useful_ops threshold
 *   skipped_exit_rate_total compilation stopped: exit rate exceeded max_exit_rate_pct
 *   skipped_budget_total    compilation stopped: compile_budget_ns exceeded
 *
 * Bailout reason breakdown:
 *   bailout_match_fail_total  JIT exited with ip == entry_ip  (match failure, no progress)
 *   bailout_partial_total     JIT exited with entry_ip < ip < region_end (partial progress)
 * --------------------------------------------------------------------------- */
typedef struct SnobolJitStats {
    /* Core counters (pre-existing, kept for compatibility) */
    uint64_t compilations_total;
    uint64_t cache_hits_total;
    uint64_t entries_total;
    uint64_t exits_total;
    uint64_t bailouts_total;
    uint64_t time_ns_total;   /* legacy aggregate; prefer exec_time_ns_total */

    /* Backtracking counters (pre-existing) */
    uint64_t choice_push_total;
    uint64_t choice_pop_total;
    uint64_t choice_bytes_total;

    /* NEW: Fine-grained timing */
    uint64_t compile_time_ns_total;
    uint64_t exec_time_ns_total;
    uint64_t interp_time_ns_total;

    /* NEW: Profitability / skip reasons */
    uint64_t skipped_cold_total;
    uint64_t skipped_exit_rate_total;
    uint64_t skipped_budget_total;

    /* NEW: Bailout reason breakdown */
    uint64_t bailout_match_fail_total;  /* ip == entry_ip after trace returns */
    uint64_t bailout_partial_total;     /* entry_ip < ip < region end */
} SnobolJitStats;

/* ---------------------------------------------------------------------------
 * JIT Context
 *
 * Ownership / lifetime rules
 * ─────────────────────────
 * • The global LRU cache owns all SnobolJitContext instances.
 * • Callers (patterns) acquire a reference via snobol_jit_acquire_context() which
 *   increments ref_count.  They MUST call snobol_jit_release_context() when the
 *   pattern is freed.
 * • Eviction only removes entries with ref_count == 0.
 * • Compiled code blocks (traces[i]) are owned by the context; they are freed
 *   via snobol_jit_free_code(traces[i], trace_sizes[i]) on context destruction.
 * • A pattern MUST NOT execute any traces after calling snobol_jit_release_context().
 * --------------------------------------------------------------------------- */
typedef struct SnobolJitContext {
    uint64_t *ip_counts;    /**< Per-IP execution counts (length = bc_len) */
    void    **traces;       /**< Per-IP compiled code pointers (length = bc_len) */
    size_t   *trace_sizes;  /**< Per-IP code allocation sizes (for munmap; length = bc_len) */
    size_t    bc_len;
    uint64_t  hash;         /**< Signature: djb2(bytecode) XOR config_hash */
    int       ref_count;

    /* LRU eviction: incremented on every acquire/touch; victim = min lru_counter with ref_count==0 */
    uint64_t  lru_counter;

    /* Profitability state (per pattern) */
    bool      stop_compiling;  /**< Profitability gate: no more regions for this pattern */
    uint64_t  ctx_entries;     /**< JIT trace entries for this pattern */
    uint64_t  ctx_exits;       /**< JIT trace exits for this pattern */
    uint64_t  compile_time_ns; /**< Cumulative compile time for this context (ns) */
} SnobolJitContext;

/* ---------------------------------------------------------------------------
 * API
 * --------------------------------------------------------------------------- */

/** Acquire a JIT context for the given bytecode (LRU cache lookup / create).
 *  Increments ref_count.  Caller MUST call snobol_jit_release_context() when done. */
SnobolJitContext *snobol_jit_acquire_context(const uint8_t *bc, size_t bc_len);

/** Decrement ref_count.  Context remains in cache until evicted. */
void snobol_jit_release_context(SnobolJitContext *ctx);

/** Return the global stats pointer (never NULL after snobol_jit_init). */
SnobolJitStats *snobol_jit_get_stats(void);

/** Reset global stats and clear ip_counts/traces in all cached contexts. */
void snobol_jit_reset_stats(void);

/** Opaque handle for a JIT trace function. */
typedef void (*jit_trace_fn)(VM *vm);

/** Initialise JIT subsystem and load config from env vars. */
void snobol_jit_init(void);

/** Shutdown JIT subsystem: free all cached contexts and compiled code. */
void snobol_jit_shutdown(void);

/* Code memory management */
void *snobol_jit_alloc_code(size_t size);
void  snobol_jit_seal_code(void *code, size_t size);
void  snobol_jit_free_code(void *code, size_t size);

/** Compile a trace starting at ip.  Returns function pointer or NULL on failure.
 *  compilations_total is incremented by the CALLER before invoking this. */
jit_trace_fn snobol_jit_compile(VM *vm, size_t ip, size_t *out_code_size);

/** Profitability gate: return false if compiling at this ip is predicted unprofitable. */
bool snobol_jit_should_compile(const VM *vm, size_t ip, const SnobolJitConfig *cfg);

#endif /* SNOBOL_JIT */

#endif /* SNOBOL_JIT_H */
