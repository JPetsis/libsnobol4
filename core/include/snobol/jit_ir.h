/**
 * @file jit_ir.h
 * @brief Architecture-neutral intermediate representation (IR) for the SNOBOL4
 * micro-JIT.
 *
 * The IR sits between the VM bytecode lifter and the target-specific code
 * emitter. Pipeline: VM bytecodes → [Lifter] → jit_ir_region_t → [Optimiser
 * passes] → [Backend lowerer] → machine code
 *
 * Design decisions:
 *  - Linear, flat instruction array (not SSA) — sufficient for the RISC-like
 * targets.
 *  - Virtual registers encoded as uint8_t indices (max 256 per region).
 *  - All operands are pre-decoded by the lifter; the backend must NOT re-parse
 * raw bytecode.
 *  - Memory: IR arrays are heap-allocated and freed immediately after the
 * backend lowers them.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Forward-declare VM to avoid circular dependency.  The lifter needs the full
 * VM definition but we keep the header dependency explicit. */
#ifdef SNOBOL_JIT
#include "snobol/vm.h"
#endif

/* -------------------------------------------------------------------------
 * IR opcodes — one entry per VM opcode
 * ------------------------------------------------------------------------- */
typedef enum {
  /* jit-compiled inline opcodes */
  JIT_IR_NOP = 0,
  JIT_IR_ACCEPT,
  JIT_IR_FAIL,
  JIT_IR_JMP,
  JIT_IR_SPLIT,
  JIT_IR_LIT,
  JIT_IR_ANY,
  JIT_IR_NOTANY,
  JIT_IR_SPAN,
  JIT_IR_BREAK,
  JIT_IR_BREAKX,
  JIT_IR_LEN,
  JIT_IR_ANCHOR,
  JIT_IR_CAP_START,
  JIT_IR_CAP_END,
  JIT_IR_ASSIGN,
  JIT_IR_REPEAT_INIT,
  JIT_IR_REPEAT_STEP,
  JIT_IR_REM,
  JIT_IR_RPOS,
  JIT_IR_RTAB,
  JIT_IR_POS,
  JIT_IR_TAB,
  JIT_IR_ABORT,
  JIT_IR_SUCCEED,
  JIT_IR_FENCE,
  JIT_IR_GOTO,
  JIT_IR_GOTO_F,
  /* call-out opcodes */
  JIT_IR_EMIT_LITERAL,
  JIT_IR_EMIT_CAPTURE,
  JIT_IR_EMIT_EXPR,
  JIT_IR_EMIT_FORMAT,
  JIT_IR_EMIT_TABLE,
  JIT_IR_TABLE_GET,
  JIT_IR_TABLE_SET,
  JIT_IR_BAL,
  JIT_IR_EVAL,
  JIT_IR_DYNAMIC,
  /* pseudo opcodes (no code emitted) */
  JIT_IR_LABEL,
  JIT_IR_DYNAMIC_DEF,
  /* internal: copy for copy-propagation analysis */
  JIT_IR_COPY,

  JIT_IR_OPCODE_COUNT
} jit_ir_opcode_t;

/* -------------------------------------------------------------------------
 * IR instruction flags
 * ------------------------------------------------------------------------- */
#define JIT_IR_FLAG_SIDE_EFFECT                                                \
  0x01u /**< Instruction has observable side effects */
#define JIT_IR_FLAG_CALLOUT                                                    \
  0x02u /**< Instruction performs a C helper call-out */
#define JIT_IR_FLAG_PURE                                                       \
  0x04u /**< Instruction is pure; removable if dst unused */
#define JIT_IR_FLAG_DEAD 0x08u   /**< Marked dead by DCE pass */
#define JIT_IR_FLAG_PSEUDO 0x10u /**< Pseudo-op: no code emitted */

/* Virtual-register sentinel: instruction has no output */
#define JIT_IR_VREG_NONE 0u
#define JIT_IR_VREG_MAX 255u

/* -------------------------------------------------------------------------
 * IR instruction
 *
 * All operand VALUES are pre-decoded by the lifter.  The backend must use
 * these fields rather than re-parsing the original bytecode stream.
 * bc_ip is retained for:
 *   1. Debug dumps (instruction provenance)
 *   2. As the compile-time IP passed to runtime helpers (e.g. emit_table_ip)
 * ------------------------------------------------------------------------- */
typedef struct {
  jit_ir_opcode_t opcode; /**< IR opcode */
  uint8_t flags;          /**< JIT_IR_FLAG_* bitmask */
  uint8_t
      dst_reg; /**< Output virtual register (JIT_IR_VREG_NONE = no output) */
  uint8_t src_reg[2]; /**< Input virtual registers ([0],[1]) */
  uint8_t _pad;       /**< Alignment padding */
  size_t bc_ip;       /**< Original bytecode offset of this instruction */

  /** Pre-decoded operands — union interpretation depends on opcode. */
  union {
    /* JIT_IR_LIT: match literal bytes */
    struct {
      const uint8_t *data;
      uint32_t len;
    } lit;

    /* JIT_IR_ANY / NOTANY / SPAN / BREAK / BREAKX: character-set match */
    struct {
      uint16_t set_id;
    } set;

    /* JIT_IR_CAP_START / CAP_END */
    struct {
      uint8_t reg;
    } cap;

    /* JIT_IR_ASSIGN */
    struct {
      uint16_t var;
      uint8_t reg;
    } assign;

    /* JIT_IR_LEN */
    struct {
      uint32_t n;
    } len;

    /* JIT_IR_ANCHOR */
    struct {
      uint8_t type;
    } anchor;

    /* JIT_IR_SPLIT: fork with two successors */
    struct {
      size_t target_a;
      size_t target_b;
    } split;

    /* JIT_IR_JMP / JIT_IR_GOTO: unconditional branch */
    struct {
      size_t target;
    } jmp;

    /* JIT_IR_GOTO_F: conditional goto (label_id retained for debugging) */
    struct {
      uint16_t label_id;
      size_t target;
    } goto_f;

    /* JIT_IR_RPOS, JIT_IR_RTAB */
    struct {
      uint32_t n;
    } rpos_rtab;

    /* JIT_IR_LABEL */
    struct {
      uint16_t label_id;
    } label;

    /* JIT_IR_EMIT_LITERAL */
    struct {
      const uint8_t *data;
      uint32_t len;
      uint32_t offset;
    } emit_lit;

    /* JIT_IR_EMIT_CAPTURE */
    struct {
      uint8_t reg;
    } emit_cap;

    /* JIT_IR_EMIT_EXPR */
    struct {
      uint8_t reg;
      uint8_t expr_type;
    } emit_expr;

    /* JIT_IR_EMIT_FORMAT */
    struct {
      uint8_t reg;
      uint8_t fmt_type;
      uint16_t width;
      uint8_t fill_char;
    } emit_fmt;

    /* JIT_IR_EMIT_TABLE: bc_ip is passed to runtime helper at run-time */
    /* (no extra fields needed — bc_ip in the outer struct is the operand) */

    /* JIT_IR_TABLE_GET */
    struct {
      uint16_t table_id;
      uint8_t key_reg;
      uint8_t dest_reg;
    } tget;

    /* JIT_IR_TABLE_SET */
    struct {
      uint16_t table_id;
      uint8_t key_reg;
      uint8_t val_reg;
    } tset;

    /* JIT_IR_BAL */
    struct {
      uint32_t open_cp;
      uint32_t close_cp;
    } bal;

    /* JIT_IR_EVAL */
    struct {
      uint16_t fn_id;
      uint8_t reg;
    } eval;

    /* JIT_IR_COPY: copy propagation — dst_reg = src_reg[0] */
    /* (uses src_reg[0] in the outer struct) */
  } u;
} jit_ir_instr_t;

/* -------------------------------------------------------------------------
 * IR region — owns the instruction array for one compiled region
 * ------------------------------------------------------------------------- */
typedef struct {
  jit_ir_instr_t *instrs; /**< Heap-allocated instruction array */
  size_t count;           /**< Number of valid instructions */
  size_t capacity;        /**< Allocated slots */
  uint16_t
      vreg_next; /**< Next virtual register to allocate (1-based; 0 = NONE) */
  uint16_t use_count[256]; /**< Reference count per virtual register */
  bool non_compilable;     /**< True if vreg limit exceeded (>256) */
} jit_ir_region_t;

/* -------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/** Allocate a new empty IR region. Returns NULL on allocation failure. */
jit_ir_region_t *jit_ir_region_new(void);

/** Free an IR region and all owned instruction storage. */
void jit_ir_region_free(jit_ir_region_t *r);

/**
 * Append an instruction to the region.
 * @return true on success; false if region is non_compilable or OOM.
 */
bool jit_ir_append(jit_ir_region_t *r, jit_ir_opcode_t op, uint8_t flags,
                   size_t bc_ip, uint8_t dst_reg, uint8_t src0, uint8_t src1);

/**
 * Allocate the next virtual register.
 * If the limit (256) is exceeded, marks the region non_compilable and logs
 * a warning, then returns JIT_IR_VREG_NONE.
 */
uint8_t jit_ir_alloc_vreg(jit_ir_region_t *r);

/** Increment the use-count of virtual register @p reg. */
void jit_ir_inc_use(jit_ir_region_t *r, uint8_t reg);

/* -------------------------------------------------------------------------
 * Optimiser passes
 * ------------------------------------------------------------------------- */

/**
 * Dead-code elimination (DCE) pass.
 *
 * Marks instructions as JIT_IR_FLAG_DEAD when:
 *  - The instruction's output virtual register (dst_reg) has use_count == 0.
 *  - AND the instruction does NOT have JIT_IR_FLAG_SIDE_EFFECT.
 *
 * Dead instructions are compacted out of the array in place.
 */
void jit_ir_dce(jit_ir_region_t *r);

/**
 * Copy-propagation pass.
 *
 * For each JIT_IR_COPY instruction (dst_reg = src_reg[0]):
 *  - Replace all downstream uses of dst_reg with src_reg[0].
 *  - Remove the COPY instruction (it becomes dead and DCE cleans it).
 */
void jit_ir_copy_propagation(jit_ir_region_t *r);

/* -------------------------------------------------------------------------
 * Debug dump
 * ------------------------------------------------------------------------- */

/**
 * Dump a human-readable representation of the IR region to @p out.
 * Called automatically when SNOBOL_JIT_DUMP_IR=1 is set in the environment.
 */
void jit_ir_dump(const jit_ir_region_t *r, FILE *out);

/* -------------------------------------------------------------------------
 * Lifter: VM bytecode → IR
 * ------------------------------------------------------------------------- */
#ifdef SNOBOL_JIT
/**
 * Lift a compiled region starting at @p start_ip to architecture-neutral IR.
 *
 * The lifter performs a linear scan of the bytecode strictly following
 * forward jumps (same as the existing pass-1 in snobol_jit_compile).
 * All operands are pre-decoded into the IR instruction's union fields so the
 * backend can lower without re-parsing the bytecode stream.
 *
 * @param vm        VM state (bytecode, charclass tables, label offsets).
 * @param start_ip  Bytecode offset where the compiled region begins.
 * @return          Heap-allocated IR region; caller must free with
 * jit_ir_region_free(). Returns NULL on allocation failure.
 */
jit_ir_region_t *jit_ir_lift_region(const VM *vm, size_t start_ip);
#endif /* SNOBOL_JIT */
