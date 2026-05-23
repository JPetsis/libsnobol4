
#include "snobol/jit.h"
#include "snobol/jit_ir.h"
#include "snobol/jit_backend.h"

#ifdef SNOBOL_JIT

#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>
#ifdef __APPLE__
#  include <pthread.h>
#endif
#include "snobol/snobol_internal.h"
#include "snobol/table.h"
#include "snobol/dynamic_pattern.h"
#include "snobol/string_fn.h"
#include "snobol/type_fn.h"

/* ---------------------------------------------------------------------------
 * JIT OPCODE COVERAGE MATRIX
 *
 * jit-compiled  — full inline ARM64 code emitted within the compiled region
 * call-out       — ARM64 code calls a C helper via BLR; no interpreter fallback
 * pseudo         — no ARM64 emitted; scan-pass only (data-block skip or marker)
 *
 * OP_ACCEPT       jit-compiled (EXIT terminator)
 * OP_FAIL         jit-compiled (EXIT terminator)
 * OP_JMP          jit-compiled (FWD/BWD terminator)
 * OP_SPLIT        jit-compiled (SPLIT terminator, pushes choice)
 * OP_LIT          jit-compiled (inline byte comparison)
 * OP_ANY          jit-compiled (inline bitmap match)
 * OP_NOTANY       jit-compiled (inline bitmap match)
 * OP_SPAN         jit-compiled (inline bitmap loop)
 * OP_BREAK        jit-compiled (inline bitmap scan)
 * OP_BREAKX       jit-compiled (inline bitmap scan)
 * OP_LEN          jit-compiled (inline codepoint advance)
 * OP_ANCHOR       jit-compiled (inline position check)
 * OP_CAP_START    jit-compiled (inline store)
 * OP_CAP_END      jit-compiled (inline store)
 * OP_ASSIGN       jit-compiled (inline var update)
 * OP_REPEAT_INIT  jit-compiled (EXIT terminator)
 * OP_REPEAT_STEP  jit-compiled (EXIT terminator)
 * OP_NOP          jit-compiled (skip)
 * OP_REM          jit-compiled (inline pos=len)
 * OP_RPOS         jit-compiled (inline position guard)
 * OP_RTAB         jit-compiled (inline cursor advance)
 * OP_FENCE        jit-compiled (inline choice-stack cut)
 * OP_LABEL        pseudo       (no-emit marker; IP recorded for GOTO resolution)
 * OP_GOTO         jit-compiled (treated as unconditional branch like JMP)
 * OP_GOTO_F       jit-compiled (NOP in compiled region; always falls through)
 * OP_EMIT_LITERAL call-out     (snobol_jit_helper_emit_literal)
 * OP_EMIT_CAPTURE call-out     (snobol_jit_helper_emit_capture)
 * OP_EMIT_EXPR    call-out     (snobol_jit_helper_emit_expr)
 * OP_EMIT_FORMAT  call-out     (snobol_jit_helper_emit_format)
 * OP_EMIT_TABLE   call-out     (snobol_jit_helper_emit_table_ip)
 * OP_TABLE_GET    call-out     (snobol_jit_helper_table_get)
 * OP_TABLE_SET    call-out     (snobol_jit_helper_table_set)
 * OP_BAL          call-out     (snobol_jit_helper_bal)
 * OP_EVAL         call-out     (snobol_jit_helper_eval)
 * OP_DYNAMIC      call-out     (snobol_jit_helper_dynamic)
 * OP_DYNAMIC_DEF  pseudo       (region-skip: inline bytecode block skipped over)
 * --------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * Global state
 * --------------------------------------------------------------------------- */

static SnobolJitStats  global_jit_stats = {0};

/* ---------------------------------------------------------------------------
 * Configuration
 * --------------------------------------------------------------------------- */

static SnobolJitConfig global_jit_cfg = {
    .hotness_threshold        = 50,
    .max_exit_rate_pct        = 80,
    .compile_budget_ns        = 500000ULL,   /* 0.5 ms */
    .cache_max_entries        = 128,
    .min_useful_ops           = 2,
    .skip_backtrack_heavy     = true,
    /* Search-mode defaults: lower thresholds so hot search
     * patterns become JIT-eligible without requiring anchored-match volumes. */
    .search_hotness_threshold = 20,
    .search_min_useful_ops    = 1,
};

void snobol_jit_set_config(const SnobolJitConfig *cfg) {
    if (cfg) global_jit_cfg = *cfg;
}

/* ---------------------------------------------------------------------------
 * Backend registry
 * --------------------------------------------------------------------------- */

static const jit_backend_t *active_backend = nullptr;

void jit_backend_register(const jit_backend_t *backend) {
    active_backend = backend;
}

const jit_backend_t *jit_backend_get(void) {
    return active_backend;
}

const char *jit_backend_name(void) {
    return active_backend ? active_backend->name : "(none)";
}

const SnobolJitConfig *snobol_jit_get_config(void) { return &global_jit_cfg; }

void snobol_jit_load_config_from_env(void) {
    const char *v;
    if ((v = getenv("SNOBOL_JIT_HOTNESS")))      global_jit_cfg.hotness_threshold        = (uint64_t)strtoull(v, nullptr, 10);
    if ((v = getenv("SNOBOL_JIT_MAX_EXIT_PCT")))  global_jit_cfg.max_exit_rate_pct        = (uint32_t)strtoul(v, nullptr, 10);
    if ((v = getenv("SNOBOL_JIT_BUDGET_NS")))     global_jit_cfg.compile_budget_ns        = (uint64_t)strtoull(v, nullptr, 10);
    if ((v = getenv("SNOBOL_JIT_CACHE_MAX")))     global_jit_cfg.cache_max_entries        = (uint32_t)strtoul(v, nullptr, 10);
    if ((v = getenv("SNOBOL_JIT_MIN_OPS")))       global_jit_cfg.min_useful_ops           = (uint32_t)strtoul(v, nullptr, 10);
    if ((v = getenv("SNOBOL_JIT_SKIP_BT")))       global_jit_cfg.skip_backtrack_heavy     = (strtol(v, nullptr, 10) != 0);
    if ((v = getenv("SNOBOL_JIT_SEARCH_HOT")))    global_jit_cfg.search_hotness_threshold = (uint64_t)strtoull(v, nullptr, 10);
    if ((v = getenv("SNOBOL_JIT_SEARCH_OPS")))    global_jit_cfg.search_min_useful_ops    = (uint32_t)strtoul(v, nullptr, 10);
}

/* ---------------------------------------------------------------------------
 * LRU compiled-artifact cache
 *
 * Ownership rules:
 *  • The cache array owns each SnobolJitContext*.
 *  • Patterns increment ref_count via snobol_jit_acquire_context().
 *  • Eviction is only allowed when ref_count == 0.
 *  • Compiled code (traces[i]) is owned by the context and freed with
 *    snobol_jit_free_code(traces[i], trace_sizes[i]) when the context
 *    is destroyed.  Eviction never frees code that is still referenced.
 * --------------------------------------------------------------------------- */

#define JIT_CACHE_MAX_HARD 512  /* absolute upper bound regardless of config */

static SnobolJitContext *jit_cache[JIT_CACHE_MAX_HARD];
static int   jit_cache_count = 0;
static uint64_t lru_clock    = 0; /* monotonically increasing access counter */

static uint64_t djb2_hash(const uint8_t *data, size_t len) {
    uint64_t h = 5381;
    for (size_t i = 0; i < len; i++) h = ((h << 5) + h) + data[i];
    return h;
}

/* Destroy a context (frees compiled code + allocations).
 * Must only be called when ref_count == 0 and the context is removed from cache. */
static void jit_context_destroy(SnobolJitContext *ctx) {
    if (!ctx) return;
    if (ctx->traces && ctx->trace_sizes) {
        for (size_t i = 0; i < ctx->bc_len; i++) {
            if (ctx->traces[i] && ctx->trace_sizes[i]) {
                snobol_jit_free_code(ctx->traces[i], ctx->trace_sizes[i]);
                ctx->traces[i] = nullptr;
            }
        }
    }
    if (ctx->ip_counts)   snobol_free(ctx->ip_counts);
    if (ctx->traces)      snobol_free(ctx->traces);
    if (ctx->trace_sizes) snobol_free(ctx->trace_sizes);
    snobol_free(ctx);
}

/* Evict the least-recently-used entry with ref_count == 0.
 * Returns the evicted slot index, or -1 if no evictable entry exists. */
static int jit_cache_evict(void) {
    int    victim      = -1;
    uint64_t min_lru  = UINT64_MAX;
    int    max        = (int)global_jit_cfg.cache_max_entries;
    if (max > JIT_CACHE_MAX_HARD) max = JIT_CACHE_MAX_HARD;

    for (int i = 0; i < jit_cache_count; i++) {
        SnobolJitContext *c = jit_cache[i];
        if (c && c->ref_count == 0 && c->lru_counter < min_lru) {
            min_lru = c->lru_counter;
            victim  = i;
        }
    }
    if (victim < 0) return -1;

    jit_context_destroy(jit_cache[victim]);
    /* Compact array */
    jit_cache[victim] = jit_cache[--jit_cache_count];
    jit_cache[jit_cache_count] = nullptr;
    return victim;
}

SnobolJitContext *snobol_jit_acquire_context(const uint8_t *bc, size_t bc_len) {
    uint64_t hash = djb2_hash(bc, bc_len);

    /* Search cache */
    for (int i = 0; i < jit_cache_count; i++) {
        SnobolJitContext *c = jit_cache[i];
        if (c && c->hash == hash && c->bc_len == bc_len) {
            c->ref_count++;
            c->lru_counter = ++lru_clock;
            global_jit_stats.cache_hits_total++;
            return c;
        }
    }

    /* Create new context */
    SnobolJitContext *ctx = snobol_calloc(1, sizeof(SnobolJitContext));
    if (!ctx) return nullptr;
    ctx->bc_len      = bc_len;
    ctx->hash        = hash;
    ctx->ref_count   = 1;
    ctx->lru_counter = ++lru_clock;
    ctx->ip_counts   = snobol_calloc(bc_len, sizeof(uint64_t));
    ctx->traces      = snobol_calloc(bc_len, sizeof(void *));
    ctx->trace_sizes = snobol_calloc(bc_len, sizeof(size_t));

    if (!ctx->ip_counts || !ctx->traces || !ctx->trace_sizes) {
        /* Allocation failure: clean up and return nullptr */
        if (ctx->ip_counts)   snobol_free(ctx->ip_counts);
        if (ctx->traces)      snobol_free(ctx->traces);
        if (ctx->trace_sizes) snobol_free(ctx->trace_sizes);
        snobol_free(ctx);
        return nullptr;
    }

    /* Insert into cache */
    int cap = (int)global_jit_cfg.cache_max_entries;
    if (cap > JIT_CACHE_MAX_HARD) cap = JIT_CACHE_MAX_HARD;

    if (jit_cache_count < cap) {
        jit_cache[jit_cache_count++] = ctx;
    } else {
        /* Try to evict an unreferenced entry */
        int slot = jit_cache_evict();
        if (slot >= 0) {
            jit_cache[jit_cache_count++] = ctx;
        }
        /* If eviction failed (all entries in use), ctx is not cached but still works */
    }

    return ctx;
}

void snobol_jit_release_context(SnobolJitContext *ctx) {
    if (!ctx) return;
    if (ctx->ref_count > 0) ctx->ref_count--;
    /* Context remains in the cache; it will be evicted lazily when needed. */
}

/* ---------------------------------------------------------------------------
 * Stats API
 * --------------------------------------------------------------------------- */

SnobolJitStats *snobol_jit_get_stats(void) { return &global_jit_stats; }

void snobol_jit_reset_stats(void) {
    memset(&global_jit_stats, 0, sizeof(global_jit_stats));
    /* Clear profiling counters so tests start with a cold JIT */
    for (int i = 0; i < jit_cache_count; i++) {
        SnobolJitContext *c = jit_cache[i];
        if (!c) continue;
        if (c->ip_counts)   memset(c->ip_counts,   0, c->bc_len * sizeof(uint64_t));
        if (c->traces)      memset(c->traces,       0, c->bc_len * sizeof(void *));
        if (c->trace_sizes) memset(c->trace_sizes,  0, c->bc_len * sizeof(size_t));
        c->stop_compiling        = false;
        c->ctx_entries           = 0;
        c->ctx_exits             = 0;
        c->compile_time_ns       = 0;
        c->search_stop_compiling = false;
        c->search_ctx_entries    = 0;
        c->search_ctx_exits      = 0;
    }
}

/* ---------------------------------------------------------------------------
 * Init / Shutdown
 * --------------------------------------------------------------------------- */

void snobol_jit_init(void) {
    memset(&global_jit_stats, 0, sizeof(global_jit_stats));
    memset(jit_cache, 0, sizeof(jit_cache));
    jit_cache_count = 0;
    lru_clock = 0;
    snobol_jit_load_config_from_env();
    /* Register the compile-time selected backend */
#if defined(__aarch64__) || defined(__arm64__)
    snobol_jit_arm64_register();
#endif
}

void snobol_jit_shutdown(void) {
    for (int i = 0; i < jit_cache_count; i++) {
        if (jit_cache[i]) {
            /* Force-destroy even if ref_count > 0 (process teardown) */
            jit_cache[i]->ref_count = 0;
            jit_context_destroy(jit_cache[i]);
            jit_cache[i] = nullptr;
        }
    }
    jit_cache_count = 0;
}

/* ---------------------------------------------------------------------------
 * Code memory
 * --------------------------------------------------------------------------- */

void *snobol_jit_alloc_code(size_t size) {
#ifdef __APPLE__
    /* On Apple Silicon, MAP_JIT is required for pages that will be executed.
     * The caller (snobol_jit_compile) is responsible for calling
     * pthread_jit_write_protect_np(0) before writing and
     * snobol_jit_seal_code() to restore exec mode afterwards.
     * We do NOT toggle the write-protect here so that every alloc/free path
     * in snobol_jit_compile can be made symmetric without leaking write mode. */
    void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    return (ptr == MAP_FAILED) ? nullptr : ptr;
#else
    void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (ptr == MAP_FAILED) ? nullptr : ptr;
#endif
}

void snobol_jit_seal_code(void *code, size_t size) {
#ifdef __APPLE__
    /* Switch current thread back to exec mode, then flush the instruction cache. */
    pthread_jit_write_protect_np(1);
    __builtin___clear_cache((char *)code, (char *)code + size);
#else
    mprotect(code, size, PROT_READ | PROT_EXEC);
    __builtin___clear_cache((char *)code, (char *)code + size);
#endif
}

void snobol_jit_free_code(void *code, size_t size) {
    if (code && size) munmap(code, size);
}

/* ---------------------------------------------------------------------------
 * Profitability gate
 *
 * Scans bytecode from ip to estimate how many useful (non-trivial) ops
 * exist before the first SPLIT, REPEAT_INIT, REPEAT_STEP, ACCEPT, or FAIL.
 *
 * Returns false (skip JIT) if:
 *   1. The region has fewer than cfg->min_useful_ops useful ops.
 *   2. cfg->skip_backtrack_heavy is true and a SPLIT/REPEAT appears within
 *      the first few ops (pattern dominated by backtracking with short prefix).
 * --------------------------------------------------------------------------- */

bool snobol_jit_should_compile(const VM *vm, size_t ip, const SnobolJitConfig *cfg,
                                bool search_mode) {
    if (!cfg) return true;

    /* Choose thresholds based on execution mode:
     * Search-mode uses lower thresholds so one-op hot search patterns can
     * become JIT-eligible without requiring anchored-match traffic volumes. */
    uint32_t min_ops = search_mode ? cfg->search_min_useful_ops : cfg->min_useful_ops;

    size_t scan   = ip;
    uint32_t useful = 0;
    bool has_backtrack = false;

    for (int i = 0; i < 64 && scan < vm->bc_len; i++) {
        uint8_t op = vm->bc[scan++];
        switch (op) {
            case OP_SPLIT:
            case OP_REPEAT_INIT:
            case OP_REPEAT_STEP:
                has_backtrack = true;
                /* In search mode, allow constrained forward SPLIT to be
                 * counted as a useful op: teleport the scan to the taken
                 * branch so the profitability check evaluates what will
                 * actually run in compiled code. */
                if (op == OP_SPLIT && search_mode && scan + 8 <= vm->bc_len) {
                    size_t split_start = scan - 1; /* position of OP_SPLIT byte */
                    uint32_t pa = ((uint32_t)vm->bc[scan]   << 24) |
                                  ((uint32_t)vm->bc[scan+1] << 16) |
                                  ((uint32_t)vm->bc[scan+2] <<  8) |
                                   (uint32_t)vm->bc[scan+3];
                    uint32_t pb = ((uint32_t)vm->bc[scan+4] << 24) |
                                  ((uint32_t)vm->bc[scan+5] << 16) |
                                  ((uint32_t)vm->bc[scan+6] <<  8) |
                                   (uint32_t)vm->bc[scan+7];
                    if ((size_t)pa > split_start && (size_t)pb > split_start &&
                        (size_t)pa < vm->bc_len   && (size_t)pb < vm->bc_len) {
                        scan = (size_t)pa; /* teleport: evaluate taken branch */
                        useful++;          /* SPLIT counts as one useful op   */
                        break;             /* continue for-loop from pa       */
                    }
                }
                goto done;
            case OP_NOP:
                break; /* no operands, no useful work */
            case OP_ACCEPT:
            case OP_FAIL:
                goto done;
            case OP_JMP:
                /* Follow forward JMP: skip 4 bytes of target */
                if (scan + 4 <= vm->bc_len) {
                    uint32_t tgt = ((uint32_t)vm->bc[scan] << 24) |
                                   ((uint32_t)vm->bc[scan+1] << 16) |
                                   ((uint32_t)vm->bc[scan+2] << 8)  |
                                    (uint32_t)vm->bc[scan+3];
                    scan += 4;
                    if (tgt > scan && tgt < vm->bc_len) { scan = tgt; continue; }
                }
                goto done;
            case OP_LIT: {
                if (scan + 8 > vm->bc_len) goto done;
                uint32_t len = ((uint32_t)vm->bc[scan+4] << 24) |
                               ((uint32_t)vm->bc[scan+5] << 16) |
                               ((uint32_t)vm->bc[scan+6] << 8)  |
                                (uint32_t)vm->bc[scan+7];
                scan += 8 + len;
                useful++;
                break;
            }
            case OP_ANY:
            case OP_NOTANY:
            case OP_SPAN:
            case OP_BREAK:
            case OP_BREAKX:  /* BREAKX included in search-aware compiler scan */
                scan += 2;
                useful++;
                break;
            case OP_LEN:
                scan += 4;
                useful++;
                break;
            case OP_ANCHOR:
            case OP_CAP_START:
            case OP_CAP_END:
                scan += 1;
                break;
            case OP_ASSIGN:
                scan += 3;
                break;
            /* --- NEW opcodes for full coverage --- */
            case OP_REM:
                useful++;
                break;
            case OP_RPOS:
                scan += 4;
                useful++;
                break;
            case OP_RTAB:
                scan += 4;
                useful++;
                break;
            case OP_FENCE:
                break;
            case OP_LABEL:
                scan += 2;
                break;
            case OP_GOTO: {
                /* treat like JMP: follow forward if we can, then stop */
                if (scan + 2 > vm->bc_len) goto done;
                scan += 2;
                goto done;
            }
            case OP_GOTO_F:
                scan += 2;
                break;
            case OP_EMIT_LITERAL: {
                if (scan + 8 > vm->bc_len) goto done;
                uint32_t elt_off = ((uint32_t)vm->bc[scan] << 24)   |
                                   ((uint32_t)vm->bc[scan+1] << 16) |
                                   ((uint32_t)vm->bc[scan+2] << 8)  |
                                    (uint32_t)vm->bc[scan+3];
                uint32_t elt_len = ((uint32_t)vm->bc[scan+4] << 24) |
                                   ((uint32_t)vm->bc[scan+5] << 16) |
                                   ((uint32_t)vm->bc[scan+6] << 8)  |
                                    (uint32_t)vm->bc[scan+7];
                scan += 8;
                /* If inline data follows, skip it */
                size_t expected_ip = ip + (scan - (ip - 1 /* rewind for op byte*/));
                (void)expected_ip;
                if ((size_t)elt_off == (size_t)(vm->bc + scan - vm->bc))
                    scan += (size_t)elt_len;
                useful++;
                break;
            }
            case OP_EMIT_CAPTURE:
                scan += 1;
                useful++;
                break;
            case OP_EMIT_EXPR:
                scan += 2;
                useful++;
                break;
            case OP_EMIT_FORMAT: {
                if (scan + 2 > vm->bc_len) goto done;
                uint8_t sc_fmt = vm->bc[scan + 1];
                scan += 2;
                if (sc_fmt == SNBL_FMT_LPAD || sc_fmt == SNBL_FMT_RPAD) scan += 3;
                useful++;
                break;
            }
            case OP_EMIT_TABLE: {
                if (scan + 4 > vm->bc_len) goto done;
                uint8_t sc_ktype = vm->bc[scan + 2];
                uint8_t sc_nlen  = vm->bc[scan + 3];
                scan += 4 + sc_nlen;
                if (sc_ktype == 0) {
                    if (scan + 2 > vm->bc_len) goto done;
                    uint16_t kl = ((uint16_t)vm->bc[scan] << 8) | vm->bc[scan+1];
                    scan += 2 + kl;
                } else if (sc_ktype == 1) {
                    scan += 1;
                }
                useful++;
                break;
            }
            case OP_TABLE_GET:
            case OP_TABLE_SET: {
                if (scan + 4 > vm->bc_len) goto done;
                uint8_t nlen = vm->bc[scan + 4];
                scan += 5 + nlen;
                useful++;
                break;
            }
            case OP_BAL:
                scan += 8;
                useful++;
                break;
            case OP_EVAL:
                scan += 3;
                useful++;
                break;
            case OP_DYNAMIC:
                useful++;
                break;
            case OP_DYNAMIC_DEF: {
                if (scan + 4 > vm->bc_len) goto done;
                uint32_t src_len = ((uint32_t)vm->bc[scan] << 24)   |
                                   ((uint32_t)vm->bc[scan+1] << 16) |
                                   ((uint32_t)vm->bc[scan+2] << 8)  |
                                    (uint32_t)vm->bc[scan+3];
                scan += 4 + src_len;
                if (scan + 4 > vm->bc_len) goto done;
                uint32_t dyn_bcl = ((uint32_t)vm->bc[scan] << 24)   |
                                   ((uint32_t)vm->bc[scan+1] << 16) |
                                   ((uint32_t)vm->bc[scan+2] << 8)  |
                                    (uint32_t)vm->bc[scan+3];
                scan += 4 + dyn_bcl;
                break;
            }
            default:
                goto done;
        }
    }
done:
    /* Skip JIT if too few useful ops using mode-appropriate threshold */
    if (useful < min_ops) return false;

    /* Skip JIT for patterns where backtracking appears immediately after a
     * short prefix — in search mode we only trigger this for anchored-heavy
     * patterns (search workloads naturally have early-exit structure). */
    if (has_backtrack && cfg->skip_backtrack_heavy && !search_mode && useful < 5) return false;

    return true;
}

/* ---------------------------------------------------------------------------
 * NOTE: ARM64 instruction helpers, helper functions, CFG builder, and
 * legacy direct-emission code have been moved to jit_backend_arm64.c.
 * The backend is registered via snobol_jit_arm64_register() in snobol_jit_init().
 * --------------------------------------------------------------------------- */

/* (Legacy ARM64 direct-emission code removed — see jit_backend_arm64.c) */

jit_trace_fn snobol_jit_compile([[maybe_unused]] VM *vm, [[maybe_unused]] size_t start_ip, size_t *out_code_size) {
    if (out_code_size) *out_code_size = 0;

#if !defined(__aarch64__) && !defined(__arm64__)
    /* JIT code generation is ARM64-only. On all other architectures, return
     * nullptr so the interpreter is used unconditionally. */
    return nullptr;
#endif

    /* ---- IR pipeline ----
     *
     * 1. Lift VM bytecode to architecture-neutral IR.
     * 2. Run optimiser passes (DCE, copy-propagation).
     * 3. Dump IR to stderr if SNOBOL_JIT_DUMP_IR=1.
     * 4. Lower IR to machine code via the registered backend.
     *
     * If no backend is registered (shouldn't happen after snobol_jit_init()),
     * fall through to the legacy ARM64 direct-emission path below.
     */
    if (active_backend) {
        jit_ir_region_t *ir = jit_ir_lift_region(vm, start_ip);
        if (!ir || ir->non_compilable || ir->count == 0) {
            if (ir) jit_ir_region_free(ir);
            return nullptr;
        }

        /* Optimiser passes */
        jit_ir_copy_propagation(ir);
        jit_ir_dce(ir);

        /* Debug dump */
        {
            const char *dump_env = getenv("SNOBOL_JIT_DUMP_IR");
            if (dump_env && dump_env[0] == '1' && dump_env[1] == '\0')
                jit_ir_dump(ir, stderr);
        }

        /* Lower IR → machine code via backend */
        jit_region_t code_region = { nullptr, nullptr, 0 };
        void *code_ptr = active_backend->lower(ir, vm, &code_region);
        jit_ir_region_free(ir);

        if (!code_ptr) return nullptr;

        /* Count CFG blocks from the region (approximate: count 1 for linear) */
        global_jit_stats.jit_blocks_compiled_total += 1;

        if (out_code_size) *out_code_size = code_region.code_size;
        return (jit_trace_fn)code_ptr;
    }

    /* No backend registered — should not happen after snobol_jit_init(). */
    return nullptr;
}

#endif /* SNOBOL_JIT */
