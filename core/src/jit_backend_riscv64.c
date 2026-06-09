/**
 * @file jit_backend_riscv64.c
 * @brief RISC-V 64-bit JIT backend for the SNOBOL4 micro-JIT.
 *
 * Implements the jit_backend_t vtable for RV64GC targets.
 * Primary entry point: riscv64_lower() — translates a jit_ir_region_t to
 * RV64I machine code stored in the provided jit_region_t.
 *
 * Register convention (RISC-V psABI-based):
 *   a0 (x10) = VM pointer
 *   t0 (x5)  = s  (string base, from VM->s)
 *   t1 (x6)  = pos (current match position, from VM->pos)
 *   t2 (x7)  = len (string length, from VM->len)
 *   t3 (x28) = scratch / function-pointer for call-outs
 *   t4 (x29) = scratch
 *   t5 (x30) = scratch
 *   t6 (x31) = scratch
 *   s2 (x18) = loop iteration counter (backward branches)
 *   ra (x1)  = return address
 */

#include "snobol/jit_backend.h"
#include "snobol/jit_ir.h"
#include "snobol/jit.h"

#ifdef SNOBOL_JIT
#if defined(__riscv) && __riscv_xlen == 64

#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include "snobol/vm.h"
#include "snobol/snobol_internal.h"
#include "snobol/table.h"
#include "snobol/dynamic_pattern.h"
#include "snobol/string_fn.h"
#include "snobol/type_fn.h"

/* =========================================================================
 * Register aliases
 * ========================================================================= */
#define RV64_ZERO  0  /* x0 - zero register */
#define RV64_RA    1  /* ra - return address */
#define RV64_SP    2  /* sp - stack pointer */
#define RV64_GP    3  /* gp - global pointer (reserved) */
#define RV64_TP    4  /* tp - thread pointer (reserved) */
#define RV64_S     5  /* t0 - s (string base) */
#define RV64_POS   6  /* t1 - pos (current match position) */
#define RV64_LEN   7  /* t2 - len (string length) */
#define RV64_T3   28  /* t3 - scratch */
#define RV64_T4   29  /* t4 - scratch */
#define RV64_T5   30  /* t5 - scratch */
#define RV64_T6   31  /* t6 - scratch */
#define RV64_FNP  28  /* t3 reused as function-pointer register */
#define RV64_LOOP 18  /* s2 - loop iteration counter */
#define RV64_VM   10  /* a0 - VM pointer */
#define RV64_A1   11  /* a1 - call-out arg 1 */
#define RV64_A2   12  /* a2 - call-out arg 2 */
#define RV64_A3   13  /* a3 - call-out arg 3 */
#define RV64_A4   14  /* a4 - call-out arg 4 */
#define RV64_A5   15  /* a5 - call-out arg 5 */
#define RV64_A6   16  /* a6 - call-out arg 6 */
#define RV64_A7   17  /* a7 - call-out arg 7 */

/* =========================================================================
 * RV64I instruction encoding helpers
 * ========================================================================= */

/* Opcodes */
#define RV_OP_LOAD   0x03
#define RV_OP_STORE  0x23
#define RV_OP_BRANCH 0x63
#define RV_OP_JALR   0x67
#define RV_OP_JAL    0x6F
#define RV_OP_AUIPC  0x17
#define RV_OP_LUI    0x37
#define RV_OP_OP_IMM 0x13
#define RV_OP_OP     0x33
#define RV_OP_FENCE  0x0F

/* funct3 values */
#define RV_F3_BEQ   0
#define RV_F3_BNE   1
#define RV_F3_BLT   4
#define RV_F3_BGE   5
#define RV_F3_BLTU  6
#define RV_F3_BGEU  7
#define RV_F3_ADD   0
#define RV_F3_ADDI  0
#define RV_F3_SLTI  2
#define RV_F3_SLTIU 3
#define RV_F3_XORI  4
#define RV_F3_ORI   6
#define RV_F3_ANDI  7
#define RV_F3_LB    0
#define RV_F3_LH    1
#define RV_F3_LW    2
#define RV_F3_LD    3
#define RV_F3_LBU   4
#define RV_F3_LHU   5
#define RV_F3_SB    0
#define RV_F3_SH    1
#define RV_F3_SW    2
#define RV_F3_SD    3
#define RV_F3_SLL   1
#define RV_F3_SRL   5
#define RV_F3_JALR  0
#define RV_F3_MUL   0
#define RV_F3_AND   7
#define RV_F3_OR    6
#define RV_F3_XOR   4
#define RV_F3_SLT   2
#define RV_F3_SLTU  3

/* funct7 values */
#define RV_F7_ADD   0x00
#define RV_F7_SUB   0x20
#define RV_F7_SRA   0x20

/* R-type: op | rd | funct3 | rs1 | rs2 | funct7 */
static inline uint32_t rv64_r(uint32_t op, uint32_t rd, uint32_t funct3,
                               uint32_t rs1, uint32_t rs2, uint32_t funct7) {
    return op | (rd << 7) | (funct3 << 12) | (rs1 << 15) | (rs2 << 20) | (funct7 << 25);
}

/* I-type: op | rd | funct3 | rs1 | imm11_0 */
static inline uint32_t rv64_i(uint32_t op, uint32_t rd, uint32_t funct3,
                               uint32_t rs1, uint32_t imm11_0) {
    return op | (rd << 7) | (funct3 << 12) | (rs1 << 15) | ((imm11_0 & 0xFFF) << 20);
}

/* S-type: op | imm11_5 | rs2 | funct3 | rs1 | imm4_0 */
static inline uint32_t rv64_s(uint32_t op, uint32_t funct3,
                               uint32_t rs1, uint32_t rs2, uint32_t imm) {
    uint32_t imm11_5 = (imm >> 5) & 0x7F;
    uint32_t imm4_0  = imm & 0x1F;
    return op | (imm11_5 << 25) | (rs2 << 20) | (funct3 << 12) | (rs1 << 15) | (imm4_0 << 7);
}

/* B-type: op | funct3 | rs1 | rs2 | imm[12|10:5|4:1|11] */
static inline uint32_t rv64_b(uint32_t op, uint32_t funct3,
                               uint32_t rs1, uint32_t rs2, uint32_t imm) {
    uint32_t b12  = (imm >> 12) & 1;
    uint32_t b11  = (imm >> 11) & 1;
    uint32_t b105 = (imm >> 5) & 0x3F;
    uint32_t b41  = (imm >> 1) & 0x0F;
    return op
        | (b12  << 31)
        | (b105 << 25)
        | (rs2  << 20)
        | (funct3 << 12)
        | (rs1  << 15)
        | (b41  << 8)
        | (b11  << 7);
}

/* U-type: op | rd | imm31_12 */
static inline uint32_t rv64_u(uint32_t op, uint32_t rd, uint32_t imm) {
    return op | (rd << 7) | (imm & 0xFFFFF000);
}

/* J-type: op | rd | imm[20|10:1|11|19:12] */
static inline uint32_t rv64_j(uint32_t op, uint32_t rd, uint32_t imm) {
    uint32_t j20   = (imm >> 20) & 1;
    uint32_t j1912 = (imm >> 12) & 0xFF;
    uint32_t j11   = (imm >> 11) & 1;
    uint32_t j101  = (imm >> 1) & 0x3FF;
    return op
        | (rd   << 7)
        | (j20  << 31)
        | (j101 << 21)
        | (j11  << 20)
        | (j1912 << 12);
}

/* Convenience wrappers */
static inline uint32_t rv64_add(uint32_t rd, uint32_t rs1, uint32_t rs2) {
    return rv64_r(RV_OP_OP, rd, RV_F3_ADD, rs1, rs2, RV_F7_ADD);
}
static inline uint32_t rv64_sub(uint32_t rd, uint32_t rs1, uint32_t rs2) {
    return rv64_r(RV_OP_OP, rd, RV_F3_ADD, rs1, rs2, RV_F7_SUB);
}
static inline uint32_t rv64_and(uint32_t rd, uint32_t rs1, uint32_t rs2) {
    return rv64_r(RV_OP_OP, rd, RV_F3_AND, rs1, rs2, RV_F7_ADD);
}
static inline uint32_t rv64_or(uint32_t rd, uint32_t rs1, uint32_t rs2) {
    return rv64_r(RV_OP_OP, rd, RV_F3_OR, rs1, rs2, RV_F7_ADD);
}
static inline uint32_t rv64_xor(uint32_t rd, uint32_t rs1, uint32_t rs2) {
    return rv64_r(RV_OP_OP, rd, RV_F3_XOR, rs1, rs2, RV_F7_ADD);
}
static inline uint32_t rv64_slt(uint32_t rd, uint32_t rs1, uint32_t rs2) {
    return rv64_r(RV_OP_OP, rd, RV_F3_SLT, rs1, rs2, RV_F7_ADD);
}
static inline uint32_t rv64_sltu(uint32_t rd, uint32_t rs1, uint32_t rs2) {
    return rv64_r(RV_OP_OP, rd, RV_F3_SLTU, rs1, rs2, RV_F7_ADD);
}
static inline uint32_t rv64_addi(uint32_t rd, uint32_t rs1, uint32_t imm) {
    return rv64_i(RV_OP_OP_IMM, rd, RV_F3_ADDI, rs1, imm);
}
static inline uint32_t rv64_andi(uint32_t rd, uint32_t rs1, uint32_t imm) {
    return rv64_i(RV_OP_OP_IMM, rd, RV_F3_ANDI, rs1, imm);
}
static inline uint32_t rv64_ori(uint32_t rd, uint32_t rs1, uint32_t imm) {
    return rv64_i(RV_OP_OP_IMM, rd, RV_F3_ORI, rs1, imm);
}
static inline uint32_t rv64_xori(uint32_t rd, uint32_t rs1, uint32_t imm) {
    return rv64_i(RV_OP_OP_IMM, rd, RV_F3_XORI, rs1, imm);
}
static inline uint32_t rv64_slli(uint32_t rd, uint32_t rs1, uint32_t shamt) {
    return rv64_i(RV_OP_OP_IMM, rd, RV_F3_SLL, rs1, shamt & 0x3F);
}
static inline uint32_t rv64_srli(uint32_t rd, uint32_t rs1, uint32_t shamt) {
    return rv64_i(RV_OP_OP_IMM, rd, RV_F3_SRL, rs1, shamt & 0x3F);
}
static inline uint32_t rv64_srai(uint32_t rd, uint32_t rs1, uint32_t shamt) {
    return rv64_i(RV_OP_OP_IMM, rd, RV_F3_SRL, rs1, (shamt & 0x3F) | (0x400 << 20));
}
static inline uint32_t rv64_sll(uint32_t rd, uint32_t rs1, uint32_t rs2) {
    return rv64_r(RV_OP_OP, rd, RV_F3_SLL, rs1, rs2, RV_F7_ADD);
}
static inline uint32_t rv64_srl(uint32_t rd, uint32_t rs1, uint32_t rs2) {
    return rv64_r(RV_OP_OP, rd, RV_F3_SRL, rs1, rs2, RV_F7_ADD);
}
static inline uint32_t rv64_sra(uint32_t rd, uint32_t rs1, uint32_t rs2) {
    return rv64_r(RV_OP_OP, rd, RV_F3_SRL, rs1, rs2, RV_F7_SUB);
}
static inline uint32_t rv64_ld(uint32_t rd, uint32_t rs1, uint32_t imm) {
    return rv64_i(RV_OP_LOAD, rd, RV_F3_LD, rs1, imm);
}
static inline uint32_t rv64_lb(uint32_t rd, uint32_t rs1, uint32_t imm) {
    return rv64_i(RV_OP_LOAD, rd, RV_F3_LB, rs1, imm);
}
static inline uint32_t rv64_lbu(uint32_t rd, uint32_t rs1, uint32_t imm) {
    return rv64_i(RV_OP_LOAD, rd, RV_F3_LBU, rs1, imm);
}
static inline uint32_t rv64_sd(uint32_t rs1, uint32_t rs2, uint32_t imm) {
    return rv64_s(RV_OP_STORE, RV_F3_SD, rs1, rs2, imm);
}
static inline uint32_t rv64_sb(uint32_t rs1, uint32_t rs2, uint32_t imm) {
    return rv64_s(RV_OP_STORE, RV_F3_SB, rs1, rs2, imm);
}
static inline uint32_t rv64_beq(uint32_t rs1, uint32_t rs2, uint32_t imm) {
    return rv64_b(RV_OP_BRANCH, RV_F3_BEQ, rs1, rs2, imm);
}
static inline uint32_t rv64_bne(uint32_t rs1, uint32_t rs2, uint32_t imm) {
    return rv64_b(RV_OP_BRANCH, RV_F3_BNE, rs1, rs2, imm);
}
static inline uint32_t rv64_blt(uint32_t rs1, uint32_t rs2, uint32_t imm) {
    return rv64_b(RV_OP_BRANCH, RV_F3_BLT, rs1, rs2, imm);
}
static inline uint32_t rv64_bge(uint32_t rs1, uint32_t rs2, uint32_t imm) {
    return rv64_b(RV_OP_BRANCH, RV_F3_BGE, rs1, rs2, imm);
}
static inline uint32_t rv64_bltu(uint32_t rs1, uint32_t rs2, uint32_t imm) {
    return rv64_b(RV_OP_BRANCH, RV_F3_BLTU, rs1, rs2, imm);
}
static inline uint32_t rv64_bgeu(uint32_t rs1, uint32_t rs2, uint32_t imm) {
    return rv64_b(RV_OP_BRANCH, RV_F3_BGEU, rs1, rs2, imm);
}
static inline uint32_t rv64_jal(uint32_t rd, uint32_t imm) {
    return rv64_j(RV_OP_JAL, rd, imm);
}
static inline uint32_t rv64_jalr(uint32_t rd, uint32_t rs1, uint32_t imm) {
    return rv64_i(RV_OP_JALR, rd, RV_F3_JALR, rs1, imm);
}
static inline uint32_t rv64_auipc(uint32_t rd, uint32_t imm) {
    return rv64_u(RV_OP_AUIPC, rd, imm << 12);
}
static inline uint32_t rv64_lui(uint32_t rd, uint32_t imm) {
    return rv64_u(RV_OP_LUI, rd, imm << 12);
}
static inline uint32_t rv64_nop(void) {
    return rv64_addi(RV64_ZERO, RV64_ZERO, 0);
}

/* Compute AUIPC hi20 and JALR lo12 for a PC-relative call/jump.
 * Returns the offset and also stores the lo12 part. */
static inline int32_t rv64_pcrel_hi(int64_t target, int64_t pc) {
    int64_t delta = target - pc;
    return (int32_t)((delta + 2048) >> 12) & 0xFFFFF;
}
static inline int32_t rv64_pcrel_lo(int64_t target, int64_t pc) {
    int64_t delta = target - pc;
    int32_t hi = (int32_t)((delta + 2048) >> 12);
    return (int32_t)(delta - ((int64_t)hi << 12)) & 0xFFF;
}

/* Emit a 32-bit instruction */
static inline void emit_instr(jit_region_t *js, uint32_t ins) {
    *(js->p)++ = ins;
}

/* Emit `n` copies of a 32-bit value (used for literal pools at the end) */
static inline void emit_data32(jit_region_t *js, uint32_t val, size_t n) {
    for (size_t i = 0; i < n; i++) *(js->p)++ = val;
}

/* =========================================================================
 * Immediate helpers for RV64
 * ========================================================================= */

/* Load a 12-bit signed immediate into rd using ADDI (range [-2048, 2047]) */
static void emit_mov_imm12(jit_region_t *js, int rd, int32_t val) {
    if (val >= -2048 && val <= 2047) {
        emit_instr(js, rv64_addi(rd, RV64_ZERO, (uint32_t)(val & 0xFFF)));
    } else {
        emit_instr(js, rv64_lui(rd, (uint32_t)((val + 0x800) >> 12) & 0xFFFFF));
        emit_instr(js, rv64_addi(rd, rd, (uint32_t)(val & 0xFFF)));
    }
}

/* Load a 32-bit signed value into rd: LUI + ADDI (sign-extends to 64-bit).
 * For values in [-2GB, 2GB-1] this is correct. */
static void emit_mov_imm32(jit_region_t *js, int rd, int32_t val) {
    uint32_t hi = (((uint32_t)val + 0x800u) >> 12) & 0xFFFFF;
    int32_t  lo = val - (int32_t)(hi << 12);
    if (hi != 0) {
        emit_instr(js, rv64_lui(rd, hi));
    }
    if (lo != 0 || hi == 0) {
        emit_instr(js, rv64_addi(rd, (hi != 0) ? (uint32_t)rd : (uint32_t)RV64_ZERO,
                                  (uint32_t)(lo & 0xFFF)));
    }
}

/* Load a 64-bit value into rd.
 * Strategy: split into upper and lower 32-bit halves.
 * Upper half: emit_mov_imm32 + SLLI 32.  Lower half: emit_mov_imm32 + SLLI 32 + SRLI 32
 * (zero-extend).  Then ADD to combine.
 *
 * The SLLI+SRLI zero-extension on the lower half is harmless when
 * lo32 bit 31 is clear (the value is already zero-extended).
 */
static void emit_mov_imm64(jit_region_t *js, int rd, uint64_t val) {
    if (val == 0) {
        emit_instr(js, rv64_addi(rd, RV64_ZERO, 0));
        return;
    }
    int64_t sval = (int64_t)val;
    if (sval >= -2147483648LL && sval <= 2147483647LL) {
        emit_mov_imm32(js, rd, (int32_t)val);
        return;
    }
    uint32_t hi32 = (uint32_t)(val >> 32);
    uint32_t lo32 = (uint32_t)val;
    if (hi32) {
        emit_mov_imm32(js, RV64_T6, (int32_t)hi32);
        emit_instr(js, rv64_slli(RV64_T6, RV64_T6, 32));
    }
    emit_mov_imm32(js, rd, (int32_t)lo32);
    emit_instr(js, rv64_slli(rd, rd, 32));
    emit_instr(js, rv64_srli(rd, rd, 32));
    if (hi32) {
        emit_instr(js, rv64_add(rd, rd, RV64_T6));
    }
}

/* ---------------------------------------------------------------------------
 * CFG structures (same model as arm64/arm32 backends)
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

/* =========================================================================
 * Low-level emit helpers
 * ========================================================================= */

/* Load from VM field at byte offset into rd: LD rd, offset(a0) */
static void emit_ld_from_vm(jit_region_t *js, int rd, size_t offset) {
    emit_instr(js, rv64_ld(rd, RV64_VM, (uint32_t)offset));
}

/* Store rd to VM field at byte offset: SD rd, offset(a0) */
static void emit_sd_to_vm(jit_region_t *js, int rd, size_t offset) {
    emit_instr(js, rv64_sd(RV64_VM, rd, (uint32_t)offset));
}

/* Emit a 32-bit unsigned immediate into a register (for imm <= 0xFFFF). */
static void emit_mov_w16(jit_region_t *js, int rd, uint32_t val) {
    if (val <= 2047) {
        emit_instr(js, rv64_addi(rd, RV64_ZERO, val));
    } else {
        emit_instr(js, rv64_lui(rd, val >> 12));
        if (val & 0xFFF) emit_instr(js, rv64_addi(rd, rd, val & 0xFFF));
    }
}

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

/* =========================================================================
 * Call-out macros
 *
 * The RV64 psABI uses a0-a7 for argument passing.  Our register convention
 * keeps the VM pointer in a0 (x10), so call-out helpers receive VM as arg0.
 *
 * Save sequence: store ra, a0(VM), t0(s), t1(pos), t2(len), t3(fnp) to stack.
 * Restore sequence: reload them and pop the stack frame.
 * ========================================================================= */

#define EMIT_CALLOUT_SAVE(js) do { \
    emit_instr((js), rv64_addi(RV64_SP, RV64_SP, -40)); \
    emit_instr((js), rv64_sd(RV64_SP, RV64_RA, 0)); \
    emit_instr((js), rv64_sd(RV64_SP, RV64_VM, 8)); \
    emit_instr((js), rv64_sd(RV64_SP, RV64_S,  16)); \
    emit_instr((js), rv64_sd(RV64_SP, RV64_POS, 24)); \
    emit_instr((js), rv64_sd(RV64_SP, RV64_LEN, 32)); \
} while(0)

#define EMIT_CALLOUT_RESTORE(js) do { \
    emit_instr((js), rv64_ld(RV64_RA, RV64_SP, 0)); \
    emit_instr((js), rv64_ld(RV64_VM, RV64_SP, 8)); \
    emit_instr((js), rv64_ld(RV64_S,  RV64_SP, 16)); \
    emit_instr((js), rv64_ld(RV64_POS, RV64_SP, 24)); \
    emit_instr((js), rv64_ld(RV64_LEN, RV64_SP, 32)); \
    emit_instr((js), rv64_addi(RV64_SP, RV64_SP, 40)); \
    emit_ld_from_vm((js), RV64_LEN, offsetof(VM, len)); \
} while(0)

#define EMIT_CALLOUT_SAVE_RET(js) \
    emit_instr((js), rv64_addi(RV64_T3, RV64_VM, 0))

/* =========================================================================
 * JIT call-out helper functions (identical across all backends)
 * These are C functions, not platform-specific.
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

static size_t ir_find_instr(const jit_ir_region_t *ir, size_t target_ip) {
    for (size_t i = 0; i < ir->count; i++) {
        if (ir->instrs[i].bc_ip == target_ip) return i;
    }
    return ir->count;
}

/* =========================================================================
 * CFG builder from IR (identical logic to arm64, but bytecode sizes
 * follow the standard SNOBOL4 bytecode layout)
 * ========================================================================= */

static int ir_find_block(const JitCfg *cfg, size_t ip) {
    for (int i = 0; i < cfg->count; i++)
        if (cfg->blocks[i].start_ip == ip) return i;
    return -1;
}

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
            blk->next_ip  = ins->bc_ip + 9;
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
    size_t last_bc_ip = ir->instrs[ir->count - 1].bc_ip;
    blk->term    = BLOCK_TERM_EXIT;
    blk->term_ip = last_bc_ip;
    blk->next_ip = last_bc_ip;
}

static int ir_cfg_build(const jit_ir_region_t *ir, JitCfg *cfg) {
    cfg->count        = 0;
    cfg->has_backward = false;

    if (!ir || ir->count == 0) return 0;

    size_t queue[JIT_CFG_MAX_BLOCKS * 2];
    int    qhead = 0, qtail = 0;
    size_t visited[JIT_CFG_MAX_BLOCKS];
    int    vcount = 0;

    queue[qtail++] = ir->instrs[0].bc_ip;

    while (qhead < qtail) {
        size_t ip = queue[qhead++];

        bool seen = false;
        for (int i = 0; i < vcount; i++) if (visited[i] == ip) { seen = true; break; }
        if (seen) continue;
        visited[vcount++] = ip;

        if (cfg->count >= JIT_CFG_MAX_BLOCKS) break;

        size_t ir_idx = ir_find_instr(ir, ip);
        if (ir_idx >= ir->count) continue;

        JitBlock *blk = &cfg->blocks[cfg->count++];
        ir_cfg_scan_block(ir, ir_idx, blk);

        switch (blk->term) {
        case BLOCK_TERM_SPLIT: {
            bool sa = false, sb = false;
            for (int i = 0; i < vcount; i++) {
                if (visited[i] == blk->succ_a) sa = true;
                if (visited[i] == blk->succ_b) sb = true;
            }
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
 * Per-block RV64I code emission from IR
 *
 * Iterates over IR instructions whose bc_ip falls in [start_ip, term_ip)
 * and emits RV64 code for each.  Operands are read from IR.
 * ========================================================================= */

#define EMIT_VOID_CALLOUT(js_, fn_ptr_, pc_) do { \
    EMIT_CALLOUT_SAVE(js_); \
    emit_mov_imm64((js_), RV64_FNP, (uint64_t)(uintptr_t)(fn_ptr_)); \
    emit_instr((js_), rv64_jalr(RV64_RA, RV64_FNP, 0)); \
    EMIT_CALLOUT_RESTORE(js_); \
} while(0)

#define EMIT_BOOL_CALLOUT(js_, fn_ptr_, fp_, fpc_, cur_) do { \
    emit_instr((js_), rv64_sd(RV64_VM, RV64_POS, offsetof(VM, pos))); \
    EMIT_CALLOUT_SAVE(js_); \
    emit_mov_imm64((js_), RV64_FNP, (uint64_t)(uintptr_t)(fn_ptr_)); \
    emit_instr((js_), rv64_jalr(RV64_RA, RV64_FNP, 0)); \
    EMIT_CALLOUT_SAVE_RET(js_); \
    EMIT_CALLOUT_RESTORE(js_); \
    emit_ld_from_vm((js_), RV64_POS, offsetof(VM, pos)); \
    (fp_)[(*(fpc_))++] = (FailPatch){ (js_)->p, (cur_) }; \
    emit_instr((js_), rv64_beq(RV64_FNP, RV64_ZERO, 0)); \
} while(0)

static bool emit_block_ops_ir(
    jit_region_t *js, const jit_ir_region_t *ir, VM *vm,
    size_t start_ip, size_t term_ip,
    FailPatch *fail_patches, size_t *fail_patch_count)
{
    const uint8_t *bc = vm->bc;

    for (size_t i = 0; i < ir->count; i++) {
        const jit_ir_instr_t *ins = &ir->instrs[i];

        if (ins->bc_ip < start_ip || ins->bc_ip >= term_ip) continue;

        size_t cur = ins->bc_ip;

        switch (ins->opcode) {
        case JIT_IR_NOP:
        case JIT_IR_LABEL:
        case JIT_IR_GOTO_F:
        case JIT_IR_DYNAMIC_DEF:
            break;

        case JIT_IR_LIT: {
            const uint8_t *lit_data = ins->u.lit.data;
            uint32_t lit_len = ins->u.lit.len;
            emit_mov_imm64(js, RV64_T4, lit_len);
            emit_instr(js, rv64_add(RV64_T5, RV64_POS, RV64_T4));
            emit_instr(js, rv64_sltu(RV64_T5, RV64_LEN, RV64_T5));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, rv64_bne(RV64_T5, RV64_ZERO, 0));
            emit_instr(js, rv64_add(RV64_T3, RV64_S, RV64_POS));
            for (uint32_t j = 0; j < lit_len; j++) {
                emit_instr(js, rv64_lbu(RV64_T4, RV64_T3, 0));
                emit_mov_imm64(js, RV64_T5, lit_data[j]);
                emit_instr(js, rv64_sub(RV64_T5, RV64_T4, RV64_T5));
                fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
                emit_instr(js, rv64_bne(RV64_T5, RV64_ZERO, 0));
                emit_instr(js, rv64_addi(RV64_T3, RV64_T3, 1));
                emit_instr(js, rv64_addi(RV64_POS, RV64_POS, 1));
            }
            break;
        }

        case JIT_IR_ANY: case JIT_IR_NOTANY:
        case JIT_IR_BREAK: case JIT_IR_BREAKX: {
            uint16_t set_id = ins->u.set.set_id;
            uint16_t count16, ci;
            const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count16, &ci);
            uint64_t ascii_map[2];
            if (!ranges || !ranges_to_ascii_bitmap(ranges, count16, ascii_map)) return false;

            emit_mov_imm64(js, RV64_T4, ascii_map[0]);
            emit_mov_imm64(js, RV64_T5, ascii_map[1]);

            emit_instr(js, rv64_sltu(RV64_T6, RV64_POS, RV64_LEN));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, rv64_beq(RV64_T6, RV64_ZERO, 0));

            emit_instr(js, rv64_add(RV64_T3, RV64_S, RV64_POS));
            emit_instr(js, rv64_lbu(RV64_T3, RV64_T3, 0));

            emit_mov_imm12(js, RV64_T6, 127);
            emit_instr(js, rv64_sltu(RV64_T6, RV64_T6, RV64_T3));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, rv64_bne(RV64_T6, RV64_ZERO, 0));

            emit_instr(js, rv64_andi(RV64_T6, RV64_T3, 0x40));
            uint32_t *use_map1 = js->p; emit_instr(js, rv64_bne(RV64_T6, RV64_ZERO, 0));
            emit_instr(js, rv64_andi(RV64_T6, RV64_T3, 63));
            emit_instr(js, rv64_srl(RV64_T6, RV64_T4, RV64_T6));
            emit_instr(js, rv64_andi(RV64_T6, RV64_T6, 1));
            uint32_t *map_done = js->p; emit_instr(js, rv64_jal(RV64_ZERO, 0));
            *use_map1 = rv64_bne(RV64_T6, RV64_ZERO, (uint32_t)((intptr_t)js->p - (intptr_t)use_map1));
            emit_instr(js, rv64_andi(RV64_T6, RV64_T3, 63));
            emit_instr(js, rv64_srl(RV64_T6, RV64_T5, RV64_T6));
            emit_instr(js, rv64_andi(RV64_T6, RV64_T6, 1));
            *map_done = rv64_jal(RV64_ZERO, (uint32_t)((intptr_t)js->p - (intptr_t)map_done));

            if (ins->opcode == JIT_IR_NOTANY) {
                fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
                emit_instr(js, rv64_bne(RV64_T6, RV64_ZERO, 0));
            } else {
                fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
                emit_instr(js, rv64_beq(RV64_T6, RV64_ZERO, 0));
            }

            if (ins->opcode != JIT_IR_BREAKX) {
                emit_instr(js, rv64_addi(RV64_POS, RV64_POS, 1));
            }
            break;
        }

        case JIT_IR_SPAN: {
            uint16_t set_id = ins->u.set.set_id;
            uint16_t count16, ci;
            const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count16, &ci);
            uint64_t ascii_map[2];
            if (!ranges || !ranges_to_ascii_bitmap(ranges, count16, ascii_map)) return false;

            emit_mov_imm64(js, RV64_T4, ascii_map[0]);
            emit_mov_imm64(js, RV64_T5, ascii_map[1]);

            emit_instr(js, rv64_sltu(RV64_T6, RV64_POS, RV64_LEN));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, rv64_beq(RV64_T6, RV64_ZERO, 0));

            emit_instr(js, rv64_add(RV64_T3, RV64_S, RV64_POS));
            emit_instr(js, rv64_lbu(RV64_T3, RV64_T3, 0));

            emit_mov_imm12(js, RV64_T6, 127);
            emit_instr(js, rv64_sltu(RV64_T6, RV64_T6, RV64_T3));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, rv64_bne(RV64_T6, RV64_ZERO, 0));

            emit_instr(js, rv64_andi(RV64_T6, RV64_T3, 0x40));
            uint32_t *sp_use_map1 = js->p; emit_instr(js, rv64_bne(RV64_T6, RV64_ZERO, 0));
            emit_instr(js, rv64_andi(RV64_T6, RV64_T3, 63));
            emit_instr(js, rv64_srl(RV64_T6, RV64_T4, RV64_T6));
            emit_instr(js, rv64_andi(RV64_T6, RV64_T6, 1));
            uint32_t *sp_map_done = js->p; emit_instr(js, rv64_jal(RV64_ZERO, 0));
            *sp_use_map1 = rv64_bne(RV64_T6, RV64_ZERO, (uint32_t)((intptr_t)js->p - (intptr_t)sp_use_map1));
            emit_instr(js, rv64_andi(RV64_T6, RV64_T3, 63));
            emit_instr(js, rv64_srl(RV64_T6, RV64_T5, RV64_T6));
            emit_instr(js, rv64_andi(RV64_T6, RV64_T6, 1));
            *sp_map_done = rv64_jal(RV64_ZERO, (uint32_t)((intptr_t)js->p - (intptr_t)sp_map_done));

            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, rv64_beq(RV64_T6, RV64_ZERO, 0));

            emit_instr(js, rv64_addi(RV64_POS, RV64_POS, 1));

            uint32_t *sp_loop_start = js->p;
            uint32_t *sp_lpatches[4];
            int sp_np = 0;

            emit_instr(js, rv64_sltu(RV64_T6, RV64_POS, RV64_LEN));
            sp_lpatches[sp_np++] = js->p; emit_instr(js, rv64_beq(RV64_T6, RV64_ZERO, 0));
            emit_instr(js, rv64_add(RV64_T3, RV64_S, RV64_POS));
            emit_instr(js, rv64_lbu(RV64_T3, RV64_T3, 0));
            emit_mov_imm12(js, RV64_T6, 127);
            emit_instr(js, rv64_sltu(RV64_T6, RV64_T6, RV64_T3));
            sp_lpatches[sp_np++] = js->p; emit_instr(js, rv64_bne(RV64_T6, RV64_ZERO, 0));
            emit_instr(js, rv64_andi(RV64_T6, RV64_T3, 0x40));
            uint32_t *sp_use1 = js->p; emit_instr(js, rv64_bne(RV64_T6, RV64_ZERO, 0));
            emit_instr(js, rv64_andi(RV64_T6, RV64_T3, 63));
            emit_instr(js, rv64_srl(RV64_T6, RV64_T4, RV64_T6));
            emit_instr(js, rv64_andi(RV64_T6, RV64_T6, 1));
            uint32_t *sp_done2 = js->p; emit_instr(js, rv64_jal(RV64_ZERO, 0));
            *sp_use1 = rv64_bne(RV64_T6, RV64_ZERO, (uint32_t)((intptr_t)js->p - (intptr_t)sp_use1));
            emit_instr(js, rv64_andi(RV64_T6, RV64_T3, 63));
            emit_instr(js, rv64_srl(RV64_T6, RV64_T5, RV64_T6));
            emit_instr(js, rv64_andi(RV64_T6, RV64_T6, 1));
            *sp_done2 = rv64_jal(RV64_ZERO, (uint32_t)((intptr_t)js->p - (intptr_t)sp_done2));
            sp_lpatches[sp_np++] = js->p; emit_instr(js, rv64_beq(RV64_T6, RV64_ZERO, 0));
            emit_instr(js, rv64_addi(RV64_POS, RV64_POS, 1));
            intptr_t sp_loop_off = (intptr_t)sp_loop_start - (intptr_t)js->p;
            emit_instr(js, rv64_jal(RV64_ZERO, (uint32_t)(sp_loop_off & 0x1FFFFF)));
            /* Patch forward branches to this point */
            for (int pi = 0; pi < sp_np; pi++) {
                uint32_t orig = *sp_lpatches[pi];
                intptr_t foff = (intptr_t)js->p - (intptr_t)sp_lpatches[pi];
                uint32_t opb = orig & 0x7F;
                uint32_t f3 = (orig >> 12) & 7;
                uint32_t rs1b = (orig >> 15) & 0x1F;
                uint32_t rs2b = (orig >> 20) & 0x1F;
                *sp_lpatches[pi] = rv64_b(opb, f3, rs1b, rs2b, (uint32_t)(foff & 0x1FFF));
            }
            break;
        }

        case JIT_IR_CAP_START:
        case JIT_IR_CAP_END: {
            uint8_t r = ins->u.cap.reg;
            size_t off_field = (ins->opcode == JIT_IR_CAP_START)
                               ? offsetof(VM, cap_start) : offsetof(VM, cap_end);
            emit_instr(js, rv64_sd(RV64_VM, RV64_POS, off_field + r * 8));
            emit_ld_from_vm(js, RV64_T4, offsetof(VM, max_cap_used));
            emit_instr(js, rv64_addi(RV64_T5, RV64_ZERO, r + 1));
            emit_instr(js, rv64_sltu(RV64_T6, RV64_T4, RV64_T5));
            uint32_t *lt_skip = js->p; emit_instr(js, rv64_beq(RV64_T6, RV64_ZERO, 0));
            emit_instr(js, rv64_sd(RV64_VM, RV64_T5, offsetof(VM, max_cap_used)));
            emit_instr(js, rv64_addi(RV64_ZERO, RV64_ZERO, 0));
            break;
        }

        case JIT_IR_LEN: {
            uint32_t n = ins->u.len.n;
            emit_mov_imm64(js, RV64_T4, n);
            emit_instr(js, rv64_add(RV64_T5, RV64_POS, RV64_T4));
            emit_instr(js, rv64_sltu(RV64_T6, RV64_LEN, RV64_T5));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, rv64_bne(RV64_T6, RV64_ZERO, 0));
            emit_instr(js, rv64_add(RV64_POS, RV64_POS, RV64_T4));
            break;
        }

        case JIT_IR_ANCHOR: {
            uint8_t type = ins->u.anchor.type;
            if (type == 0) {
                emit_instr(js, rv64_sltu(RV64_T6, RV64_ZERO, RV64_POS));
                fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
                emit_instr(js, rv64_bne(RV64_T6, RV64_ZERO, 0));
            } else {
                emit_instr(js, rv64_sub(RV64_T6, RV64_POS, RV64_LEN));
                fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
                emit_instr(js, rv64_bne(RV64_T6, RV64_ZERO, 0));
            }
            break;
        }

        case JIT_IR_ASSIGN: {
            uint16_t var = ins->u.assign.var;
            uint8_t  reg = ins->u.assign.reg;
            emit_ld_from_vm(js, RV64_T4, offsetof(VM, cap_start) + reg * 8);
            emit_instr(js, rv64_sd(RV64_VM, RV64_T4, offsetof(VM, var_start) + var * 8));
            emit_ld_from_vm(js, RV64_T4, offsetof(VM, cap_end) + reg * 8);
            emit_instr(js, rv64_sd(RV64_VM, RV64_T4, offsetof(VM, var_end) + var * 8));
            emit_ld_from_vm(js, RV64_T4, offsetof(VM, var_count));
            emit_instr(js, rv64_addi(RV64_T5, RV64_ZERO, var + 1));
            emit_instr(js, rv64_sltu(RV64_T6, RV64_T4, RV64_T5));
            uint32_t *lt_skip = js->p; emit_instr(js, rv64_beq(RV64_T6, RV64_ZERO, 0));
            emit_instr(js, rv64_sd(RV64_VM, RV64_T5, offsetof(VM, var_count)));
            emit_instr(js, rv64_addi(RV64_ZERO, RV64_ZERO, 0));
            break;
        }

        case JIT_IR_REM:
            emit_instr(js, rv64_add(RV64_POS, RV64_LEN, RV64_ZERO));
            break;

        case JIT_IR_RPOS: {
            uint32_t n = ins->u.rpos_rtab.n;
            emit_mov_imm64(js, RV64_T4, n);
            emit_instr(js, rv64_sub(RV64_T5, RV64_LEN, RV64_T4));
            emit_instr(js, rv64_sub(RV64_T6, RV64_POS, RV64_T5));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, rv64_bne(RV64_T6, RV64_ZERO, 0));
            break;
        }

        case JIT_IR_RTAB: {
            uint32_t n = ins->u.rpos_rtab.n;
            emit_mov_imm64(js, RV64_T4, n);
            emit_instr(js, rv64_sltu(RV64_T6, RV64_LEN, RV64_T4));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, rv64_bne(RV64_T6, RV64_ZERO, 0));
            emit_instr(js, rv64_sub(RV64_T5, RV64_LEN, RV64_T4));
            emit_instr(js, rv64_sltu(RV64_T6, RV64_POS, RV64_T5));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, rv64_bne(RV64_T6, RV64_ZERO, 0));
            emit_instr(js, rv64_add(RV64_POS, RV64_T5, RV64_ZERO));
            break;
        }

        case JIT_IR_FENCE:
            emit_instr(js, rv64_addi(RV64_T4, RV64_ZERO, 0));
            emit_instr(js, rv64_sd(RV64_VM, RV64_T4, offsetof(VM, choices_top)));
            break;

        case JIT_IR_EMIT_LITERAL: {
            uint32_t elt_off = ins->u.emit_lit.offset;
            uint32_t elt_len = ins->u.emit_lit.len;
            emit_instr(js, rv64_sd(RV64_VM, RV64_POS, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_imm64(js, RV64_FNP, (uint64_t)(uintptr_t)snobol_jit_helper_emit_literal);
            emit_mov_imm64(js, RV64_T4, elt_off);
            emit_instr(js, rv64_add(RV64_A1, RV64_T4, RV64_ZERO));
            emit_mov_imm64(js, RV64_T4, elt_len);
            emit_instr(js, rv64_add(RV64_A2, RV64_T4, RV64_ZERO));
            emit_instr(js, rv64_jalr(RV64_RA, RV64_FNP, 0));
            EMIT_CALLOUT_RESTORE(js);
            break;
        }

        case JIT_IR_EMIT_CAPTURE: {
            uint8_t ecap_reg = ins->u.emit_cap.reg;
            emit_instr(js, rv64_sd(RV64_VM, RV64_POS, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_imm64(js, RV64_FNP, (uint64_t)(uintptr_t)snobol_jit_helper_emit_capture);
            emit_instr(js, rv64_addi(RV64_A1, RV64_ZERO, ecap_reg));
            emit_instr(js, rv64_jalr(RV64_RA, RV64_FNP, 0));
            EMIT_CALLOUT_RESTORE(js);
            break;
        }

        case JIT_IR_EMIT_EXPR: {
            uint8_t eex_r = ins->u.emit_expr.reg;
            uint8_t eex_t = ins->u.emit_expr.expr_type;
            emit_instr(js, rv64_sd(RV64_VM, RV64_POS, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_imm64(js, RV64_FNP, (uint64_t)(uintptr_t)snobol_jit_helper_emit_expr);
            emit_instr(js, rv64_addi(RV64_A1, RV64_ZERO, eex_r));
            emit_instr(js, rv64_addi(RV64_A2, RV64_ZERO, eex_t));
            emit_instr(js, rv64_jalr(RV64_RA, RV64_FNP, 0));
            EMIT_CALLOUT_RESTORE(js);
            break;
        }

        case JIT_IR_EMIT_FORMAT: {
            uint8_t  efmt_r = ins->u.emit_fmt.reg;
            uint8_t  efmt_t = ins->u.emit_fmt.fmt_type;
            uint16_t efmt_w = ins->u.emit_fmt.width;
            uint8_t  efmt_f = ins->u.emit_fmt.fill_char;
            emit_instr(js, rv64_sd(RV64_VM, RV64_POS, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_imm64(js, RV64_FNP, (uint64_t)(uintptr_t)snobol_jit_helper_emit_format);
            emit_instr(js, rv64_addi(RV64_A1, RV64_ZERO, efmt_r));
            emit_instr(js, rv64_addi(RV64_A2, RV64_ZERO, efmt_t));
            emit_mov_imm64(js, RV64_T4, efmt_w);
            emit_instr(js, rv64_add(RV64_A3, RV64_T4, RV64_ZERO));
            emit_instr(js, rv64_addi(RV64_A4, RV64_ZERO, efmt_f));
            emit_instr(js, rv64_jalr(RV64_RA, RV64_FNP, 0));
            EMIT_CALLOUT_RESTORE(js);
            break;
        }

        case JIT_IR_EMIT_TABLE: {
            emit_instr(js, rv64_sd(RV64_VM, RV64_POS, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_imm64(js, RV64_FNP, (uint64_t)(uintptr_t)snobol_jit_helper_emit_table_ip);
            emit_mov_imm64(js, RV64_T4, (uint64_t)cur);
            emit_instr(js, rv64_add(RV64_A1, RV64_T4, RV64_ZERO));
            emit_instr(js, rv64_jalr(RV64_RA, RV64_FNP, 0));
            EMIT_CALLOUT_RESTORE(js);
            break;
        }

        case JIT_IR_TABLE_GET: {
#ifdef SNOBOL_DYNAMIC_PATTERN
            uint16_t tg_tid  = ins->u.tget.table_id;
            uint8_t  tg_kreg = ins->u.tget.key_reg;
            uint8_t  tg_dreg = ins->u.tget.dest_reg;
            emit_instr(js, rv64_sd(RV64_VM, RV64_POS, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_imm64(js, RV64_FNP, (uint64_t)(uintptr_t)snobol_jit_helper_table_get);
            emit_mov_imm64(js, RV64_T4, tg_tid);
            emit_instr(js, rv64_add(RV64_A1, RV64_T4, RV64_ZERO));
            emit_instr(js, rv64_addi(RV64_A2, RV64_ZERO, tg_kreg));
            emit_instr(js, rv64_addi(RV64_A3, RV64_ZERO, tg_dreg));
            emit_instr(js, rv64_jalr(RV64_RA, RV64_FNP, 0));
            EMIT_CALLOUT_SAVE_RET(js);
            EMIT_CALLOUT_RESTORE(js);
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, rv64_beq(RV64_FNP, RV64_ZERO, 0));
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
            emit_instr(js, rv64_sd(RV64_VM, RV64_POS, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_imm64(js, RV64_FNP, (uint64_t)(uintptr_t)snobol_jit_helper_table_set);
            emit_mov_imm64(js, RV64_T4, ts_tid);
            emit_instr(js, rv64_add(RV64_A1, RV64_T4, RV64_ZERO));
            emit_instr(js, rv64_addi(RV64_A2, RV64_ZERO, ts_kreg));
            emit_instr(js, rv64_addi(RV64_A3, RV64_ZERO, ts_vreg));
            emit_instr(js, rv64_jalr(RV64_RA, RV64_FNP, 0));
            EMIT_CALLOUT_RESTORE(js);
            emit_ld_from_vm(js, RV64_POS, offsetof(VM, pos));
#else
            (void)ins;
#endif
            break;
        }

        case JIT_IR_BAL: {
            uint32_t bal_open  = ins->u.bal.open_cp;
            uint32_t bal_close = ins->u.bal.close_cp;
            emit_instr(js, rv64_sd(RV64_VM, RV64_POS, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_imm64(js, RV64_FNP, (uint64_t)(uintptr_t)snobol_jit_helper_bal);
            emit_mov_imm64(js, RV64_T4, bal_open);
            emit_instr(js, rv64_add(RV64_A1, RV64_T4, RV64_ZERO));
            emit_mov_imm64(js, RV64_T4, bal_close);
            emit_instr(js, rv64_add(RV64_A2, RV64_T4, RV64_ZERO));
            emit_instr(js, rv64_jalr(RV64_RA, RV64_FNP, 0));
            EMIT_CALLOUT_SAVE_RET(js);
            EMIT_CALLOUT_RESTORE(js);
            emit_ld_from_vm(js, RV64_POS, offsetof(VM, pos));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, rv64_beq(RV64_FNP, RV64_ZERO, 0));
            break;
        }

        case JIT_IR_EVAL: {
            uint16_t ev_fn  = ins->u.eval.fn_id;
            uint8_t  ev_reg = ins->u.eval.reg;
            emit_instr(js, rv64_sd(RV64_VM, RV64_POS, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_imm64(js, RV64_FNP, (uint64_t)(uintptr_t)snobol_jit_helper_eval);
            emit_mov_imm64(js, RV64_T4, ev_fn);
            emit_instr(js, rv64_add(RV64_A1, RV64_T4, RV64_ZERO));
            emit_instr(js, rv64_addi(RV64_A2, RV64_ZERO, ev_reg));
            emit_instr(js, rv64_jalr(RV64_RA, RV64_FNP, 0));
            EMIT_CALLOUT_SAVE_RET(js);
            EMIT_CALLOUT_RESTORE(js);
            emit_ld_from_vm(js, RV64_POS, offsetof(VM, pos));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, rv64_beq(RV64_FNP, RV64_ZERO, 0));
            break;
        }

        case JIT_IR_DYNAMIC: {
#ifdef SNOBOL_DYNAMIC_PATTERN
            emit_instr(js, rv64_sd(RV64_VM, RV64_POS, offsetof(VM, pos)));
            EMIT_CALLOUT_SAVE(js);
            emit_mov_imm64(js, RV64_FNP, (uint64_t)(uintptr_t)snobol_jit_helper_dynamic);
            emit_instr(js, rv64_jalr(RV64_RA, RV64_FNP, 0));
            EMIT_CALLOUT_SAVE_RET(js);
            EMIT_CALLOUT_RESTORE(js);
            emit_ld_from_vm(js, RV64_POS, offsetof(VM, pos));
            fail_patches[(*fail_patch_count)++] = (FailPatch){ js->p, cur };
            emit_instr(js, rv64_beq(RV64_FNP, RV64_ZERO, 0));
#endif
            break;
        }

        case JIT_IR_SPLIT:
            break;

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
    (void)bc;
    return true;
}

#undef EMIT_VOID_CALLOUT
#undef EMIT_BOOL_CALLOUT

/* =========================================================================
 * CFG epilogue helper
 * ========================================================================= */
static void emit_cfg_bailout_riscv64(jit_region_t *js, size_t bail_ip) {
    emit_instr(js, rv64_sd(RV64_VM, RV64_POS, offsetof(VM, pos)));
    emit_mov_imm64(js, RV64_T4, (uint64_t)bail_ip);
    emit_instr(js, rv64_sd(RV64_VM, RV64_T4, offsetof(VM, ip)));
    /* Restore stack and return */
    emit_instr(js, rv64_ld(RV64_RA, RV64_SP, 0));
    emit_instr(js, rv64_addi(RV64_SP, RV64_SP, 16));
    emit_instr(js, rv64_jalr(RV64_ZERO, RV64_RA, 0));
}

/* =========================================================================
 * riscv64_lower()  — main function: IR → RV64 machine code
 * ========================================================================= */
static void *riscv64_lower(const jit_ir_region_t *ir, VM *vm, jit_region_t *out) {
    if (!ir || !vm || !out) return nullptr;
    if (ir->non_compilable || ir->count == 0) return nullptr;


    JitCfg cfg;
    int n_blocks = ir_cfg_build(ir, &cfg);

    if (n_blocks <= 0) return nullptr;

    bool use_cfg = (n_blocks >= 2 || (n_blocks == 1 && cfg.has_backward));

    size_t code_size = use_cfg ? 32768u : 16384u;
    uint32_t *code = (uint32_t *)snobol_jit_alloc_code(code_size);
    if (!code) return nullptr;

    out->p          = code;
    out->code_start = code;
    out->code_size  = code_size;

    FailPatch fail_patches[1024];
    size_t    fail_patch_count = 0;

    if (use_cfg) {
        StubPatch stub_patches[256];
        int       stub_patch_count = 0;

        /* Prologue: save ra on stack */
        emit_instr(out, rv64_addi(RV64_SP, RV64_SP, -16));
        emit_instr(out, rv64_sd(RV64_SP, RV64_RA, 0));

        /* Load VM fields into registers */
        emit_ld_from_vm(out, RV64_S,   offsetof(VM, s));
        emit_ld_from_vm(out, RV64_POS, offsetof(VM, pos));
        emit_ld_from_vm(out, RV64_LEN, offsetof(VM, len));
        if (cfg.has_backward)
            emit_mov_imm32(out, RV64_LOOP, JIT_LOOP_ITER_MAX);

        for (int bi = 0; bi < cfg.count; bi++) {
            JitBlock *blk = &cfg.blocks[bi];
            blk->stub_start = out->p;

            bool ok = emit_block_ops_ir(out, ir, vm,
                                        blk->start_ip, blk->term_ip,
                                        fail_patches, &fail_patch_count);

            if (!ok) {
                emit_cfg_bailout_riscv64(out, blk->term_ip);
                blk->term = BLOCK_TERM_EXIT;
                continue;
            }

            switch (blk->term) {
            case BLOCK_TERM_SPLIT: {
                if (blk->succ_a_blk < 0) {
                    emit_cfg_bailout_riscv64(out, blk->term_ip);
                    break;
                }
                /* Save state and push choice.
                 * vm_push_choice(VM *vm, size_t ip, size_t pos)
                 * Callee may clobber A0 (VM) and caller-saved temporaries,
                 * so we save RA, VM, S, POS and restore them after the call.
                 * A2 (pos) is passed explicitly from our POS register. */
                emit_instr(out, rv64_addi(RV64_SP, RV64_SP, -40));
                emit_instr(out, rv64_sd(RV64_SP, RV64_RA, 0));
                emit_instr(out, rv64_sd(RV64_SP, RV64_VM, 8));
                emit_instr(out, rv64_sd(RV64_SP, RV64_S, 16));
                emit_instr(out, rv64_sd(RV64_SP, RV64_POS, 24));
                emit_mov_imm64(out, RV64_FNP, (uint64_t)(uintptr_t)vm_push_choice);
                emit_mov_imm64(out, RV64_T4, (uint64_t)blk->succ_b);
                emit_instr(out, rv64_add(RV64_A1, RV64_T4, RV64_ZERO));
                emit_instr(out, rv64_add(RV64_A2, RV64_POS, RV64_ZERO));
                emit_instr(out, rv64_jalr(RV64_RA, RV64_FNP, 0));
                emit_instr(out, rv64_ld(RV64_RA, RV64_SP, 0));
                emit_instr(out, rv64_ld(RV64_VM, RV64_SP, 8));
                emit_instr(out, rv64_ld(RV64_S, RV64_SP, 16));
                emit_instr(out, rv64_ld(RV64_POS, RV64_SP, 24));
                emit_instr(out, rv64_addi(RV64_SP, RV64_SP, 40));
                emit_ld_from_vm(out, RV64_LEN, offsetof(VM, len));
                stub_patches[stub_patch_count++] = (StubPatch){ out->p, blk->succ_a };
                emit_instr(out, rv64_jal(RV64_ZERO, 0));
                break;
            }
            case BLOCK_TERM_JMP_FWD: {
                if (blk->succ_a_blk < 0) {
                    emit_cfg_bailout_riscv64(out, blk->term_ip);
                    break;
                }
                stub_patches[stub_patch_count++] = (StubPatch){ out->p, blk->succ_a };
                emit_instr(out, rv64_jal(RV64_ZERO, 0));
                break;
            }
            case BLOCK_TERM_JMP_BWD: {
                if (blk->succ_a_blk < 0) {
                    emit_cfg_bailout_riscv64(out, blk->term_ip);
                    break;
                }
                emit_instr(out, rv64_addi(RV64_LOOP, RV64_LOOP, -1));
                uint32_t *bail_patch = out->p;
                emit_instr(out, rv64_beq(RV64_LOOP, RV64_ZERO, 0));
                uint32_t *loop_head = cfg.blocks[blk->succ_a_blk].stub_start;
                intptr_t  diff = (intptr_t)loop_head - (intptr_t)out->p;
                emit_instr(out, rv64_jal(RV64_ZERO, (uint32_t)(diff & 0x1FFFFF)));
                emit_instr(out, rv64_addi(RV64_ZERO, RV64_ZERO, 0));
                uint32_t *bailout_start = out->p;
                emit_cfg_bailout_riscv64(out, blk->term_ip);
                intptr_t bail_diff = (intptr_t)bailout_start - (intptr_t)bail_patch;
                *bail_patch = rv64_beq(RV64_LOOP, RV64_ZERO, (uint32_t)(bail_diff & 0x1FFF));
                break;
            }
            case BLOCK_TERM_EXIT:
                emit_cfg_bailout_riscv64(out, blk->term_ip);
                break;
            }
        }

        /* Fail stubs */
        for (size_t f = 0; f < fail_patch_count; f++) {
            uint32_t *fail_stub = out->p;
            emit_cfg_bailout_riscv64(out, fail_patches[f].ip);
            /* Patch the branch to this fail stub */
            uint32_t orig = *fail_patches[f].instr_p;
            /* Determine branch type from operand bits */
            intptr_t stub_diff = (intptr_t)fail_stub - (intptr_t)fail_patches[f].instr_p;
            uint32_t opcode = orig & 0x7F;
            if (opcode == RV_OP_BRANCH) {
                *fail_patches[f].instr_p = rv64_b(opcode, (orig >> 12) & 7,
                    (orig >> 15) & 0x1F, (orig >> 20) & 0x1F,
                    (uint32_t)(stub_diff & 0x1FFF));
            }
        }

        /* Forward-branch fixup */
        for (int s = 0; s < stub_patch_count; s++) {
            uint32_t *p         = stub_patches[s].instr_p;
            size_t    target_ip = stub_patches[s].target_ip;
            int       blk_idx   = ir_find_block(&cfg, target_ip);
            if (blk_idx >= 0 && cfg.blocks[blk_idx].stub_start) {
                uint32_t *target = cfg.blocks[blk_idx].stub_start;
                intptr_t  diff2  = (intptr_t)target - (intptr_t)p;
                *p = rv64_jal(RV64_ZERO, (uint32_t)(diff2 & 0x1FFFFF));
            }
        }

    } else {
        /* ---- Linear single-block path ---- */
        JitBlock *blk = &cfg.blocks[0];

        /* Prologue: save ra on stack */
        emit_instr(out, rv64_addi(RV64_SP, RV64_SP, -16));
        emit_instr(out, rv64_sd(RV64_SP, RV64_RA, 0));

        emit_ld_from_vm(out, RV64_S,   offsetof(VM, s));
        emit_ld_from_vm(out, RV64_POS, offsetof(VM, pos));
        emit_ld_from_vm(out, RV64_LEN, offsetof(VM, len));

        bool ok = emit_block_ops_ir(out, ir, vm,
                                    blk->start_ip, blk->term_ip,
                                    fail_patches, &fail_patch_count);

        if (!ok) {
            snobol_jit_free_code(code, code_size);
            return nullptr;
        }

        /* Success epilogue — use term_ip so the interpreter re-dispatches
         * ACCEPT (or FAIL) and returns the correct result to the caller. */
        emit_instr(out, rv64_sd(RV64_VM, RV64_POS, offsetof(VM, pos)));
        emit_mov_imm64(out, RV64_T4, (uint64_t)blk->term_ip);
        emit_instr(out, rv64_sd(RV64_VM, RV64_T4, offsetof(VM, ip)));
        emit_instr(out, rv64_ld(RV64_RA, RV64_SP, 0));
        emit_instr(out, rv64_addi(RV64_SP, RV64_SP, 16));
        emit_instr(out, rv64_jalr(RV64_ZERO, RV64_RA, 0));

        /* Fail stubs */
        for (size_t f = 0; f < fail_patch_count; f++) {
            uint32_t *fail_stub = out->p;
            emit_instr(out, rv64_sd(RV64_VM, RV64_POS, offsetof(VM, pos)));
            emit_mov_imm64(out, RV64_T4, fail_patches[f].ip);
            emit_instr(out, rv64_sd(RV64_VM, RV64_T4, offsetof(VM, ip)));
            emit_instr(out, rv64_ld(RV64_RA, RV64_SP, 0));
            emit_instr(out, rv64_addi(RV64_SP, RV64_SP, 16));
            emit_instr(out, rv64_jalr(RV64_ZERO, RV64_RA, 0));
            /* Patch the branch to fail stub */
            uint32_t orig = *fail_patches[f].instr_p;
            intptr_t stub_diff = (intptr_t)fail_stub - (intptr_t)fail_patches[f].instr_p;
            uint32_t opcode = orig & 0x7F;
            if (opcode == RV_OP_BRANCH) {
                *fail_patches[f].instr_p = rv64_b(opcode, (orig >> 12) & 7,
                    (orig >> 15) & 0x1F, (orig >> 20) & 0x1F,
                    (uint32_t)(stub_diff & 0x1FFF));
            }
        }
    }

    out->n_blocks = (size_t)n_blocks;
    snobol_jit_seal_code(code, code_size);
    return (void *)code;
}

/* =========================================================================
 * riscv64_flush_icache()
 * ========================================================================= */
static void riscv64_flush_icache(void *code, size_t size) {
    __builtin___clear_cache((char *)code, (char *)code + size);
}

/* =========================================================================
 * Backend vtable and registration
 * ========================================================================= */

static const jit_backend_t riscv64_backend_vtable = {
    .name        = "riscv64",
    .lower       = riscv64_lower,
    .flush_icache = riscv64_flush_icache,
};

void snobol_jit_riscv64_register(void) {
    jit_backend_register(&riscv64_backend_vtable);
}

#endif /* __riscv && __riscv_xlen == 64 */
#endif /* SNOBOL_JIT */
