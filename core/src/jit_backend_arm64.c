/**
 * @file jit_backend_arm64.c
 * @brief ARM64 JIT backend for the SNOBOL4 micro-JIT.
 *
 * Implements the jit_backend_t vtable for the ARM64 target.
 * Primary entry point: arm64_lower() — translates a jit_ir_region_t to
 * ARM64 machine code stored in the provided jit_region_t.
 *
 * The design uses a two-pass compile strategy:
 *   Pass 1: Build a CFG from the IR's control-flow instructions
 *   Pass 2: Emit ARM64 machine code block-by-block
 *
 * All per-instruction operands are read from the IR (jit_ir_instr_t.u)
 * rather than re-parsed from the VM bytecode stream.
 */

#include "snobol/jit_backend.h"
#include "snobol/jit_ir.h"
#include "snobol/jit.h"

#ifdef SNOBOL_JIT
#if defined(__aarch64__) || defined(__arm64__)

#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#ifdef SNOBOL_JIT_PLATFORM_MACOS
#  include <pthread.h>
#endif
#ifdef SNOBOL_JIT_PLATFORM_LINUX
#  include <sys/syscall.h>
#  include <unistd.h>
/* __NR_cacheflush may not be exposed by all kernel headers on AArch64. */
#  ifndef __NR_cacheflush
#    define __NR_cacheflush 232
#  endif
#endif

#include "snobol/vm.h"
#include "snobol/snobol_internal.h"
#include "snobol/table.h"
#include "snobol/dynamic_pattern.h"
#include "snobol/string_fn.h"
#include "snobol/type_fn.h"

/* =========================================================================
 * ARM64 instruction helpers
 * ========================================================================= */
#define A64_RET ((uint32_t)0xd65f03c0u)

static inline uint32_t A64_LDR_X_X_IMM(uint32_t rt, uint32_t rn, uint32_t imm) {
    return 0xf9400000u | (rt & 31u) | ((rn & 31u) << 5) | ((imm / 8u) << 10);
}
static inline uint32_t A64_STR_X_X_IMM(uint32_t rt, uint32_t rn, uint32_t imm) {
    return 0xf9000000u | (rt & 31u) | ((rn & 31u) << 5) | ((imm / 8u) << 10);
}
static inline uint32_t A64_ADD_X_X_X(uint32_t rd, uint32_t rn, uint32_t rm) {
    return 0x8b000000u | (rd & 31u) | ((rn & 31u) << 5) | ((rm & 31u) << 16);
}
static inline uint32_t A64_CMP_X_X(uint32_t rn, uint32_t rm) {
    return 0xeb00001fu | ((rn & 31u) << 5) | ((rm & 31u) << 16);
}
static inline uint32_t A64_CMP_W_W(uint32_t rn, uint32_t rm) {
    return 0x6b00001fu | ((rn & 31u) << 5) | ((rm & 31u) << 16);
}
static inline uint32_t A64_B_GE(uint32_t imm) {
    return 0x5400000au | ((imm & 0x3ffffu) << 5);
}
static inline uint32_t A64_B(uint32_t imm) {
    return 0x14000000u | (imm & 0x3ffffffu);
}
static inline uint32_t A64_LDRB_W_X_X(uint32_t rt, uint32_t rn, uint32_t rm) {
    return 0x38606800u | (rt & 31u) | ((rn & 31u) << 5) | ((rm & 31u) << 16);
}
static inline uint32_t A64_MOV_X_IMM(uint32_t rd, uint32_t imm) {
    return 0xd2800000u | (rd & 31u) | ((imm & 0xffffu) << 5);
}
static inline uint32_t A64_MOV_W_IMM(uint32_t rd, uint32_t imm) {
    return 0x52800000u | (rd & 31u) | ((imm & 0xffffu) << 5);
}
static inline uint32_t A64_MOVK_X_IMM_LSL16(uint32_t rd, uint32_t imm) {
    return 0xf2a00000u | (rd & 31u) | ((imm & 0xffffu) << 5);
}
static inline uint32_t A64_MOVK_X_IMM_LSL32(uint32_t rd, uint32_t imm) {
    return 0xf2c00000u | (rd & 31u) | ((imm & 0xffffu) << 5);
}
static inline uint32_t A64_MOVK_X_IMM_LSL48(uint32_t rd, uint32_t imm) {
    return 0xf2e00000u | (rd & 31u) | ((imm & 0xffffu) << 5);
}
static inline uint32_t A64_TBZ(uint32_t rt, uint32_t bit, uint32_t imm) {
    return 0xb6000000u | (rt & 31u) | ((bit & 0x1fu) << 19) | (((bit >> 5) & 1u) << 31) | ((imm & 0x3fffu) << 5);
}
static inline uint32_t A64_TBNZ(uint32_t rt, uint32_t bit, uint32_t imm) {
    return 0xb7000000u | (rt & 31u) | ((bit & 0x1fu) << 19) | (((bit >> 5) & 1u) << 31) | ((imm & 0x3fffu) << 5);
}
static inline uint32_t A64_AND_W_W_W(uint32_t rd, uint32_t rn, uint32_t rm) {
    return 0x0a000000u | (rd & 31u) | ((rn & 31u) << 5) | ((rm & 31u) << 16);
}
static inline uint32_t A64_LSR_X_X_X(uint32_t rd, uint32_t rn, uint32_t rm) {
    return 0x9ac02000u | (rd & 31u) | ((rn & 31u) << 5) | ((rm & 31u) << 16);
}
static inline uint32_t A64_B_EQ(uint32_t imm) {
    return 0x54000000u | ((imm & 0x3ffffu) << 5);
}
static inline uint32_t A64_B_NE(uint32_t imm) {
    return 0x54000001u | ((imm & 0x3ffffu) << 5);
}
static inline uint32_t A64_B_HI(uint32_t imm) {
    return 0x54000008u | ((imm & 0x3ffffu) << 5);
}
static inline uint32_t A64_B_LO(uint32_t imm) {
    return 0x54000003u | ((imm & 0x3ffffu) << 5);
}
static inline uint32_t A64_MOV_X_X(uint32_t rd, uint32_t rm) {
    return 0xAA000000u | (rd & 31u) | (31u << 5) | ((rm & 31u) << 16);
}
static inline uint32_t A64_SUB_X_X_X(uint32_t rd, uint32_t rn, uint32_t rm) {
    return 0xCB000000u | (rd & 31u) | ((rn & 31u) << 5) | ((rm & 31u) << 16);
}
/* Suppress unused-function warnings for helpers not referenced in all paths */
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

/* ---------------------------------------------------------------------------
 * CFG structures (same as in original jit.c)
 * --------------------------------------------------------------------------- */
#define JIT_CFG_MAX_BLOCKS 64
#define JIT_LOOP_ITER_MAX  1024

typedef enum {
    BLOCK_TERM_SPLIT,
    BLOCK_TERM_JMP_FWD,
    BLOCK_TERM_JMP_BWD,
    BLOCK_TERM_EXIT,
} BlockTerm;

typedef struct {
    size_t    start_ip;
    size_t    term_ip;
    size_t    next_ip;
    BlockTerm term;
    size_t    succ_a;
    size_t    succ_b;
    bool      worthy;
    int       succ_a_blk;
    int       succ_b_blk;
    uint32_t *stub_start;
} JitBlock;

typedef struct {
    JitBlock blocks[JIT_CFG_MAX_BLOCKS];
    int      count;
    bool     has_backward;
} JitCfg;

typedef struct {
    uint32_t *instr_p;
    size_t    ip;
} FailPatch;

typedef struct {
    uint32_t *instr_p;
    size_t    target_ip;
} StubPatch;

#define A64_STP_X19_X30_PRE16  ((uint32_t)0xA9BF7BF3u)
#define A64_LDP_X19_X30_POST16 ((uint32_t)0xA8C17BF3u)
#define A64_SUBS_X19_X19_1     ((uint32_t)0xF1000673u)

/* =========================================================================
 * Low-level emit helpers
 * ========================================================================= */
static void emit_instr(jit_region_t *js, uint32_t ins) { *(js->p)++ = ins; }

static void emit_mov_x64(jit_region_t *js, int rd, uint64_t val) {
    emit_instr(js, A64_MOV_X_IMM(rd, (uint32_t)(val & 0xffff)));
    emit_instr(js, A64_MOVK_X_IMM_LSL16(rd, (uint32_t)((val >> 16) & 0xffff)));
    emit_instr(js, A64_MOVK_X_IMM_LSL32(rd, (uint32_t)((val >> 32) & 0xffff)));
    emit_instr(js, A64_MOVK_X_IMM_LSL48(rd, (uint32_t)((val >> 48) & 0xffff)));
}

static void emit_mov_w32(jit_region_t *js, int rd, uint32_t val) {
    emit_instr(js, A64_MOV_W_IMM(rd, val & 0xffff));
    if (val > 0xffff) emit_instr(js, 0x72a00000 | (rd & 31) | ((val >> 16) << 5));
}

static void emit_ldrb_w_x_imm(jit_region_t *js, int rt, int rn, uint32_t imm) {
    emit_instr(js, 0x39400000 | (rt & 31) | ((rn & 31) << 5) | (imm << 10));
}
static void emit_strb_w_x_imm(jit_region_t *js, int rt, int rn, uint32_t imm) {
    emit_instr(js, 0x39000000 | (rt & 31) | ((rn & 31) << 5) | (imm << 10));
}
static void emit_ubfx_w(jit_region_t *js, int rd, int rn, int lsb, int width) {
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

/* =========================================================================
 * Call-out macros
 * ========================================================================= */
#define EMIT_CALLOUT_SAVE(js) do { \
    emit_instr((js), 0xa9be7be0u); \
    emit_instr((js), 0xa9010be1u); \
} while(0)

#define EMIT_CALLOUT_RESTORE(js) do { \
    emit_instr((js), 0xa9410be1u); \
    emit_instr((js), 0xa8c27be0u); \
    emit_instr((js), A64_LDR_X_X_IMM(3, 0, offsetof(VM, len))); \
} while(0)

#define EMIT_CALLOUT_SAVE_RET(js) \
    emit_instr((js), A64_MOV_X_X(9, 0))

/* =========================================================================
 * JIT call-out helper functions (identical to originals in jit.c)
 * ========================================================================= */

static void snobol_jit_helper_emit_literal(VM *vm, uint32_t offset, uint32_t len) {
    if (vm->out) snobol_buf_append(vm->out, (const char *)vm->bc + offset, (size_t)len);
    if (vm->emit_fn) vm->emit_fn((const char *)vm->bc + offset, (size_t)len, vm->emit_udata);
}

static void snobol_jit_helper_emit_capture(VM *vm, uint8_t reg) {
    if (reg < MAX_CAPS &&
        vm->cap_end[reg] >= vm->cap_start[reg] &&
        vm->cap_end[reg] <= vm->len) {
        size_t s = vm->cap_start[reg];
        size_t e = vm->cap_end[reg];
        if (vm->out)    snobol_buf_append(vm->out, vm->s + s, e - s);
        if (vm->emit_fn) vm->emit_fn(vm->s + s, e - s, vm->emit_udata);
    }
}

static void snobol_jit_helper_emit_expr(VM *vm, uint8_t reg, uint8_t expr_type) {
    if (reg >= MAX_CAPS) return;
    if (vm->cap_end[reg] < vm->cap_start[reg] || vm->cap_end[reg] > vm->len) return;
    const char *data = vm->s + vm->cap_start[reg];
    size_t len = vm->cap_end[reg] - vm->cap_start[reg];
    if (expr_type == 1) {
        char *tmp = (char *)snobol_malloc(len + 1);
        if (!tmp) return;
        for (size_t i = 0; i < len; ++i)
            tmp[i] = (data[i] >= 'a' && data[i] <= 'z') ? (char)(data[i] - 32) : data[i];
        if (vm->out)    snobol_buf_append(vm->out, tmp, len);
        if (vm->emit_fn) vm->emit_fn(tmp, len, vm->emit_udata);
        snobol_free(tmp);
    } else if (expr_type == 2) {
        char tmp[32];
        int n = snprintf(tmp, sizeof(tmp), "%zu", len);
        if (vm->out)    snobol_buf_append(vm->out, tmp, (size_t)n);
        if (vm->emit_fn) vm->emit_fn(tmp, (size_t)n, vm->emit_udata);
    } else {
        if (vm->out)    snobol_buf_append(vm->out, data, len);
        if (vm->emit_fn) vm->emit_fn(data, len, vm->emit_udata);
    }
}

static void snobol_jit_helper_emit_format(VM *vm, uint8_t reg, uint8_t format_type,
                                           uint16_t width, uint8_t fill_char) {
    if (reg >= MAX_CAPS) return;
    if (vm->cap_end[reg] < vm->cap_start[reg] || vm->cap_end[reg] > vm->len) return;
    const char *data = vm->s + vm->cap_start[reg];
    size_t len = vm->cap_end[reg] - vm->cap_start[reg];

    if (format_type == SNBL_FMT_UPPER) {
        char *tmp = (char *)snobol_malloc(len + 1);
        if (!tmp) return;
        for (size_t i = 0; i < len; ++i)
            tmp[i] = (data[i] >= 'a' && data[i] <= 'z') ? (char)(data[i] - 32) : data[i];
        if (vm->out)    snobol_buf_append(vm->out, tmp, len);
        if (vm->emit_fn) vm->emit_fn(tmp, len, vm->emit_udata);
        snobol_free(tmp);
    } else if (format_type == SNBL_FMT_LOWER) {
        char *tmp = (char *)snobol_malloc(len + 1);
        if (!tmp) return;
        for (size_t i = 0; i < len; ++i)
            tmp[i] = (data[i] >= 'A' && data[i] <= 'Z') ? (char)(data[i] + 32) : data[i];
        if (vm->out)    snobol_buf_append(vm->out, tmp, len);
        if (vm->emit_fn) vm->emit_fn(tmp, len, vm->emit_udata);
        snobol_free(tmp);
    } else if (format_type == SNBL_FMT_LENGTH) {
        char tmp[32];
        int n = snprintf(tmp, sizeof(tmp), "%zu", len);
        if (vm->out)    snobol_buf_append(vm->out, tmp, (size_t)n);
        if (vm->emit_fn) vm->emit_fn(tmp, (size_t)n, vm->emit_udata);
    } else if (format_type == SNBL_FMT_LPAD || format_type == SNBL_FMT_RPAD) {
        if (width > 1024) width = 1024;
        if (len >= (size_t)width) {
            if (vm->out)    snobol_buf_append(vm->out, data, len);
            if (vm->emit_fn) vm->emit_fn(data, len, vm->emit_udata);
        } else {
            size_t pad   = (size_t)width - len;
            size_t total = (size_t)width;
            char *buf = (char *)snobol_malloc(total);
            if (buf) {
                if (format_type == SNBL_FMT_LPAD) { memset(buf, fill_char, pad); memcpy(buf + pad, data, len); }
                else                              { memcpy(buf, data, len); memset(buf + len, fill_char, pad); }
                if (vm->out)    snobol_buf_append(vm->out, buf, total);
                if (vm->emit_fn) vm->emit_fn(buf, total, vm->emit_udata);
                snobol_free(buf);
            }
        }
    } else {
        if (vm->out)    snobol_buf_append(vm->out, data, len);
        if (vm->emit_fn) vm->emit_fn(data, len, vm->emit_udata);
    }
}

static void snobol_jit_helper_emit_table_ip(VM *vm, uint64_t op_ip) {
    size_t ip = (size_t)op_ip + 1;
    if (ip + 4 > vm->bc_len) return;
    uint16_t table_id = ((uint16_t)vm->bc[ip] << 8) | vm->bc[ip+1]; ip += 2;
    uint8_t key_type  = vm->bc[ip++];
    uint8_t name_len  = vm->bc[ip++];
    ip += name_len;
#ifdef SNOBOL_DYNAMIC_PATTERN
    snobol_table_t *table = vm_get_table(vm, table_id);
    const char *value = nullptr;
    if (key_type == 0) {
        if (ip + 2 > vm->bc_len) return;
        uint16_t key_len = ((uint16_t)vm->bc[ip] << 8) | vm->bc[ip+1]; ip += 2;
        if (key_len > 0 && ip + key_len <= vm->bc_len) {
            char *key = (char *)snobol_malloc(key_len + 1);
            if (key) {
                memcpy(key, vm->bc + ip, key_len); key[key_len] = '\0';
                if (table) value = table_get(table, key);
                snobol_free(key);
            }
        }
    } else if (key_type == 1) {
        if (ip >= vm->bc_len) return;
        uint8_t key_reg = vm->bc[ip];
        if (table && key_reg < MAX_CAPS &&
            vm->cap_end[key_reg] >= vm->cap_start[key_reg] &&
            vm->cap_end[key_reg] <= vm->len) {
            size_t kl = vm->cap_end[key_reg] - vm->cap_start[key_reg];
            char *key = (char *)snobol_malloc(kl + 1);
            if (key) {
                memcpy(key, vm->s + vm->cap_start[key_reg], kl); key[kl] = '\0';
                value = table_get(table, key);
                snobol_free(key);
            }
        }
    }
    if (value) {
        size_t vl = strlen(value);
        if (vm->out)    snobol_buf_append(vm->out, value, vl);
        if (vm->emit_fn) vm->emit_fn(value, vl, vm->emit_udata);
    }
#else
    (void)table_id; (void)key_type;
#endif
}

#ifdef SNOBOL_DYNAMIC_PATTERN
static bool snobol_jit_helper_table_get(VM *vm, uint16_t table_id,
                                         uint8_t key_reg, uint8_t dest_reg) {
    (void)dest_reg;
    snobol_table_t *table = vm_get_table(vm, table_id);
    if (!table) return false;
    if (key_reg >= MAX_CAPS || vm->cap_end[key_reg] <= vm->cap_start[key_reg]) return false;
    size_t kl = vm->cap_end[key_reg] - vm->cap_start[key_reg];
    char *key = (char *)snobol_malloc(kl + 1);
    if (!key) return false;
    memcpy(key, vm->s + vm->cap_start[key_reg], kl); key[kl] = '\0';
    const char *value = table_get(table, key);
    snobol_free(key);
    return value != nullptr;
}

static bool snobol_jit_helper_table_set(VM *vm, uint16_t table_id,
                                         uint8_t key_reg, uint8_t value_reg) {
    snobol_table_t *table = vm_get_table(vm, table_id);
    if (!table) return false;
    if (key_reg   >= MAX_CAPS || vm->cap_end[key_reg]   <= vm->cap_start[key_reg]) return false;
    if (value_reg >= MAX_CAPS || vm->cap_end[value_reg] <= vm->cap_start[value_reg]) return false;
    size_t kl = vm->cap_end[key_reg]   - vm->cap_start[key_reg];
    size_t vl = vm->cap_end[value_reg] - vm->cap_start[value_reg];
    char *key = (char *)snobol_malloc(kl + 1);
    char *val = (char *)snobol_malloc(vl + 1);
    if (!key || !val) { snobol_free(key); snobol_free(val); return false; }
    memcpy(key, vm->s + vm->cap_start[key_reg],   kl); key[kl] = '\0';
    memcpy(val, vm->s + vm->cap_start[value_reg], vl); val[vl] = '\0';
    (void)table_set(table, key, val);
    snobol_free(key); snobol_free(val);
    return true;
}
#endif

static bool snobol_jit_helper_bal(VM *vm, uint32_t open_cp, uint32_t close_cp) {
    size_t pos = vm->pos;
    uint32_t first; int fb;
    if (!utf8_peek_next(vm->s, vm->len, pos, &first, &fb) || first != open_cp) return false;
    int depth = 0; bool ok = false;
    while (pos < vm->len) {
        uint32_t cp; int cb;
        if (!utf8_peek_next(vm->s, vm->len, pos, &cp, &cb)) break;
        if (cp == open_cp)       depth++;
        else if (cp == close_cp) { depth--; if (depth == 0) { pos += (size_t)cb; ok = true; break; } }
        pos += (size_t)cb;
    }
    if (ok) vm->pos = pos;
    return ok;
}

static bool snobol_jit_helper_eval(VM *vm, uint16_t fn_id, uint8_t reg) {
    if (reg >= MAX_CAPS) return false;
    if (vm->cap_end[reg] < vm->cap_start[reg] || vm->cap_end[reg] > vm->len) return false;
    if (vm->eval_fn)
        return vm->eval_fn((int)fn_id, vm->s, vm->cap_start[reg], vm->cap_end[reg], vm->eval_udata);
    return true;
}

#ifdef SNOBOL_DYNAMIC_PATTERN
static bool snobol_jit_helper_dynamic(VM *vm) {
    if (!vm->dyn_pending_source || !vm->dyn_pending_bc) return false;
    dynamic_pattern_t *pattern = dynamic_pattern_cache_get(
        vm->dyn_cache, vm->dyn_pending_source, (int)vm->dyn_pending_source_len);
    if (!pattern) {
        uint8_t *bc_copy = (uint8_t *)snobol_malloc(vm->dyn_pending_bc_len);
        if (!bc_copy) return false;
        memcpy(bc_copy, vm->dyn_pending_bc, vm->dyn_pending_bc_len);
        pattern = dynamic_pattern_create(vm->dyn_pending_source, bc_copy, vm->dyn_pending_bc_len);
        if (!pattern || !pattern->is_valid) {
            if (pattern) dynamic_pattern_release(pattern);
            snobol_free(bc_copy); return false;
        }
        if (!dynamic_pattern_cache_put(vm->dyn_cache, vm->dyn_pending_source, pattern)) {
            dynamic_pattern_release(pattern); return false;
        }
    }
    /* The pattern has been built and cached; the pending buffers are no
     * longer needed.  Free them to avoid leaking across vm_exec calls. */
    if (vm->dyn_pending_source) {
        snobol_free(vm->dyn_pending_source);
        vm->dyn_pending_source = nullptr;
        vm->dyn_pending_source_len = 0;
    }
    if (vm->dyn_pending_bc) {
        snobol_free(vm->dyn_pending_bc);
        vm->dyn_pending_bc = nullptr;
        vm->dyn_pending_bc_len = 0;
    }
    const uint8_t *saved_bc  = vm->bc;
    size_t         saved_bcl = vm->bc_len;
    size_t         saved_pos = vm->pos;
    size_t         saved_ip  = vm->ip;
#ifdef SNOBOL_JIT
    SnobolJitContext *saved_ctx = (SnobolJitContext *)vm->jit.ctx;
    void **saved_traces = vm->jit.traces;
    uint64_t *saved_ip_counts = vm->jit.ip_counts;
    SnobolJitContext *inner_ctx = (SnobolJitContext *)pattern->jit_ctx;
    vm->jit.ctx = inner_ctx;
    vm->jit.traces = inner_ctx ? inner_ctx->traces : nullptr;
    vm->jit.ip_counts = inner_ctx ? inner_ctx->ip_counts : nullptr;
#endif
    vm->bc = pattern->bc; vm->bc_len = pattern->bc_len; vm->ip = 0;
    bool result = vm_run(vm);
    vm->bc = saved_bc; vm->bc_len = saved_bcl; vm->ip = saved_ip;
#ifdef SNOBOL_JIT
    vm->jit.ctx = saved_ctx;
    vm->jit.traces = saved_traces;
    vm->jit.ip_counts = saved_ip_counts;
#endif
    if (!result) vm->pos = saved_pos;
    dynamic_pattern_release(pattern);
    return result;
}
#endif

/* =========================================================================
 * IR-based instruction lookup helpers
 * ========================================================================= */

/**
 * Find the index of the first IR instruction whose bc_ip equals target_ip.
 * Returns the count (past end) if not found.
 */
static size_t ir_find_instr(const jit_ir_region_t *ir, size_t target_ip) {
    for (size_t i = 0; i < ir->count; i++) {
        if (ir->instrs[i].bc_ip == target_ip) return i;
    }
    return ir->count; /* not found */
}

/* =========================================================================
 * CFG builder from IR
 *
 * Builds the same JitCfg structure as the original jit.c code but reads
 * block structure from the IR (control-flow instructions with decoded targets)
 * rather than re-parsing the bytecode for structural information.
 *
 * Operand data (charclass IDs, literal bytes) still comes through the VM
 * pointer (for get_ranges_ptr), but instruction opcodes and targets come
 * exclusively from the IR.
 * ========================================================================= */

static inline uint32_t cfg_read_u32_arm64(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

static int ir_find_block(const JitCfg *cfg, size_t ip) {
    for (int i = 0; i < cfg->count; i++)
        if (cfg->blocks[i].start_ip == ip) return i;
    return -1;
}

/**
 * Scan one basic block starting at ir_start_idx in the IR array.
 * A block ends when we hit SPLIT, JMP, GOTO, ACCEPT, FAIL, REPEAT_*, or end-of-IR.
 */
static void ir_cfg_scan_block(const jit_ir_region_t *ir, size_t ir_start_idx,
                               JitBlock *blk) {
    blk->stub_start = nullptr;
    blk->succ_a_blk = -1;
    blk->succ_b_blk = -1;
    blk->succ_a     = 0;
    blk->succ_b     = 0;
    blk->worthy     = false;

    if (ir_start_idx >= ir->count) {
        blk->start_ip = 0;
        blk->term_ip  = 0;
        blk->next_ip  = 0;
        blk->term     = BLOCK_TERM_EXIT;
        return;
    }

    blk->start_ip = ir->instrs[ir_start_idx].bc_ip;

    for (size_t i = ir_start_idx; i < ir->count; i++) {
        const jit_ir_instr_t *ins = &ir->instrs[i];

        switch (ins->opcode) {
        case JIT_IR_SPLIT:
            blk->term     = BLOCK_TERM_SPLIT;
            blk->term_ip  = ins->bc_ip;
            blk->next_ip  = ins->bc_ip + 9; /* SPLIT opcode(1) + u32(4) + u32(4) */
            blk->succ_a   = ins->u.split.target_a;
            blk->succ_b   = ins->u.split.target_b;
            return;

        case JIT_IR_JMP:
        case JIT_IR_GOTO: {
            size_t tgt = ins->u.jmp.target;
            blk->term_ip  = ins->bc_ip;
            blk->next_ip  = ins->bc_ip + (ins->opcode == JIT_IR_JMP ? 5u : 3u);
            blk->succ_a   = tgt;
            blk->term     = (tgt > ins->bc_ip) ? BLOCK_TERM_JMP_FWD : BLOCK_TERM_JMP_BWD;
            return;
        }

        case JIT_IR_ACCEPT:
        case JIT_IR_FAIL:
        case JIT_IR_REPEAT_INIT:
        case JIT_IR_REPEAT_STEP:
            blk->term    = BLOCK_TERM_EXIT;
            blk->term_ip = ins->bc_ip;
            blk->next_ip = ins->bc_ip + 1;
            return;

        /* Mark worthy if instruction does meaningful work */
        case JIT_IR_LIT:
        case JIT_IR_ANY: case JIT_IR_NOTANY: case JIT_IR_SPAN:
        case JIT_IR_BREAK: case JIT_IR_BREAKX:
        case JIT_IR_LEN:
        case JIT_IR_REM: case JIT_IR_RPOS: case JIT_IR_RTAB:
        case JIT_IR_EMIT_LITERAL: case JIT_IR_EMIT_CAPTURE: case JIT_IR_EMIT_EXPR:
        case JIT_IR_EMIT_FORMAT: case JIT_IR_EMIT_TABLE:
        case JIT_IR_TABLE_GET: case JIT_IR_TABLE_SET:
        case JIT_IR_BAL: case JIT_IR_EVAL: case JIT_IR_DYNAMIC:
            blk->worthy = true;
            break;

        default:
            break;
        }
    }
    /* Reached end of IR without a terminator */
    size_t last_bc_ip = ir->instrs[ir->count - 1].bc_ip;
    blk->term    = BLOCK_TERM_EXIT;
    blk->term_ip = last_bc_ip;
    blk->next_ip = last_bc_ip;
}

/**
 * Build a JitCfg from the IR region.
 * Returns the number of blocks found (0 = nothing worth compiling).
 */
static int ir_cfg_build(const jit_ir_region_t *ir, JitCfg *cfg) {
    cfg->count        = 0;
    cfg->has_backward = false;

    if (!ir || ir->count == 0) return 0;

    /* Queue of bytecode IPs to process (BFS) */
    size_t queue[JIT_CFG_MAX_BLOCKS * 2];
    int    qhead = 0, qtail = 0;
    size_t visited[JIT_CFG_MAX_BLOCKS];
    int    vcount = 0;

    queue[qtail++] = ir->instrs[0].bc_ip; /* Start from first IR instruction */

    while (qhead < qtail) {
        size_t ip = queue[qhead++];

        /* Skip already-visited IPs */
        bool seen = false;
        for (int i = 0; i < vcount; i++) if (visited[i] == ip) { seen = true; break; }
        if (seen) continue;
        visited[vcount++] = ip;

        if (cfg->count >= JIT_CFG_MAX_BLOCKS) break;

        /* Find IR instruction at this bc IP */
        size_t ir_idx = ir_find_instr(ir, ip);
        if (ir_idx >= ir->count) continue; /* Not in IR */

        JitBlock *blk = &cfg->blocks[cfg->count++];
        ir_cfg_scan_block(ir, ir_idx, blk);

        switch (blk->term) {
        case BLOCK_TERM_SPLIT: {
            bool sa = false, sb = false;
            for (int i = 0; i < vcount; i++) {
                if (visited[i] == blk->succ_a) sa = true;
                if (visited[i] == blk->succ_b) sb = true;
            }
            /* Check if successors are in IR */
            if (!sa && ir_find_instr(ir, blk->succ_a) < ir->count &&
                qtail < (int)(sizeof(queue)/sizeof(queue[0])))
                queue[qtail++] = blk->succ_a;
            if (!sb && ir_find_instr(ir, blk->succ_b) < ir->count &&
                qtail < (int)(sizeof(queue)/sizeof(queue[0])))
                queue[qtail++] = blk->succ_b;
            break;
        }
        case BLOCK_TERM_JMP_FWD: {
            bool sa = false;
            for (int i = 0; i < vcount; i++) if (visited[i] == blk->succ_a) sa = true;
            if (!sa && ir_find_instr(ir, blk->succ_a) < ir->count &&
                qtail < (int)(sizeof(queue)/sizeof(queue[0])))
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
            blk->succ_a_blk = ir_find_block(cfg, blk->succ_a);
        if (blk->term == BLOCK_TERM_SPLIT)
            blk->succ_b_blk = ir_find_block(cfg, blk->succ_b);
    }

    bool any_worthy = false;
    for (int i = 0; i < cfg->count; i++)
        if (cfg->blocks[i].worthy) { any_worthy = true; break; }

    return (any_worthy && cfg->count > 0) ? cfg->count : 0;
}

/* =========================================================================
 * Per-block ARM64 code emission from IR
 *
 * Iterates over IR instructions whose bc_ip falls in [start_ip, term_ip)
 * and emits ARM64 code for each.  Operands are read from IR; no bytecode
 * re-parsing occurs.
 * ========================================================================= */

#define EMIT_VOID_CALLOUT(js_, fn_ptr_) do { \
    EMIT_CALLOUT_SAVE(js_); \
    emit_mov_x64((js_), 9, (uint64_t)(uintptr_t)(fn_ptr_)); \
    emit_instr((js_), 0xd63f0120u); \
    EMIT_CALLOUT_RESTORE(js_); \
} while(0)

#define EMIT_BOOL_CALLOUT(js_, fn_ptr_, fp_, fpc_, cur_) do { \
    emit_instr((js_), A64_STR_X_X_IMM(2, 0, offsetof(VM, pos))); \
    EMIT_CALLOUT_SAVE(js_); \
    emit_mov_x64((js_), 9, (uint64_t)(uintptr_t)(fn_ptr_)); \
    emit_instr((js_), 0xd63f0120u); \
    EMIT_CALLOUT_SAVE_RET(js_); \
    EMIT_CALLOUT_RESTORE(js_); \
    emit_instr((js_), A64_LDR_X_X_IMM(2, 0, offsetof(VM, pos))); \
    emit_instr((js_), A64_MOV_W_IMM(7, 0)); \
    emit_instr((js_), A64_CMP_W_W(9, 7)); \
    (fp_)[(*(fpc_))++] = (FailPatch){ (js_)->p, (cur_) }; \
    emit_instr((js_), A64_B_EQ(0)); \
} while(0)

/**
 * Emit ARM64 code for IR instructions in [start_ip, term_ip).
 * Returns false if any instruction cannot be compiled.
 */
static bool emit_block_ops_ir(
    jit_region_t *js, const jit_ir_region_t *ir, VM *vm,
    size_t start_ip, size_t term_ip,
    FailPatch *fail_patches, size_t *fail_patch_count)
{
    const uint8_t *bc = vm->bc; /* For literal bytes and range lookups only */

    for (size_t i = 0; i < ir->count; i++) {
        const jit_ir_instr_t *ins = &ir->instrs[i];

        /* Only process instructions in [start_ip, term_ip) */
        if (ins->bc_ip < start_ip || ins->bc_ip >= term_ip) continue;

        size_t cur = ins->bc_ip;

        switch (ins->opcode) {
        case JIT_IR_NOP:
        case JIT_IR_LABEL:
        case JIT_IR_GOTO_F:
        case JIT_IR_DYNAMIC_DEF:
            /* Pseudo-ops / no-emit */
            break;

        case JIT_IR_LIT: {
            const uint8_t *lit_data = ins->u.lit.data;
            uint32_t lit_len = ins->u.lit.len;
            emit_mov_x64(js, 7, lit_len);
            emit_instr(js, A64_ADD_X_X_X(8, 2, 7));
            emit_instr(js, A64_CMP_X_X(8, 3));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, A64_B_HI(0));
            for (uint32_t j = 0; j < lit_len; j++) {
                emit_instr(js, A64_LDRB_W_X_X(4, 1, 2));
                emit_mov_w32(js, 7, lit_data[j]);
                emit_instr(js, A64_CMP_W_W(4, 7));
                fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
                emit_instr(js, A64_B_NE(0));
                emit_instr(js, A64_MOV_X_IMM(7, 1));
                emit_instr(js, A64_ADD_X_X_X(2, 2, 7));
            }
            break;
        }

        case JIT_IR_ANY: case JIT_IR_NOTANY: case JIT_IR_SPAN:
        case JIT_IR_BREAK: case JIT_IR_BREAKX: {
            uint16_t set_id = ins->u.set.set_id;
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
            if (ins->opcode == JIT_IR_NOTANY) {
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
            if (ins->opcode == JIT_IR_NOTANY) {
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
            if (ins->opcode == JIT_IR_SPAN) {
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
                emit_patch_b_cond(loop_ge_done, span_done, 0x5400000a);
                emit_patch_b_cond(loop_hi_done, span_done, 0x54000008);
                emit_patch_tbz(loop_bit64_done, span_done, *loop_bit64_done & 0xfff8001f);
                emit_patch_tbz(loop_bit0_done,  span_done, *loop_bit0_done  & 0xfff8001f);
            }
            break;
        }

        case JIT_IR_CAP_START:
        case JIT_IR_CAP_END: {
            uint8_t r = ins->u.cap.reg;
            size_t off_field = (ins->opcode == JIT_IR_CAP_START)
                               ? offsetof(VM, cap_start) : offsetof(VM, cap_end);
            emit_instr(js, A64_STR_X_X_IMM(2, 0, off_field + r * 8));
            emit_ldrb_w_x_imm(js, 7, 0, (uint32_t)offsetof(VM, max_cap_used));
            emit_instr(js, A64_MOV_W_IMM(8, r + 1));
            emit_instr(js, A64_CMP_W_W(7, 8));
            uint32_t *lt_skip = js->p; emit_instr(js, A64_B_GE(0));
            emit_strb_w_x_imm(js, 8, 0, (uint32_t)offsetof(VM, max_cap_used));
            emit_patch_b_cond(lt_skip, js->p, 0x5400000a);
            break;
        }

        case JIT_IR_LEN: {
            uint32_t n = ins->u.len.n;
            emit_mov_x64(js, 7, n);
            emit_instr(js, A64_ADD_X_X_X(8, 2, 7));
            emit_instr(js, A64_CMP_X_X(8, 3));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, A64_B_HI(0));
            emit_instr(js, A64_ADD_X_X_X(2, 2, 7));
            break;
        }

        case JIT_IR_ANCHOR: {
            uint8_t type = ins->u.anchor.type;
            if (type == 0) { emit_instr(js, A64_MOV_X_IMM(7, 0)); emit_instr(js, A64_CMP_X_X(2, 7)); }
            else           { emit_instr(js, A64_CMP_X_X(2, 3)); }
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, A64_B_NE(0));
            break;
        }

        case JIT_IR_ASSIGN: {
            uint16_t var = ins->u.assign.var;
            uint8_t  reg = ins->u.assign.reg;
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
            break;
        }

        case JIT_IR_REM:
            emit_instr(js, A64_MOV_X_X(2, 3));
            break;

        case JIT_IR_RPOS: {
            uint32_t n = ins->u.rpos_rtab.n;
            emit_mov_x64(js, 7, n);
            emit_instr(js, A64_SUB_X_X_X(8, 3, 7));
            emit_instr(js, A64_CMP_X_X(2, 8));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, A64_B_NE(0));
            break;
        }

        case JIT_IR_RTAB: {
            uint32_t n = ins->u.rpos_rtab.n;
            emit_mov_x64(js, 7, n);
            emit_instr(js, A64_CMP_X_X(3, 7));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, A64_B_LO(0));
            emit_instr(js, A64_SUB_X_X_X(8, 3, 7));
            emit_instr(js, A64_CMP_X_X(2, 8));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, A64_B_HI(0));
            emit_instr(js, A64_MOV_X_X(2, 8));
            break;
        }

        case JIT_IR_FENCE:
            emit_instr(js, A64_MOV_X_IMM(7, 0));
            emit_instr(js, A64_STR_X_X_IMM(7, 0, offsetof(VM, choices_top)));
            break;

        case JIT_IR_EMIT_LITERAL: {
            uint32_t elt_off = ins->u.emit_lit.offset;
            uint32_t elt_len = ins->u.emit_lit.len;
            emit_instr(js, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_x64(js, 9, (uint64_t)(uintptr_t)snobol_jit_helper_emit_literal);
            emit_mov_w32(js, 1, elt_off);
            emit_mov_w32(js, 2, elt_len);
            emit_instr(js, 0xd63f0120u);
            EMIT_CALLOUT_RESTORE(js);
            break;
        }

        case JIT_IR_EMIT_CAPTURE: {
            uint8_t ecap_reg = ins->u.emit_cap.reg;
            emit_instr(js, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_x64(js, 9, (uint64_t)(uintptr_t)snobol_jit_helper_emit_capture);
            emit_instr(js, A64_MOV_X_IMM(1, ecap_reg));
            emit_instr(js, 0xd63f0120u);
            EMIT_CALLOUT_RESTORE(js);
            break;
        }

        case JIT_IR_EMIT_EXPR: {
            uint8_t eex_r = ins->u.emit_expr.reg;
            uint8_t eex_t = ins->u.emit_expr.expr_type;
            emit_instr(js, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_x64(js, 9, (uint64_t)(uintptr_t)snobol_jit_helper_emit_expr);
            emit_instr(js, A64_MOV_X_IMM(1, eex_r));
            emit_instr(js, A64_MOV_X_IMM(2, eex_t));
            emit_instr(js, 0xd63f0120u);
            EMIT_CALLOUT_RESTORE(js);
            break;
        }

        case JIT_IR_EMIT_FORMAT: {
            uint8_t  efmt_r = ins->u.emit_fmt.reg;
            uint8_t  efmt_t = ins->u.emit_fmt.fmt_type;
            uint16_t efmt_w = ins->u.emit_fmt.width;
            uint8_t  efmt_f = ins->u.emit_fmt.fill_char;
            emit_instr(js, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_x64(js, 9, (uint64_t)(uintptr_t)snobol_jit_helper_emit_format);
            emit_instr(js, A64_MOV_X_IMM(1, efmt_r));
            emit_instr(js, A64_MOV_X_IMM(2, efmt_t));
            emit_mov_w32(js, 3, efmt_w);
            emit_instr(js, A64_MOV_X_IMM(4, efmt_f));
            emit_instr(js, 0xd63f0120u);
            EMIT_CALLOUT_RESTORE(js);
            break;
        }

        case JIT_IR_EMIT_TABLE: {
            /* bc_ip is passed to the runtime helper which decodes operands from bc */
            emit_instr(js, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_x64(js, 9, (uint64_t)(uintptr_t)snobol_jit_helper_emit_table_ip);
            emit_mov_x64(js, 1, (uint64_t)cur);
            emit_instr(js, 0xd63f0120u);
            EMIT_CALLOUT_RESTORE(js);
            break;
        }

        case JIT_IR_TABLE_GET: {
#ifdef SNOBOL_DYNAMIC_PATTERN
            uint16_t tg_tid  = ins->u.tget.table_id;
            uint8_t  tg_kreg = ins->u.tget.key_reg;
            uint8_t  tg_dreg = ins->u.tget.dest_reg;
            emit_instr(js, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_x64(js, 9, (uint64_t)(uintptr_t)snobol_jit_helper_table_get);
            emit_mov_w32(js, 1, tg_tid);
            emit_instr(js, A64_MOV_X_IMM(2, tg_kreg));
            emit_instr(js, A64_MOV_X_IMM(3, tg_dreg));
            emit_instr(js, 0xd63f0120u);
            EMIT_CALLOUT_SAVE_RET(js);
            EMIT_CALLOUT_RESTORE(js);
            emit_instr(js, A64_MOV_W_IMM(7, 0));
            emit_instr(js, A64_CMP_W_W(9, 7));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, A64_B_EQ(0));
#else
            (void)ins;
#endif
            break;
        }

        case JIT_IR_TABLE_SET: {
#ifdef SNOBOL_DYNAMIC_PATTERN
            uint16_t ts_tid  = ins->u.tset.table_id;
            uint8_t  ts_kreg = ins->u.tset.key_reg;
            uint8_t  ts_vreg = ins->u.tset.val_reg;
            emit_instr(js, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_x64(js, 9, (uint64_t)(uintptr_t)snobol_jit_helper_table_set);
            emit_mov_w32(js, 1, ts_tid);
            emit_instr(js, A64_MOV_X_IMM(2, ts_kreg));
            emit_instr(js, A64_MOV_X_IMM(3, ts_vreg));
            emit_instr(js, 0xd63f0120u);
            EMIT_CALLOUT_RESTORE(js);
            emit_instr(js, A64_LDR_X_X_IMM(2, 0, offsetof(VM, pos)));
#else
            (void)ins;
#endif
            break;
        }

        case JIT_IR_BAL: {
            uint32_t bal_open  = ins->u.bal.open_cp;
            uint32_t bal_close = ins->u.bal.close_cp;
            emit_instr(js, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_x64(js, 9, (uint64_t)(uintptr_t)snobol_jit_helper_bal);
            emit_mov_w32(js, 1, bal_open);
            emit_mov_w32(js, 2, bal_close);
            emit_instr(js, 0xd63f0120u);
            EMIT_CALLOUT_SAVE_RET(js);
            EMIT_CALLOUT_RESTORE(js);
            emit_instr(js, A64_LDR_X_X_IMM(2, 0, offsetof(VM, pos)));
            emit_instr(js, A64_MOV_W_IMM(7, 0));
            emit_instr(js, A64_CMP_W_W(9, 7));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, A64_B_EQ(0));
            break;
        }

        case JIT_IR_EVAL: {
            uint16_t ev_fn  = ins->u.eval.fn_id;
            uint8_t  ev_reg = ins->u.eval.reg;
            emit_instr(js, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_x64(js, 9, (uint64_t)(uintptr_t)snobol_jit_helper_eval);
            emit_mov_w32(js, 1, ev_fn);
            emit_instr(js, A64_MOV_X_IMM(2, ev_reg));
            emit_instr(js, 0xd63f0120u);
            EMIT_CALLOUT_SAVE_RET(js);
            EMIT_CALLOUT_RESTORE(js);
            emit_instr(js, A64_LDR_X_X_IMM(2, 0, offsetof(VM, pos)));
            emit_instr(js, A64_MOV_W_IMM(7, 0));
            emit_instr(js, A64_CMP_W_W(9, 7));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, A64_B_EQ(0));
            break;
        }

        case JIT_IR_DYNAMIC: {
#ifdef SNOBOL_DYNAMIC_PATTERN
            emit_instr(js, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_x64(js, 9, (uint64_t)(uintptr_t)snobol_jit_helper_dynamic);
            emit_instr(js, 0xd63f0120u);
            EMIT_CALLOUT_SAVE_RET(js);
            EMIT_CALLOUT_RESTORE(js);
            emit_instr(js, A64_LDR_X_X_IMM(2, 0, offsetof(VM, pos)));
            emit_instr(js, A64_MOV_W_IMM(7, 0));
            emit_instr(js, A64_CMP_W_W(9, 7));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, A64_B_EQ(0));
#endif
            break;
        }

        /* SPLIT: handled by the block terminator code in arm64_lower() */
        case JIT_IR_SPLIT:
            break;

        /* Terminators not expected here (processed as block terminators) */
        case JIT_IR_ACCEPT:
        case JIT_IR_FAIL:
        case JIT_IR_REPEAT_INIT:
        case JIT_IR_REPEAT_STEP:
        case JIT_IR_JMP:
        case JIT_IR_GOTO:
            break;

        default:
            return false;
        }
    }
    (void)bc; /* suppress unused-variable warning */
    return true;
}

#undef EMIT_VOID_CALLOUT
#undef EMIT_BOOL_CALLOUT

/* =========================================================================
 * CFG epilogue helper
 * ========================================================================= */
static void emit_cfg_bailout_arm64(jit_region_t *js, size_t bail_ip) {
    emit_instr(js, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
    emit_mov_x64(js, 7, bail_ip);
    emit_instr(js, A64_STR_X_X_IMM(7, 0, offsetof(VM, ip)));
    emit_instr(js, A64_LDP_X19_X30_POST16);
    emit_instr(js, A64_RET);
}

/* =========================================================================
 * arm64_lower()  — main function: IR → ARM64 machine code
 * ========================================================================= */
static void *arm64_lower(const jit_ir_region_t *ir, VM *vm, jit_region_t *out) {
    if (!ir || !vm || !out) return nullptr;
    if (ir->non_compilable || ir->count == 0) return nullptr;

    /* Build CFG from IR */
    JitCfg cfg;
    int n_blocks = ir_cfg_build(ir, &cfg);

    if (n_blocks <= 0) return nullptr;

    bool use_cfg = (n_blocks >= 2 || (n_blocks == 1 && cfg.has_backward));

    /* Allocate code buffer */
    size_t code_size = use_cfg ? 32768u : 16384u;
    uint32_t *code = (uint32_t *)snobol_jit_alloc_code(code_size);
    if (!code) return nullptr;

#ifdef SNOBOL_JIT_PLATFORM_MACOS
    pthread_jit_write_protect_np(0);
#endif

    out->p          = code;
    out->code_start = code;
    out->code_size  = code_size;

    FailPatch fail_patches[1024];
    size_t    fail_patch_count = 0;

    if (use_cfg) {
        /* ---- CFG multi-block path ---- */
        StubPatch stub_patches[256];
        int       stub_patch_count = 0;

        /* Prologue */
        emit_instr(out, A64_STP_X19_X30_PRE16);
        emit_instr(out, A64_LDR_X_X_IMM(1, 0, offsetof(VM, s)));
        emit_instr(out, A64_LDR_X_X_IMM(2, 0, offsetof(VM, pos)));
        emit_instr(out, A64_LDR_X_X_IMM(3, 0, offsetof(VM, len)));
        if (cfg.has_backward)
            emit_mov_w32(out, 19, JIT_LOOP_ITER_MAX);

        for (int bi = 0; bi < cfg.count; bi++) {
            JitBlock *blk = &cfg.blocks[bi];
            blk->stub_start = out->p;

            bool ok = emit_block_ops_ir(out, ir, vm,
                                        blk->start_ip, blk->term_ip,
                                        fail_patches, &fail_patch_count);

            if (!ok) {
                emit_cfg_bailout_arm64(out, blk->term_ip);
                blk->term = BLOCK_TERM_EXIT;
                continue;
            }

            switch (blk->term) {
            case BLOCK_TERM_SPLIT: {
                if (blk->succ_a_blk < 0) {
                    emit_cfg_bailout_arm64(out, blk->term_ip);
                    break;
                }
                emit_instr(out, 0xa9be7be0u); /* STP x0, x30, [sp, #-32]! */
                emit_instr(out, 0xa9010be1u); /* STP x1, x2,  [sp, #16]   */
                emit_mov_x64(out, 9, (uint64_t)(uintptr_t)vm_push_choice);
                emit_mov_x64(out, 1, (uint64_t)blk->succ_b);
                emit_instr(out, 0xd63f0120u); /* BLR x9 */
                emit_instr(out, 0xa9410be1u); /* LDP x1, x2, [sp, #16]   */
                emit_instr(out, 0xa8c27be0u); /* LDP x0, x30, [sp], #32  */
                emit_instr(out, A64_LDR_X_X_IMM(3, 0, offsetof(VM, len)));
                stub_patches[stub_patch_count++] = (StubPatch){ out->p, blk->succ_a };
                emit_instr(out, A64_B(0));
                break;
            }
            case BLOCK_TERM_JMP_FWD: {
                if (blk->succ_a_blk < 0) {
                    emit_cfg_bailout_arm64(out, blk->term_ip);
                    break;
                }
                stub_patches[stub_patch_count++] = (StubPatch){ out->p, blk->succ_a };
                emit_instr(out, A64_B(0));
                break;
            }
            case BLOCK_TERM_JMP_BWD: {
                if (blk->succ_a_blk < 0) {
                    emit_cfg_bailout_arm64(out, blk->term_ip);
                    break;
                }
                emit_instr(out, A64_SUBS_X19_X19_1);
                uint32_t *bail_patch = out->p;
                emit_instr(out, A64_B_EQ(0));
                uint32_t *loop_head = cfg.blocks[blk->succ_a_blk].stub_start;
                intptr_t  diff = loop_head - out->p;
                emit_instr(out, A64_B((uint32_t)(diff & 0x3ffffff)));
                emit_patch_b_cond(bail_patch, out->p, 0x54000000);
                emit_cfg_bailout_arm64(out, blk->term_ip);
                break;
            }
            case BLOCK_TERM_EXIT:
                emit_cfg_bailout_arm64(out, blk->term_ip);
                break;
            }
        }

        /* Fail stubs */
        for (size_t f = 0; f < fail_patch_count; f++) {
            uint32_t *fail_stub = out->p;
            emit_cfg_bailout_arm64(out, fail_patches[f].ip);
            uint32_t orig = *fail_patches[f].instr_p;
            if ((orig & 0xff000000) == 0x54000000)
                emit_patch_b_cond(fail_patches[f].instr_p, fail_stub, orig & 0xff00001f);
            else if ((orig & 0x7e000000) == 0x36000000)
                emit_patch_tbz(fail_patches[f].instr_p, fail_stub, orig & 0xfff8001f);
        }

        /* Forward-branch fixup */
        for (int s = 0; s < stub_patch_count; s++) {
            uint32_t *p         = stub_patches[s].instr_p;
            size_t    target_ip = stub_patches[s].target_ip;
            int       blk_idx   = ir_find_block(&cfg, target_ip);
            if (blk_idx >= 0 && cfg.blocks[blk_idx].stub_start)
                emit_patch_b(p, cfg.blocks[blk_idx].stub_start);
        }

    } else {
        /* ---- Linear single-block path ---- */
        JitBlock *blk = &cfg.blocks[0];

        emit_instr(out, A64_LDR_X_X_IMM(1, 0, offsetof(VM, s)));
        emit_instr(out, A64_LDR_X_X_IMM(2, 0, offsetof(VM, pos)));
        emit_instr(out, A64_LDR_X_X_IMM(3, 0, offsetof(VM, len)));

        bool ok = emit_block_ops_ir(out, ir, vm,
                                    blk->start_ip, blk->term_ip,
                                    fail_patches, &fail_patch_count);

        if (!ok) {
#ifdef SNOBOL_JIT_PLATFORM_MACOS
            pthread_jit_write_protect_np(1);
#endif
            snobol_jit_free_code(code, code_size);
            return nullptr;
        }

        /* Success epilogue — use term_ip so the interpreter re-dispatches
         * ACCEPT (or FAIL) and returns the correct result to the caller. */
        emit_instr(out, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
        emit_mov_x64(out, 7, blk->term_ip);
        emit_instr(out, A64_STR_X_X_IMM(7, 0, offsetof(VM, ip)));
        emit_instr(out, A64_RET);

        /* Fail stubs */
        for (size_t f = 0; f < fail_patch_count; f++) {
            uint32_t *fail_stub = out->p;
            emit_instr(out, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
            emit_mov_x64(out, 7, fail_patches[f].ip);
            emit_instr(out, A64_STR_X_X_IMM(7, 0, offsetof(VM, ip)));
            emit_instr(out, A64_RET);
            uint32_t orig = *fail_patches[f].instr_p;
            if ((orig & 0xff000000) == 0x54000000)
                emit_patch_b_cond(fail_patches[f].instr_p, fail_stub, orig & 0xff00001f);
            else if ((orig & 0x7e000000) == 0x36000000)
                emit_patch_tbz(fail_patches[f].instr_p, fail_stub, orig & 0xfff8001f);
        }
    }

    snobol_jit_seal_code(code, code_size);
    return (void *)code;
}

/* =========================================================================
 * arm64_flush_icache()
 * Wraps the existing clear-cache / seal-code mechanism.
 * Called automatically by arm64_lower() via snobol_jit_seal_code().
 * Exposed separately for completeness of the vtable interface.
 * ========================================================================= */
static void arm64_flush_icache(void *code, size_t size) {
#ifdef SNOBOL_JIT_PLATFORM_MACOS
    pthread_jit_write_protect_np(1);
    __builtin___clear_cache((char *)code, (char *)code + size);
#elif defined(SNOBOL_JIT_PLATFORM_LINUX)
    /* __builtin___clear_cache emits the ISB barrier on AArch64.
     * Some older kernels or QEMU user-mode may not honour it, so we
     * fall back to the cacheflush syscall for robustness. */
    __builtin___clear_cache((char *)code, (char *)code + size);
    syscall(__NR_cacheflush, code, size, 0);
#endif
}

/* =========================================================================
 * Backend vtable and registration
 * ========================================================================= */

static const jit_backend_t arm64_backend_vtable = {
    .name        = "arm64",
    .lower       = arm64_lower,
    .flush_icache = arm64_flush_icache,
};

void snobol_jit_arm64_register(void) {
    jit_backend_register(&arm64_backend_vtable);
}

#endif /* __aarch64__ || __arm64__ */
#endif /* SNOBOL_JIT */



