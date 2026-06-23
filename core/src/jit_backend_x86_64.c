#include "snobol/jit.h"
#include "snobol/jit_backend.h"
#include "snobol/jit_ir.h"

#ifdef SNOBOL_JIT
#if defined(__x86_64__) || defined(_M_X64)

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "snobol/dynamic_pattern.h"
#include "snobol/snobol_internal.h"
#include "snobol/string_fn.h"
#include "snobol/table.h"
#include "snobol/type_fn.h"
#include "snobol/vm.h"

/* =========================================================================
 * Register convention
 *
 * Callee-saved (survive C call-outs):
 *   rbx  = VM pointer
 *   r12  = len (subject length)
 *   rbp  = loop counter / temp (callee-saved)
 *
 * Volatile (caller-saved, scratch):
 *   rdi  = pos (current position in subject)
 *   rsi  = s  (subject pointer)
 *   rax  = scratch / return value
 *   rcx  = scratch
 *   rdx  = scratch
 *   r8   = scratch
 *   r9   = scratch
 *   r10  = scratch
 *   r11  = scratch
 *   r13  = scratch
 *   r14  = scratch
 *   r15  = scratch
 *
 * On SysV AMD64:  rdi, rsi, rdx, rcx, r8, r9 = arg regs for call-outs
 * On MS x64:      rcx, rdx, r8, r9 = arg regs + 32-byte shadow space
 * ========================================================================= */
#define X64_VM 0  /* rbx — maps to ModRM reg field encoding */
#define X64_S 1   /* rsi */
#define X64_POS 2 /* rdi */
#define X64_LEN 3 /* r12 */

#define X64_RAX 0
#define X64_RCX 1
#define X64_RDX 2
#define X64_RBX 3
#define X64_RSP 4
#define X64_RBP 5
#define X64_RSI 6
#define X64_RDI 7
#define X64_R8 8
#define X64_R9 9
#define X64_R10 10
#define X64_R11 11
#define X64_R12 12
#define X64_R13 13
#define X64_R14 14
#define X64_R15 15

/* =========================================================================
 * x86-64 instruction encoding helpers
 * ========================================================================= */

/* Emit 1 byte */
static void emit_byte(jit_region_t *out, uint8_t b) {
  ((uint8_t *)out->p)[0] = b;
  out->p = (uint32_t *)((uint8_t *)out->p + 1);
}

/* Emit 4 bytes (little-endian) */
static void emit_u32(jit_region_t *out, uint32_t v) {
  uint8_t *p = (uint8_t *)out->p;
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
  out->p = (uint32_t *)(p + 4);
}

/* Emit 1 byte as a signed 8-bit displacement */
static void emit_disp8(jit_region_t *out, int8_t d) {
  emit_byte(out, (uint8_t)d);
}

/* Emit 4 bytes as a signed 32-bit displacement */
static void emit_disp32(jit_region_t *out, int32_t d) {
  emit_u32(out, (uint32_t)d);
}

/* Encode REX prefix byte: W=64-bit operand, R=extended reg.modrm, X=extended
 * SIB index, B=extended rm/base */
static uint8_t rex(int w, int r, int x, int b) {
  return (uint8_t)(0x40u | (w ? 8u : 0u) | (r ? 4u : 0u) | (x ? 2u : 0u) |
                   (b ? 1u : 0u));
}

/* Build ModRM byte: mod=addressing mode, reg=opcode/register,
 * rm=register/memory */
static uint8_t modrm(int mod, int reg, int rm) {
  return (uint8_t)(((mod & 3) << 6) | ((reg & 7) << 3) | (rm & 7));
}

/* Build SIB byte: scale, index, base */
static uint8_t sib(int scale, int index, int base) {
  return (uint8_t)(((scale & 3) << 6) | ((index & 7) << 3) | (base & 7));
}

/* Emit REX + ModRM for a 64-bit register-to-register operation */
static void emit_rex_w(jit_region_t *out, int reg, int rm) {
  emit_byte(out, rex(1, reg > 7, 0, rm > 7));
}

/* Emit REX.W + ModRM where fields reg and rm are register indices (0-15) */
static void emit_rm_64(jit_region_t *out, int reg, int rm) {
  emit_rex_w(out, reg, rm);
  emit_byte(out, modrm(3, reg & 7, rm & 7));
}

/* Emit REX.W + ModRM.rm with no REX.R (for instructions using the reg field
 * differently) */
static void emit_rm_64_nr(jit_region_t *out, int reg, int rm) {
  emit_byte(out, rex(1, 0, 0, rm > 7));
  emit_byte(out, modrm(3, reg & 7, rm & 7));
}

/* Emit REX (no W) + ModRM for a 32-bit register-to-register operation */
static void emit_rm_32(jit_region_t *out, int reg, int rm) {
  int need_rex = (reg > 7) || (rm > 7);
  if (need_rex) {
    emit_byte(out, rex(0, reg > 7, 0, rm > 7));
  }
  emit_byte(out, modrm(3, reg & 7, rm & 7));
}

/* =========================================================================
 * MOV rr (64-bit): 0x89 /r  (REX.W + 89 + modrm(3, src, dst))
 * MOV ri (64-bit): 0xB8+reg (REX.W + B8+reg + imm64) - but we only use movabs
 * ========================================================================= */

/* MOV r64, r64 — REX.W 0x89 /r */
static void x64_mov_rr(jit_region_t *out, int dst, int src) {
  emit_rex_w(out, src, dst);
  emit_byte(out, 0x89);
  emit_byte(out, modrm(3, src & 7, dst & 7));
}

/* MOV r64, imm64 (movabs: REX.W B8+reg + 8 bytes) */
static void x64_mov_ri64(jit_region_t *out, int dst, uint64_t imm) {
  emit_byte(out, rex(1, 0, 0, dst > 7));
  emit_byte(out, (uint8_t)(0xB8u | (dst & 7)));
  emit_u32(out, (uint32_t)(imm & 0xFFFFFFFFu));
  emit_u32(out, (uint32_t)((imm >> 32) & 0xFFFFFFFFu));
}

/* MOV r64, imm32 (sign-extended: REX.W C7 /0 + 4 bytes) */
static void x64_mov_ri32(jit_region_t *out, int dst, int32_t imm) {
  emit_rex_w(out, 0, dst);
  emit_byte(out, 0xC7);
  emit_byte(out, modrm(3, 0, dst & 7));
  emit_disp32(out, imm);
}

/* MOVZX r32, r/m8 (0xB6 /r) */
static void x64_movzx_r8(jit_region_t *out, int dst, int src) {
  emit_byte(out, rex(0, dst > 7, 0, src > 7));
  emit_byte(out, 0x0F);
  emit_byte(out, 0xB6);
  emit_byte(out, modrm(3, dst & 7, src & 7));
}

/* =========================================================================
 * Integer ALU: ADD, SUB, XOR, CMP, TEST
 * All use REX.W + opcode + ModRM
 * ========================================================================= */

/* ADD r64, r64: REX.W 0x01 /r */
static void x64_add_rr(jit_region_t *out, int dst, int src) {
  emit_rex_w(out, src, dst);
  emit_byte(out, 0x01);
  emit_byte(out, modrm(3, src & 7, dst & 7));
}

/* ADD r64, imm32 (sign-extended): REX.W 0x81 /0 + 4 bytes */
static void x64_add_ri32(jit_region_t *out, int dst, int32_t imm) {
  emit_rex_w(out, 0, dst);
  emit_byte(out, 0x81);
  emit_byte(out, modrm(3, 0, dst & 7));
  emit_disp32(out, imm);
}

/* SUB r64, r64: REX.W 0x29 /r */
static void x64_sub_rr(jit_region_t *out, int dst, int src) {
  emit_rex_w(out, src, dst);
  emit_byte(out, 0x29);
  emit_byte(out, modrm(3, src & 7, dst & 7));
}

/* SUB r64, imm32: REX.W 0x81 /5 + 4 bytes */
static void x64_sub_ri32(jit_region_t *out, int dst, int32_t imm) {
  emit_rex_w(out, 0, dst);
  emit_byte(out, 0x81);
  emit_byte(out, modrm(3, 5, dst & 7));
  emit_disp32(out, imm);
}

/* AND r64, imm32: REX.W 0x81 /4 + 4 bytes */
static void x64_and_ri32(jit_region_t *out, int dst, int32_t imm) {
  emit_rex_w(out, 0, dst);
  emit_byte(out, 0x81);
  emit_byte(out, modrm(3, 4, dst & 7));
  emit_disp32(out, imm);
}

/* XOR r64, r64: REX.W 0x31 /r */
static void x64_xor_rr(jit_region_t *out, int dst, int src) {
  emit_rex_w(out, src, dst);
  emit_byte(out, 0x31);
  emit_byte(out, modrm(3, src & 7, dst & 7));
}

/* XOR r64, imm32: REX.W 0x81 /6 + 4 bytes */
static void x64_xor_ri32(jit_region_t *out, int dst, int32_t imm) {
  emit_rex_w(out, 0, dst);
  emit_byte(out, 0x81);
  emit_byte(out, modrm(3, 6, dst & 7));
  emit_disp32(out, imm);
}

/* BT r64, r64: REX.W 0x0F 0xA3 /r (ModRM reg=r, rm=base) */
static void x64_bt_rr(jit_region_t *out, int base, int r) {
  emit_rex_w(out, r, base);
  emit_byte(out, 0x0F);
  emit_byte(out, 0xA3);
  emit_byte(out, modrm(3, r & 7, base & 7));
}

/* CMP r64, r64: REX.W 0x39 /r */
static void x64_cmp_rr(jit_region_t *out, int a, int b) {
  emit_rex_w(out, b, a);
  emit_byte(out, 0x39);
  emit_byte(out, modrm(3, b & 7, a & 7));
}

/* CMP r64, imm32: REX.W 0x81 /7 + 4 bytes */
static void x64_cmp_ri32(jit_region_t *out, int a, int32_t imm) {
  emit_rex_w(out, 0, a);
  emit_byte(out, 0x81);
  emit_byte(out, modrm(3, 7, a & 7));
  emit_disp32(out, imm);
}

/* CMP r/m32, imm32: no REX.W, 0x81 /7 + 4 bytes */
static void x64_cmp_rm32_imm32(jit_region_t *out, int rm, int32_t imm) {
  int need_rex = rm > 7;
  if (need_rex)
    emit_byte(out, rex(0, 0, 0, 1));
  emit_byte(out, 0x81);
  emit_byte(out, modrm(3, 7, rm & 7));
  emit_disp32(out, imm);
}

/* TEST r64, r64: REX.W 0x85 /r */
static void x64_test_rr(jit_region_t *out, int a, int b) {
  emit_rex_w(out, b, a);
  emit_byte(out, 0x85);
  emit_byte(out, modrm(3, b & 7, a & 7));
}

/* =========================================================================
 * Load/store with [base + offset] addressing
 * MOV r64, [base+off]: REX.W 0x8B /r (8B + ModRM)
 * MOV [base+off], r64: REX.W 0x89 /r (89 + ModRM)
 * For offset beyond signed 8-bit, use disp32 encoding.
 * ========================================================================= */
enum { MOD_DISP0 = 0, MOD_DISP8 = 1, MOD_DISP32 = 2, MOD_REG = 3 };

/* Load r64 from [base+disp]: MOV r64, r/m64  (REX.W 8B /r) */
static void x64_load_mr(jit_region_t *out, int reg, int base, int32_t disp) {
  if (disp == 0 && base != X64_RBP && base != X64_RSP) {
    /* [base] — no displacement */
    emit_rex_w(out, reg, base);
    emit_byte(out, 0x8B);
    emit_byte(out, modrm(MOD_DISP0, reg & 7, base & 7));
  } else if (disp >= -128 && disp <= 127) {
    /* [base+disp8] */
    emit_rex_w(out, reg, base);
    emit_byte(out, 0x8B);
    emit_byte(out, modrm(MOD_DISP8, reg & 7, base & 7));
    emit_disp8(out, (int8_t)disp);
  } else {
    /* [base+disp32] */
    emit_rex_w(out, reg, base);
    emit_byte(out, 0x8B);
    emit_byte(out, modrm(MOD_DISP32, reg & 7, base & 7));
    emit_disp32(out, disp);
  }
}

/* Load r64 from [rsp]: special encoding for base=rsp (no disp) — always use SIB
 */
static void x64_load_mr_rsp(jit_region_t *out, int reg, int base,
                            int32_t disp) {
  if (disp == 0 && base != X64_RBP) {
    emit_rex_w(out, reg, base);
    emit_byte(out, 0x8B);
    emit_byte(out, modrm(MOD_DISP0, reg & 7, base & 7));
    if (base == X64_RSP)
      emit_byte(out, sib(0, 4, base & 7));
  } else if (disp >= -128 && disp <= 127) {
    emit_rex_w(out, reg, base);
    emit_byte(out, 0x8B);
    emit_byte(out, modrm(MOD_DISP8, reg & 7, base & 7));
    if (base == X64_RSP)
      emit_byte(out, sib(0, 4, base & 7));
    emit_disp8(out, (int8_t)disp);
  } else {
    emit_rex_w(out, reg, base);
    emit_byte(out, 0x8B);
    emit_byte(out, modrm(MOD_DISP32, reg & 7, base & 7));
    if (base == X64_RSP)
      emit_byte(out, sib(0, 4, base & 7));
    emit_disp32(out, disp);
  }
}

/* Store r64 to [base+off]: MOV [base+off], r64  (REX.W 89 /r) */
static void x64_store_mr(jit_region_t *out, int base, int reg, int32_t disp) {
  if (disp == 0 && base != X64_RBP && base != X64_RSP) {
    emit_rex_w(out, reg, base);
    emit_byte(out, 0x89);
    emit_byte(out, modrm(MOD_DISP0, reg & 7, base & 7));
  } else if (disp >= -128 && disp <= 127) {
    emit_rex_w(out, reg, base);
    emit_byte(out, 0x89);
    emit_byte(out, modrm(MOD_DISP8, reg & 7, base & 7));
    if (base == X64_RSP)
      emit_byte(out, sib(0, 4, base & 7));
    emit_disp8(out, (int8_t)disp);
  } else {
    emit_rex_w(out, reg, base);
    emit_byte(out, 0x89);
    emit_byte(out, modrm(MOD_DISP32, reg & 7, base & 7));
    if (base == X64_RSP)
      emit_byte(out, sib(0, 4, base & 7));
    emit_disp32(out, disp);
  }
}

/* Load byte from [base+off], zero-extend to r64: MOVZX r64, byte ptr [base+off]
 * Uses 0F B6 /r (MOVZX) with memory operand encoding */
static void x64_loadb_mr(jit_region_t *out, int reg, int base, int32_t disp) {
  int need_rex_r = reg > 7;
  int need_rex_b = base > 7;
  uint8_t rex_byte = rex(0, need_rex_r, 0, need_rex_b);
  if (need_rex_r || need_rex_b)
    emit_byte(out, rex_byte);
  emit_byte(out, 0x0F);
  emit_byte(out, 0xB6);
  if (disp == 0 && base != X64_RBP && base != X64_RSP) {
    emit_byte(out, modrm(MOD_DISP0, reg & 7, base & 7));
  } else if (disp >= -128 && disp <= 127) {
    emit_byte(out, modrm(MOD_DISP8, reg & 7, base & 7));
    emit_disp8(out, (int8_t)disp);
  } else {
    emit_byte(out, modrm(MOD_DISP32, reg & 7, base & 7));
    if (base == X64_RSP)
      emit_byte(out, sib(0, 4, base & 7));
    emit_disp32(out, disp);
  }
}

/* Load byte from [base + index] using SIB addressing (no displacement):
 *   MOVZX reg64, byte ptr [base + index]
 * The index register is used with scale=1 (no SIB scale field = 0). */
static void x64_loadb_mr_idx(jit_region_t *out, int reg, int base, int index) {
  int need_rex_r = reg > 7;
  int need_rex_x = index > 7;
  int need_rex_b = base > 7;
  uint8_t rex_byte = rex(0, need_rex_r, need_rex_x, need_rex_b);
  if (need_rex_r || need_rex_x || need_rex_b)
    emit_byte(out, rex_byte);
  emit_byte(out, 0x0F);
  emit_byte(out, 0xB6);
  if (base == X64_RBP) {
    /* MOD=00 with SIB base=RBP means no base register; use MOD=01, disp8=0 */
    emit_byte(out, modrm(1, reg & 7, 4));
    emit_byte(out, sib(0, index & 7, base & 7));
    emit_disp8(out, 0);
  } else {
    emit_byte(out, modrm(0, reg & 7, 4));
    emit_byte(out, sib(0, index & 7, base & 7));
  }
}

/* =========================================================================
 * Load/store with [base + index*scale + disp] (SIB addressing)
 * ========================================================================= */

/* MOV r64, [base + index*scale + disp] — full SIB */
static void x64_load_sib(jit_region_t *out, int reg, int base, int index,
                         int scale, int32_t disp) {
  emit_rex_w(out, reg, base);
  emit_byte(out, 0x8B);
  if (disp == 0 && base != X64_RBP) {
    emit_byte(out, modrm(MOD_DISP0, reg & 7, 4));
  } else if (disp >= -128 && disp <= 127) {
    emit_byte(out, modrm(MOD_DISP8, reg & 7, 4));
    emit_disp8(out, (int8_t)disp);
  } else {
    emit_byte(out, modrm(MOD_DISP32, reg & 7, 4));
    emit_disp32(out, disp);
  }
  emit_byte(out, sib(scale, index & 7, base & 7));
}

/* =========================================================================
 * Branches
 * ========================================================================= */

/* JMP rel8: 0xEB + 1 byte offset */
static void x64_jmp_rel8(jit_region_t *out, int8_t off) {
  emit_byte(out, 0xEB);
  emit_disp8(out, off);
}

/* JMP rel32: 0xE9 + 4 byte offset */
static void x64_jmp_rel32(jit_region_t *out, int32_t off) {
  emit_byte(out, 0xE9);
  emit_disp32(out, off);
}

/* Jcc rel8: 0x70+cc + 1 byte offset (cc=0 for JO, 1 for JNO, ..., 4 for JE, 5
 * for JNE, ...) JE  = 0x74 (cc=4) JNE = 0x75 (cc=5) JL  = 0x7C (cc=12) JGE =
 * 0x7D (cc=13) JLE = 0x7E (cc=14) JG  = 0x7F (cc=15)
 */
static void x64_jcc_rel8(jit_region_t *out, int cc, int8_t off) {
  emit_byte(out, (uint8_t)(0x70u | cc));
  emit_disp8(out, off);
}

/* Jcc rel32: 0x0F 0x80+cc + 4 byte offset */
static void x64_jcc_rel32(jit_region_t *out, int cc, int32_t off) {
  emit_byte(out, 0x0F);
  emit_byte(out, (uint8_t)(0x80u | cc));
  emit_disp32(out, off);
}

#define JCC_JO 0
#define JCC_JNO 1
#define JCC_JB 2
#define JCC_JAE 3
#define JCC_JE 4
#define JCC_JNE 5
#define JCC_JBE 6
#define JCC_JA 7
#define JCC_JS 8
#define JCC_JNS 9
#define JCC_JP 10
#define JCC_JNP 11
#define JCC_JL 12
#define JCC_JGE 13
#define JCC_JLE 14
#define JCC_JG 15

/* =========================================================================
 * CALL
 * ========================================================================= */

/* CALL rel32: 0xE8 + 4 byte offset */
static void x64_call_rel32(jit_region_t *out, int32_t off) {
  emit_byte(out, 0xE8);
  emit_disp32(out, off);
}

/* CALL r64: FF /2 with ModRM mod=3 (register direct — reg holds the address) */
static void x64_call_reg(jit_region_t *out, int reg) {
  emit_byte(out, rex(0, 0, 0, reg > 7));
  emit_byte(out, 0xFF);
  emit_byte(out, modrm(MOD_REG, 2, reg & 7));
}

/* CALL [mem]: FF /2 (ModRM with reg=2, memory indirect) */
static void x64_call_mem(jit_region_t *out, int base, int32_t disp) {
  if (disp == 0 && base != X64_RBP && base != X64_RSP) {
    emit_byte(out, rex(0, 0, 0, base > 7));
    emit_byte(out, 0xFF);
    emit_byte(out, modrm(MOD_DISP0, 2, base & 7));
  } else if (disp >= -128 && disp <= 127) {
    emit_byte(out, rex(0, 0, 0, base > 7));
    emit_byte(out, 0xFF);
    emit_byte(out, modrm(MOD_DISP8, 2, base & 7));
    emit_disp8(out, (int8_t)disp);
  } else {
    emit_byte(out, rex(0, 0, 0, base > 7));
    emit_byte(out, 0xFF);
    emit_byte(out, modrm(MOD_DISP32, 2, base & 7));
    if (base == X64_RSP)
      emit_byte(out, sib(0, 4, base & 7));
    emit_disp32(out, disp);
  }
}

/* RET: 0xC3 */
static void x64_ret(jit_region_t *out) { emit_byte(out, 0xC3); }

/* =========================================================================
 * PUSH r64 (0x50+reg) / POP r64 (0x58+reg)
 * ========================================================================= */
static void x64_push(jit_region_t *out, int reg) {
  emit_byte(out, (uint8_t)(0x50u | (reg & 7)));
}
static void x64_pop(jit_region_t *out, int reg) {
  emit_byte(out, (uint8_t)(0x58u | (reg & 7)));
}

/* =========================================================================
 * PUSH/POP with REX.B for extended registers
 * ========================================================================= */
static void x64_push_r(jit_region_t *out, int reg) {
  if (reg > 7)
    emit_byte(out, rex(0, 0, 0, 1));
  x64_push(out, reg);
}
static void x64_pop_r(jit_region_t *out, int reg) {
  if (reg > 7)
    emit_byte(out, rex(0, 0, 0, 1));
  x64_pop(out, reg);
}

/* =========================================================================
 * Prologue / Epilogue
 * Prologue: push callee-saved regs, set up frame
 * Epilogue: pop callee-saved regs, ret
 * ========================================================================= */

static void x64_prologue(jit_region_t *out) {
  /* Push callee-saved registers we use.
   * SysV AMD64: RBP, RBX, R12 are callee-saved; RDI/RSI are caller-saved.
   * Win64:      RBP, RBX, R12, RDI, RSI are all callee-saved. */
  x64_push_r(out, X64_RBP);
  x64_push_r(out, X64_RBX);
  x64_push_r(out, X64_R12);
#ifdef SNOBOL_JIT_WIN64_ABI
  /* On Win64, RDI and RSI are callee-saved — we use them as pos/s registers
   * so we must preserve them across the JIT function call.
   * Stack alignment at JIT function entry (after caller's CALL):
   *   RSP mod 16 = 8  (return address pushed)
   *   push RBP  → RSP mod 16 = 0
   *   push RBX  → RSP mod 16 = 8
   *   push R12  → RSP mod 16 = 0
   *   push RDI  → RSP mod 16 = 8
   *   push RSI  → RSP mod 16 = 0
   *   sub  48   → RSP mod 16 = 0  ← 16-byte aligned before any nested CALL ✓
   * 5th stack arg (fill_char in EMIT_FORMAT) at [RSP+32] is within the 48-byte
   * allocation and does NOT overwrite the saved RSI at [RSP+40]. */
  x64_push_r(out, X64_RDI);
  x64_push_r(out, X64_RSI);
  x64_sub_ri32(out, X64_RSP, 48);
#endif
}

static void x64_epilogue(jit_region_t *out) {
#ifdef SNOBOL_JIT_WIN64_ABI
  /* Unwind 48 bytes allocated in x64_prologue */
  x64_add_ri32(out, X64_RSP, 48);
  /* Restore Win64 callee-saved RDI and RSI (pushed in prologue) */
  x64_pop_r(out, X64_RSI);
  x64_pop_r(out, X64_RDI);
#endif
  /* Pop r12, rbx, rbp */
  x64_pop_r(out, X64_R12);
  x64_pop_r(out, X64_RBX);
  x64_pop_r(out, X64_RBP);
  x64_ret(out);
}

/* =========================================================================
 * VM load/store helpers
 * ========================================================================= */

/* Save JIT register state (pos, etc.) back to VM struct */
static void x64_save_state(jit_region_t *out) {
  /* Store pos to VM->pos */
  x64_store_mr(out, X64_RBX, X64_RDI, offsetof(VM, pos));
  /* Store current s pointer isn't needed, s is passed to call-outs in VM */
}

/* Reload JIT register state from VM struct */
static void x64_reload_state(jit_region_t *out) {
  /* Reload pos */
  x64_load_mr(out, X64_RDI, X64_RBX, offsetof(VM, pos));
  /* s is in the VM; we keep it in rsi */
  x64_load_mr(out, X64_RSI, X64_RBX, offsetof(VM, s));
  /* len in r12 */
  x64_load_mr(out, X64_R12, X64_RBX, offsetof(VM, len));
}

/* Run-length of a jump instruction (rel8 = 2 bytes, rel32 = 5 bytes) */
#define JMP_REL8_SIZE 2
#define JMP_REL32_SIZE 5
#define JCC_REL8_SIZE 2
#define JCC_REL32_SIZE 6

/* =========================================================================
 * CFG structures (same as ARM64 backend)
 * ========================================================================= */
#define JIT_CFG_MAX_BLOCKS 64
#define JIT_LOOP_ITER_MAX 1024

typedef enum {
  BLOCK_TERM_SPLIT,
  BLOCK_TERM_JMP_FWD,
  BLOCK_TERM_JMP_BWD,
  BLOCK_TERM_EXIT,
} BlockTerm;

typedef struct {
  size_t start_ip;
  size_t term_ip;
  size_t next_ip;
  BlockTerm term;
  size_t succ_a;
  size_t succ_b;
  bool worthy;
  int succ_a_blk;
  int succ_b_blk;
  uint32_t *stub_start;
} JitBlock;

typedef struct {
  JitBlock blocks[JIT_CFG_MAX_BLOCKS];
  int count;
  bool has_backward;
} JitCfg;

typedef struct {
  uint8_t *instr_p;
  size_t ip;
} FailPatch;

typedef struct {
  uint8_t *instr_p;
  size_t target_ip;
} StubPatch;

/* =========================================================================
 * Call-out helper functions (same as in ARM64 backend)
 * ========================================================================= */

static void snobol_jit_helper_emit_literal(VM *vm, uint32_t offset,
                                           uint32_t len) {
  if (vm->out)
    snobol_buf_append(vm->out, (const char *)vm->bc + offset, (size_t)len);
  if (vm->emit_fn)
    vm->emit_fn((const char *)vm->bc + offset, (size_t)len, vm->emit_udata);
}

static void snobol_jit_helper_emit_capture(VM *vm, uint8_t reg) {
  if (reg < MAX_CAPS && vm->cap_end[reg] >= vm->cap_start[reg] &&
      vm->cap_end[reg] <= vm->len) {
    size_t s = vm->cap_start[reg];
    size_t e = vm->cap_end[reg];
    if (vm->out)
      snobol_buf_append(vm->out, vm->s + s, e - s);
    if (vm->emit_fn)
      vm->emit_fn(vm->s + s, e - s, vm->emit_udata);
  }
}

static void snobol_jit_helper_emit_expr(VM *vm, uint8_t reg,
                                        uint8_t expr_type) {
  if (reg >= MAX_CAPS)
    return;
  if (vm->cap_end[reg] < vm->cap_start[reg] || vm->cap_end[reg] > vm->len)
    return;
  const char *data = vm->s + vm->cap_start[reg];
  size_t len = vm->cap_end[reg] - vm->cap_start[reg];
  if (expr_type == 1) {
    char *tmp = (char *)snobol_malloc(len + 1);
    if (!tmp)
      return;
    for (size_t i = 0; i < len; ++i)
      tmp[i] =
          (data[i] >= 'a' && data[i] <= 'z') ? (char)(data[i] - 32) : data[i];
    if (vm->out)
      snobol_buf_append(vm->out, tmp, len);
    if (vm->emit_fn)
      vm->emit_fn(tmp, len, vm->emit_udata);
    snobol_free(tmp);
  } else if (expr_type == 2) {
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "%zu", len);
    if (vm->out)
      snobol_buf_append(vm->out, tmp, (size_t)n);
    if (vm->emit_fn)
      vm->emit_fn(tmp, (size_t)n, vm->emit_udata);
  } else {
    if (vm->out)
      snobol_buf_append(vm->out, data, len);
    if (vm->emit_fn)
      vm->emit_fn(data, len, vm->emit_udata);
  }
}

static void snobol_jit_helper_emit_format(VM *vm, uint8_t reg,
                                          uint8_t format_type, uint16_t width,
                                          uint8_t fill_char) {
  if (reg >= MAX_CAPS)
    return;
  if (vm->cap_end[reg] < vm->cap_start[reg] || vm->cap_end[reg] > vm->len)
    return;
  const char *data = vm->s + vm->cap_start[reg];
  size_t len = vm->cap_end[reg] - vm->cap_start[reg];

  if (format_type == SNBL_FMT_UPPER) {
    char *tmp = (char *)snobol_malloc(len + 1);
    if (!tmp)
      return;
    for (size_t i = 0; i < len; ++i)
      tmp[i] =
          (data[i] >= 'a' && data[i] <= 'z') ? (char)(data[i] - 32) : data[i];
    if (vm->out)
      snobol_buf_append(vm->out, tmp, len);
    if (vm->emit_fn)
      vm->emit_fn(tmp, len, vm->emit_udata);
    snobol_free(tmp);
  } else if (format_type == SNBL_FMT_LOWER) {
    char *tmp = (char *)snobol_malloc(len + 1);
    if (!tmp)
      return;
    for (size_t i = 0; i < len; ++i)
      tmp[i] =
          (data[i] >= 'A' && data[i] <= 'Z') ? (char)(data[i] + 32) : data[i];
    if (vm->out)
      snobol_buf_append(vm->out, tmp, len);
    if (vm->emit_fn)
      vm->emit_fn(tmp, len, vm->emit_udata);
    snobol_free(tmp);
  } else if (format_type == SNBL_FMT_LENGTH) {
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "%zu", len);
    if (vm->out)
      snobol_buf_append(vm->out, tmp, (size_t)n);
    if (vm->emit_fn)
      vm->emit_fn(tmp, (size_t)n, vm->emit_udata);
  } else if (format_type == SNBL_FMT_LPAD || format_type == SNBL_FMT_RPAD) {
    if (width > 1024)
      width = 1024;
    if (len >= (size_t)width) {
      if (vm->out)
        snobol_buf_append(vm->out, data, len);
      if (vm->emit_fn)
        vm->emit_fn(data, len, vm->emit_udata);
    } else {
      size_t pad = (size_t)width - len;
      size_t total = (size_t)width;
      char *buf = (char *)snobol_malloc(total);
      if (buf) {
        if (format_type == SNBL_FMT_LPAD) {
          memset(buf, fill_char, pad);
          memcpy(buf + pad, data, len);
        } else {
          memcpy(buf, data, len);
          memset(buf + len, fill_char, pad);
        }
        if (vm->out)
          snobol_buf_append(vm->out, buf, total);
        if (vm->emit_fn)
          vm->emit_fn(buf, total, vm->emit_udata);
        snobol_free(buf);
      }
    }
  } else {
    if (vm->out)
      snobol_buf_append(vm->out, data, len);
    if (vm->emit_fn)
      vm->emit_fn(data, len, vm->emit_udata);
  }
}

static void snobol_jit_helper_emit_table_ip(VM *vm, uint64_t op_ip) {
  size_t ip = (size_t)op_ip + 1;
  if (ip + 4 > vm->bc_len)
    return;
  uint16_t table_id = ((uint16_t)vm->bc[ip] << 8) | vm->bc[ip + 1];
  ip += 2;
  uint8_t key_type = vm->bc[ip++];
  uint8_t name_len = vm->bc[ip++];
  ip += name_len;
#ifdef SNOBOL_DYNAMIC_PATTERN
  snobol_table_t *table = vm_get_table(vm, table_id);
  const char *value = nullptr;
  if (key_type == 0) {
    if (ip + 2 > vm->bc_len)
      return;
    uint16_t key_len = ((uint16_t)vm->bc[ip] << 8) | vm->bc[ip + 1];
    ip += 2;
    if (key_len > 0 && ip + key_len <= vm->bc_len) {
      char *key = (char *)snobol_malloc(key_len + 1);
      if (key) {
        memcpy(key, vm->bc + ip, key_len);
        key[key_len] = '\0';
        if (table)
          value = table_get(table, key);
        snobol_free(key);
      }
    }
  } else if (key_type == 1) {
    if (ip >= vm->bc_len)
      return;
    uint8_t key_reg = vm->bc[ip];
    if (table && key_reg < MAX_CAPS &&
        vm->cap_end[key_reg] >= vm->cap_start[key_reg] &&
        vm->cap_end[key_reg] <= vm->len) {
      size_t kl = vm->cap_end[key_reg] - vm->cap_start[key_reg];
      char *key = (char *)snobol_malloc(kl + 1);
      if (key) {
        memcpy(key, vm->s + vm->cap_start[key_reg], kl);
        key[kl] = '\0';
        value = table_get(table, key);
        snobol_free(key);
      }
    }
  }
  if (value) {
    size_t vl = strlen(value);
    if (vm->out)
      snobol_buf_append(vm->out, value, vl);
    if (vm->emit_fn)
      vm->emit_fn(value, vl, vm->emit_udata);
  }
#else
  (void)table_id;
  (void)key_type;
#endif
}

#ifdef SNOBOL_DYNAMIC_PATTERN
static bool snobol_jit_helper_table_get(VM *vm, uint16_t table_id,
                                        uint8_t key_reg, uint8_t dest_reg) {
  (void)dest_reg;
  snobol_table_t *table = vm_get_table(vm, table_id);
  if (!table)
    return false;
  if (key_reg >= MAX_CAPS || vm->cap_end[key_reg] <= vm->cap_start[key_reg])
    return false;
  size_t kl = vm->cap_end[key_reg] - vm->cap_start[key_reg];
  char *key = (char *)snobol_malloc(kl + 1);
  if (!key)
    return false;
  memcpy(key, vm->s + vm->cap_start[key_reg], kl);
  key[kl] = '\0';
  const char *value = table_get(table, key);
  snobol_free(key);
  return value != nullptr;
}

static bool snobol_jit_helper_table_set(VM *vm, uint16_t table_id,
                                        uint8_t key_reg, uint8_t val_reg) {
  snobol_table_t *table = vm_get_table(vm, table_id);
  if (!table)
    return false;
  if (key_reg >= MAX_CAPS || vm->cap_end[key_reg] <= vm->cap_start[key_reg])
    return false;
  if (val_reg >= MAX_CAPS || vm->cap_end[val_reg] <= vm->cap_start[val_reg])
    return false;
  size_t kl = vm->cap_end[key_reg] - vm->cap_start[key_reg];
  size_t vl = vm->cap_end[val_reg] - vm->cap_start[val_reg];
  char *key = (char *)snobol_malloc(kl + 1);
  char *val = (char *)snobol_malloc(vl + 1);
  if (!key || !val) {
    snobol_free(key);
    snobol_free(val);
    return false;
  }
  memcpy(key, vm->s + vm->cap_start[key_reg], kl);
  key[kl] = '\0';
  memcpy(val, vm->s + vm->cap_start[val_reg], vl);
  val[vl] = '\0';
  (void)table_set(table, key, val);
  snobol_free(key);
  snobol_free(val);
  return true;
}
#endif

static bool snobol_jit_helper_bal(VM *vm, uint32_t open_cp, uint32_t close_cp) {
  size_t pos = vm->pos;
  uint32_t first;
  int fb;
  if (!utf8_peek_next(vm->s, vm->len, pos, &first, &fb) || first != open_cp)
    return false;
  int depth = 0;
  bool ok = false;
  while (pos < vm->len) {
    uint32_t cp;
    int cb;
    if (!utf8_peek_next(vm->s, vm->len, pos, &cp, &cb))
      break;
    if (cp == open_cp)
      depth++;
    else if (cp == close_cp) {
      depth--;
      if (depth == 0) {
        pos += (size_t)cb;
        ok = true;
        break;
      }
    }
    pos += (size_t)cb;
  }
  if (ok)
    vm->pos = pos;
  return ok;
}

static bool snobol_jit_helper_eval(VM *vm, uint16_t fn_id, uint8_t reg) {
  if (reg >= MAX_CAPS)
    return false;
  if (vm->cap_end[reg] < vm->cap_start[reg] || vm->cap_end[reg] > vm->len)
    return false;
  if (vm->eval_fn)
    return vm->eval_fn((int)fn_id, vm->s, vm->cap_start[reg], vm->cap_end[reg],
                       vm->eval_udata);
  return true;
}

#ifdef SNOBOL_DYNAMIC_PATTERN
static bool snobol_jit_helper_dynamic(VM *vm) {
  if (!vm->dyn_pending_source || !vm->dyn_pending_bc)
    return false;
  dynamic_pattern_t *pattern = dynamic_pattern_cache_get(
      vm->dyn_cache, vm->dyn_pending_source, (int)vm->dyn_pending_source_len);
  if (!pattern) {
    uint8_t *bc_copy = (uint8_t *)snobol_malloc(vm->dyn_pending_bc_len);
    if (!bc_copy)
      return false;
    memcpy(bc_copy, vm->dyn_pending_bc, vm->dyn_pending_bc_len);
    pattern = dynamic_pattern_create(vm->dyn_pending_source, bc_copy,
                                     vm->dyn_pending_bc_len);
    if (!pattern || !pattern->is_valid) {
      if (pattern)
        dynamic_pattern_release(pattern);
      snobol_free(bc_copy);
      return false;
    }
    if (!dynamic_pattern_cache_put(vm->dyn_cache, vm->dyn_pending_source,
                                   pattern)) {
      dynamic_pattern_release(pattern);
      return false;
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
  const uint8_t *saved_bc = vm->bc;
  size_t saved_bcl = vm->bc_len;
  size_t saved_pos = vm->pos;
  size_t saved_ip = vm->ip;
#ifdef SNOBOL_JIT
  SnobolJitContext *saved_ctx = (SnobolJitContext *)vm->jit.ctx;
  void **saved_traces = vm->jit.traces;
  uint64_t *saved_ip_counts = vm->jit.ip_counts;
  SnobolJitContext *inner_ctx = (SnobolJitContext *)pattern->jit_ctx;
  vm->jit.ctx = inner_ctx;
  vm->jit.traces = inner_ctx ? inner_ctx->traces : nullptr;
  vm->jit.ip_counts = inner_ctx ? inner_ctx->ip_counts : nullptr;
#endif
  vm->bc = pattern->bc;
  vm->bc_len = pattern->bc_len;
  vm->ip = 0;
  bool result = vm_run(vm);
  vm->bc = saved_bc;
  vm->bc_len = saved_bcl;
  vm->ip = saved_ip;
#ifdef SNOBOL_JIT
  vm->jit.ctx = saved_ctx;
  vm->jit.traces = saved_traces;
  vm->jit.ip_counts = saved_ip_counts;
#endif
  if (!result)
    vm->pos = saved_pos;
  dynamic_pattern_release(pattern);
  return result;
}
#endif

/* =========================================================================
 * IR instruction lookup helpers
 * ========================================================================= */
static size_t ir_find_instr(const jit_ir_region_t *ir, size_t target_ip) {
  for (size_t i = 0; i < ir->count; i++) {
    if (ir->instrs[i].bc_ip == target_ip)
      return i;
  }
  return ir->count;
}

/* =========================================================================
 * CFG builder from IR (same as ARM64 backend)
 * ========================================================================= */

static int ir_find_block(const JitCfg *cfg, size_t ip) {
  for (int i = 0; i < cfg->count; i++)
    if (cfg->blocks[i].start_ip == ip)
      return i;
  return -1;
}

static void ir_cfg_scan_block(const jit_ir_region_t *ir, size_t ir_start_idx,
                              JitBlock *blk) {
  blk->stub_start = nullptr;
  blk->succ_a_blk = -1;
  blk->succ_b_blk = -1;
  blk->succ_a = 0;
  blk->succ_b = 0;
  blk->worthy = false;

  if (ir_start_idx >= ir->count) {
    blk->start_ip = 0;
    blk->term_ip = 0;
    blk->next_ip = 0;
    blk->term = BLOCK_TERM_EXIT;
    return;
  }

  blk->start_ip = ir->instrs[ir_start_idx].bc_ip;

  for (size_t i = ir_start_idx; i < ir->count; i++) {
    const jit_ir_instr_t *ins = &ir->instrs[i];

    switch (ins->opcode) {
    case JIT_IR_SPLIT:
      blk->term = BLOCK_TERM_SPLIT;
      blk->term_ip = ins->bc_ip;
      blk->next_ip = ins->bc_ip + 9;
      blk->succ_a = ins->u.split.target_a;
      blk->succ_b = ins->u.split.target_b;
      return;

    case JIT_IR_JMP:
    case JIT_IR_GOTO: {
      size_t tgt = ins->u.jmp.target;
      blk->term_ip = ins->bc_ip;
      blk->next_ip = ins->bc_ip + (ins->opcode == JIT_IR_JMP ? 5u : 3u);
      blk->succ_a = tgt;
      blk->term = (tgt > ins->bc_ip) ? BLOCK_TERM_JMP_FWD : BLOCK_TERM_JMP_BWD;
      return;
    }

    case JIT_IR_ACCEPT:
    case JIT_IR_FAIL:
    case JIT_IR_REPEAT_INIT:
    case JIT_IR_REPEAT_STEP:
      blk->term = BLOCK_TERM_EXIT;
      blk->term_ip = ins->bc_ip;
      blk->next_ip = ins->bc_ip + 1;
      return;

    case JIT_IR_LIT:
    case JIT_IR_ANY:
    case JIT_IR_NOTANY:
    case JIT_IR_SPAN:
    case JIT_IR_BREAK:
    case JIT_IR_BREAKX:
    case JIT_IR_LEN:
    case JIT_IR_REM:
    case JIT_IR_RPOS:
    case JIT_IR_RTAB:
    case JIT_IR_EMIT_LITERAL:
    case JIT_IR_EMIT_CAPTURE:
    case JIT_IR_EMIT_EXPR:
    case JIT_IR_EMIT_FORMAT:
    case JIT_IR_EMIT_TABLE:
    case JIT_IR_TABLE_GET:
    case JIT_IR_TABLE_SET:
    case JIT_IR_BAL:
    case JIT_IR_EVAL:
    case JIT_IR_DYNAMIC:
      blk->worthy = true;
      break;

    default:
      break;
    }
  }
  size_t last_bc_ip = ir->instrs[ir->count - 1].bc_ip;
  blk->term = BLOCK_TERM_EXIT;
  blk->term_ip = last_bc_ip;
  blk->next_ip = last_bc_ip;
}

static int ir_cfg_build(const jit_ir_region_t *ir, JitCfg *cfg) {
  cfg->count = 0;
  cfg->has_backward = false;
  if (!ir || ir->count == 0)
    return 0;

  size_t queue[JIT_CFG_MAX_BLOCKS * 2];
  int qhead = 0, qtail = 0;
  size_t visited[JIT_CFG_MAX_BLOCKS];
  int vcount = 0;

  queue[qtail++] = ir->instrs[0].bc_ip;

  while (qhead < qtail) {
    size_t ip = queue[qhead++];
    bool seen = false;
    for (int i = 0; i < vcount; i++)
      if (visited[i] == ip) {
        seen = true;
        break;
      }
    if (seen)
      continue;
    visited[vcount++] = ip;
    if (cfg->count >= JIT_CFG_MAX_BLOCKS)
      break;

    size_t ir_idx = ir_find_instr(ir, ip);
    if (ir_idx >= ir->count)
      continue;

    JitBlock *blk = &cfg->blocks[cfg->count++];
    ir_cfg_scan_block(ir, ir_idx, blk);

    switch (blk->term) {
    case BLOCK_TERM_SPLIT: {
      bool sa = false, sb = false;
      for (int i = 0; i < vcount; i++) {
        if (visited[i] == blk->succ_a)
          sa = true;
        if (visited[i] == blk->succ_b)
          sb = true;
      }
      if (!sa && ir_find_instr(ir, blk->succ_a) < ir->count &&
          qtail < (int)(sizeof(queue) / sizeof(queue[0])))
        queue[qtail++] = blk->succ_a;
      if (!sb && ir_find_instr(ir, blk->succ_b) < ir->count &&
          qtail < (int)(sizeof(queue) / sizeof(queue[0])))
        queue[qtail++] = blk->succ_b;
      break;
    }
    case BLOCK_TERM_JMP_FWD: {
      bool sa = false;
      for (int i = 0; i < vcount; i++)
        if (visited[i] == blk->succ_a)
          sa = true;
      if (!sa && ir_find_instr(ir, blk->succ_a) < ir->count &&
          qtail < (int)(sizeof(queue) / sizeof(queue[0])))
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
    if (cfg->blocks[i].worthy) {
      any_worthy = true;
      break;
    }

  return (any_worthy && cfg->count > 0) ? cfg->count : 0;
}

/* =========================================================================
 * Call-out abstractions for SysV and Win64 ABI
 * ========================================================================= */

/*
 * On SysV AMD64:
 *   call-out args: rdi, rsi, rdx, rcx, r8, r9
 *   No shadow space.
 *
 * On MS x64:
 *   call-out args: rcx, rdx, r8, r9 (only 4 integer args)
 *   32 bytes shadow space before the call.
 *
 * Our JIT registers conflict with arg registers:
 *   rdi = pos (SysV arg #1)
 *   rsi = s   (SysV arg #2)
 *   rcx = scratch (SysV arg #4, MS arg #1)
 *
 * Strategy for call-outs:
 *   1. Save JIT state (pos, etc.) to VM struct
 *   2. Load first arg (VM *) into rdi (SysV) or rcx (MS)
 *   3. Set up remaining args
 *   4. Load function pointer and call
 *   5. Restore JIT state from VM struct
 */

/* =========================================================================
 * Block emission
 * ========================================================================= */

static uint8_t *x64_emit_block_ops(jit_region_t *out, const jit_ir_region_t *ir,
                                   size_t ir_start, size_t ir_end,
                                   FailPatch *fail_patches, size_t *fp_count,
                                   size_t fail_patch_cap, size_t start_ip,
                                   size_t term_ip, VM *vm) {
  (void)fail_patches;
  (void)fp_count;
  (void)fail_patch_cap;
  (void)start_ip;
  (void)term_ip;

  for (size_t i = ir_start; i < ir_end; i++) {
    const jit_ir_instr_t *ins = &ir->instrs[i];

    switch (ins->opcode) {

    case JIT_IR_NOP:
      break;

    case JIT_IR_ACCEPT:
      x64_save_state(out);
      x64_mov_ri64(out, X64_RAX, ins->bc_ip);
      x64_store_mr(out, X64_RBX, X64_RAX, offsetof(VM, ip));
      x64_mov_ri64(out, X64_RAX, 1);
      x64_epilogue(out);
      return (uint8_t *)out->p; /* actual end of emitted code */

    case JIT_IR_FAIL:
      x64_save_state(out);
      x64_mov_ri64(out, X64_RAX, ins->bc_ip);
      x64_store_mr(out, X64_RBX, X64_RAX, offsetof(VM, ip));
      x64_xor_rr(out, X64_RAX, X64_RAX);
      x64_epilogue(out);
      return (uint8_t *)out->p;

    case JIT_IR_JMP: {
      size_t target = ins->u.jmp.target;
      bool backward = target <= ins->bc_ip;
      if (backward) {
        int32_t diff =
            (int32_t)(intptr_t)((uint8_t *)out->p - (uint8_t *)out->code_start);
        (void)diff;
        /* Backward JMP to loop start — just continue linear. Real loop happens
         * via CFG exit + re-enter. For now patch later. */
        uint8_t *patch = (uint8_t *)out->p;
        x64_jmp_rel32(out, 0);
        (void)patch;
        /* Save for patching */
        /* We store stub patches directly */
        StubPatch *sp = (StubPatch *)snobol_malloc(sizeof(StubPatch));
        if (sp) {
          sp->instr_p = patch;
          sp->target_ip = target;
          /* We store in out->code_start for now — but we can't allocate
           * dynamically. For simplicity, use linear pass and no loop unrolling
           * in stub. */
          snobol_free(sp);
        }
      } else {
        /* Forward JMP: emit placeholder rel32 */
        uint8_t *patch = (uint8_t *)out->p;
        x64_jmp_rel32(out, 0);
        StubPatch *sp = (StubPatch *)snobol_malloc(sizeof(StubPatch));
        if (sp) {
          sp->instr_p = patch;
          sp->target_ip = target;
          snobol_free(sp);
        }
      }
      break;
    }

    case JIT_IR_SPLIT: {
      size_t ta = ins->u.split.target_a;
      size_t tb = ins->u.split.target_b;
      (void)ta;
      (void)tb;
      /* SPLIT: try first branch, if it fails try second.
       * For now, just JMP to first branch. Second branch acts as fallback. */
      uint8_t *patch = (uint8_t *)out->p;
      x64_jmp_rel32(out, 0);
      StubPatch *sp = (StubPatch *)snobol_malloc(sizeof(StubPatch));
      if (sp) {
        sp->instr_p = patch;
        sp->target_ip = ta;
        snobol_free(sp);
      }
      break;
    }

    case JIT_IR_LIT: {
      /* Compare bytes at s+pos with literal data; if mismatch, fail */
      const uint8_t *data = ins->u.lit.data;
      uint32_t lit_len = ins->u.lit.len;

      /* Check if pos + lit_len > len → fail */
      x64_mov_ri64(out, X64_RAX, lit_len);
      x64_add_rr(out, X64_RAX, X64_RDI);
      x64_cmp_rr(out, X64_RAX, X64_R12);
      uint8_t *past_len = (uint8_t *)out->p;
      x64_jcc_rel8(out, JCC_JA, 0);
      /* Fail */
      x64_xor_rr(out, X64_RAX, X64_RAX);
      x64_epilogue(out);
      /* Patch the JA */
      intptr_t past_len_off = (uint8_t *)out->p - past_len;
      past_len[1] = (uint8_t)(past_len_off - 2); /* rel8 offset */

      /* Compare each byte */
      for (uint32_t j = 0; j < lit_len; j++) {
        x64_loadb_mr_idx(out, X64_RAX, X64_RSI, X64_RDI);
        x64_cmp_rm32_imm32(out, X64_RAX, data[j]);
        uint8_t *ok = (uint8_t *)out->p;
        x64_jcc_rel8(out, JCC_JE, 0);
        x64_xor_rr(out, X64_RAX, X64_RAX);
        x64_epilogue(out);
        intptr_t ok_off = (uint8_t *)out->p - ok;
        ok[1] = (uint8_t)(ok_off - 2);
        x64_add_ri32(out, X64_RDI, 1);
      }
      break;
    }

    case JIT_IR_ANY:
    case JIT_IR_NOTANY:
    case JIT_IR_BREAK:
    case JIT_IR_BREAKX: {
      uint16_t set_id = ins->u.set.set_id;
      uint16_t count16, ci;
      const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count16, &ci);
      uint64_t ascii_map[2];
      if (!ranges || !ranges_to_ascii_bitmap(ranges, count16, ascii_map))
        return (uint8_t *)out->p;

      x64_mov_ri64(out, X64_R11, ascii_map[0]);
      x64_mov_ri64(out, X64_R8, ascii_map[1]);

      x64_cmp_rr(out, X64_RDI, X64_R12);
      uint8_t *in_range = (uint8_t *)out->p;
      x64_jcc_rel8(out, JCC_JA, 0);
      x64_xor_rr(out, X64_RAX, X64_RAX);
      x64_epilogue(out);
      intptr_t in_range_off = (uint8_t *)out->p - in_range;
      in_range[1] = (uint8_t)(in_range_off - 2);

      x64_loadb_mr_idx(out, X64_RAX, X64_RSI, X64_RDI);
      x64_cmp_rm32_imm32(out, X64_RAX, 127);
      uint8_t *ascii_ok = (uint8_t *)out->p;
      x64_jcc_rel8(out, JCC_JBE, 0);
      x64_xor_rr(out, X64_RAX, X64_RAX);
      x64_epilogue(out);
      intptr_t ascii_ok_off = (uint8_t *)out->p - ascii_ok;
      ascii_ok[1] = (uint8_t)(ascii_ok_off - 2);

      x64_mov_rr(out, X64_RCX, X64_RAX);
      x64_and_ri32(out, X64_RCX, 0x40);
      uint8_t *use_high = (uint8_t *)out->p;
      x64_jcc_rel8(out, JCC_JNE, 0);
      x64_and_ri32(out, X64_RAX, 63);
      x64_bt_rr(out, X64_R11, X64_RAX);
      uint8_t *bit_checked = (uint8_t *)out->p;
      x64_jmp_rel8(out, 0);
      intptr_t use_high_off = (uint8_t *)out->p - use_high;
      use_high[1] = (uint8_t)(use_high_off - 2);
      x64_and_ri32(out, X64_RAX, 63);
      x64_bt_rr(out, X64_R8, X64_RAX);
      intptr_t bit_checked_off = (uint8_t *)out->p - bit_checked;
      bit_checked[1] = (uint8_t)(bit_checked_off - 2);

      if (ins->opcode == JIT_IR_NOTANY) {
        uint8_t *found = (uint8_t *)out->p;
        x64_jcc_rel8(out, JCC_JAE, 0); /* JNC: CF=0 (not in set) → success */
        x64_xor_rr(out, X64_RAX, X64_RAX);
        x64_epilogue(out);
        intptr_t found_off = (uint8_t *)out->p - found;
        found[1] = (uint8_t)(found_off - 2);
      } else {
        uint8_t *found = (uint8_t *)out->p;
        x64_jcc_rel8(out, JCC_JB, 0); /* JC: CF=1 (in set) → success */
        x64_xor_rr(out, X64_RAX, X64_RAX);
        x64_epilogue(out);
        intptr_t found_off = (uint8_t *)out->p - found;
        found[1] = (uint8_t)(found_off - 2);
      }

      if (ins->opcode != JIT_IR_BREAKX) {
        x64_add_ri32(out, X64_RDI, 1);
      }
      break;
    }

    case JIT_IR_SPAN: {
      uint16_t set_id = ins->u.set.set_id;
      uint16_t count16, ci;
      const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count16, &ci);
      uint64_t ascii_map[2];
      if (!ranges || !ranges_to_ascii_bitmap(ranges, count16, ascii_map))
        return (uint8_t *)out->p;

      x64_mov_ri64(out, X64_R11, ascii_map[0]);
      x64_mov_ri64(out, X64_R8, ascii_map[1]);

      uint8_t *loop_start = (uint8_t *)out->p;
      x64_cmp_rr(out, X64_RDI, X64_R12);
      uint8_t *span_done = (uint8_t *)out->p;
      x64_jcc_rel8(out, JCC_JAE, 0);

      x64_loadb_mr_idx(out, X64_RAX, X64_RSI, X64_RDI);
      x64_cmp_rm32_imm32(out, X64_RAX, 127);
      uint8_t *span_ascii_ok = (uint8_t *)out->p;
      x64_jcc_rel8(out, JCC_JBE, 0);
      uint8_t *span_nonascii_done = (uint8_t *)out->p;
      x64_jmp_rel8(out, 0);
      intptr_t span_ascii_ok_off = (uint8_t *)out->p - span_ascii_ok;
      span_ascii_ok[1] = (uint8_t)(span_ascii_ok_off - 2);

      x64_mov_rr(out, X64_RCX, X64_RAX);
      x64_and_ri32(out, X64_RCX, 0x40);
      uint8_t *use_high_s = (uint8_t *)out->p;
      x64_jcc_rel8(out, JCC_JNE, 0);
      x64_and_ri32(out, X64_RAX, 63);
      x64_bt_rr(out, X64_R11, X64_RAX);
      uint8_t *bit_checked_s = (uint8_t *)out->p;
      x64_jmp_rel8(out, 0);
      intptr_t use_high_s_off = (uint8_t *)out->p - use_high_s;
      use_high_s[1] = (uint8_t)(use_high_s_off - 2);
      x64_and_ri32(out, X64_RAX, 63);
      x64_bt_rr(out, X64_R8, X64_RAX);
      intptr_t bit_checked_s_off = (uint8_t *)out->p - bit_checked_s;
      bit_checked_s[1] = (uint8_t)(bit_checked_s_off - 2);

      uint8_t *span_not_in = (uint8_t *)out->p;
      x64_jcc_rel8(out, JCC_JAE, 0); /* JNC: CF=0 (not in set) → done */
      x64_add_ri32(out, X64_RDI, 1);
      x64_jmp_rel32(out,
                    (int32_t)(intptr_t)(loop_start - (uint8_t *)out->p) - 5);
      intptr_t span_not_in_off = (uint8_t *)out->p - span_not_in;
      span_not_in[1] = (uint8_t)(span_not_in_off - 2);
      intptr_t span_done_off = (uint8_t *)out->p - span_done;
      span_done[1] = (uint8_t)(span_done_off - 2);
      intptr_t span_nonascii_done_off = (uint8_t *)out->p - span_nonascii_done;
      span_nonascii_done[1] = (uint8_t)(span_nonascii_done_off - 2);
      break;
    }

    case JIT_IR_LEN: {
      /* Advance pos by N bytes */
      uint32_t n = ins->u.len.n;
      x64_add_ri32(out, X64_RDI, (int32_t)n);
      break;
    }

    case JIT_IR_ANCHOR: {
      /* ANCHOR: check position is at start of line (type 1) or subject start */
      uint8_t type = ins->u.anchor.type;
      if (type == 1) {
        /* Check if pos == 0 or previous char is newline */
        x64_test_rr(out, X64_RDI, X64_RDI);
        uint8_t *at_start = (uint8_t *)out->p;
        x64_jcc_rel8(out, JCC_JE, 0);
        /* Check byte at s[pos-1] == '\n' */
        x64_mov_ri64(out, X64_RAX, (uint64_t)-1);
        x64_add_rr(out, X64_RAX, X64_RDI);
        x64_loadb_mr_idx(out, X64_RAX, X64_RSI, X64_RAX);
        x64_cmp_rm32_imm32(out, X64_RAX, '\n');
        uint8_t *anchored = (uint8_t *)out->p;
        x64_jcc_rel8(out, JCC_JE, 0);
        x64_xor_rr(out, X64_RAX, X64_RAX);
        x64_epilogue(out);
        intptr_t anchored_off = (uint8_t *)out->p - anchored;
        anchored[1] = (uint8_t)(anchored_off - 2);
        intptr_t at_start_off = (uint8_t *)out->p - at_start;
        at_start[1] = (uint8_t)(at_start_off - 2);
      }
      break;
    }

    case JIT_IR_CAP_START: {
      /* Store pos into cap_start[reg] */
      uint8_t cap_reg = ins->u.cap.reg;
      x64_store_mr(out, X64_RBX, X64_RDI,
                   offsetof(VM, cap_start) + (int32_t)cap_reg * 8);
      break;
    }

    case JIT_IR_CAP_END: {
      uint8_t cap_reg2 = ins->u.cap.reg;
      x64_store_mr(out, X64_RBX, X64_RDI,
                   offsetof(VM, cap_end) + (int32_t)cap_reg2 * 8);
      break;
    }

    case JIT_IR_ASSIGN: {
      /* Copy cap_start[reg] -> var_start[var] and cap_end[reg] -> var_end[var]
       */
      uint8_t cap_reg3 = ins->u.assign.reg;
      uint16_t var = ins->u.assign.var;
      x64_load_mr(out, X64_RAX, X64_RBX,
                  offsetof(VM, cap_start) + (int32_t)cap_reg3 * 8);
      x64_store_mr(out, X64_RBX, X64_RAX,
                   offsetof(VM, var_start) + (int32_t)var * 8);
      x64_load_mr(out, X64_RAX, X64_RBX,
                  offsetof(VM, cap_end) + (int32_t)cap_reg3 * 8);
      x64_store_mr(out, X64_RBX, X64_RAX,
                   offsetof(VM, var_end) + (int32_t)var * 8);
      /* var_count = max(var_count, var+1) */
      x64_load_mr(out, X64_RAX, X64_RBX, offsetof(VM, var_count));
      x64_mov_ri64(out, X64_RCX, var + 1);
      x64_cmp_rr(out, X64_RAX, X64_RCX);
      uint8_t *skip = (uint8_t *)out->p;
      x64_jcc_rel8(out, JCC_JAE, 0);
      x64_store_mr(out, X64_RBX, X64_RCX, offsetof(VM, var_count));
      intptr_t skip_off = (uint8_t *)out->p - skip;
      skip[1] = (uint8_t)(skip_off - 2);
      break;
    }

    case JIT_IR_REPEAT_INIT: {
      /* Save current pos and len as repeat start anchor */
      x64_store_mr(out, X64_RBX, X64_RDI, offsetof(VM, pos));
      break;
    }

    case JIT_IR_REPEAT_STEP: {
      /* Repeat step: ensure progress (pos > saved pos), then loop.
       * For now treat as ACCEPT. */
      x64_mov_ri64(out, X64_RAX, 1);
      x64_epilogue(out);
      return (uint8_t *)out->p;
    }

    case JIT_IR_REM: {
      /* pos = len */
      x64_mov_rr(out, X64_RDI, X64_R12);
      break;
    }

    case JIT_IR_RPOS: {
      /* RPOS N: fails unless pos == len - N */
      uint32_t rn = ins->u.rpos_rtab.n;
      x64_mov_ri64(out, X64_RAX, rn);
      x64_mov_rr(out, X64_R8, X64_R12);
      x64_sub_rr(out, X64_R8, X64_RAX);
      x64_cmp_rr(out, X64_RDI, X64_R8);
      uint8_t *rpos_ok = (uint8_t *)out->p;
      x64_jcc_rel8(out, JCC_JE, 0);
      x64_xor_rr(out, X64_RAX, X64_RAX);
      x64_epilogue(out);
      intptr_t rpos_ok_off = (uint8_t *)out->p - rpos_ok;
      rpos_ok[1] = (uint8_t)(rpos_ok_off - 2);
      break;
    }

    case JIT_IR_RTAB: {
      /* RTAB N: set pos = len - N (fail if N > len) */
      uint32_t tn = ins->u.rpos_rtab.n;
      x64_cmp_ri32(out, X64_R12, (int32_t)tn);
      uint8_t *rtab_ok = (uint8_t *)out->p;
      x64_jcc_rel8(out, JCC_JAE, 0);
      x64_xor_rr(out, X64_RAX, X64_RAX);
      x64_epilogue(out);
      intptr_t rtab_ok_off = (uint8_t *)out->p - rtab_ok;
      rtab_ok[1] = (uint8_t)(rtab_ok_off - 2);
      x64_mov_ri64(out, X64_RAX, tn);
      x64_mov_rr(out, X64_R8, X64_R12);
      x64_sub_rr(out, X64_R8, X64_RAX);
      x64_mov_rr(out, X64_RDI, X64_R8);
      break;
    }

    case JIT_IR_FENCE: {
      /* FENCE: cut backtracking by clearing choice stack */
      x64_mov_ri64(out, X64_RAX, 0);
      x64_store_mr(out, X64_RBX, X64_RAX, offsetof(VM, choices_top));
      break;
    }

    case JIT_IR_GOTO: {
      /* GOTO: unconditional branch (like JMP) */
      size_t target2 = ins->u.jmp.target;
      (void)target2;
      uint8_t *patch2 = (uint8_t *)out->p;
      x64_jmp_rel32(out, 0);
      StubPatch *sp2 = (StubPatch *)snobol_malloc(sizeof(StubPatch));
      if (sp2) {
        sp2->instr_p = patch2;
        sp2->target_ip = target2;
        snobol_free(sp2);
      }
      break;
    }

    case JIT_IR_GOTO_F: {
      /* GOTO_F: conditional fall-through (always NOP in compiled region) */
      break;
    }

    case JIT_IR_LABEL:
    case JIT_IR_DYNAMIC_DEF:
    case JIT_IR_COPY:
      break;

    /* ---- Call-out opcodes ---- */
    case JIT_IR_EMIT_LITERAL: {
      uint32_t lit_off = ins->u.emit_lit.offset;
      uint32_t lit_len2 = ins->u.emit_lit.len;
      /* Save pos to VM */
      x64_save_state(out);
      /* Set up args: VM is in rbx, we need to set rdi (SysV) or rcx (MS) */
#ifdef SNOBOL_JIT_WIN64_ABI
      x64_mov_rr(out, X64_RCX, X64_RBX);
      x64_mov_ri64(out, X64_RDX, lit_off);
      x64_mov_ri64(out, X64_R8, lit_len2);
#else
      x64_mov_rr(out, X64_RDI, X64_RBX);
      x64_mov_ri64(out, X64_RSI, lit_off);
      x64_mov_ri64(out, X64_RDX, lit_len2);
#endif
      x64_mov_ri64(out, X64_R10,
                   (uint64_t)(uintptr_t)&snobol_jit_helper_emit_literal);
      x64_call_reg(out, X64_R10);
      x64_reload_state(out);
      break;
    }

    case JIT_IR_EMIT_CAPTURE: {
      uint8_t cap_reg4 = ins->u.emit_cap.reg;
      x64_save_state(out);
#ifdef SNOBOL_JIT_WIN64_ABI
      x64_mov_rr(out, X64_RCX, X64_RBX);
      x64_mov_ri64(out, X64_RDX, cap_reg4);
#else
      x64_mov_rr(out, X64_RDI, X64_RBX);
      x64_mov_ri64(out, X64_RSI, cap_reg4);
#endif
      x64_mov_ri64(out, X64_R10,
                   (uint64_t)(uintptr_t)&snobol_jit_helper_emit_capture);
      x64_call_reg(out, X64_R10);
      x64_reload_state(out);
      break;
    }

    case JIT_IR_EMIT_EXPR: {
      uint8_t cap_reg5 = ins->u.emit_expr.reg;
      uint8_t expr_type = ins->u.emit_expr.expr_type;
      x64_save_state(out);
#ifdef SNOBOL_JIT_WIN64_ABI
      x64_mov_rr(out, X64_RCX, X64_RBX);
      x64_mov_ri64(out, X64_RDX, cap_reg5);
      x64_mov_ri64(out, X64_R8, expr_type);
#else
      x64_mov_rr(out, X64_RDI, X64_RBX);
      x64_mov_ri64(out, X64_RSI, cap_reg5);
      x64_mov_ri64(out, X64_RDX, expr_type);
#endif
      x64_mov_ri64(out, X64_R10,
                   (uint64_t)(uintptr_t)&snobol_jit_helper_emit_expr);
      x64_call_reg(out, X64_R10);
      x64_reload_state(out);
      break;
    }

    case JIT_IR_EMIT_FORMAT: {
      uint8_t cap_reg6 = ins->u.emit_fmt.reg;
      uint8_t fmt_type = ins->u.emit_fmt.fmt_type;
      uint16_t width = ins->u.emit_fmt.width;
      uint8_t fill_char = ins->u.emit_fmt.fill_char;
      x64_save_state(out);
#ifdef SNOBOL_JIT_WIN64_ABI
      x64_mov_rr(out, X64_RCX, X64_RBX);
      x64_mov_ri64(out, X64_RDX, cap_reg6);
      x64_mov_ri64(out, X64_R8, fmt_type);
      x64_mov_ri64(out, X64_R9, width);
      x64_mov_ri64(out, X64_R10, fill_char);
      x64_store_mr(out, X64_RSP, X64_R10, 32); /* 5th arg at [RSP+32] */
#else
      x64_mov_rr(out, X64_RDI, X64_RBX);
      x64_mov_ri64(out, X64_RSI, cap_reg6);
      x64_mov_ri64(out, X64_RDX, fmt_type);
      x64_mov_ri64(out, X64_RCX, width);
      x64_mov_ri64(out, X64_R8, fill_char);
#endif
      x64_mov_ri64(out, X64_R10,
                   (uint64_t)(uintptr_t)&snobol_jit_helper_emit_format);
      x64_call_reg(out, X64_R10);
      x64_reload_state(out);
      break;
    }

    case JIT_IR_EMIT_TABLE: {
      x64_save_state(out);
#ifdef SNOBOL_JIT_WIN64_ABI
      x64_mov_rr(out, X64_RCX, X64_RBX);
      x64_mov_ri64(out, X64_RDX, (uint64_t)ins->bc_ip);
#else
      x64_mov_rr(out, X64_RDI, X64_RBX);
      x64_mov_ri64(out, X64_RSI, (uint64_t)ins->bc_ip);
#endif
      x64_mov_ri64(out, X64_R10,
                   (uint64_t)(uintptr_t)&snobol_jit_helper_emit_table_ip);
      x64_call_reg(out, X64_R10);
      x64_reload_state(out);
      break;
    }

    case JIT_IR_TABLE_GET: {
      uint16_t table_id2 = ins->u.tget.table_id;
      uint8_t key_reg2 = ins->u.tget.key_reg;
      uint8_t dest_reg2 = ins->u.tget.dest_reg;
      x64_save_state(out);
#ifdef SNOBOL_JIT_WIN64_ABI
      x64_mov_rr(out, X64_RCX, X64_RBX);
      x64_mov_ri64(out, X64_RDX, table_id2);
      x64_mov_ri64(out, X64_R8, key_reg2);
      x64_mov_ri64(out, X64_R9, dest_reg2);
#else
      x64_mov_rr(out, X64_RDI, X64_RBX);
      x64_mov_ri64(out, X64_RSI, table_id2);
      x64_mov_ri64(out, X64_RDX, key_reg2);
      x64_mov_ri64(out, X64_RCX, dest_reg2);
#endif
      x64_mov_ri64(out, X64_R10,
                   (uint64_t)(uintptr_t)&snobol_jit_helper_table_get);
      x64_call_reg(out, X64_R10);
      x64_reload_state(out);
      /* Check result in rax */
      x64_test_rr(out, X64_RAX, X64_RAX);
      uint8_t *tget_ok = (uint8_t *)out->p;
      x64_jcc_rel8(out, JCC_JNE, 0);
      x64_xor_rr(out, X64_RAX, X64_RAX);
      x64_epilogue(out);
      intptr_t tget_ok_off = (uint8_t *)out->p - tget_ok;
      tget_ok[1] = (uint8_t)(tget_ok_off - 2);
      break;
    }

    case JIT_IR_TABLE_SET: {
      uint16_t table_id3 = ins->u.tset.table_id;
      uint8_t key_reg3 = ins->u.tset.key_reg;
      uint8_t val_reg = ins->u.tset.val_reg;
      x64_save_state(out);
#ifdef SNOBOL_JIT_WIN64_ABI
      x64_mov_rr(out, X64_RCX, X64_RBX);
      x64_mov_ri64(out, X64_RDX, table_id3);
      x64_mov_ri64(out, X64_R8, key_reg3);
      x64_mov_ri64(out, X64_R9, val_reg);
#else
      x64_mov_rr(out, X64_RDI, X64_RBX);
      x64_mov_ri64(out, X64_RSI, table_id3);
      x64_mov_ri64(out, X64_RDX, key_reg3);
      x64_mov_ri64(out, X64_RCX, val_reg);
#endif
      x64_mov_ri64(out, X64_R10,
                   (uint64_t)(uintptr_t)&snobol_jit_helper_table_set);
      x64_call_reg(out, X64_R10);
      x64_reload_state(out);
      break;
    }

    case JIT_IR_BAL: {
      uint32_t open_cp = ins->u.bal.open_cp;
      uint32_t close_cp = ins->u.bal.close_cp;
      x64_save_state(out);
#ifdef SNOBOL_JIT_WIN64_ABI
      x64_mov_rr(out, X64_RCX, X64_RBX);
      x64_mov_ri64(out, X64_RDX, open_cp);
      x64_mov_ri64(out, X64_R8, close_cp);
#else
      x64_mov_rr(out, X64_RDI, X64_RBX);
      x64_mov_ri64(out, X64_RSI, open_cp);
      x64_mov_ri64(out, X64_RDX, close_cp);
#endif
      x64_mov_ri64(out, X64_R10, (uint64_t)(uintptr_t)&snobol_jit_helper_bal);
      x64_call_reg(out, X64_R10);
      x64_reload_state(out);
      x64_test_rr(out, X64_RAX, X64_RAX);
      uint8_t *bal_ok = (uint8_t *)out->p;
      x64_jcc_rel8(out, JCC_JNE, 0);
      x64_xor_rr(out, X64_RAX, X64_RAX);
      x64_epilogue(out);
      intptr_t bal_ok_off = (uint8_t *)out->p - bal_ok;
      bal_ok[1] = (uint8_t)(bal_ok_off - 2);
      break;
    }

    case JIT_IR_EVAL: {
      uint16_t fn_id = ins->u.eval.fn_id;
      uint8_t reg = ins->u.eval.reg;
      x64_save_state(out);
#ifdef SNOBOL_JIT_WIN64_ABI
      x64_mov_rr(out, X64_RCX, X64_RBX);
      x64_mov_ri64(out, X64_RDX, fn_id);
      x64_mov_ri64(out, X64_R8, reg);
#else
      x64_mov_rr(out, X64_RDI, X64_RBX);
      x64_mov_ri64(out, X64_RSI, fn_id);
      x64_mov_ri64(out, X64_RDX, reg);
#endif
      x64_mov_ri64(out, X64_R10, (uint64_t)(uintptr_t)&snobol_jit_helper_eval);
      x64_call_reg(out, X64_R10);
      x64_reload_state(out);
      x64_test_rr(out, X64_RAX, X64_RAX);
      uint8_t *eval_ok = (uint8_t *)out->p;
      x64_jcc_rel8(out, JCC_JNE, 0);
      x64_xor_rr(out, X64_RAX, X64_RAX);
      x64_epilogue(out);
      intptr_t eval_ok_off = (uint8_t *)out->p - eval_ok;
      eval_ok[1] = (uint8_t)(eval_ok_off - 2);
      break;
    }

    case JIT_IR_DYNAMIC: {
#ifdef SNOBOL_DYNAMIC_PATTERN
      x64_save_state(out);
#ifdef SNOBOL_JIT_WIN64_ABI
      x64_mov_rr(out, X64_RCX, X64_RBX);
#else
      x64_mov_rr(out, X64_RDI, X64_RBX);
#endif
      x64_mov_ri64(out, X64_R10,
                   (uint64_t)(uintptr_t)&snobol_jit_helper_dynamic);
      x64_call_reg(out, X64_R10);
      x64_reload_state(out);
      x64_test_rr(out, X64_RAX, X64_RAX);
      uint8_t *dyn_ok = (uint8_t *)out->p;
      x64_jcc_rel8(out, JCC_JNE, 0);
      x64_xor_rr(out, X64_RAX, X64_RAX);
      x64_epilogue(out);
      intptr_t dyn_ok_off = (uint8_t *)out->p - dyn_ok;
      dyn_ok[1] = (uint8_t)(dyn_ok_off - 2);
#else
      x64_xor_rr(out, X64_RAX, X64_RAX);
      x64_epilogue(out);
      return out->p;
#endif
      break;
    }

    default:
      break;
    }
  }
  return (uint8_t *)out->p;
}

/* =========================================================================
 * x86_64_lower() — main entry point
 * ========================================================================= */

static void *x86_64_lower(const jit_ir_region_t *ir, VM *vm,
                          jit_region_t *out) {
  if (!ir || !vm || !out)
    return nullptr;
  if (ir->non_compilable || ir->count == 0)
    return nullptr;

  /* Build CFG from IR */
  JitCfg cfg;
  int n_blocks = ir_cfg_build(ir, &cfg);
  if (n_blocks <= 0)
    return nullptr;
  bool use_cfg = (n_blocks >= 2 || (n_blocks == 1 && cfg.has_backward));

  /* Allocate code buffer */
  size_t code_size = use_cfg ? 65536u : 32768u;
  uint8_t *code = (uint8_t *)snobol_jit_alloc_code(code_size);
  if (!code)
    return nullptr;

  /* We store the write pointer as a uint8_t* in out->p but cast it to uint32_t*
   * for the struct type. We'll re-cast throughout. */
  out->p = (uint32_t *)code;
  out->code_start = (uint32_t *)code;
  out->code_size = code_size;

  /* Prologue: push callee-saved registers */
  x64_prologue(out);

  /* Copy the VM pointer argument (rdi on SysV / rcx on Win64) into
   * the callee-saved rbx. This MUST be a register-to-register move,
   * not a literal load of the compile-time `vm` value: each call
   * site passes a different (stack-allocated) VM, and the trace
   * code persists across many calls inside the JIT code cache.
   * Using the literal here would access stale stack memory on every
   * call after the first. */
#ifdef SNOBOL_JIT_WIN64_ABI
  x64_mov_rr(out, X64_RBX, X64_RCX); /* rcx = 1st arg = vm (Win64) */
#else
  x64_mov_rr(out, X64_RBX, X64_RDI); /* rdi = 1st arg = vm (SysV) */
#endif

  /* Load initial state from VM struct */
  x64_reload_state(out);

  /* =====================================================================
   * Simple linear emission (no CFG for now — emit blocks sequentially)
   * ===================================================================== */
  FailPatch fail_patches[64];
  size_t fp_count = 0;
  (void)fail_patches;

  if (use_cfg) {
    /* Multi-block CFG: emit block by block */
    for (int b = 0; b < cfg.count; b++) {
      JitBlock *blk = &cfg.blocks[b];
      size_t ir_start = ir_find_instr(ir, blk->start_ip);
      size_t ir_end = ir->count;
      if (b + 1 < cfg.count) {
        ir_end = ir_find_instr(ir, cfg.blocks[b + 1].start_ip);
      }

      /* Emit block body */
      out->p = (uint32_t *)x64_emit_block_ops(out, ir, ir_start, ir_end,
                                              fail_patches, &fp_count, 64,
                                              blk->start_ip, blk->term_ip, vm);

      /* If block doesn't end with a terminator that emitted already,
       * add fall-through to next block */
      if (blk->term != BLOCK_TERM_EXIT) {
        /* Find next block IP */
        size_t next_ip = 0;
        if (b + 1 < cfg.count) {
          next_ip = cfg.blocks[b + 1].start_ip;
        }
        if (next_ip > 0) {
          /* Emit JMP to next block (placeholder, patched later) */
          x64_jmp_rel32(out, 0);
        }
      }
    }
  } else {
    /* Single block: linear emit all instructions */
    out->p = (uint32_t *)x64_emit_block_ops(out, ir, 0, ir->count, fail_patches,
                                            &fp_count, 64, 0, 0, vm);
  }

  /* Fall-through epilogue (if not already returned) */
  x64_mov_ri64(out, X64_RAX, 1);
  x64_epilogue(out);

  out->n_blocks = (size_t)n_blocks;
  /* Compute actual code size and seal */
  size_t actual_size = (size_t)((uint8_t *)out->p - code);
  out->code_size = actual_size;
  snobol_jit_seal_code(code, actual_size);
  return (void *)code;
}

static void x86_64_flush_icache(void *code, size_t size) {
#ifdef _WIN32
  FlushInstructionCache(GetCurrentProcess(), code, size);
#else
  (void)code;
  (void)size;
#endif
}

static const jit_backend_t x86_64_backend_vtable = {
    .name = "x86_64",
    .lower = x86_64_lower,
    .flush_icache = x86_64_flush_icache,
};

void snobol_jit_x86_64_register(void) {
  jit_backend_register(&x86_64_backend_vtable);
}

#endif /* __x86_64__ || _M_X64 */
#endif /* SNOBOL_JIT */
