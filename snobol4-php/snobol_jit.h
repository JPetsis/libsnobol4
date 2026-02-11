#ifndef SNOBOL_JIT_H
#define SNOBOL_JIT_H

#include <stddef.h>
#include <stdint.h>

#ifdef SNOBOL_JIT

#include "snobol_vm.h"

/* JIT Statistics */
typedef struct SnobolJitStats {
    uint64_t compilations_total;
    uint64_t cache_hits_total;
    uint64_t entries_total;
    uint64_t exits_total;
    uint64_t bailouts_total;
    uint64_t time_ns_total;
    
    /* Backtracking counters */
    uint64_t choice_push_total;
    uint64_t choice_pop_total;
    uint64_t choice_bytes_total;
} SnobolJitStats;

/* JIT Context - shared across patterns with same bytecode */
typedef struct SnobolJitContext {
    uint64_t *ip_counts;
    void **traces;
    size_t bc_len;
    uint64_t hash;
    int ref_count;
} SnobolJitContext;

/* Acquire a JIT context for the given bytecode */
SnobolJitContext *snobol_jit_acquire_context(const uint8_t *bc, size_t bc_len);

/* Release a JIT context */
void snobol_jit_release_context(SnobolJitContext *ctx);

/* Get the global stats pointer */
SnobolJitStats *snobol_jit_get_stats(void);

/* Reset global stats */
void snobol_jit_reset_stats(void);

/* Opaque handle for a JIT trace */
typedef void (*jit_trace_fn)(VM *vm);

/* Initialize JIT subsystem (global) */
void snobol_jit_init(void);

/* Shutdown JIT subsystem */
void snobol_jit_shutdown(void);

/* Allocator for JIT code */
void *snobol_jit_alloc_code(size_t size);

/* Finalize code buffer (make executable) */
void snobol_jit_seal_code(void *code, size_t size);

/* Free code buffer */
void snobol_jit_free_code(void *code, size_t size);

/* Compile a trace starting at ip */
jit_trace_fn snobol_jit_compile(VM *vm, size_t ip);

#endif

#endif // SNOBOL_JIT_H
