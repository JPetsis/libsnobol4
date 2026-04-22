
#include "snobol/jit.h"

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
const SnobolJitConfig *snobol_jit_get_config(void) { return &global_jit_cfg; }

void snobol_jit_load_config_from_env(void) {
    const char *v;
    if ((v = getenv("SNOBOL_JIT_HOTNESS")))      global_jit_cfg.hotness_threshold        = (uint64_t)strtoull(v, NULL, 10);
    if ((v = getenv("SNOBOL_JIT_MAX_EXIT_PCT")))  global_jit_cfg.max_exit_rate_pct        = (uint32_t)strtoul(v, NULL, 10);
    if ((v = getenv("SNOBOL_JIT_BUDGET_NS")))     global_jit_cfg.compile_budget_ns        = (uint64_t)strtoull(v, NULL, 10);
    if ((v = getenv("SNOBOL_JIT_CACHE_MAX")))     global_jit_cfg.cache_max_entries        = (uint32_t)strtoul(v, NULL, 10);
    if ((v = getenv("SNOBOL_JIT_MIN_OPS")))       global_jit_cfg.min_useful_ops           = (uint32_t)strtoul(v, NULL, 10);
    if ((v = getenv("SNOBOL_JIT_SKIP_BT")))       global_jit_cfg.skip_backtrack_heavy     = (strtol(v, NULL, 10) != 0);
    if ((v = getenv("SNOBOL_JIT_SEARCH_HOT")))    global_jit_cfg.search_hotness_threshold = (uint64_t)strtoull(v, NULL, 10);
    if ((v = getenv("SNOBOL_JIT_SEARCH_OPS")))    global_jit_cfg.search_min_useful_ops    = (uint32_t)strtoul(v, NULL, 10);
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

#define JIT_CACHE_MAX_HARD 512   /* absolute upper bound regardless of config */

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
                ctx->traces[i] = NULL;
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
    jit_cache[jit_cache_count] = NULL;
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
    if (!ctx) return NULL;
    ctx->bc_len      = bc_len;
    ctx->hash        = hash;
    ctx->ref_count   = 1;
    ctx->lru_counter = ++lru_clock;
    ctx->ip_counts   = snobol_calloc(bc_len, sizeof(uint64_t));
    ctx->traces      = snobol_calloc(bc_len, sizeof(void *));
    ctx->trace_sizes = snobol_calloc(bc_len, sizeof(size_t));

    if (!ctx->ip_counts || !ctx->traces || !ctx->trace_sizes) {
        /* Allocation failure: clean up and return NULL */
        if (ctx->ip_counts)   snobol_free(ctx->ip_counts);
        if (ctx->traces)      snobol_free(ctx->traces);
        if (ctx->trace_sizes) snobol_free(ctx->trace_sizes);
        snobol_free(ctx);
        return NULL;
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
}

void snobol_jit_shutdown(void) {
    for (int i = 0; i < jit_cache_count; i++) {
        if (jit_cache[i]) {
            /* Force-destroy even if ref_count > 0 (process teardown) */
            jit_cache[i]->ref_count = 0;
            jit_context_destroy(jit_cache[i]);
            jit_cache[i] = NULL;
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
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    return (ptr == MAP_FAILED) ? NULL : ptr;
#else
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (ptr == MAP_FAILED) ? NULL : ptr;
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
 * ARM64 instruction helpers
 * --------------------------------------------------------------------------- */
#define A64_RET 0xd65f03c0
#define A64_LDR_X_X_IMM(rt, rn, imm) (0xf9400000 | ((rt) & 31) | (((rn) & 31) << 5) | (((imm) / 8) << 10))
#define A64_STR_X_X_IMM(rt, rn, imm) (0xf9000000 | ((rt) & 31) | (((rn) & 31) << 5) | (((imm) / 8) << 10))
#define A64_ADD_X_X_X(rd, rn, rm)    (0x8b000000 | ((rd) & 31) | (((rn) & 31) << 5) | (((rm) & 31) << 16))
#define A64_CMP_X_X(rn, rm)          (0xeb00001f | (((rn) & 31) << 5) | (((rm) & 31) << 16))
#define A64_CMP_W_W(rn, rm)          (0x6b00001f | (((rn) & 31) << 5) | (((rm) & 31) << 16))
#define A64_B_GE(imm)                (0x5400000a | (((imm) & 0x3ffff) << 5))
#define A64_B(imm)                   (0x14000000 | ((imm) & 0x3ffffff))
#define A64_LDRB_W_X_X(rt, rn, rm)   (0x38606800 | ((rt) & 31) | (((rn) & 31) << 5) | (((rm) & 31) << 16))
#define A64_MOV_X_IMM(rd, imm)       (0xd2800000 | ((rd) & 31) | (((imm) & 0xffff) << 5))
#define A64_MOV_W_IMM(rd, imm)       (0x52800000 | ((rd) & 31) | (((imm) & 0xffff) << 5))
#define A64_MOVK_X_IMM_LSL16(rd, imm) (0xf2a00000 | ((rd) & 31) | (((imm) & 0xffff) << 5))
#define A64_MOVK_X_IMM_LSL32(rd, imm) (0xf2c00000 | ((rd) & 31) | (((imm) & 0xffff) << 5))
#define A64_MOVK_X_IMM_LSL48(rd, imm) (0xf2e00000 | ((rd) & 31) | (((imm) & 0xffff) << 5))
#define A64_TBZ(rt, bit, imm)        (0xb6000000 | ((rt) & 31) | (((bit) & 0x1f) << 19) | ((((bit) >> 5) & 1) << 31) | (((imm) & 0x3fff) << 5))
#define A64_TBNZ(rt, bit, imm)       (0xb7000000 | ((rt) & 31) | (((bit) & 0x1f) << 19) | ((((bit) >> 5) & 1) << 31) | (((imm) & 0x3fff) << 5))
#define A64_AND_W_W_W(rd, rn, rm)    (0x0a000000 | ((rd) & 31) | (((rn) & 31) << 5) | (((rm) & 31) << 16))
#define A64_LSR_X_X_X(rd, rn, rm)    (0x9ac02000 | ((rd) & 31) | (((rn) & 31) << 5) | (((rm) & 31) << 16))
#define A64_B_EQ(imm)                (0x54000000 | (((imm) & 0x3ffff) << 5))
#define A64_B_NE(imm)                (0x54000001 | (((imm) & 0x3ffff) << 5))
#define A64_B_HI(imm)                (0x54000008 | (((imm) & 0x3ffff) << 5))

typedef struct {
    uint32_t *p;
    uint32_t *code_start;
    size_t    code_size;
} JITState;

typedef struct {
    uint32_t *instr_p;
    size_t    ip;
} FailPatch;

/* Metadata for each op in the compiled sequence (supports JMP following) */
typedef struct {
    size_t ip;       /* bytecode IP of this op */
    size_t next_ip;  /* bytecode IP after this op (including operands) */
    uint8_t op;
} OpInfo;

/* ---------------------------------------------------------------------------
 * CFG-based multi-block JIT structures (Phase 1c)
 * --------------------------------------------------------------------------- */

#define JIT_CFG_MAX_BLOCKS   64   /* BFS expansion limit */
#define JIT_LOOP_ITER_MAX    1024 /* max iterations before bailing to interpreter */

typedef enum {
    BLOCK_TERM_SPLIT,    /* SPLIT opcode — two outgoing edges (succ_a, succ_b) */
    BLOCK_TERM_JMP_FWD,  /* unconditional forward JMP */
    BLOCK_TERM_JMP_BWD,  /* backward JMP — loop body back-edge */
    BLOCK_TERM_EXIT,     /* ACCEPT / FAIL / unknown / out-of-bounds → bail out */
} BlockTerm;

typedef struct {
    size_t    start_ip;   /* bytecode IP where this block starts */
    size_t    term_ip;    /* IP of the terminating opcode (SPLIT/JMP/…) */
    size_t    next_ip;    /* IP immediately after terminator + operands */
    BlockTerm term;
    size_t    succ_a;     /* primary successor: SPLIT arm-a / JMP target */
    size_t    succ_b;     /* secondary successor: SPLIT arm-b only */
    bool      worthy;     /* block contains ≥1 useful (non-trivial) op */
    /* Resolved after BFS completes: */
    int       succ_a_blk; /* index into cfg->blocks, -1 = not in region */
    int       succ_b_blk; /* index into cfg->blocks, -1 = not in region */
    /* Filled during pass 2: */
    uint32_t *stub_start; /* address of this block's stub in the code buffer */
} JitBlock;

typedef struct {
    JitBlock blocks[JIT_CFG_MAX_BLOCKS];
    int      count;
    bool     has_backward; /* any backward edge (loop) present */
} JitCfg;

/* Forward-branch fixup: an emitted B(0) that needs to be patched to point at
 * the stub for block whose bytecode entry is target_ip. */
typedef struct {
    uint32_t *instr_p;   /* address of the placeholder B instruction */
    size_t    target_ip; /* bytecode IP of the target block */
} StubPatch;

/* ARM64 instruction encodings for the CFG prologue / epilogue.
 *
 * STP x19, x30, [sp, #-16]!  — pre-index store pair (callee-saved x19 + LR)
 *   Formula: 0xA9800000 | (imm7 << 15) | (Rt2 << 10) | (Rn << 5) | Rt
 *   imm7 = -16/8 = -2 → 7-bit 2's-complement = 0x7E
 *   Rt=19, Rt2=30, Rn=31(sp)
 *   = 0xA9800000 | (0x7E<<15) | (30<<10) | (31<<5) | 19
 *   = 0xA9800000 | 0x003F0000 | 0x7800 | 0x3E0 | 0x13
 *   = 0xA9BF7BF3
 *
 * LDP x19, x30, [sp], #16    — post-index load pair
 *   Formula: 0xA8C00000 | (imm7 << 15) | (Rt2 << 10) | (Rn << 5) | Rt
 *   imm7 = +16/8 = +2 → 0x02
 *   = 0xA8C00000 | (0x02<<15) | (30<<10) | (31<<5) | 19
 *   = 0xA8C17BF3
 *
 * SUBS X19, X19, #1          — decrement loop counter and set flags
 *   Encoding: 0xF1000000 | (1<<10) | (19<<5) | 19 = 0xF1000673
 */
#define A64_STP_X19_X30_PRE16   0xA9BF7BF3u
#define A64_LDP_X19_X30_POST16  0xA8C17BF3u
#define A64_SUBS_X19_X19_1      0xF1000673u

static void emit_instr(JITState *js, uint32_t ins) { *(js->p)++ = ins; }

static void emit_mov_x64(JITState *js, int rd, uint64_t val) {
    emit_instr(js, A64_MOV_X_IMM(rd, (uint32_t)(val & 0xffff)));
    emit_instr(js, A64_MOVK_X_IMM_LSL16(rd, (uint32_t)((val >> 16) & 0xffff)));
    emit_instr(js, A64_MOVK_X_IMM_LSL32(rd, (uint32_t)((val >> 32) & 0xffff)));
    emit_instr(js, A64_MOVK_X_IMM_LSL48(rd, (uint32_t)((val >> 48) & 0xffff)));
}

static void emit_mov_w32(JITState *js, int rd, uint32_t val) {
    emit_instr(js, A64_MOV_W_IMM(rd, val & 0xffff));
    if (val > 0xffff) emit_instr(js, 0x72a00000 | (rd & 31) | ((val >> 16) << 5));
}

static void emit_ldrb_w_x_imm(JITState *js, int rt, int rn, uint32_t imm) {
    emit_instr(js, 0x39400000 | (rt & 31) | ((rn & 31) << 5) | (imm << 10));
}

static void emit_strb_w_x_imm(JITState *js, int rt, int rn, uint32_t imm) {
    emit_instr(js, 0x39000000 | (rt & 31) | ((rn & 31) << 5) | (imm << 10));
}

static void emit_ubfx_w(JITState *js, int rd, int rn, int lsb, int width) {
    emit_instr(js, 0x53000000 | (rd & 31) | ((rn & 31) << 5) | (lsb << 10) | ((lsb + width - 1) << 16));
}

static void emit_patch_b_cond(uint32_t *p, uint32_t *target, uint32_t base_opcode) {
    intptr_t diff = target - p;
    *p = base_opcode | (((uint32_t)diff & 0x3ffff) << 5);
}

static void emit_patch_b(uint32_t *p, uint32_t *target) {
    intptr_t diff = target - p;
    *p = A64_B((uint32_t)diff & 0x3ffffff);
}

static void emit_patch_tbz(uint32_t *p, uint32_t *target, uint32_t base_opcode) {
    intptr_t diff = target - p;
    *p = base_opcode | (((uint32_t)diff & 0x3fff) << 5);
}

/* ---------------------------------------------------------------------------
 * Compiler
 *
 * Changes vs original:
 *  • Two-pass: first build OpInfo[] (following forward JMPs), then emit.
 *  • out_code_size set on success so caller can store it in trace_sizes[].
 *  • compilations_total NOT incremented here — caller owns the counter.
 * --------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * CFG builder helpers
 * --------------------------------------------------------------------------- */

static inline uint32_t cfg_read_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

static int jit_find_block(const JitCfg *cfg, size_t ip) {
    for (int i = 0; i < cfg->count; i++)
        if (cfg->blocks[i].start_ip == ip) return i;
    return -1;
}

/* Scan one basic block starting at start_ip.
 * A block ends when we hit SPLIT, JMP, ACCEPT, FAIL, REPEAT_*, or unknown op.
 * Forward JMPs are treated as block terminators (not inlined). */
static void jit_cfg_scan_block(const VM *vm, size_t start_ip, JitBlock *blk) {
    blk->start_ip   = start_ip;
    blk->worthy     = false;
    blk->stub_start = NULL;
    blk->succ_a_blk = -1;
    blk->succ_b_blk = -1;
    blk->succ_a     = 0;
    blk->succ_b     = 0;

    size_t scan = start_ip;
    while (scan < vm->bc_len) {
        uint8_t op = vm->bc[scan];

        if (op == OP_SPLIT) {
            if (scan + 9 > vm->bc_len) goto done_exit;
            blk->term    = BLOCK_TERM_SPLIT;
            blk->term_ip = scan;
            blk->next_ip = scan + 9;
            blk->succ_a  = (size_t)cfg_read_u32(vm->bc + scan + 1);
            blk->succ_b  = (size_t)cfg_read_u32(vm->bc + scan + 5);
            return;
        }
        if (op == OP_JMP) {
            if (scan + 5 > vm->bc_len) goto done_exit;
            size_t tgt = (size_t)cfg_read_u32(vm->bc + scan + 1);
            blk->term_ip = scan;
            blk->next_ip = scan + 5;
            blk->succ_a  = tgt;
            blk->term    = (tgt > scan) ? BLOCK_TERM_JMP_FWD : BLOCK_TERM_JMP_BWD;
            return;
        }
        if (op == OP_ACCEPT || op == OP_FAIL ||
            op == OP_REPEAT_INIT || op == OP_REPEAT_STEP) {
            blk->term    = BLOCK_TERM_EXIT;
            blk->term_ip = scan;
            blk->next_ip = scan + 1;
            return;
        }

        /* Advance over known linear ops */
        size_t next;
        switch (op) {
            case OP_NOP:   next = scan + 1; break;
            case OP_LIT: {
                if (scan + 9 > vm->bc_len) goto done_exit;
                uint32_t lit_len = cfg_read_u32(vm->bc + scan + 5);
                next = scan + 9 + (size_t)lit_len;
                blk->worthy = true;
                break;
            }
            case OP_ANY: case OP_NOTANY: case OP_SPAN:
            case OP_BREAK: case OP_BREAKX:
                next = scan + 3;
                blk->worthy = true;
                break;
            case OP_LEN:
                next = scan + 5;
                blk->worthy = true;
                break;
            case OP_ANCHOR: case OP_CAP_START: case OP_CAP_END:
                next = scan + 2;
                break;
            case OP_ASSIGN:
                next = scan + 4;
                break;
            default:
                goto done_exit;
        }
        if (next > vm->bc_len) goto done_exit;
        scan = next;
    }
done_exit:
    blk->term    = BLOCK_TERM_EXIT;
    blk->term_ip = scan;
    blk->next_ip = scan;
}

/* BFS CFG discovery from entry_ip.
 * Returns number of blocks found (0 = nothing worth compiling). */
static int jit_cfg_build(const VM *vm, size_t entry_ip, JitCfg *cfg) {
    cfg->count        = 0;
    cfg->has_backward = false;

    size_t queue[JIT_CFG_MAX_BLOCKS * 2];
    int    qhead = 0, qtail = 0;
    size_t visited[JIT_CFG_MAX_BLOCKS];
    int    vcount = 0;

    queue[qtail++] = entry_ip;

    while (qhead < qtail) {
        size_t ip = queue[qhead++];
        /* Skip already-visited IPs */
        bool seen = false;
        for (int i = 0; i < vcount; i++) if (visited[i] == ip) { seen = true; break; }
        if (seen) continue;
        visited[vcount++] = ip;

        if (cfg->count >= JIT_CFG_MAX_BLOCKS) break;

        JitBlock *blk = &cfg->blocks[cfg->count++];
        jit_cfg_scan_block(vm, ip, blk);

        switch (blk->term) {
            case BLOCK_TERM_SPLIT:
                if (cfg->count < JIT_CFG_MAX_BLOCKS) {
                    bool sa = false, sb = false;
                    for (int i = 0; i < vcount; i++) {
                        if (visited[i] == blk->succ_a) sa = true;
                        if (visited[i] == blk->succ_b) sb = true;
                    }
                    if (!sa && qtail < (int)(sizeof(queue)/sizeof(queue[0])))
                        queue[qtail++] = blk->succ_a;
                    if (!sb && qtail < (int)(sizeof(queue)/sizeof(queue[0])))
                        queue[qtail++] = blk->succ_b;
                }
                break;
            case BLOCK_TERM_JMP_FWD: {
                bool sa = false;
                for (int i = 0; i < vcount; i++) if (visited[i] == blk->succ_a) sa = true;
                if (!sa && qtail < (int)(sizeof(queue)/sizeof(queue[0])))
                    queue[qtail++] = blk->succ_a;
                break;
            }
            case BLOCK_TERM_JMP_BWD:
                cfg->has_backward = true;
                break;
            case BLOCK_TERM_EXIT:
                break;
        }
    }

    /* Resolve succ_a_blk / succ_b_blk */
    for (int i = 0; i < cfg->count; i++) {
        JitBlock *blk = &cfg->blocks[i];
        if (blk->term == BLOCK_TERM_SPLIT || blk->term == BLOCK_TERM_JMP_FWD ||
            blk->term == BLOCK_TERM_JMP_BWD)
            blk->succ_a_blk = jit_find_block(cfg, blk->succ_a);
        if (blk->term == BLOCK_TERM_SPLIT)
            blk->succ_b_blk = jit_find_block(cfg, blk->succ_b);
    }

    /* Require at least one worthy block */
    bool any_worthy = false;
    for (int i = 0; i < cfg->count; i++)
        if (cfg->blocks[i].worthy) { any_worthy = true; break; }

    return (any_worthy && cfg->count > 0) ? cfg->count : 0;
}

/* ---------------------------------------------------------------------------
 * Per-block op emission helper (shared by CFG and single-block paths)
 * Emits ARM64 for ops from start_ip (inclusive) to term_ip (exclusive).
 * Returns false if an op cannot be compiled (caller should treat as EXIT).
 * --------------------------------------------------------------------------- */
static bool emit_block_ops(
    JITState *js, const VM *vm,
    size_t start_ip, size_t term_ip,
    FailPatch *fail_patches, size_t *fail_patch_count)
{
    size_t scan = start_ip;
    while (scan < term_ip && scan < vm->bc_len) {
        uint8_t op  = vm->bc[scan];
        size_t oip  = scan + 1; /* operand start */
        size_t cur  = scan;     /* saved for fail-patch IP */

        if (op == OP_NOP) { scan++; continue; }

        if (op == OP_LIT) {
            if (oip + 8 > vm->bc_len) return false;
            uint32_t off     = cfg_read_u32(vm->bc + oip);
            uint32_t lit_len = cfg_read_u32(vm->bc + oip + 4);
            scan = oip + 8 + (size_t)lit_len;
            emit_mov_x64(js, 7, lit_len);
            emit_instr(js, A64_ADD_X_X_X(8, 2, 7));
            emit_instr(js, A64_CMP_X_X(8, 3));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, A64_B_HI(0));
            for (uint32_t j = 0; j < lit_len; j++) {
                emit_instr(js, A64_LDRB_W_X_X(4, 1, 2));
                emit_mov_w32(js, 7, vm->bc[off + j]);
                emit_instr(js, A64_CMP_W_W(4, 7));
                fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
                emit_instr(js, A64_B_NE(0));
                emit_instr(js, A64_MOV_X_IMM(7, 1));
                emit_instr(js, A64_ADD_X_X_X(2, 2, 7));
            }
            continue;
        }

        if (op == OP_SPAN || op == OP_ANY || op == OP_NOTANY ||
            op == OP_BREAK || op == OP_BREAKX) {
            if (oip + 2 > vm->bc_len) return false;
            scan = oip + 2;
            uint16_t set_id = ((uint16_t)vm->bc[oip] << 8) | vm->bc[oip+1];
            uint16_t count16, ci;
            const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count16, &ci);
            uint64_t ascii_map[2];
            if (!ranges || !ranges_to_ascii_bitmap(ranges, count16, ascii_map)) return false;

            emit_mov_x64(js, 5, ascii_map[0]);
            emit_mov_x64(js, 6, ascii_map[1]);
            emit_instr(js, A64_CMP_X_X(2, 3));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, A64_B_GE(0));
            emit_instr(js, A64_LDRB_W_X_X(4, 1, 2));
            emit_instr(js, A64_MOV_W_IMM(7, 127));
            emit_instr(js, A64_CMP_W_W(4, 7));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, A64_B_HI(0));
            uint32_t *tbz_instr = js->p; emit_instr(js, 0);
            emit_ubfx_w(js, 7, 4, 0, 6);
            emit_instr(js, A64_LSR_X_X_X(8, 6, 7));
            if (op == OP_NOTANY) {
                fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
                emit_instr(js, A64_TBZ(8, 0, 0));
            } else {
                fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
                emit_instr(js, A64_TBZ(8, 0, 0));
            }
            uint32_t *b_success1 = js->p; emit_instr(js, 0);
            *tbz_instr = A64_TBZ(4, 6, (js->p - tbz_instr));
            emit_ubfx_w(js, 7, 4, 0, 6);
            emit_instr(js, A64_LSR_X_X_X(8, 5, 7));
            if (op == OP_NOTANY) {
                fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
                emit_instr(js, A64_TBZ(8, 0, 0));
            } else {
                fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
                emit_instr(js, A64_TBZ(8, 0, 0));
            }
            uint32_t *success_label = js->p;
            emit_patch_b(b_success1, success_label);
            emit_instr(js, A64_MOV_X_IMM(7, 1));
            emit_instr(js, A64_ADD_X_X_X(2, 2, 7));
            if (op == OP_SPAN) {
                uint32_t *loop_start = js->p;
                emit_instr(js, A64_CMP_X_X(2, 3));
                uint32_t *loop_ge_done = js->p; emit_instr(js, A64_B_GE(0));
                emit_instr(js, A64_LDRB_W_X_X(4, 1, 2));
                emit_instr(js, A64_MOV_W_IMM(7, 127));
                emit_instr(js, A64_CMP_W_W(4, 7));
                uint32_t *loop_hi_done = js->p; emit_instr(js, A64_B_HI(0));
                uint32_t *loop_tbz = js->p; emit_instr(js, 0);
                emit_ubfx_w(js, 7, 4, 0, 6);
                emit_instr(js, A64_LSR_X_X_X(8, 6, 7));
                uint32_t *loop_bit64_done = js->p; emit_instr(js, A64_TBZ(8, 0, 0));
                emit_instr(js, A64_MOV_X_IMM(7, 1));
                emit_instr(js, A64_ADD_X_X_X(2, 2, 7));
                emit_instr(js, A64_B(loop_start - js->p));
                *loop_tbz = A64_TBZ(4, 6, (js->p - loop_tbz));
                emit_ubfx_w(js, 7, 4, 0, 6);
                emit_instr(js, A64_LSR_X_X_X(8, 5, 7));
                uint32_t *loop_bit0_done = js->p; emit_instr(js, A64_TBZ(8, 0, 0));
                emit_instr(js, A64_MOV_X_IMM(7, 1));
                emit_instr(js, A64_ADD_X_X_X(2, 2, 7));
                emit_instr(js, A64_B(loop_start - js->p));
                uint32_t *span_done = js->p;
                emit_patch_b_cond(loop_ge_done,  span_done, 0x5400000a);
                emit_patch_b_cond(loop_hi_done,  span_done, 0x54000008);
                emit_patch_tbz(loop_bit64_done, span_done, *loop_bit64_done & 0xfff8001f);
                emit_patch_tbz(loop_bit0_done,  span_done, *loop_bit0_done  & 0xfff8001f);
            }
            continue;
        }

        if (op == OP_CAP_START || op == OP_CAP_END) {
            if (oip + 1 > vm->bc_len) return false;
            uint8_t r = vm->bc[oip];
            scan = oip + 1;
            size_t off_field = (op == OP_CAP_START)
                               ? offsetof(VM, cap_start) : offsetof(VM, cap_end);
            emit_instr(js, A64_STR_X_X_IMM(2, 0, off_field + r * 8));
            emit_ldrb_w_x_imm(js, 7, 0, (uint32_t)offsetof(VM, max_cap_used));
            emit_instr(js, A64_MOV_W_IMM(8, r + 1));
            emit_instr(js, A64_CMP_W_W(7, 8));
            uint32_t *lt_skip = js->p; emit_instr(js, A64_B_GE(0));
            emit_strb_w_x_imm(js, 8, 0, (uint32_t)offsetof(VM, max_cap_used));
            emit_patch_b_cond(lt_skip, js->p, 0x5400000a);
            continue;
        }

        if (op == OP_LEN) {
            if (oip + 4 > vm->bc_len) return false;
            scan = oip + 4;
            uint32_t n = cfg_read_u32(vm->bc + oip);
            emit_mov_x64(js, 7, n);
            emit_instr(js, A64_ADD_X_X_X(8, 2, 7));
            emit_instr(js, A64_CMP_X_X(8, 3));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, A64_B_HI(0));
            emit_instr(js, A64_ADD_X_X_X(2, 2, 7));
            continue;
        }

        if (op == OP_ANCHOR) {
            if (oip + 1 > vm->bc_len) return false;
            uint8_t type = vm->bc[oip];
            scan = oip + 1;
            if (type == 0) { emit_instr(js, A64_MOV_X_IMM(7, 0)); emit_instr(js, A64_CMP_X_X(2, 7)); }
            else emit_instr(js, A64_CMP_X_X(2, 3));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, A64_B_NE(0));
            continue;
        }

        if (op == OP_ASSIGN) {
            if (oip + 3 > vm->bc_len) return false;
            uint16_t var = ((uint16_t)vm->bc[oip] << 8) | vm->bc[oip+1];
            uint8_t  reg = vm->bc[oip+2];
            scan = oip + 3;
            emit_instr(js, A64_LDR_X_X_IMM(7, 0, offsetof(VM, cap_start) + reg * 8));
            emit_instr(js, A64_STR_X_X_IMM(7, 0, offsetof(VM, var_start)  + var * 8));
            emit_instr(js, A64_LDR_X_X_IMM(7, 0, offsetof(VM, cap_end)   + reg * 8));
            emit_instr(js, A64_STR_X_X_IMM(7, 0, offsetof(VM, var_end)   + var * 8));
            emit_instr(js, A64_LDR_X_X_IMM(7, 0, offsetof(VM, var_count)));
            emit_mov_x64(js, 8, var + 1);
            emit_instr(js, A64_CMP_X_X(7, 8));
            uint32_t *lt_skip = js->p; emit_instr(js, A64_B_GE(0));
            emit_instr(js, A64_STR_X_X_IMM(8, 0, offsetof(VM, var_count)));
            emit_patch_b_cond(lt_skip, js->p, 0x5400000a);
            continue;
        }

        /* Unknown or unreachable op */
        return false;
    }
    return true;
}

/* Helper: emit a bail-out epilogue for the CFG path (restores x19/x30 frame). */
static void emit_cfg_bailout(JITState *js, size_t bail_ip) {
    emit_instr(js, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
    emit_mov_x64(js, 7, bail_ip);
    emit_instr(js, A64_STR_X_X_IMM(7, 0, offsetof(VM, ip)));
    emit_instr(js, A64_LDP_X19_X30_POST16); /* LDP x19, x30, [sp], #16 */
    emit_instr(js, A64_RET);
}

/* ---------------------------------------------------------------------------
 * CFG multi-block compiler
 * --------------------------------------------------------------------------- */
static jit_trace_fn snobol_jit_compile_cfg(VM *vm, JitCfg *cfg,
                                            size_t *out_code_size) {
    size_t code_size = 32768; /* larger buffer for multi-block regions */
    uint32_t *code = (uint32_t *)snobol_jit_alloc_code(code_size);
    if (!code) return NULL;

#ifdef __APPLE__
    pthread_jit_write_protect_np(0);
#endif

    JITState js = { code, code, code_size };

    FailPatch fail_patches[1024];
    size_t    fail_patch_count = 0;

    StubPatch stub_patches[256];
    int       stub_patch_count = 0;

    /* ---- Prologue ---- */
    emit_instr(&js, A64_STP_X19_X30_PRE16);          /* STP x19, x30, [sp, #-16]! */
    emit_instr(&js, A64_LDR_X_X_IMM(1, 0, offsetof(VM, s)));
    emit_instr(&js, A64_LDR_X_X_IMM(2, 0, offsetof(VM, pos)));
    emit_instr(&js, A64_LDR_X_X_IMM(3, 0, offsetof(VM, len)));
    if (cfg->has_backward)
        emit_mov_w32(&js, 19, JIT_LOOP_ITER_MAX);     /* MOV w19, #1024 */

    /* ---- Emit one stub per CFG block (BFS order) ---- */
    for (int i = 0; i < cfg->count; i++) {
        JitBlock *blk = &cfg->blocks[i];
        blk->stub_start = js.p;

        /* Emit the block's non-terminator ops */
        bool ok = emit_block_ops(&js, vm, blk->start_ip, blk->term_ip,
                                 fail_patches, &fail_patch_count);

        if (!ok) {
            /* op compilation failed — treat as bail-out at term_ip */
            emit_cfg_bailout(&js, blk->term_ip);
            /* Mark remaining blocks as EXIT so fixup pass ignores them */
            blk->term = BLOCK_TERM_EXIT;
            continue;
        }

        /* Emit terminator */
        switch (blk->term) {
        case BLOCK_TERM_SPLIT: {
            if (blk->succ_a_blk < 0) {
                /* arm-a not in region — bail at SPLIT ip so interpreter handles it */
                emit_cfg_bailout(&js, blk->term_ip);
                break;
            }
            /* Push choice for arm-b via vm_push_choice(vm, succ_b, pos).
             *
             * ARM64 calling convention: x0–x18 are caller-saved (volatile).
             * We MUST save x0 (vm pointer) because the BLR clobbers it with
             * the return value of vm_push_choice.  Also save x1 (vm->s),
             * x2 (pos), and x30 (LR).  x3 (vm->len) is reloaded afterwards.
             *
             * Stack frame (32 bytes, 16-byte aligned):
             *   [sp+ 0] x0  (vm pointer)   ← critical: preserved across BLR
             *   [sp+ 8] x30 (return addr)
             *   [sp+16] x1  (vm->s)
             *   [sp+24] x2  (pos)
             */
            emit_instr(&js, 0xa9be7be0u); /* STP x0, x30, [sp, #-32]! */
            emit_instr(&js, 0xa9010be1u); /* STP x1, x2,  [sp, #16]   */
            emit_mov_x64(&js, 9, (uint64_t)(uintptr_t)vm_push_choice);
            emit_mov_x64(&js, 1, (uint64_t)blk->succ_b);
            /* x0=vm (arg 0), x2=pos (arg 2 — unchanged since we saved it) */
            emit_instr(&js, 0xd63f0120u); /* BLR x9 */
            emit_instr(&js, 0xa9410be1u); /* LDP x1, x2,  [sp, #16]   */
            emit_instr(&js, 0xa8c27be0u); /* LDP x0, x30, [sp], #32   */
            /* Reload x3=len (clobbered by callee) from vm->len */
            emit_instr(&js, A64_LDR_X_X_IMM(3, 0, offsetof(VM, len)));
            /* Branch to arm-a stub (forward; fixup later) */
            stub_patches[stub_patch_count++] = (StubPatch){ js.p, blk->succ_a };
            emit_instr(&js, A64_B(0));
            break;
        }
        case BLOCK_TERM_JMP_FWD: {
            if (blk->succ_a_blk < 0) {
                emit_cfg_bailout(&js, blk->term_ip);
                break;
            }
            /* Forward branch to target stub (fixup later) */
            stub_patches[stub_patch_count++] = (StubPatch){ js.p, blk->succ_a };
            emit_instr(&js, A64_B(0));
            break;
        }
        case BLOCK_TERM_JMP_BWD: {
            if (blk->succ_a_blk < 0) {
                emit_cfg_bailout(&js, blk->term_ip);
                break;
            }
            /* Loop guard: decrement x19; bail out if zero */
            emit_instr(&js, A64_SUBS_X19_X19_1);           /* SUBS x19, x19, #1 */
            uint32_t *bail_patch = js.p;
            emit_instr(&js, A64_B_EQ(0));                   /* B.EQ <bail> placeholder */
            /* Backward branch to loop-head stub (already emitted → known address) */
            uint32_t *loop_head = cfg->blocks[blk->succ_a_blk].stub_start;
            intptr_t  diff = loop_head - js.p;
            emit_instr(&js, A64_B((uint32_t)(diff & 0x3ffffff)));
            /* Bail-out epilogue: patch B.EQ here */
            emit_patch_b_cond(bail_patch, js.p, 0x54000000); /* B.EQ to here */
            emit_cfg_bailout(&js, blk->term_ip);
            break;
        }
        case BLOCK_TERM_EXIT:
            emit_cfg_bailout(&js, blk->term_ip);
            break;
        }
    }

    /* ---- Fail stubs (one per fail-patch site) ---- */
    for (size_t f = 0; f < fail_patch_count; f++) {
        uint32_t *fail_stub = js.p;
        emit_cfg_bailout(&js, fail_patches[f].ip);
        /* Patch the conditional branch at the op site */
        uint32_t orig = *fail_patches[f].instr_p;
        if ((orig & 0xff000000) == 0x54000000)
            emit_patch_b_cond(fail_patches[f].instr_p, fail_stub, orig & 0xff00001f);
        else if ((orig & 0x7e000000) == 0x36000000)
            emit_patch_tbz(fail_patches[f].instr_p, fail_stub, orig & 0xfff8001f);
    }

    /* ---- Forward-branch fixup: resolve stub-to-stub B instructions ---- */
    for (int s = 0; s < stub_patch_count; s++) {
        uint32_t *p         = stub_patches[s].instr_p;
        size_t    target_ip = stub_patches[s].target_ip;
        int       blk_idx   = jit_find_block(cfg, target_ip);
        if (blk_idx >= 0 && cfg->blocks[blk_idx].stub_start) {
            emit_patch_b(p, cfg->blocks[blk_idx].stub_start);
        }
        /* If unresolved (shouldn't happen in practice): leave as B(0) → falls
         * through to next instruction which will eventually bail out. */
    }

    snobol_jit_seal_code(code, code_size);

    /* Update global block counter */
    global_jit_stats.jit_blocks_compiled_total += (uint64_t)cfg->count;

    if (out_code_size) *out_code_size = code_size;
    return (jit_trace_fn)code;
}


#define MAX_OPS_IN_REGION 64

jit_trace_fn snobol_jit_compile(VM *vm, size_t start_ip, size_t *out_code_size) {
    if (out_code_size) *out_code_size = 0;

#if !defined(__aarch64__) && !defined(__arm64__)
    /* JIT code generation is ARM64-only. On all other architectures, return
     * NULL so the interpreter is used unconditionally. */
    (void)vm; (void)start_ip;
    return NULL;
#endif

    /* ---- Try CFG-based multi-block compilation first ---- *
     * Build the CFG; if we get more than one block OR the single block has a
     * backward edge (loop), use the multi-block CFG compiler.
     * Zero-SPLIT single-block patterns fall through to the existing linear path
     * so compile latency is unchanged for straight-line regions. */
    JitCfg cfg;
    int n_blocks = jit_cfg_build(vm, start_ip, &cfg);
    if (n_blocks >= 2 || (n_blocks == 1 && cfg.has_backward)) {
        return snobol_jit_compile_cfg(vm, &cfg, out_code_size);
    }

    /* ---- Pass 1: scan and collect op sequence (follow forward JMPs) ---- */
    OpInfo op_seq[MAX_OPS_IN_REGION];
    size_t op_count = 0;
    bool   worthy   = false;

    size_t scan_ip = start_ip;
    while (scan_ip < vm->bc_len && op_count < MAX_OPS_IN_REGION) {
        uint8_t op = vm->bc[scan_ip];

        /* Forward JMP: teleport, don't add to op sequence */
        if (op == OP_JMP) {
            size_t tmp = scan_ip + 1;
            if (tmp + 4 > vm->bc_len) break;
            uint32_t tgt = ((uint32_t)vm->bc[tmp] << 24) |
                           ((uint32_t)vm->bc[tmp+1] << 16) |
                           ((uint32_t)vm->bc[tmp+2] << 8)  |
                            (uint32_t)vm->bc[tmp+3];
            tmp += 4;
            if (tgt > scan_ip && tgt < vm->bc_len) {
                /* Follow the jump — inline the target region */
                scan_ip = tgt;
                continue;
            }
            break; /* backward or out-of-range: stop */
        }

        /* Stop at control-flow ops that can't be inlined */
        if (op == OP_ACCEPT || op == OP_FAIL || op == OP_REPEAT_INIT ||
            op == OP_REPEAT_STEP) break;

        /* OP_SPLIT: allow constrained 2-way SPLIT in compiled regions.
         * Both branch targets must be forward (> current IP) and within bc_len
         * so the region is acyclic.  The JIT pushes a choice for the non-taken
         * branch (b) and falls through to the taken branch (a), which is
         * inlined into the region by teleporting scan_ip to a below. */
        if (op == OP_SPLIT) {
            if (scan_ip + 9 > vm->bc_len) break;
            uint32_t a = ((uint32_t)vm->bc[scan_ip+1] << 24) |
                         ((uint32_t)vm->bc[scan_ip+2] << 16) |
                         ((uint32_t)vm->bc[scan_ip+3] <<  8) |
                          (uint32_t)vm->bc[scan_ip+4];
            uint32_t b = ((uint32_t)vm->bc[scan_ip+5] << 24) |
                         ((uint32_t)vm->bc[scan_ip+6] << 16) |
                         ((uint32_t)vm->bc[scan_ip+7] <<  8) |
                          (uint32_t)vm->bc[scan_ip+8];
            /* Guard: both branches must be strictly forward to keep the
             * region acyclic.  Backward or equal targets indicate loops. */
            if ((size_t)a <= scan_ip || (size_t)b <= scan_ip ||
                (size_t)a >= vm->bc_len || (size_t)b >= vm->bc_len) break;
            op_seq[op_count++] = (OpInfo){ scan_ip, scan_ip + 9, OP_SPLIT };
            worthy = true;
            scan_ip = (size_t)a; /* teleport: inline the taken branch next */
            continue;
        }

        size_t cur_ip  = scan_ip;
        size_t next_ip = scan_ip + 1;

        if (op == OP_LIT) {
            if (next_ip + 8 > vm->bc_len) break;
            uint32_t lit_len = ((uint32_t)vm->bc[next_ip+4] << 24) |
                               ((uint32_t)vm->bc[next_ip+5] << 16) |
                               ((uint32_t)vm->bc[next_ip+6] << 8)  |
                                (uint32_t)vm->bc[next_ip+7];
            next_ip += 8 + lit_len;
            worthy = true;
        } else if (op == OP_ANY || op == OP_NOTANY || op == OP_SPAN ||
                   op == OP_BREAK || op == OP_BREAKX) {
            /* BREAKX is now included in compiled regions */
            next_ip += 2;
            worthy = true;
        } else if (op == OP_LEN) {
            next_ip += 4;
        } else if (op == OP_ANCHOR || op == OP_CAP_START || op == OP_CAP_END) {
            next_ip += 1;
        } else if (op == OP_ASSIGN) {
            next_ip += 3;
        } else if (op == OP_NOP) {
            /* No operands; skip without adding to op_seq */
            scan_ip = next_ip;
            continue;
        } else {
            break;
        }

        op_seq[op_count++] = (OpInfo){ cur_ip, next_ip, op };
        scan_ip = next_ip;
    }

    /* scan_ip now points to the first op NOT compiled into this region */
    size_t region_end_ip = scan_ip;

    if (op_count < 1 || !worthy) return NULL;

    /* ---- Pass 2: code emission ---- */
    size_t code_size = 16384;
    uint32_t *code = (uint32_t *)snobol_jit_alloc_code(code_size);
    if (!code) return NULL;

#ifdef __APPLE__
    /* Enable writes to this MAP_JIT page for the current thread.
     * Every exit path below that does NOT call snobol_jit_seal_code() must
     * restore exec mode by calling pthread_jit_write_protect_np(1) itself
     * so the thread is never left stranded in write mode. */
    pthread_jit_write_protect_np(0);
#endif

    JITState js = { code, code, code_size };

    FailPatch fail_patches[512];
    size_t fail_patch_count = 0;

    /* Prologue: load frequently used values into registers
     *   x0 = vm  (caller arg — unchanged)
     *   x1 = vm->s
     *   x2 = vm->pos
     *   x3 = vm->len */
    emit_instr(&js, A64_LDR_X_X_IMM(1, 0, offsetof(VM, s)));
    emit_instr(&js, A64_LDR_X_X_IMM(2, 0, offsetof(VM, pos)));
    emit_instr(&js, A64_LDR_X_X_IMM(3, 0, offsetof(VM, len)));

    for (size_t i = 0; i < op_count; i++) {
        size_t cur_op_ip  = op_seq[i].ip;
        size_t next_op_ip = op_seq[i].next_ip;
        uint8_t op = op_seq[i].op;

        /* Re-read operands from bytecode using next_op_ip offsets */
        size_t operand_ip = cur_op_ip + 1; /* position after opcode byte */

        if (op == OP_LIT) {
            uint32_t off     = ((uint32_t)vm->bc[operand_ip]   << 24) |
                               ((uint32_t)vm->bc[operand_ip+1] << 16) |
                               ((uint32_t)vm->bc[operand_ip+2] << 8)  |
                                (uint32_t)vm->bc[operand_ip+3];
            uint32_t lit_len = ((uint32_t)vm->bc[operand_ip+4] << 24) |
                               ((uint32_t)vm->bc[operand_ip+5] << 16) |
                               ((uint32_t)vm->bc[operand_ip+6] << 8)  |
                                (uint32_t)vm->bc[operand_ip+7];
            (void)next_op_ip; /* consumed via op_seq */

            emit_mov_x64(&js, 7, lit_len);
            emit_instr(&js, A64_ADD_X_X_X(8, 2, 7));
            emit_instr(&js, A64_CMP_X_X(8, 3));
            fail_patches[fail_patch_count++] = (FailPatch){ js.p, cur_op_ip };
            emit_instr(&js, A64_B_HI(0));

            for (uint32_t j = 0; j < lit_len; j++) {
                emit_instr(&js, A64_LDRB_W_X_X(4, 1, 2));
                emit_mov_w32(&js, 7, vm->bc[off + j]);
                emit_instr(&js, A64_CMP_W_W(4, 7));
                fail_patches[fail_patch_count++] = (FailPatch){ js.p, cur_op_ip };
                emit_instr(&js, A64_B_NE(0));
                emit_instr(&js, A64_MOV_X_IMM(7, 1));
                emit_instr(&js, A64_ADD_X_X_X(2, 2, 7));
            }
        } else if (op == OP_SPAN || op == OP_ANY || op == OP_NOTANY ||
                   op == OP_BREAK || op == OP_BREAKX) {
            /* BREAK and BREAKX: both compiled using the same bitmap-match
             * logic.  BREAKX's retry choice point is managed by the VM choice stack
             * outside the compiled region; the JIT handles the initial scan step. */
            uint16_t set_id = ((uint16_t)vm->bc[operand_ip] << 8) | vm->bc[operand_ip+1];
            uint16_t count, ci;
            const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count, &ci);
            uint64_t ascii_map[2];
            if (!ranges || !ranges_to_ascii_bitmap(ranges, count, ascii_map)) {
                /* Can't compile this op — bail and discard partial code.
                 * Must restore exec mode before returning on Apple so the
                 * thread is not left stranded in write mode. */
#ifdef __APPLE__
                pthread_jit_write_protect_np(1);
#endif
                snobol_jit_free_code(code, code_size);
                return NULL;
            }
            emit_mov_x64(&js, 5, ascii_map[0]);
            emit_mov_x64(&js, 6, ascii_map[1]);
            emit_instr(&js, A64_CMP_X_X(2, 3));
            fail_patches[fail_patch_count++] = (FailPatch){ js.p, cur_op_ip };
            emit_instr(&js, A64_B_GE(0));
            emit_instr(&js, A64_LDRB_W_X_X(4, 1, 2));
            emit_instr(&js, A64_MOV_W_IMM(7, 127));
            emit_instr(&js, A64_CMP_W_W(4, 7));
            fail_patches[fail_patch_count++] = (FailPatch){ js.p, cur_op_ip };
            emit_instr(&js, A64_B_HI(0));
            uint32_t *tbz_instr = js.p; emit_instr(&js, 0);
            emit_ubfx_w(&js, 7, 4, 0, 6);
            emit_instr(&js, A64_LSR_X_X_X(8, 6, 7));
            if (op == OP_NOTANY) {
                fail_patches[fail_patch_count++] = (FailPatch){ js.p, cur_op_ip };
                emit_instr(&js, A64_TBZ(8, 0, 0));
            } else {
                fail_patches[fail_patch_count++] = (FailPatch){ js.p, cur_op_ip };
                emit_instr(&js, A64_TBZ(8, 0, 0));
            }
            uint32_t *b_success1 = js.p; emit_instr(&js, 0);
            *tbz_instr = A64_TBZ(4, 6, (js.p - tbz_instr));
            emit_ubfx_w(&js, 7, 4, 0, 6);
            emit_instr(&js, A64_LSR_X_X_X(8, 5, 7));
            if (op == OP_NOTANY) {
                fail_patches[fail_patch_count++] = (FailPatch){ js.p, cur_op_ip };
                emit_instr(&js, A64_TBZ(8, 0, 0));
            } else {
                fail_patches[fail_patch_count++] = (FailPatch){ js.p, cur_op_ip };
                emit_instr(&js, A64_TBZ(8, 0, 0));
            }
            uint32_t *success_label = js.p;
            emit_patch_b(b_success1, success_label);
            emit_instr(&js, A64_MOV_X_IMM(7, 1));
            emit_instr(&js, A64_ADD_X_X_X(2, 2, 7));
            if (op == OP_SPAN) {
                uint32_t *loop_start = js.p;
                emit_instr(&js, A64_CMP_X_X(2, 3));
                uint32_t *loop_ge_done = js.p; emit_instr(&js, A64_B_GE(0));
                emit_instr(&js, A64_LDRB_W_X_X(4, 1, 2));
                emit_instr(&js, A64_MOV_W_IMM(7, 127));
                emit_instr(&js, A64_CMP_W_W(4, 7));
                uint32_t *loop_hi_done = js.p; emit_instr(&js, A64_B_HI(0));
                uint32_t *loop_tbz = js.p; emit_instr(&js, 0);
                emit_ubfx_w(&js, 7, 4, 0, 6);
                emit_instr(&js, A64_LSR_X_X_X(8, 6, 7));
                uint32_t *loop_bit64_done = js.p; emit_instr(&js, A64_TBZ(8, 0, 0));
                emit_instr(&js, A64_MOV_X_IMM(7, 1));
                emit_instr(&js, A64_ADD_X_X_X(2, 2, 7));
                emit_instr(&js, A64_B(loop_start - js.p));
                *loop_tbz = A64_TBZ(4, 6, (js.p - loop_tbz));
                emit_ubfx_w(&js, 7, 4, 0, 6);
                emit_instr(&js, A64_LSR_X_X_X(8, 5, 7));
                uint32_t *loop_bit0_done = js.p; emit_instr(&js, A64_TBZ(8, 0, 0));
                emit_instr(&js, A64_MOV_X_IMM(7, 1));
                emit_instr(&js, A64_ADD_X_X_X(2, 2, 7));
                emit_instr(&js, A64_B(loop_start - js.p));
                uint32_t *span_done = js.p;
                emit_patch_b_cond(loop_ge_done,  span_done, 0x5400000a);
                emit_patch_b_cond(loop_hi_done,  span_done, 0x54000008);
                /* Preserve the register field (x8) from the original TBZ instruction
                 * by masking out only the imm14 bits before patching.  Using the
                 * hardcoded 0xb6000000 base would reset rt to x0, causing the
                 * branch to always be taken (VM pointer bit-0 is always 0). */
                emit_patch_tbz(loop_bit64_done,  span_done, *loop_bit64_done & 0xfff8001f);
                emit_patch_tbz(loop_bit0_done,   span_done, *loop_bit0_done  & 0xfff8001f);
            }
        } else if (op == OP_CAP_START || op == OP_CAP_END) {
            uint8_t r = vm->bc[operand_ip];
            size_t off = (op == OP_CAP_START) ? offsetof(VM, cap_start) : offsetof(VM, cap_end);
            emit_instr(&js, A64_STR_X_X_IMM(2, 0, off + r * 8));
            emit_ldrb_w_x_imm(&js, 7, 0, (uint32_t)offsetof(VM, max_cap_used));
            emit_instr(&js, A64_MOV_W_IMM(8, r + 1));
            emit_instr(&js, A64_CMP_W_W(7, 8));
            uint32_t *lt_skip = js.p; emit_instr(&js, A64_B_GE(0));
            emit_strb_w_x_imm(&js, 8, 0, (uint32_t)offsetof(VM, max_cap_used));
            emit_patch_b_cond(lt_skip, js.p, 0x5400000a);
        } else if (op == OP_LEN) {
            uint32_t n = ((uint32_t)vm->bc[operand_ip]   << 24) |
                         ((uint32_t)vm->bc[operand_ip+1] << 16) |
                         ((uint32_t)vm->bc[operand_ip+2] << 8)  |
                          (uint32_t)vm->bc[operand_ip+3];
            emit_mov_x64(&js, 7, n);
            emit_instr(&js, A64_ADD_X_X_X(8, 2, 7));
            emit_instr(&js, A64_CMP_X_X(8, 3));
            fail_patches[fail_patch_count++] = (FailPatch){ js.p, cur_op_ip };
            emit_instr(&js, A64_B_HI(0));
            emit_instr(&js, A64_ADD_X_X_X(2, 2, 7));
        } else if (op == OP_ANCHOR) {
            uint8_t type = vm->bc[operand_ip];
            if (type == 0) { emit_instr(&js, A64_MOV_X_IMM(7, 0)); emit_instr(&js, A64_CMP_X_X(2, 7)); }
            else emit_instr(&js, A64_CMP_X_X(2, 3));
            fail_patches[fail_patch_count++] = (FailPatch){ js.p, cur_op_ip };
            emit_instr(&js, A64_B_NE(0));
        } else if (op == OP_ASSIGN) {
            uint16_t var = ((uint16_t)vm->bc[operand_ip] << 8) | vm->bc[operand_ip+1];
            uint8_t  reg = vm->bc[operand_ip+2];
            emit_instr(&js, A64_LDR_X_X_IMM(7, 0, offsetof(VM, cap_start) + reg * 8));
            emit_instr(&js, A64_STR_X_X_IMM(7, 0, offsetof(VM, var_start)  + var * 8));
            emit_instr(&js, A64_LDR_X_X_IMM(7, 0, offsetof(VM, cap_end)   + reg * 8));
            emit_instr(&js, A64_STR_X_X_IMM(7, 0, offsetof(VM, var_end)   + var * 8));
            emit_instr(&js, A64_LDR_X_X_IMM(7, 0, offsetof(VM, var_count)));
            emit_mov_x64(&js, 8, var + 1);
            emit_instr(&js, A64_CMP_X_X(7, 8));
            uint32_t *lt_skip = js.p; emit_instr(&js, A64_B_GE(0));
            emit_instr(&js, A64_STR_X_X_IMM(8, 0, offsetof(VM, var_count)));
            emit_patch_b_cond(lt_skip, js.p, 0x5400000a);
        } else if (op == OP_SPLIT) {
            /* Emit: vm_push_choice(vm, b, current_pos)
             *
             * Pass 1 already verified both targets are forward and in-range.
             * operand layout: a = bc[operand_ip..+3], b = bc[operand_ip+4..+7]
             *
             * ARM64 calling convention: x0=vm (unchanged), x1=b, x2=pos.
             * We must save/restore x1 (vm->s), x2 (pos), x3 (len), x30 (LR)
             * around the call because they are caller-saved.
             *
             * Stack layout (32 bytes, 16-byte aligned):
             *   [sp+ 0] x1 (vm->s)
             *   [sp+ 8] x2 (pos)
             *   [sp+16] x3 (len)
             *   [sp+24] x30 (LR)
             */
            uint32_t b = ((uint32_t)vm->bc[operand_ip+4] << 24) |
                         ((uint32_t)vm->bc[operand_ip+5] << 16) |
                         ((uint32_t)vm->bc[operand_ip+6] <<  8) |
                          (uint32_t)vm->bc[operand_ip+7];
            emit_instr(&js, 0xa9be0be1u); /* STP x1, x2, [sp, #-32]! */
            emit_instr(&js, 0xa9017be3u); /* STP x3, x30, [sp, #16]  */
            /* Load vm_push_choice address into x9, b into x1 */
            emit_mov_x64(&js, 9, (uint64_t)(uintptr_t)vm_push_choice);
            emit_mov_x64(&js, 1, (uint64_t)b);
            /* x0 = vm (unchanged), x1 = b, x2 = current pos */
            emit_instr(&js, 0xd63f0120u); /* BLR x9 */
            emit_instr(&js, 0xa9417be3u); /* LDP x3, x30, [sp, #16]  */
            emit_instr(&js, 0xa8c20be1u); /* LDP x1, x2, [sp], #32   */
            /* Taken branch (a) is inlined as the next op(s) in op_seq */
        }
    }

    /* ---- Success epilogue ---- */
    emit_instr(&js, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
    emit_mov_x64(&js, 7, region_end_ip);
    emit_instr(&js, A64_STR_X_X_IMM(7, 0, offsetof(VM, ip)));
    emit_instr(&js, A64_RET);

    /* ---- Fail stubs (one per fail patch site) ---- */
    for (size_t f = 0; f < fail_patch_count; f++) {
        uint32_t *fail_stub = js.p;
        emit_instr(&js, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
        emit_mov_x64(&js, 7, fail_patches[f].ip);
        emit_instr(&js, A64_STR_X_X_IMM(7, 0, offsetof(VM, ip)));
        emit_instr(&js, A64_RET);

        uint32_t orig = *fail_patches[f].instr_p;
        if ((orig & 0xff000000) == 0x54000000) {
            emit_patch_b_cond(fail_patches[f].instr_p, fail_stub, orig & 0xff00001f);
        } else if ((orig & 0x7e000000) == 0x36000000) {
            emit_patch_tbz(fail_patches[f].instr_p, fail_stub, orig & 0xfff8001f);
        }
    }

    snobol_jit_seal_code(code, code_size);

    /* Single-block compilation: count it as 1 block */
    global_jit_stats.jit_blocks_compiled_total += 1;

    if (out_code_size) *out_code_size = code_size;
    return (jit_trace_fn)code;
}

#endif /* SNOBOL_JIT */
