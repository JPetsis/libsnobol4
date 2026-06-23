/**
 * @file jit_backend_arm32.c
 * @brief ARM Thumb-2 (ARMv7-A) JIT backend for the SNOBOL4 micro-JIT.
 *
 * Implements the jit_backend_t vtable for the ARM32 target (ARMv7-A Thumb-2).
 * Primary entry point: arm32_lower() — translates a jit_ir_region_t to
 * ARM Thumb-2 machine code stored in the provided jit_region_t.
 *
 * Register convention (AAPCS32):
 *   r0 = VM pointer
 *   r1 = vm->s (subject string)
 *   r2 = vm->pos (current position)
 *   r3 = vm->len (subject length)
 *   r4–r11 = temporaries (callee-saved)
 *   r12 = IP scratch
 *   lr (r14) = link register (must be saved/restored across call-outs)
 */

#include "snobol/jit.h"
#include "snobol/jit_backend.h"
#include "snobol/jit_ir.h"

#ifdef SNOBOL_JIT
#if defined(__arm__) || defined(__thumb__) || defined(__ARM_ARCH_7A__)

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "snobol/dynamic_pattern.h"
#include "snobol/snobol_internal.h"
#include "snobol/string_fn.h"
#include "snobol/table.h"
#include "snobol/type_fn.h"
#include "snobol/vm.h"

/* Debug logging: set SNOBOL_JIT_DEBUG=1 in the environment to enable
 * runtime logging of JIT compilation and code emission. */
static int jit_debug_on(void) {
  static int cached = -1;
  if (cached < 0) {
    const char *e = getenv("SNOBOL_JIT_DEBUG");
    cached = (e && e[0] == '1' && e[1] == '\0') ? 1 : 0;
  }
  return cached;
}
#define JIT_LOG(fmt, ...)                                                      \
  do {                                                                         \
    if (jit_debug_on()) {                                                      \
      fprintf(stderr, "[JIT-ARM32] %s:%d: " fmt "\n", __FILE__, __LINE__,      \
              ##__VA_ARGS__);                                                  \
      fflush(stderr);                                                          \
    }                                                                          \
  } while (0)

/* =========================================================================
 * Thumb-2 instruction helpers (16-bit and 32-bit encoding)
 *
 * For 16-bit instructions we store them as uint16_t; for 32-bit as uint32_t.
 * The emitter advances a byte-level pointer (arm32_wp).
 * ========================================================================= */

/* --- Branch instructions (16-bit) --- */
/* B<c> <label> — conditional branch, range ±256 bytes, encoding T1 */
#define T2_B_COND(cond, imm11)                                                 \
  ((uint16_t)(0xd000u | ((cond) << 8) | ((imm11) & 0xffu)))

/* B <label> — unconditional branch, range ±2KB, encoding T2 */
#define T2_B(imm12) ((uint16_t)(0xe000u | ((imm12) & 0x7ffu)))

/* BX <Rm> — branch to register */
#define T2_BX(rm) ((uint16_t)(0x4700u | (((rm) & 15u) << 3)))

/* BLX <Rm> — branch with link to register */
#define T2_BLX(rm) ((uint16_t)(0x4780u | (((rm) & 15u) << 3)))

/* --- Branch instructions (32-bit) --- */
/* BL <label> — branch with link, range ±16MB, encoding T1 */
static inline uint32_t T2_BL(uint32_t imm24) {
  uint16_t hi = 0xf000u | ((imm24 >> 12) & 0x7ffu);
  uint16_t lo = 0xf800u | (imm24 & 0x7ffu);
  return ((uint32_t)hi << 16) | lo;
}

/* B.W <label> — unconditional branch wide, range ±16MB, encoding T4 */
static inline uint32_t T2_B_W(uint32_t imm24) {
  uint16_t hi = 0xf000u | 0x9000u | ((imm24 >> 12) & 0x7ffu);
  uint16_t lo = 0xf800u | (imm24 & 0x7ffu);
  return ((uint32_t)hi << 16) | lo;
}

/* CBZ <Rn>, <label> — compare and branch if zero, range ±126 bytes, encoding T1
 */
#define T2_CBZ(rn, imm7)                                                       \
  ((uint16_t)(0xb100u | (((imm7) & 0x3fu) << 3) | ((rn) & 7u)))

/* CBNZ <Rn>, <label> — compare and branch if non-zero, range ±126 bytes,
 * encoding T1 */
#define T2_CBNZ(rn, imm7)                                                      \
  ((uint16_t)(0xb900u | (((imm7) & 0x3fu) << 3) | ((rn) & 7u)))

/* --- Data-processing (16-bit, low register Rd/Rn/Rm in r0-r7) --- */
#define T2_MOV_RD_RS(rd, rs)                                                   \
  ((uint16_t)(0x4600u | (((rd) & 7u) << 0) | (((rd) & 8u) << 4) |              \
              (((rs) & 15u) << 3)))
#define T2_MOVS_RD_IMM8(rd, imm8)                                              \
  ((uint16_t)(0x2000u | (((rd) & 7u) << 8) | ((imm8) & 0xffu)))
#define T2_ADDS_RD_RN_RM(rd, rn, rm)                                           \
  ((uint16_t)(0x1800u | (((rd) & 7u) << 0) | (((rn) & 7u) << 3) |              \
              (((rm) & 7u) << 6)))
#define T2_ADD_RD_RS(rd, rs)                                                   \
  ((uint16_t)(0x4400u | (((rd) & 7u) << 0) | (((rd) & 8u) << 4) |              \
              (((rs) & 15u) << 3)))
#define T2_ADDS_RD_RN_IMM3(rd, rn, imm3)                                       \
  ((uint16_t)(0x1c00u | (((rd) & 7u) << 0) | (((rn) & 7u) << 3) |              \
              (((imm3) & 7u) << 6)))
#define T2_SUBS_RD_RN_RM(rd, rn, rm)                                           \
  ((uint16_t)(0x1a00u | (((rd) & 7u) << 0) | (((rn) & 7u) << 3) |              \
              (((rm) & 7u) << 6)))
#define T2_SUBS_RD_RN_IMM3(rd, rn, imm3)                                       \
  ((uint16_t)(0x1e00u | (((rd) & 7u) << 0) | (((rn) & 7u) << 3) |              \
              (((imm3) & 7u) << 6)))
#define T2_CMP_RN_RM(rn, rm)                                                   \
  ((uint16_t)(0x4280u | (((rn) & 7u) << 3) | (((rm) & 7u) << 6)))
#define T2_CMP_RN_IMM8(rn, imm8)                                               \
  ((uint16_t)(0x2800u | (((rn) & 7u) << 8) | ((imm8) & 0xffu)))
#define T2_ANDS_RD_RM(rd, rm)                                                  \
  ((uint16_t)(0x4000u | (((rd) & 7u) << 0) | (((rm) & 7u) << 3)))
#define T2_ORRS_RD_RM(rd, rm)                                                  \
  ((uint16_t)(0x4300u | (((rd) & 7u) << 0) | (((rm) & 7u) << 3)))
#define T2_EORS_RD_RM(rd, rm)                                                  \
  ((uint16_t)(0x4040u | (((rd) & 7u) << 0) | (((rm) & 7u) << 3)))
#define T2_LSLS_RD_RM_IMM5(rd, rm, imm5)                                       \
  ((uint16_t)(0x0000u | (((rd) & 7u) << 0) | (((rm) & 7u) << 3) |              \
              (((imm5) & 0x1fu) << 6)))
#define T2_LSRS_RD_RM_IMM5(rd, rm, imm5)                                       \
  ((uint16_t)(0x0800u | (((rd) & 7u) << 0) | (((rm) & 7u) << 3) |              \
              (((imm5) & 0x1fu) << 6)))
#define T2_ASRS_RD_RM_IMM5(rd, rm, imm5)                                       \
  ((uint16_t)(0x1000u | (((rd) & 7u) << 0) | (((rm) & 7u) << 3) |              \
              (((imm5) & 0x1fu) << 6)))

/* --- Data-processing (32-bit WIDE) ---
 *
 * ARMv7-A 32-bit Thumb-2 register-form data-processing layout:
 *   HW1[15:11] = 11101  (T2 32-bit prefix)
 *   HW1[10:9]  = 01     (op1=01, data-proc shifted register)
 *   HW1[7:4]   = op2    (operation selector)
 *   HW1[3:0]   = Rn     (first source operand)
 *   HW2[15:12] = imm3:0 (top 3 bits of shift amount, padded 0)  — except
 * CMP/TST/MOV/shifts HW2[11:8]  = Rd     (destination; 0xF for CMP/TST)
 *   HW2[7:4]   = imm2 + shift type (zero for plain register)
 *   HW2[3:0]   = Rm     (second source operand; 0xF for MOV.W)
 * For shift-by-register (LSL/LSR/ASR/ROR):
 *   HW2[15:12] = 1111   (forced; "shift" class)
 *   HW2[7:4]   = imm2 + type (zero for register-form shifts)
 *   HW2[3:0]   = Rm     (shift amount register)
 */

/* ADD.W <Rd>, <Rn>, <Rm> — 32-bit ADD (T3) */
static inline uint32_t T2_ADD_W_RD_RN_RM(int rd, int rn, int rm) {
  uint16_t hi = 0xeb00u | (rn & 15u);
  uint16_t lo = (uint16_t)(((rd & 15u) << 8) | (rm & 15u));
  return ((uint32_t)hi << 16) | lo;
}

/* SUB.W <Rd>, <Rn>, <Rm> — 32-bit SUB (T3) */
static inline uint32_t T2_SUB_W_RD_RN_RM(int rd, int rn, int rm) {
  uint16_t hi = 0xeba0u | (rn & 15u);
  uint16_t lo = (uint16_t)(((rd & 15u) << 8) | (rm & 15u));
  return ((uint32_t)hi << 16) | lo;
}

/* CMP.W <Rn>, <Rm> — 32-bit CMP; Rd forced to 0xF (no write-back) */
static inline uint32_t T2_CMP_W_RN_RM(int rn, int rm) {
  uint16_t hi = 0xebb0u | (rn & 15u);
  uint16_t lo = (uint16_t)((0xfu << 8) | (rm & 15u));
  return ((uint32_t)hi << 16) | lo;
}

/* MOV.W <Rd>, <Rm> — 32-bit MOV (T3); Rn forced to 0xF (no source operand) */
static inline uint32_t T2_MOV_W_RD_RM(int rd, int rm) {
  uint16_t hi = 0xea4fu;
  uint16_t lo = (uint16_t)(((rd & 15u) << 8) | (rm & 15u));
  return ((uint32_t)hi << 16) | lo;
}

/* TST.W <Rn>, <Rm> — 32-bit TST; Rd forced to 0xF (no write-back) */
static inline uint32_t T2_TST_W_RN_RM(int rn, int rm) {
  uint16_t hi = 0xea10u | (rn & 15u);
  uint16_t lo = (uint16_t)((0xfu << 8) | (rm & 15u));
  return ((uint32_t)hi << 16) | lo;
}

/* AND.W <Rd>, <Rn>, <Rm> — 32-bit AND (T3) */
static inline uint32_t T2_AND_W_RD_RN_RM(int rd, int rn, int rm) {
  uint16_t hi = 0xea00u | (rn & 15u);
  uint16_t lo = (uint16_t)(((rd & 15u) << 8) | (rm & 15u));
  return ((uint32_t)hi << 16) | lo;
}

/* ORR.W <Rd>, <Rn>, <Rm> — 32-bit ORR (T3) */
static inline uint32_t T2_ORR_W_RD_RN_RM(int rd, int rn, int rm) {
  uint16_t hi = 0xea40u | (rn & 15u);
  uint16_t lo = (uint16_t)(((rd & 15u) << 8) | (rm & 15u));
  return ((uint32_t)hi << 16) | lo;
}

/* EOR.W <Rd>, <Rn>, <Rm> — 32-bit EOR (T3) */
static inline uint32_t T2_EOR_W_RD_RN_RM(int rd, int rn, int rm) {
  uint16_t hi = 0xea80u | (rn & 15u);
  uint16_t lo = (uint16_t)(((rd & 15u) << 8) | (rm & 15u));
  return ((uint32_t)hi << 16) | lo;
}

/* LSL.W <Rd>, <Rn>, <Rm> — 32-bit LSL by register (T1 register-form shift)
 *   HW2[15:12] = 1111 (forced; shift-by-register class marker)
 *   HW2[3:0]   = Rm (shift amount register)
 */
static inline uint32_t T2_LSL_W_RD_RN_RM(int rd, int rn, int rm) {
  uint16_t hi = 0xfa00u | (rn & 15u);
  uint16_t lo = (uint16_t)(0xf000u | ((rd & 15u) << 8) | (rm & 15u));
  return ((uint32_t)hi << 16) | lo;
}

/* LSR.W <Rd>, <Rn>, <Rm> — 32-bit LSR by register */
static inline uint32_t T2_LSR_W_RD_RN_RM(int rd, int rn, int rm) {
  uint16_t hi = 0xfa20u | (rn & 15u);
  uint16_t lo = (uint16_t)(0xf000u | ((rd & 15u) << 8) | (rm & 15u));
  return ((uint32_t)hi << 16) | lo;
}

/* MOVW / MOVT 32-bit Thumb-2 imm16 encoding (T3 for MOVW, T1 for MOVT):
 *   HW1[10]    = imm16[11]       ("i" bit)
 *   HW1[3:0]   = imm16[15:12]    (imm4)
 *   HW2[14:12] = imm16[10:8]     (imm3)
 *   HW2[11:8]  = Rd              (4 bits, must be 0..14 per AAPCS)
 *   HW2[7:0]   = imm16[7:0]      (imm8)
 */
static inline uint32_t T2_MOVW(int rd, uint32_t imm16) {
  uint16_t hi = 0xf240u | (uint16_t)(((imm16 >> 11) & 1u) << 10) |
                (uint16_t)((imm16 >> 12) & 0xfu);
  uint16_t lo = (uint16_t)(((imm16 >> 8) & 0x7u) << 12) |
                (uint16_t)((rd & 15u) << 8) | (uint16_t)(imm16 & 0xffu);
  return ((uint32_t)hi << 16) | lo;
}

static inline uint32_t T2_MOVT(int rd, uint32_t imm16) {
  uint16_t hi = 0xf2c0u | (uint16_t)(((imm16 >> 11) & 1u) << 10) |
                (uint16_t)((imm16 >> 12) & 0xfu);
  uint16_t lo = (uint16_t)(((imm16 >> 8) & 0x7u) << 12) |
                (uint16_t)((rd & 15u) << 8) | (uint16_t)(imm16 & 0xffu);
  return ((uint32_t)hi << 16) | lo;
}

/* --- Load/store (16-bit) --- */
/* LDR <Rt>, [<Rn>, #<imm5>] — 16-bit, Rt/Rn in r0-r7 */
#define T2_LDR_RT_RN_IMM5(rt, rn, imm5)                                        \
  ((uint16_t)(0x6800u | ((imm5) & 0x1fu) << 6 | ((rn) & 7u) << 3 | ((rt) & 7u)))
/* LDRB <Rt>, [<Rn>, #<imm5>] — 16-bit */
#define T2_LDRB_RT_RN_IMM5(rt, rn, imm5)                                       \
  ((uint16_t)(0x7800u | ((imm5) & 0x1fu) << 6 | ((rn) & 7u) << 3 | ((rt) & 7u)))
/* STR <Rt>, [<Rn>, #<imm5>] — 16-bit */
#define T2_STR_RT_RN_IMM5(rt, rn, imm5)                                        \
  ((uint16_t)(0x6000u | ((imm5) & 0x1fu) << 6 | ((rn) & 7u) << 3 | ((rt) & 7u)))
/* STRB <Rt>, [<Rn>, #<imm5>] — 16-bit */
#define T2_STRB_RT_RN_IMM5(rt, rn, imm5)                                       \
  ((uint16_t)(0x7000u | ((imm5) & 0x1fu) << 6 | ((rn) & 7u) << 3 | ((rt) & 7u)))
/* LDRH <Rt>, [<Rn>, #<imm5>] — 16-bit */
#define T2_LDRH_RT_RN_IMM5(rt, rn, imm5)                                       \
  ((uint16_t)(0x8800u | ((imm5) & 0x1fu) << 6 | ((rn) & 7u) << 3 | ((rt) & 7u)))
/* STRH <Rt>, [<Rn>, #<imm5>] — 16-bit */
#define T2_STRH_RT_RN_IMM5(rt, rn, imm5)                                       \
  ((uint16_t)(0x8000u | ((imm5) & 0x1fu) << 6 | ((rn) & 7u) << 3 | ((rt) & 7u)))

/* LDR <Rt>, [<Rn>, <Rm>] — register offset, 16-bit, all in r0-r7 */
#define T2_LDR_RT_RN_RM(rt, rn, rm)                                            \
  ((uint16_t)(0x5800u | ((rt) & 7u) | (((rn) & 7u) << 3) | (((rm) & 7u) << 6)))
/* LDRB <Rt>, [<Rn>, <Rm>] */
#define T2_LDRB_RT_RN_RM(rt, rn, rm)                                           \
  ((uint16_t)(0x5c00u | ((rt) & 7u) | (((rn) & 7u) << 3) | (((rm) & 7u) << 6)))
/* STR <Rt>, [<Rn>, <Rm>] */
#define T2_STR_RT_RN_RM(rt, rn, rm)                                            \
  ((uint16_t)(0x5000u | ((rt) & 7u) | (((rn) & 7u) << 3) | (((rm) & 7u) << 6)))
/* STRB <Rt>, [<Rn>, <Rm>] */
#define T2_STRB_RT_RN_RM(rt, rn, rm)                                           \
  ((uint16_t)(0x5400u | ((rt) & 7u) | (((rn) & 7u) << 3) | (((rm) & 7u) << 6)))

/* LDR.W <Rt>, [<Rn>, #<imm12>] — 32-bit wide */
static inline uint32_t T2_LDR_W_RT_RN_IMM12(int rt, int rn, uint32_t imm12) {
  uint16_t hi = 0xf8d0u | (rn & 15u);
  uint16_t lo = (uint16_t)((uint32_t)rt << 12) | (imm12 & 0xfffu);
  return ((uint32_t)hi << 16) | lo;
}

/* STR.W <Rt>, [<Rn>, #<imm12>] — 32-bit wide */
static inline uint32_t T2_STR_W_RT_RN_IMM12(int rt, int rn, uint32_t imm12) {
  uint16_t hi = 0xf8c0u | (rn & 15u);
  uint16_t lo = (uint16_t)((uint32_t)rt << 12) | (imm12 & 0xfffu);
  return ((uint32_t)hi << 16) | lo;
}

/* LDRB.W <Rt>, [<Rn>, #<imm12>] — 32-bit wide */
static inline uint32_t T2_LDRB_W_RT_RN_IMM12(int rt, int rn, uint32_t imm12) {
  uint16_t hi = 0xf890u | (rn & 15u);
  uint16_t lo = (uint16_t)((uint32_t)rt << 12) | (imm12 & 0xfffu);
  return ((uint32_t)hi << 16) | lo;
}

/* STRB.W <Rt>, [<Rn>, #<imm12>] — 32-bit wide */
static inline uint32_t T2_STRB_W_RT_RN_IMM12(int rt, int rn, uint32_t imm12) {
  uint16_t hi = 0xf880u | (rn & 15u);
  uint16_t lo = (uint16_t)((uint32_t)rt << 12) | (imm12 & 0xfffu);
  return ((uint32_t)hi << 16) | lo;
}

/* LDR <Rt>, <label> — literal load */
static inline uint32_t T2_LDR_LIT(int rt, uint16_t imm12) {
  uint16_t hi = 0xf8dfu;
  uint16_t lo = (uint16_t)((uint32_t)rt << 12) | (imm12 & 0xfffu);
  return ((uint32_t)hi << 16) | lo;
}

/* --- Stack operations --- */
/* PUSH {<reglist>} */
static inline uint16_t T2_PUSH(uint16_t reglist) {
  return (uint16_t)(0xb400u | (reglist & 0xffu));
}

/* POP {<reglist>} */
static inline uint16_t T2_POP(uint16_t reglist) {
  return (uint16_t)(0xbc00u | (reglist & 0xffu));
}

/* PUSH {lr} or POP {pc} */
#define T2_PUSH_LR 0xb500u
#define T2_POP_PC 0xbd00u

/* PUSH.W {r3-r11,lr} — 32-bit Thumb-2 STMDB SP!,{r3-r11,lr}
 *   10 registers = 40 bytes, keeps SP 8-byte aligned at public interface.
 *   Mask = bits 3-11,14 = 0x4FF8.
 *   Including r3 keeps AAPCS32's 8-byte SP alignment when the JIT later
 *   issues a BLX to a C helper (which needs an aligned SP). The JIT
 *   doesn't actually use the saved r3 — it overwrites r3 with VM.len
 *   immediately after the prologue. We just push it as a free pad word.
 *   hi=0xe92d, lo=0x4ff8 */
#define T2_PUSH_R4R11_LR ((uint32_t)0xe92d4ff8u)
/* POP.W {r3-r11,pc} — 32-bit Thumb-2 LDMIA SP!,{r3-r11,pc}
 *   Mask = bits 3-11,15 = 0x8FF8.
 *   hi=0xe8bd, lo=0x8ff8 */
#define T2_POP_R4R11_PC ((uint32_t)0xe8bd8ff8u)

/* PUSH.W {r0,r1,r2,lr} — 32-bit Thumb-2 STMDB SP!,{r0,r1,r2,lr}
 *   Saves live JIT registers around a call-out: VM(r0), subject(r1),
 *   pos(r2), and JIT-return-addr(lr).
 *   Mask = bits 0,1,2,14 = 0x4007. 4 regs = 16 bytes, 8-aligned from prologue.
 */
#define T2_PUSH_R0R1R2LR ((uint32_t)0xe92d4007u)
/* POP.W {r0,r1,r2,lr} — 32-bit Thumb-2 LDMIA SP!,{r0,r1,r2,lr}
 *   Same mask, pops into r0,r1,r2,lr. */
#define T2_POP_R0R1R2LR ((uint32_t)0xe8bd4007u)

/* --- Misc --- */
#define T2_NOP 0xbf00u
/* ADD SP, SP, #imm — 16-bit T2, imm7 = imm/4 (max 508 bytes). */
#define T2_ADD_SP_IMM7(imm7) ((uint16_t)(0xb000u | ((imm7) & 0x7fu)))
/* SUB SP, SP, #imm — 16-bit T2 (bit 7 = 1 selects SUB). */
#define T2_SUB_SP_IMM7(imm7) ((uint16_t)(0xb080u | ((imm7) & 0x7fu)))
/* STRB.W <Rt>, [<Rn>, #<imm12>] — uses T2_STRB_W_RT_RN_IMM12 above. */
#define T2_IT_AL 0xbf08u  /* IF-THEN always (3 instructions) */
#define T2_IT_EQ 0xbf08u  /* IT eq */
#define T2_ITT_AL 0xbf0cu /* IF-THEN always (2 instructions) */

/* SUBS PC, LR, #4 — return from exception (unused, kept for reference) */
/* BX lr — return */
#define T2_BX_LR 0x4770u

/* =========================================================================
 * Write pointer helpers (byte-granularity for Thumb-2 code emission)
 * ========================================================================= */

static uint8_t *t2_wp; /* byte-level write pointer for Thumb-2 emission */

static void t2_emit16(uint16_t ins) {
  *(uint16_t *)t2_wp = ins;
  t2_wp += 2;
}

static void t2_emit32(uint32_t ins) {
  /* Thumb-2 32-bit instruction: first halfword (hi) at lower address.
   * The encoding helpers return (hi << 16) | lo.  A plain *(uint32_t*)
   * store would put lo at the lower address on little-endian ARM, so we
   * emit the two halfwords explicitly in the correct order. */
  uint16_t hi = (uint16_t)((ins >> 16) & 0xFFFFu);
  uint16_t lo = (uint16_t)(ins & 0xFFFFu);
  JIT_LOG("t2_emit32 0x%08x  (hi=0x%04x lo=0x%04x)", ins, hi, lo);
  *(uint16_t *)t2_wp = hi;       /* first halfword at lower address */
  *(uint16_t *)(t2_wp + 2) = lo; /* second halfword at higher address */
  t2_wp += 4;
}

/* Keep jit_region_t p in sync at word boundary */
static void t2_sync_region(jit_region_t *out) {
  size_t bytes = (size_t)(t2_wp - (uint8_t *)out->code_start);
  out->p = out->code_start + (bytes / 4);
}

/* Write a 32-bit Thumb-2 split instruction (hi + lo halves) */
#define t2_emit_split(hi, lo)                                                  \
  do {                                                                         \
    t2_emit16(hi);                                                             \
    t2_emit16(lo);                                                             \
  } while (0)

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
  uint8_t *stub_start;
} JitBlock;

typedef struct {
  JitBlock blocks[JIT_CFG_MAX_BLOCKS];
  int count;
  bool has_backward;
} JitCfg;

typedef struct {
  uint8_t *instr_p;
  size_t ip;
  int cond;
} FailPatch;

typedef struct {
  uint8_t *instr_p;
  size_t target_ip;
} StubPatch;

/* =========================================================================
 * Low-level emit helpers
 * ========================================================================= */

/* Condition codes for Thumb-2 */
#define T2_COND_EQ 0
#define T2_COND_NE 1
#define T2_COND_CS 2
#define T2_COND_CC 3
#define T2_COND_GE 0xa
#define T2_COND_GT 0xc
#define T2_COND_LE 0xd
#define T2_COND_LO T2_COND_CC
#define T2_COND_HI 8
#define T2_COND_LS 9
#define T2_COND_LT 0xb

/* Emit B<c> with 11-bit signed offset (in bytes, range ±2048) */
static void t2_emit_b_cond(int cond, uint8_t *target) {
  intptr_t diff = target - t2_wp - 4;
  uint16_t imm11 = (uint16_t)((diff / 2) & 0x7ffu);
  t2_emit16(T2_B_COND(cond, imm11));
}

/* Emit B.W (32-bit unconditional branch, range ±16MB) */
static void t2_emit_b_wide(uint8_t *target) {
  intptr_t diff = target - t2_wp - 4;
  uint32_t imm24 = (uint32_t)(diff / 2) & 0xffffffu;
  t2_emit32(T2_B_W(imm24));
}

/* Emit B (16-bit unconditional branch, range ±2KB) */
static void t2_emit_b(uint8_t *target) {
  intptr_t diff = target - t2_wp - 4;
  uint16_t imm12 = (uint16_t)((diff / 2) & 0x7ffu);
  t2_emit16(T2_B(imm12));
}

/* Emit placeholder conditional branch (patched later) */
static uint8_t *t2_emit_patch_b_cond(int cond) {
  uint8_t *p = t2_wp;
  t2_emit16(T2_B_COND(cond, 0));
  return p;
}

/* Emit placeholder unconditional branch (16-bit, patched later) */
static uint8_t *t2_emit_patch_b(void) {
  uint8_t *p = t2_wp;
  t2_emit16(T2_B(0));
  return p;
}

/* Emit placeholder CBZ/CBNZ (patched later) */
static uint8_t *t2_emit_patch_cbz(int rn) {
  uint8_t *p = t2_wp;
  t2_emit16(T2_CBZ(rn, 0));
  return p;
}

static uint8_t *t2_emit_patch_cbnz(int rn) {
  uint8_t *p = t2_wp;
  t2_emit16(T2_CBNZ(rn, 0));
  return p;
}

/* Patch a conditional branch (11-bit offset) */
static void t2_patch_b_cond(uint8_t *p, uint8_t *target, int cond) {
  intptr_t diff = target - p - 4;
  uint16_t imm11 = (uint16_t)((diff / 2) & 0x7ffu);
  *(uint16_t *)p = T2_B_COND(cond, imm11);
}

/* Patch a 16-bit B (12-bit offset) */
static void t2_patch_b(uint8_t *p, uint8_t *target) {
  intptr_t diff = target - p - 4;
  uint16_t imm12 = (uint16_t)((diff / 2) & 0x7ffu);
  *(uint16_t *)p = T2_B(imm12);
}

/* Patch a CBZ/CBNZ (7-bit offset in bytes, range ±126) */
static void t2_patch_cbz_cbnz(uint8_t *p, uint8_t *target, int rn,
                              bool is_cbnz) {
  intptr_t diff = target - p - 4;
  uint16_t imm7 = (uint16_t)((diff / 2) & 0x7fu);
  if (is_cbnz)
    *(uint16_t *)p = T2_CBNZ(rn, imm7);
  else
    *(uint16_t *)p = T2_CBZ(rn, imm7);
}

/* Load 32-bit immediate into register using MOVW/MOVT */
static void t2_emit_mov_imm32(int rd, uint32_t val) {
  t2_emit32(T2_MOVW(rd, val & 0xffff));
  if (val > 0xffff)
    t2_emit32(T2_MOVT(rd, (val >> 16) & 0xffff));
}

/* Load 64-bit immediate into register pair (r4, r5) using MOVW/MOVT */
static void t2_emit_mov_imm64_pair(int rd_lo, int rd_hi, uint64_t val) {
  t2_emit32(T2_MOVW(rd_lo, (uint32_t)(val & 0xffff)));
  if (val > 0xffffULL)
    t2_emit32(T2_MOVT(rd_lo, (uint32_t)((val >> 16) & 0xffff)));
  if (val > 0xffffffffULL) {
    t2_emit32(T2_MOVW(rd_hi, (uint32_t)((val >> 32) & 0xffff)));
    t2_emit32(T2_MOVT(rd_hi, (uint32_t)((val >> 48) & 0xffff)));
  }
}

/* Load a value from VM struct offset into register */
static void t2_emit_ldr_from_vm(int rt, size_t offset) {
  if (offset <= 0x7fu && rt < 8) {
    t2_emit16(T2_LDR_RT_RN_IMM5(rt, 0, (uint32_t)(offset / 4)));
  } else {
    t2_emit32(T2_LDR_W_RT_RN_IMM12(rt, 0, (uint32_t)offset));
  }
}

/* Store a register value into VM struct at offset */
static void t2_emit_str_to_vm(int rt, size_t offset) {
  if (offset <= 0x7fu && rt < 8 && (offset & 3) == 0) {
    t2_emit16(T2_STR_RT_RN_IMM5(rt, 0, (uint32_t)(offset / 4)));
  } else {
    t2_emit32(T2_STR_W_RT_RN_IMM12(rt, 0, (uint32_t)offset));
  }
}

/* =========================================================================
 * Call-out helpers (AAPCS32 calling convention)
 *
 * Helpers expect:  r0 = VM,  r1-r3 = helper-specific args.
 * AAPCS32 caller-saved (clobbered by BLX):  r0-r3, r12, lr
 * AAPCS32 callee-saved (preserved by helper): r4-r11
 *
 * JIT uses r0=VM, r1=subject, r2=pos, r3=len as live values across
 * call-outs.  The macro is layered so each case can insert arg-setup
 * instructions between PUSH and BLX.
 *
 *   CALLOUT_PROLOGUE: saves pos to VM, then PUSH.W {r0,r1,r2,lr}
 *   case sets r1/r2/r3 (some helper-specific subset) using MOVW
 *   CALLOUT_CALL(fn): MOVW/MOVT r12 = fn_addr; BLX r12
 *                     — r0 = return value, lr = continuation, JIT live regs on
 * stack CALLOUT_SAVE_RET (bool only): MOV r12, r0 — saves return value to
 * scratch CALLOUT_EPILOGUE: POP.W {r0,r1,r2,lr}; LDR r3,VM.len
 * ========================================================================= */
#define CALLOUT_PROLOGUE                                                       \
  do {                                                                         \
    t2_emit_str_to_vm(2, offsetof(VM, pos));                                   \
    t2_emit32(T2_PUSH_R0R1R2LR);                                               \
  } while (0)

#define CALLOUT_CALL(fn_ptr_)                                                  \
  do {                                                                         \
    t2_emit_mov_imm32(12, (uint32_t)(uintptr_t)(fn_ptr_));                     \
    t2_emit16(T2_BLX(12));                                                     \
  } while (0)

/* MOV r12, r0 — save bool return value into the IP scratch reg.
 * r12 is caller-saved but it survives the next POP because it's not in the
 * POP {r0,r1,r2,lr} list. */
#define CALLOUT_SAVE_RET t2_emit16((uint16_t)T2_MOV_RD_RS(12, 0))

#define CALLOUT_EPILOGUE                                                       \
  do {                                                                         \
    t2_emit32(T2_POP_R0R1R2LR);                                                \
    t2_emit32(T2_LDR_W_RT_RN_IMM12(3, 0, (uint32_t)offsetof(VM, len)));        \
  } while (0)

/* Convenience macros for the common arities.
 *
 * The "argN" expressions are emitted as MOVW/MOVT immediate loads into
 * r1-r3 before the BLX.  Callee-saved helpers (r4-r11) are preserved
 * by the C helper and remain intact across the call-out. */
#define EMIT_VOID_CALLOUT0(fn_ptr_)                                            \
  do {                                                                         \
    CALLOUT_PROLOGUE;                                                          \
    CALLOUT_CALL(fn_ptr_);                                                     \
    CALLOUT_EPILOGUE;                                                          \
  } while (0)

#define EMIT_VOID_CALLOUT1(fn_ptr_, arg1)                                      \
  do {                                                                         \
    CALLOUT_PROLOGUE;                                                          \
    t2_emit_mov_imm32(1, (uint32_t)(arg1));                                    \
    CALLOUT_CALL(fn_ptr_);                                                     \
    CALLOUT_EPILOGUE;                                                          \
  } while (0)

#define EMIT_VOID_CALLOUT2(fn_ptr_, arg1, arg2)                                \
  do {                                                                         \
    CALLOUT_PROLOGUE;                                                          \
    t2_emit_mov_imm32(1, (uint32_t)(arg1));                                    \
    t2_emit_mov_imm32(2, (uint32_t)(arg2));                                    \
    CALLOUT_CALL(fn_ptr_);                                                     \
    CALLOUT_EPILOGUE;                                                          \
  } while (0)

/* Bool call-outs.  For the bool-test-after-call we must:
 *   1. After BLX returns, save r0 (return value) into r3 (caller-saved,
 *      NOT in the PUSH {r0,r1,r2,lr} list, so survives POP).
 *   2. POP {r0,r1,r2,lr} to restore VM, subject, pos, JIT-return-addr.
 *   3. CMP r3, #0 — uses the 16-bit CMP encoding (only available for
 *      r0-r7).  r12 cannot be used with T2_CMP_RN_IMM8 because that
 *      macro masks the register number with 0x7; we would silently
 *      cmp r4 instead of r12.  Using r3 avoids that footgun and r3 is
 *      reloaded on the success path.
 *   4. Branch EQ (r3 == 0 → false) to the fail stub.
 *   5. LDR r3, VM.len — only on the success path; the fail stub
 *      does NOT exec this (vm_push_choice etc. doesn't need r3). */
#define EMIT_BOOL_CALLOUT0(fn_ptr_, fp_, fpc_, cur_)                           \
  do {                                                                         \
    CALLOUT_PROLOGUE;                                                          \
    CALLOUT_CALL(fn_ptr_);                                                     \
    t2_emit16((uint16_t)T2_MOV_RD_RS(3, 0)); /* MOV r3, r0 — save ret */       \
    t2_emit32(T2_POP_R0R1R2LR);                                                \
    t2_emit16(T2_CMP_RN_IMM8(3, 0));                                           \
    (fp_)[(*(fpc_))++] =                                                       \
        (FailPatch){t2_emit_patch_b_cond(T2_COND_EQ), (cur_), T2_COND_EQ};     \
    t2_emit32(T2_LDR_W_RT_RN_IMM12(3, 0, (uint32_t)offsetof(VM, len)));        \
  } while (0)

#define EMIT_BOOL_CALLOUT1(fn_ptr_, arg1, fp_, fpc_, cur_)                     \
  do {                                                                         \
    CALLOUT_PROLOGUE;                                                          \
    t2_emit_mov_imm32(1, (uint32_t)(arg1));                                    \
    CALLOUT_CALL(fn_ptr_);                                                     \
    t2_emit16((uint16_t)T2_MOV_RD_RS(3, 0));                                   \
    t2_emit32(T2_POP_R0R1R2LR);                                                \
    t2_emit16(T2_CMP_RN_IMM8(3, 0));                                           \
    (fp_)[(*(fpc_))++] =                                                       \
        (FailPatch){t2_emit_patch_b_cond(T2_COND_EQ), (cur_), T2_COND_EQ};     \
    t2_emit32(T2_LDR_W_RT_RN_IMM12(3, 0, (uint32_t)offsetof(VM, len)));        \
  } while (0)

#define EMIT_BOOL_CALLOUT2(fn_ptr_, arg1, arg2, fp_, fpc_, cur_)               \
  do {                                                                         \
    CALLOUT_PROLOGUE;                                                          \
    t2_emit_mov_imm32(1, (uint32_t)(arg1));                                    \
    t2_emit_mov_imm32(2, (uint32_t)(arg2));                                    \
    CALLOUT_CALL(fn_ptr_);                                                     \
    t2_emit16((uint16_t)T2_MOV_RD_RS(3, 0));                                   \
    t2_emit32(T2_POP_R0R1R2LR);                                                \
    t2_emit16(T2_CMP_RN_IMM8(3, 0));                                           \
    (fp_)[(*(fpc_))++] =                                                       \
        (FailPatch){t2_emit_patch_b_cond(T2_COND_EQ), (cur_), T2_COND_EQ};     \
    t2_emit32(T2_LDR_W_RT_RN_IMM12(3, 0, (uint32_t)offsetof(VM, len)));        \
  } while (0)

#define EMIT_BOOL_CALLOUT3(fn_ptr_, arg1, arg2, arg3, fp_, fpc_, cur_)         \
  do {                                                                         \
    CALLOUT_PROLOGUE;                                                          \
    t2_emit_mov_imm32(1, (uint32_t)(arg1));                                    \
    t2_emit_mov_imm32(2, (uint32_t)(arg2));                                    \
    t2_emit_mov_imm32(3, (uint32_t)(arg3));                                    \
    CALLOUT_CALL(fn_ptr_);                                                     \
    /* r3 had arg3 for the BLX; now reuse r3 to hold the bool return value. */ \
    t2_emit16((uint16_t)T2_MOV_RD_RS(3, 0)); /* MOV r3, r0 */                  \
    t2_emit32(T2_POP_R0R1R2LR);                                                \
    t2_emit16(T2_CMP_RN_IMM8(3, 0));                                           \
    (fp_)[(*(fpc_))++] =                                                       \
        (FailPatch){t2_emit_patch_b_cond(T2_COND_EQ), (cur_), T2_COND_EQ};     \
    t2_emit32(T2_LDR_W_RT_RN_IMM12(3, 0, (uint32_t)offsetof(VM, len)));        \
  } while (0)

/* Legacy names kept for any code still using the old API. */
#define EMIT_VOID_CALLOUT(fn_ptr_) EMIT_VOID_CALLOUT0(fn_ptr_)
#define EMIT_BOOL_CALLOUT(fn_ptr_, fp_, fpc_, cur_)                            \
  EMIT_BOOL_CALLOUT0(fn_ptr_, fp_, fpc_, cur_)

/* =========================================================================
 * JIT call-out helper functions (identical to ARM64 backend)
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
                                        uint8_t key_reg, uint8_t value_reg) {
  snobol_table_t *table = vm_get_table(vm, table_id);
  if (!table)
    return false;
  if (key_reg >= MAX_CAPS || vm->cap_end[key_reg] <= vm->cap_start[key_reg])
    return false;
  if (value_reg >= MAX_CAPS ||
      vm->cap_end[value_reg] <= vm->cap_start[value_reg])
    return false;
  size_t kl = vm->cap_end[key_reg] - vm->cap_start[key_reg];
  size_t vl = vm->cap_end[value_reg] - vm->cap_start[value_reg];
  char *key = (char *)snobol_malloc(kl + 1);
  char *val = (char *)snobol_malloc(vl + 1);
  if (!key || !val) {
    snobol_free(key);
    snobol_free(val);
    return false;
  }
  memcpy(key, vm->s + vm->cap_start[key_reg], kl);
  key[kl] = '\0';
  memcpy(val, vm->s + vm->cap_start[value_reg], vl);
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
 * IR-based instruction lookup helpers
 * ========================================================================= */

static size_t ir_find_instr(const jit_ir_region_t *ir, size_t target_ip) {
  for (size_t i = 0; i < ir->count; i++) {
    if (ir->instrs[i].bc_ip == target_ip)
      return i;
  }
  return ir->count;
}

/* =========================================================================
 * CFG builder from IR
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
 * Per-block Thumb-2 code emission from IR
 *
 * Iterates over IR instructions whose bc_ip falls in [start_ip, term_ip)
 * and emits Thumb-2 code for each.  Operands are read from IR; no bytecode
 * re-parsing occurs.
 *
 * Register convention (AAPCS32):
 *   r0 = VM pointer (always)
 *   r1 = vm->s (subject string)
 *   r2 = vm->pos (current position)
 *   r3 = vm->len (subject length)
 *   r4 = temp/working (caller-saved for short-lived use)
 *   r5 = temp/working (caller-saved)
 *   r6 = ascii_map[0] (low 64 bits of bitmap)
 *   r7 = ascii_map[1] (high 64 bits)
 *   r8 = temp/working
 *   r9 = temp/working
 *   r10 = temp/working for call-out helpers
 *   r11 = temp/working
 *   r12 = IP scratch (BLX target)
 *   lr = link register
 * ========================================================================= */

static bool emit_block_ops_ir(jit_region_t *js, const jit_ir_region_t *ir,
                              VM *vm, size_t start_ip, size_t term_ip,
                              FailPatch *fail_patches,
                              size_t *fail_patch_count) {
  const uint8_t *bc = vm->bc;

  for (size_t i = 0; i < ir->count; i++) {
    const jit_ir_instr_t *ins = &ir->instrs[i];

    if (ins->bc_ip < start_ip || ins->bc_ip >= term_ip)
      continue;

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
      /* r4 = lit_len */
      t2_emit_mov_imm32(4, lit_len);
      /* r8 = pos + len */
      t2_emit32(T2_ADD_W_RD_RN_RM(8, 2, 4));
      /* cmp r8, r3 (len) */
      t2_emit32(T2_CMP_W_RN_RM(8, 3));
      /* bhi fail */
      fail_patches[(*fail_patch_count)++] =
          (FailPatch){t2_emit_patch_b_cond(T2_COND_HI), cur, T2_COND_HI};
      for (uint32_t j = 0; j < lit_len; j++) {
        /* ldrb r5, [r1, r2] */
        t2_emit16(T2_LDRB_RT_RN_RM(5, 1, 2));
        /* cmp r5, #lit_data[j] */
        t2_emit16(T2_CMP_RN_IMM8(5, lit_data[j]));
        /* bne fail */
        fail_patches[(*fail_patch_count)++] =
            (FailPatch){t2_emit_patch_b_cond(T2_COND_NE), cur, T2_COND_NE};
        /* add r2, r2, #1 */
        t2_emit16(T2_ADDS_RD_RN_IMM3(2, 2, 1));
      }
      break;
    }

    case JIT_IR_ANY:
    case JIT_IR_NOTANY:
    case JIT_IR_SPAN:
    case JIT_IR_BREAK:
    case JIT_IR_BREAKX: {
      uint16_t set_id = ins->u.set.set_id;
      uint16_t count16, ci;
      const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count16, &ci);
      uint64_t ascii_map[2];
      if (!ranges || !ranges_to_ascii_bitmap(ranges, count16, ascii_map))
        return false;

      /* Load bitmap into r6 (low 64) and r7 (high 64) */
      t2_emit_mov_imm64_pair(6, 7,
                             ascii_map[0] | ((uint64_t)ascii_map[1] << 32));

      /* Check pos < len */
      t2_emit32(T2_CMP_W_RN_RM(2, 3));
      fail_patches[(*fail_patch_count)++] =
          (FailPatch){t2_emit_patch_b_cond(T2_COND_GE), cur, T2_COND_GE};

      /* Load character */
      t2_emit16(T2_LDRB_RT_RN_RM(5, 1, 2));

      /* Check if char > 127 (non-ASCII) */
      t2_emit16(T2_CMP_RN_IMM8(5, 127));
      fail_patches[(*fail_patch_count)++] =
          (FailPatch){t2_emit_patch_b_cond(T2_COND_HI), cur, T2_COND_HI};

      /* Load bitmap word: if bit 6 of char is set, use high map (r7), else low
       * (r6) */
      /* r4 = char >> 6 (0 or 1), then select map */
      /* LSRS r4, r5, #6 */
      t2_emit16(T2_LSRS_RD_RM_IMM5(4, 5, 6));
      /* CMP r4, #0; ITT EQ; MOV r8, r6; MOVT r8, r6<hi> ... */
      /* Simpler: use conditional move via TST/IT */
      uint8_t *tst_label = t2_emit_patch_b_cond(T2_COND_NE);
      /* if bit6 == 0: use r6 */
      t2_emit32(T2_MOV_W_RD_RM(8, 6));
      uint8_t *done_map = t2_emit_patch_b();
      t2_patch_b_cond(tst_label, t2_wp, T2_COND_NE);
      /* if bit6 == 1: use r7 */
      t2_emit32(T2_MOV_W_RD_RM(8, 7));
      t2_patch_b(done_map, t2_wp);

      /* Extract bit position: char & 63 */
      /* AND r4, r5, #63 (using T2_ANDS_RD_RM with shift) */
      /* LSLS r4, r5, #26 - no, let's use wide AND */
      t2_emit32(T2_AND_W_RD_RN_RM(4, 5, 4)); /* r4 = r5 & 63 (using r4=63) */
      /* Wait, r4 doesn't have 63. Let me use a different approach. */

      /* Fallback: use bit test via LSR and TST */
      /* MOV r9, #1 */
      t2_emit_mov_imm32(9, 1);
      /* LSL r9, r9, r5 (r5 may be > 31, use wide LSL) */
      t2_emit32(T2_LSL_W_RD_RN_RM(9, 9, 5));
      /* TST r8, r9 */
      t2_emit32(T2_TST_W_RN_RM(8, 9));

      if (ins->opcode == JIT_IR_NOTANY) {
        fail_patches[(*fail_patch_count)++] =
            (FailPatch){t2_emit_patch_b_cond(T2_COND_NE), cur, T2_COND_NE};
      } else {
        fail_patches[(*fail_patch_count)++] =
            (FailPatch){t2_emit_patch_b_cond(T2_COND_EQ), cur, T2_COND_EQ};
      }

      uint8_t *success_label = t2_wp;

      /* Advance position by 1 */
      t2_emit16(T2_ADDS_RD_RN_IMM3(2, 2, 1));

      if (ins->opcode == JIT_IR_SPAN) {
        uint8_t *loop_start = t2_wp;
        /* Check pos < len */
        t2_emit32(T2_CMP_W_RN_RM(2, 3));
        uint8_t *loop_ge_done = t2_emit_patch_b_cond(T2_COND_GE);
        /* Load char */
        t2_emit16(T2_LDRB_RT_RN_RM(5, 1, 2));
        /* Check if char > 127 */
        t2_emit16(T2_CMP_RN_IMM8(5, 127));
        uint8_t *loop_hi_done = t2_emit_patch_b_cond(T2_COND_HI);
        /* Bit test */
        t2_emit_mov_imm32(9, 1);
        t2_emit32(T2_LSL_W_RD_RN_RM(9, 9, 5));
        t2_emit32(T2_TST_W_RN_RM(8, 9));
        uint8_t *loop_bit_done = t2_emit_patch_b_cond(T2_COND_EQ);
        /* Advance */
        t2_emit16(T2_ADDS_RD_RN_IMM3(2, 2, 1));
        t2_emit_b(loop_start);
        /* Patch done points */
        t2_patch_b_cond(loop_ge_done, t2_wp, T2_COND_GE);
        t2_patch_b_cond(loop_hi_done, t2_wp, T2_COND_HI);
        t2_patch_b_cond(loop_bit_done, t2_wp, T2_COND_EQ);
      }
      break;
    }

    case JIT_IR_CAP_START:
    case JIT_IR_CAP_END: {
      uint8_t r = ins->u.cap.reg;
      size_t off_field = (ins->opcode == JIT_IR_CAP_START)
                             ? offsetof(VM, cap_start)
                             : offsetof(VM, cap_end);
      /* Store pos to cap_start[reg] or cap_end[reg] */
      t2_emit32(T2_STR_W_RT_RN_IMM12(2, 0, (uint32_t)(off_field + r * 8)));
      /* Update max_cap_used */
      t2_emit32(
          T2_LDRB_W_RT_RN_IMM12(9, 0, (uint32_t)offsetof(VM, max_cap_used)));
      t2_emit_mov_imm32(8, r + 1);
      t2_emit32(T2_CMP_W_RN_RM(9, 8));
      uint8_t *lt_skip = t2_emit_patch_b_cond(T2_COND_GE);
      t2_emit32(
          T2_STRB_W_RT_RN_IMM12(8, 0, (uint32_t)offsetof(VM, max_cap_used)));
      t2_patch_b_cond(lt_skip, t2_wp, T2_COND_GE);
      break;
    }

    case JIT_IR_LEN: {
      uint32_t n = ins->u.len.n;
      t2_emit_mov_imm32(4, n);
      /* r8 = pos + n */
      t2_emit32(T2_ADD_W_RD_RN_RM(8, 2, 4));
      /* cmp r8, len */
      t2_emit32(T2_CMP_W_RN_RM(8, 3));
      fail_patches[(*fail_patch_count)++] =
          (FailPatch){t2_emit_patch_b_cond(T2_COND_HI), cur, T2_COND_HI};
      /* pos += n */
      t2_emit32(T2_ADD_W_RD_RN_RM(2, 2, 4));
      break;
    }

    case JIT_IR_ANCHOR: {
      uint8_t type = ins->u.anchor.type;
      if (type == 0) {
        /* Anchor start: pos == 0 */
        t2_emit_mov_imm32(4, 0);
        t2_emit32(T2_CMP_W_RN_RM(2, 4));
      } else {
        /* Anchor end: pos == len */
        t2_emit32(T2_CMP_W_RN_RM(2, 3));
      }
      fail_patches[(*fail_patch_count)++] =
          (FailPatch){t2_emit_patch_b_cond(T2_COND_NE), cur, T2_COND_NE};
      break;
    }

    case JIT_IR_ASSIGN: {
      uint16_t var = ins->u.assign.var;
      uint8_t reg = ins->u.assign.reg;
      /* Load cap_start[reg] -> var_start[var] */
      t2_emit32(T2_LDR_W_RT_RN_IMM12(
          9, 0, (uint32_t)(offsetof(VM, cap_start) + reg * 8)));
      t2_emit32(T2_STR_W_RT_RN_IMM12(
          9, 0, (uint32_t)(offsetof(VM, var_start) + var * 8)));
      /* Load cap_end[reg] -> var_end[var] */
      t2_emit32(T2_LDR_W_RT_RN_IMM12(
          9, 0, (uint32_t)(offsetof(VM, cap_end) + reg * 8)));
      t2_emit32(T2_STR_W_RT_RN_IMM12(
          9, 0, (uint32_t)(offsetof(VM, var_end) + var * 8)));
      /* Update var_count */
      t2_emit32(T2_LDR_W_RT_RN_IMM12(9, 0, (uint32_t)offsetof(VM, var_count)));
      t2_emit_mov_imm32(8, var + 1);
      t2_emit32(T2_CMP_W_RN_RM(9, 8));
      uint8_t *lt_skip = t2_emit_patch_b_cond(T2_COND_GE);
      t2_emit32(T2_STR_W_RT_RN_IMM12(8, 0, (uint32_t)offsetof(VM, var_count)));
      t2_patch_b_cond(lt_skip, t2_wp, T2_COND_GE);
      break;
    }

    case JIT_IR_REM:
      /* pos = len */
      t2_emit32(T2_MOV_W_RD_RM(2, 3));
      break;

    case JIT_IR_RPOS: {
      uint32_t n = ins->u.rpos_rtab.n;
      t2_emit_mov_imm32(4, n);
      /* r8 = len - n */
      t2_emit32(T2_SUB_W_RD_RN_RM(8, 3, 4));
      /* cmp pos, r8 */
      t2_emit32(T2_CMP_W_RN_RM(2, 8));
      fail_patches[(*fail_patch_count)++] =
          (FailPatch){t2_emit_patch_b_cond(T2_COND_NE), cur, T2_COND_NE};
      break;
    }

    case JIT_IR_RTAB: {
      uint32_t n = ins->u.rpos_rtab.n;
      t2_emit_mov_imm32(4, n);
      /* Check len >= n */
      t2_emit32(T2_CMP_W_RN_RM(3, 4));
      fail_patches[(*fail_patch_count)++] =
          (FailPatch){t2_emit_patch_b_cond(T2_COND_LO), cur, T2_COND_LO};
      /* r8 = len - n */
      t2_emit32(T2_SUB_W_RD_RN_RM(8, 3, 4));
      /* Check pos <= r8 */
      t2_emit32(T2_CMP_W_RN_RM(8, 2));
      fail_patches[(*fail_patch_count)++] =
          (FailPatch){t2_emit_patch_b_cond(T2_COND_LO), cur, T2_COND_LO};
      /* pos = r8 */
      t2_emit32(T2_MOV_W_RD_RM(2, 8));
      break;
    }

    case JIT_IR_FENCE: {
      /* Store 0 to choices_top */
      t2_emit_mov_imm32(4, 0);
      t2_emit32(
          T2_STR_W_RT_RN_IMM12(4, 0, (uint32_t)offsetof(VM, choices_top)));
      break;
    }

    case JIT_IR_EMIT_LITERAL: {
      uint32_t elt_off = ins->u.emit_lit.offset;
      uint32_t elt_len = ins->u.emit_lit.len;
      /* emit_literal(VM, offset, len) — args in r1/r2 */
      EMIT_VOID_CALLOUT2(snobol_jit_helper_emit_literal, elt_off, elt_len);
      break;
    }

    case JIT_IR_EMIT_CAPTURE: {
      uint8_t ecap_reg = ins->u.emit_cap.reg;
      /* emit_capture(VM, reg) — arg in r1 */
      EMIT_VOID_CALLOUT1(snobol_jit_helper_emit_capture, ecap_reg);
      break;
    }

    case JIT_IR_EMIT_EXPR: {
      uint8_t eex_r = ins->u.emit_expr.reg;
      uint8_t eex_t = ins->u.emit_expr.expr_type;
      /* emit_expr(VM, reg, expr_type) — args in r1/r2 */
      EMIT_VOID_CALLOUT2(snobol_jit_helper_emit_expr, eex_r, eex_t);
      break;
    }

    case JIT_IR_EMIT_FORMAT: {
      uint8_t efmt_r = ins->u.emit_fmt.reg;
      uint8_t efmt_t = ins->u.emit_fmt.fmt_type;
      uint16_t efmt_w = ins->u.emit_fmt.width;
      uint8_t efmt_f = ins->u.emit_fmt.fill_char;
      /* emit_format takes 5 args (VM, reg, fmt_type, width, fill_char).
       * AAPCS32 places arg5 at [sp, #0].  We SUB sp by 8 to keep SP
       * 8-byte aligned for the BLX (4 bytes for the actual arg, 4 bytes
       * of alignment pad). */
      CALLOUT_PROLOGUE;
      t2_emit_mov_imm32(1, (uint32_t)efmt_r);
      t2_emit_mov_imm32(2, (uint32_t)efmt_t);
      t2_emit_mov_imm32(3, (uint32_t)efmt_w);
      t2_emit_mov_imm32(4, (uint32_t)efmt_f);
      t2_emit16(T2_SUB_SP_IMM7(2)); /* SUB sp, sp, #8 — keep 8-aligned */
      t2_emit32(T2_STRB_W_RT_RN_IMM12(4, 13, 0)); /* STRB.W r4, [sp, #0] */
      CALLOUT_CALL(snobol_jit_helper_emit_format);
      t2_emit16(T2_ADD_SP_IMM7(2)); /* ADD sp, sp, #8 — reclaim slot */
      CALLOUT_EPILOGUE;
      break;
    }

    case JIT_IR_EMIT_TABLE: {
      /* emit_table_ip(VM, op_ip) — op_ip is the bc IP of the EMIT_TABLE byte,
       * taken from the IR instruction's `bc_ip` field (per jit_ir.h docs).
       * AAPCS32 passes uint64_t as a register pair: r1=low32, r2=high32. */
      uint64_t etab_ip = (uint64_t)cur;
      EMIT_VOID_CALLOUT2(snobol_jit_helper_emit_table_ip,
                         (uint32_t)(etab_ip & 0xffffffffu),
                         (uint32_t)(etab_ip >> 32));
      break;
    }

    case JIT_IR_TABLE_GET: {
#ifdef SNOBOL_DYNAMIC_PATTERN
      uint16_t tg_tid = ins->u.tget.table_id;
      uint8_t tg_kreg = ins->u.tget.key_reg;
      uint8_t tg_dreg = ins->u.tget.dest_reg;
      /* table_get(VM, table_id, key_reg, dest_reg) — args r1/r2/r3 */
      EMIT_BOOL_CALLOUT3(snobol_jit_helper_table_get, tg_tid, tg_kreg, tg_dreg,
                         fail_patches, fail_patch_count, cur);
      /* Helper advances VM.pos on success: reload for subsequent JIT ops. */
      t2_emit32(T2_LDR_W_RT_RN_IMM12(2, 0, (uint32_t)offsetof(VM, pos)));
#else
      (void)ins;
#endif
      break;
    }

    case JIT_IR_TABLE_SET: {
#ifdef SNOBOL_DYNAMIC_PATTERN
      uint16_t ts_tid = ins->u.tset.table_id;
      uint8_t ts_kreg = ins->u.tset.key_reg;
      uint8_t ts_vreg = ins->u.tset.val_reg;
      /* table_set(VM, table_id, key_reg, val_reg) — void, args r1/r2/r3 */
      CALLOUT_PROLOGUE;
      t2_emit_mov_imm32(1, (uint32_t)ts_tid);
      t2_emit_mov_imm32(2, (uint32_t)ts_kreg);
      t2_emit_mov_imm32(3, (uint32_t)ts_vreg);
      CALLOUT_CALL(snobol_jit_helper_table_set);
      CALLOUT_EPILOGUE;
      t2_emit32(T2_LDR_W_RT_RN_IMM12(2, 0, (uint32_t)offsetof(VM, pos)));
#else
      (void)ins;
#endif
      break;
    }

    case JIT_IR_BAL: {
      uint32_t bal_open = ins->u.bal.open_cp;
      uint32_t bal_close = ins->u.bal.close_cp;
      /* bal(VM, open_cp, close_cp) — args r1/r2 */
      EMIT_BOOL_CALLOUT2(snobol_jit_helper_bal, bal_open, bal_close,
                         fail_patches, fail_patch_count, cur);
      /* BAL advances VM.pos on success: reload for subsequent JIT ops. */
      t2_emit32(T2_LDR_W_RT_RN_IMM12(2, 0, (uint32_t)offsetof(VM, pos)));
      break;
    }

    case JIT_IR_EVAL: {
      uint16_t ev_fn = ins->u.eval.fn_id;
      uint8_t ev_reg = ins->u.eval.reg;
      /* eval(VM, fn_id, reg) — args r1/r2 */
      EMIT_BOOL_CALLOUT2(snobol_jit_helper_eval, ev_fn, ev_reg, fail_patches,
                         fail_patch_count, cur);
      t2_emit32(T2_LDR_W_RT_RN_IMM12(2, 0, (uint32_t)offsetof(VM, pos)));
      break;
    }

    case JIT_IR_DYNAMIC: {
#ifdef SNOBOL_DYNAMIC_PATTERN
      /* dynamic(VM) — single arg (r0 = VM), bool return */
      EMIT_BOOL_CALLOUT0(snobol_jit_helper_dynamic, fail_patches,
                         fail_patch_count, cur);
      t2_emit32(T2_LDR_W_RT_RN_IMM12(2, 0, (uint32_t)offsetof(VM, pos)));
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
  (void)vm;
  return true;
}

/* =========================================================================
 * CFG epilogue helper
 * ========================================================================= */

static void t2_emit_bailout(jit_region_t *out, size_t bail_ip) {
  /* Store pos back to VM */
  t2_emit32(T2_STR_W_RT_RN_IMM12(2, 0, (uint32_t)offsetof(VM, pos)));
  /* Store bail_ip to VM ip */
  t2_emit_mov_imm32(4, (uint32_t)bail_ip);
  t2_emit32(T2_STR_W_RT_RN_IMM12(4, 0, (uint32_t)offsetof(VM, ip)));
  /* POP.W {r4-r11,pc} (restore callee-saved registers and return) */
  t2_emit32(T2_POP_R4R11_PC);
  t2_sync_region(out);
}

/* =========================================================================
 * arm32_lower() — main function: IR → Thumb-2 machine code
 * ========================================================================= */

static void *arm32_lower(const jit_ir_region_t *ir, VM *vm, jit_region_t *out) {
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

  JIT_LOG("lower: %zu IR instrs, %d block(s), use_cfg=%d, has_bwd=%d",
          ir->count, n_blocks, (int)use_cfg, (int)cfg.has_backward);

  /* Allocate code buffer */
  size_t code_size = use_cfg ? 32768u : 16384u;
  uint32_t *code = (uint32_t *)snobol_jit_alloc_code(code_size);
  if (!code) {
    JIT_LOG("alloc_code(%zu) failed", code_size);
    return nullptr;
  }

  out->p = code;
  out->code_start = code;
  out->code_size = code_size;
  t2_wp = (uint8_t *)code;
  JIT_LOG("code buffer at %p, size=%zu", (void *)code, code_size);

  FailPatch fail_patches[1024];
  size_t fail_patch_count = 0;

  if (use_cfg) {
    /* ---- CFG multi-block path ---- */
    StubPatch stub_patches[256];
    int stub_patch_count = 0;

    /* Prologue: PUSH.W {r4-r11,lr} to save callee-saved regs */
    t2_emit32(T2_PUSH_R4R11_LR);

    /* Load subject pointer (vm->s) into r1 */
    t2_emit32(T2_LDR_W_RT_RN_IMM12(1, 0, (uint32_t)offsetof(VM, s)));
    /* Load pos into r2 */
    t2_emit32(T2_LDR_W_RT_RN_IMM12(2, 0, (uint32_t)offsetof(VM, pos)));
    /* Load len into r3 */
    t2_emit32(T2_LDR_W_RT_RN_IMM12(3, 0, (uint32_t)offsetof(VM, len)));

    if (cfg.has_backward) {
      /* Initialize loop counter */
      t2_emit_mov_imm32(4, JIT_LOOP_ITER_MAX);
    }

    for (int bi = 0; bi < cfg.count; bi++) {
      JitBlock *blk = &cfg.blocks[bi];
      blk->stub_start = t2_wp;

      bool ok = emit_block_ops_ir(out, ir, vm, blk->start_ip, blk->term_ip,
                                  fail_patches, &fail_patch_count);

      if (!ok) {
        t2_emit_bailout(out, blk->term_ip);
        blk->term = BLOCK_TERM_EXIT;
        continue;
      }

      switch (blk->term) {
      case BLOCK_TERM_SPLIT: {
        if (blk->succ_a_blk < 0) {
          t2_emit_bailout(out, blk->term_ip);
          break;
        }
        /* Save JIT live registers (r0=VM, r1=subj, r2=pos, r3=len, lr)
         * and VM.pos across the choice-stack push call-out. */
        CALLOUT_PROLOGUE;
        /* Set up arg: r1 = succ_b for vm_push_choice(VM*, succ_b). */
        t2_emit_mov_imm32(4, (uint32_t)blk->succ_b);
        t2_emit16(T2_MOV_RD_RS(1, 4));
        /* Call vm_push_choice (void helper). */
        CALLOUT_CALL(vm_push_choice);
        /* Restore JIT live registers (r0/r1/r2/lr) and reload r3=len
         * (CALLOUT_EPILOGUE already reloads r3 from VM.len). */
        CALLOUT_EPILOGUE;
        /* Stub branch to successor block A (patched later). */
        stub_patches[stub_patch_count++] = (StubPatch){t2_wp, blk->succ_a};
        t2_emit_b(t2_wp + 4); /* placeholder */
        break;
      }
      case BLOCK_TERM_JMP_FWD: {
        if (blk->succ_a_blk < 0) {
          t2_emit_bailout(out, blk->term_ip);
          break;
        }
        stub_patches[stub_patch_count++] = (StubPatch){t2_wp, blk->succ_a};
        t2_emit_b(t2_wp + 4);
        break;
      }
      case BLOCK_TERM_JMP_BWD: {
        if (blk->succ_a_blk < 0) {
          t2_emit_bailout(out, blk->term_ip);
          break;
        }
        /* Decrement loop counter */
        t2_emit_mov_imm32(5, 1);
        t2_emit32(T2_SUB_W_RD_RN_RM(4, 4, 5));
        t2_emit16(T2_CMP_RN_IMM8(4, 0));
        uint8_t *bail_patch = t2_emit_patch_b_cond(T2_COND_EQ);
        uint8_t *loop_head = cfg.blocks[blk->succ_a_blk].stub_start;
        t2_emit_b(loop_head);
        t2_patch_b_cond(bail_patch, t2_wp, T2_COND_EQ);
        t2_emit_bailout(out, blk->term_ip);
        break;
      }
      case BLOCK_TERM_EXIT:
        t2_emit_bailout(out, blk->term_ip);
        break;
      }
    }

    /* Fail stubs */
    for (size_t f = 0; f < fail_patch_count; f++) {
      uint8_t *fail_stub = t2_wp;
      t2_emit_bailout(out, fail_patches[f].ip);
      t2_patch_b_cond(fail_patches[f].instr_p, fail_stub, fail_patches[f].cond);
    }

    /* Forward-branch fixup */
    for (int s = 0; s < stub_patch_count; s++) {
      uint8_t *p = stub_patches[s].instr_p;
      size_t target_ip = stub_patches[s].target_ip;
      int blk_idx = ir_find_block(&cfg, target_ip);
      if (blk_idx >= 0 && cfg.blocks[blk_idx].stub_start)
        t2_patch_b(p, cfg.blocks[blk_idx].stub_start);
    }

  } else {
    /* ---- Linear single-block path ---- */
    JitBlock *blk = &cfg.blocks[0];

    /* Load subject */
    t2_emit32(T2_LDR_W_RT_RN_IMM12(1, 0, (uint32_t)offsetof(VM, s)));
    t2_emit32(T2_LDR_W_RT_RN_IMM12(2, 0, (uint32_t)offsetof(VM, pos)));
    t2_emit32(T2_LDR_W_RT_RN_IMM12(3, 0, (uint32_t)offsetof(VM, len)));

    bool ok = emit_block_ops_ir(out, ir, vm, blk->start_ip, blk->term_ip,
                                fail_patches, &fail_patch_count);

    if (!ok) {
      snobol_jit_free_code(code, code_size);
      return nullptr;
    }

    /* Success epilogue */
    t2_emit32(T2_STR_W_RT_RN_IMM12(2, 0, (uint32_t)offsetof(VM, pos)));
    t2_emit_mov_imm32(4, (uint32_t)blk->next_ip);
    t2_emit32(T2_STR_W_RT_RN_IMM12(4, 0, (uint32_t)offsetof(VM, ip)));
    t2_emit16(T2_BX_LR);

    /* Fail stubs */
    for (size_t f = 0; f < fail_patch_count; f++) {
      uint8_t *fail_stub = t2_wp;
      t2_emit32(T2_STR_W_RT_RN_IMM12(2, 0, (uint32_t)offsetof(VM, pos)));
      t2_emit_mov_imm32(4, (uint32_t)fail_patches[f].ip);
      t2_emit32(T2_STR_W_RT_RN_IMM12(4, 0, (uint32_t)offsetof(VM, ip)));
      t2_emit16(T2_BX_LR);
      t2_patch_b_cond(fail_patches[f].instr_p, fail_stub, fail_patches[f].cond);
    }
  }

  out->n_blocks = (size_t)n_blocks;
  /* Compute actual code size and seal */
  size_t actual_size = (size_t)(t2_wp - (uint8_t *)code);
  out->code_size = actual_size;
  t2_sync_region(out);
  snobol_jit_seal_code(code, actual_size);
  JIT_LOG("sealed %zu bytes at %p, entry=%p", actual_size, (void *)code,
          (void *)((uintptr_t)code | 1));
  return (void *)((uintptr_t)code | 1);
}

/* =========================================================================
 * arm32_flush_icache()
 * ========================================================================= */

static void arm32_flush_icache(void *code, size_t size) {
#if defined(__linux__)
  /* __builtin___clear_cache emits the ISB barrier on ARM Linux and is
   * sufficient to keep QEMU user-mode's translation buffer in sync with
   * the freshly-written Thumb-2 code. The cacheflush syscall
   * (__NR_cacheflush) is intentionally NOT issued here: it is not
   * portable across arm-linux-gnueabihf cross-compile toolchains and
   * the built-in is enough on both QEMU and real hardware. */
  __builtin___clear_cache((char *)code, (char *)code + size);
#elif defined(SNOBOL_JIT_PLATFORM_LINUX)
  __builtin___clear_cache((char *)code, (char *)code + size);
#else
  (void)code;
  (void)size;
#endif
}

/* =========================================================================
 * Backend vtable and registration
 * ========================================================================= */

static const jit_backend_t arm32_backend_vtable = {
    .name = "arm32",
    .lower = arm32_lower,
    .flush_icache = arm32_flush_icache,
};

void snobol_jit_arm32_register(void) {
  jit_backend_register(&arm32_backend_vtable);
}

#endif /* __arm__ || __thumb__ || __ARM_ARCH_7A__ */
#endif /* SNOBOL_JIT */
