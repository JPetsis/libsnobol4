#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "snobol_jit.h"

#ifdef SNOBOL_JIT

#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>
#include "snobol_internal.h"

/* ---------------------------------------------------------------------------
 * Global state
 * --------------------------------------------------------------------------- */

static SnobolJitStats  global_jit_stats = {0};

/* ---------------------------------------------------------------------------
 * Configuration  (task 2.1)
 * --------------------------------------------------------------------------- */

static SnobolJitConfig global_jit_cfg = {
    .hotness_threshold    = 50,
    .max_exit_rate_pct    = 80,
    .compile_budget_ns    = 500000ULL,   /* 0.5 ms */
    .cache_max_entries    = 128,
    .min_useful_ops       = 2,
    .skip_backtrack_heavy = true,
};

void snobol_jit_set_config(const SnobolJitConfig *cfg) {
    if (cfg) global_jit_cfg = *cfg;
}
const SnobolJitConfig *snobol_jit_get_config(void) { return &global_jit_cfg; }

void snobol_jit_load_config_from_env(void) {
    const char *v;
    if ((v = getenv("SNOBOL_JIT_HOTNESS")))      global_jit_cfg.hotness_threshold    = (uint64_t)strtoull(v, NULL, 10);
    if ((v = getenv("SNOBOL_JIT_MAX_EXIT_PCT")))  global_jit_cfg.max_exit_rate_pct    = (uint32_t)strtoul(v, NULL, 10);
    if ((v = getenv("SNOBOL_JIT_BUDGET_NS")))     global_jit_cfg.compile_budget_ns    = (uint64_t)strtoull(v, NULL, 10);
    if ((v = getenv("SNOBOL_JIT_CACHE_MAX")))     global_jit_cfg.cache_max_entries    = (uint32_t)strtoul(v, NULL, 10);
    if ((v = getenv("SNOBOL_JIT_MIN_OPS")))       global_jit_cfg.min_useful_ops       = (uint32_t)strtoul(v, NULL, 10);
    if ((v = getenv("SNOBOL_JIT_SKIP_BT")))       global_jit_cfg.skip_backtrack_heavy = (strtol(v, NULL, 10) != 0);
}

/* ---------------------------------------------------------------------------
 * LRU compiled-artifact cache  (task 3.2)
 *
 * Ownership rules (task 3.3):
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
        c->stop_compiling  = false;
        c->ctx_entries     = 0;
        c->ctx_exits       = 0;
        c->compile_time_ns = 0;
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
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (ptr == MAP_FAILED) ? NULL : ptr;
}

void snobol_jit_seal_code(void *code, size_t size) {
    mprotect(code, size, PROT_READ | PROT_EXEC);
    __builtin___clear_cache((char *)code, (char *)code + size);
}

void snobol_jit_free_code(void *code, size_t size) {
    if (code && size) munmap(code, size);
}

/* ---------------------------------------------------------------------------
 * Profitability gate  (task 2.2)
 *
 * Scans bytecode from ip to estimate how many useful (non-trivial) ops
 * exist before the first SPLIT, REPEAT_INIT, REPEAT_STEP, ACCEPT, or FAIL.
 *
 * Returns false (skip JIT) if:
 *   1. The region has fewer than cfg->min_useful_ops useful ops.
 *   2. cfg->skip_backtrack_heavy is true and a SPLIT/REPEAT appears within
 *      the first few ops (pattern dominated by backtracking with short prefix).
 * --------------------------------------------------------------------------- */

bool snobol_jit_should_compile(const VM *vm, size_t ip, const SnobolJitConfig *cfg) {
    if (!cfg) return true;

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
                goto done;
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
    /* Skip JIT if too few useful ops (not worth the compilation overhead) */
    if (useful < cfg->min_useful_ops) return false;

    /* Skip JIT for patterns where backtracking appears immediately after a
     * short prefix — the JIT↔interp transition cost exceeds any speedup. */
    if (has_backtrack && cfg->skip_backtrack_heavy && useful < 5) return false;

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
 * Compiler  (tasks 3.5, 4.2)
 *
 * Changes vs original:
 *  • Two-pass: first build OpInfo[] (following forward JMPs), then emit.
 *  • out_code_size set on success so caller can store it in trace_sizes[].
 *  • compilations_total NOT incremented here — caller owns the counter.
 * --------------------------------------------------------------------------- */

#define MAX_OPS_IN_REGION 64

jit_trace_fn snobol_jit_compile(VM *vm, size_t start_ip, size_t *out_code_size) {
    if (out_code_size) *out_code_size = 0;

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
        if (op == OP_ACCEPT || op == OP_FAIL || op == OP_SPLIT ||
            op == OP_REPEAT_INIT || op == OP_REPEAT_STEP) break;

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
        } else if (op == OP_ANY || op == OP_NOTANY || op == OP_SPAN || op == OP_BREAK) {
            next_ip += 2;
            worthy = true;
        } else if (op == OP_LEN) {
            next_ip += 4;
        } else if (op == OP_ANCHOR || op == OP_CAP_START || op == OP_CAP_END) {
            next_ip += 1;
        } else if (op == OP_ASSIGN) {
            next_ip += 3;
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
        } else if (op == OP_SPAN || op == OP_ANY || op == OP_NOTANY) {
            uint16_t set_id = ((uint16_t)vm->bc[operand_ip] << 8) | vm->bc[operand_ip+1];
            uint16_t count, ci;
            const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count, &ci);
            uint64_t ascii_map[2];
            if (!ranges || !ranges_to_ascii_bitmap(ranges, count, ascii_map)) {
                /* Can't compile this op — bail and discard partial code */
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
                emit_patch_tbz(loop_bit64_done,  span_done, 0xb6000000);
                emit_patch_tbz(loop_bit0_done,   span_done, 0xb6000000);
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

    if (out_code_size) *out_code_size = code_size;
    return (jit_trace_fn)code;
}

#endif /* SNOBOL_JIT */
