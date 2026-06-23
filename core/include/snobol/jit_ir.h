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
 *  - Linear, flat instruction array with SSA properties (each virtual register
 *    is defined exactly once).
 *  - Phi nodes (JIT_IR_PHI) are used at CFG join points where a value may
 *    arrive from multiple predecessors.
 *  - Virtual registers encoded as uint8_t indices (max 256 per region).
 *  - All operands are pre-decoded by the lifter; the backend must NOT re-parse
 *    raw bytecode.
 *  - Memory: IR arrays are heap-allocated and freed immediately after the
 *    backend lowers them.
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

  /* SSA phi node (µ-op): dst_reg = phi(src_reg[0], src_reg[1], …).
   * Inserted at CFG join points during SSA construction. */
  JIT_IR_PHI,

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
#define JIT_IR_FLAG_DEAD 0x08u           /**< Marked dead by DCE pass */
#define JIT_IR_FLAG_PSEUDO 0x10u         /**< Pseudo-op: no code emitted */
#define JIT_IR_FLAG_LOOP_INVARIANT 0x20u /**< Marked loop-invariant by LICM */
#define JIT_IR_FLAG_CONSTANT_FOLDED                                            \
  0x40u /**< Replaced by constant-folding pass */

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

    /* JIT_IR_PHI: dst_reg = phi(operand[0], operand[1], …) */
    struct {
      uint8_t operand_count; /**< Number of phi operands */
      size_t operand_first;  /**< First index into region->phi_operands[] */
    } phi;
  } u;
} jit_ir_instr_t;

/* -------------------------------------------------------------------------
 * SSA / CFG data structures
 * ------------------------------------------------------------------------- */

/** One phi operand: incoming value vreg from predecessor block. */
typedef struct {
  uint8_t vreg;     /**< Incoming value virtual register */
  uint16_t pred_id; /**< Predecessor basic-block index */
} jit_ir_phi_operand_t;

/** Forward declaration for use in basic block. */
typedef struct _jit_ir_basic_block jit_ir_basic_block_t;

/**
 * Basic-block descriptor for the CFG.
 *
 * Each block spans a contiguous range of IR instructions [start_idx, end_idx]
 * (inclusive).  Branches are always the last instruction of a block; labels
 * are always the first instruction of a block.
 */
struct _jit_ir_basic_block {
  size_t start_idx; /**< First instruction index (inclusive) */
  size_t end_idx;   /**< Last instruction index (inclusive) */
  uint16_t id;      /**< Block index (position in region->blocks[]) */

  /* CFG adjacency — stored as dynamic arrays owned by the region.  The raw
   * arrays live in the region's *_pool and each block records offset+count. */
  uint16_t pred_count; /**< Number of predecessors */
  uint16_t pred_first; /**< First index in region->pred_pool[] */
  uint16_t succ_count; /**< Number of successors */
  uint16_t succ_first; /**< First index in region->succ_pool[] */

  /* Loop-nest depth for LICM (0 = outside any loop).  Filled by
   * jit_ir_find_loops(). */
  uint16_t loop_depth;

  /* Dominator-tree fields (filled by jit_ir_compute_dominators). */
  uint16_t idom;      /**< Immediate dominator block id (itself = root) */
  uint16_t dom_depth; /**< Depth in dominator tree (root = 0) */
};

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

  /* -----------------------------------------------------------------------
   * Basic-block CFG — built by jit_ir_build_cfg()
   * ----------------------------------------------------------------------- */
  jit_ir_basic_block_t *blocks; /**< Heap-allocated block array */
  uint16_t block_count;         /**< Number of valid blocks */
  uint16_t block_capacity;      /**< Allocated block slots */

  /* Adjacency pool: flat uint16_t arrays where each block's preds/succs are
   * stored as contiguous slices indexed by pred_first/succ_first + count. */
  uint16_t *pred_pool;       /**< Heap-allocated predecessor pool */
  size_t pred_pool_count;    /**< Valid entries in pred_pool */
  size_t pred_pool_capacity; /**< Allocated pred_pool slots */

  uint16_t *succ_pool;       /**< Heap-allocated successor pool */
  size_t succ_pool_count;    /**< Valid entries in succ_pool */
  size_t succ_pool_capacity; /**< Allocated succ_pool slots */

  /* -----------------------------------------------------------------------
   * Phi operands pool — flat array indexed by phi instructions
   * ----------------------------------------------------------------------- */
  jit_ir_phi_operand_t *phi_operands; /**< Heap-allocated phi operand array */
  size_t phi_operands_count;          /**< Valid entries */
  size_t phi_operands_capacity;       /**< Allocated slots */
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

/**
 * Append a phi instruction with the given operands.
 *
 * The phi opcode and @p dst_reg are recorded in a new instruction entry;
 * the operand list (vreg + predecessor block id for each incoming edge) is
 * copied into the region's phi_operands pool.
 *
 * @return The instruction index of the appended phi, or (size_t)-1 on failure.
 */
size_t jit_ir_append_phi(jit_ir_region_t *r, size_t bc_ip, uint8_t dst_reg,
                         const jit_ir_phi_operand_t *operands,
                         uint8_t operand_count);

/* -------------------------------------------------------------------------
 * CFG construction & analysis
 * ------------------------------------------------------------------------- */

/**
 * Reconstruct the control-flow graph from the linear instruction array.
 *
 * Scans for JIT_IR_LABEL (block starts), JIT_IR_JMP / JIT_IR_GOTO /
 * JIT_IR_GOTO_F / JIT_IR_SPLIT (block ends) and builds the basic block
 * descriptor array plus predecessor/successor adjacency pools.
 *
 * Must be called before any CFG-dependent pass.
 */
void jit_ir_build_cfg(jit_ir_region_t *r);

/**
 * Compute immediate dominators for all blocks using a simple data-flow
 * algorithm (Lengauer-Tarjan is unnecessary at this scale).
 *
 * Requires jit_ir_build_cfg() to have been called first.
 */
void jit_ir_compute_dominators(jit_ir_region_t *r);

/**
 * Identify natural loops via back-edge detection and annotate each block's
 * loop_depth field.  A block inside k nested loops gets loop_depth = k.
 *
 * Requires jit_ir_compute_dominators() to have been called first.
 */
void jit_ir_find_loops(jit_ir_region_t *r);

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

/**
 * Global Value Numbering (GVN) pass.
 *
 * Assigns a value number to each instruction based on a hash of (opcode,
 * src_reg[0], src_reg[1], union data).  When two instructions have the same
 * value number, they compute the same result; the redundant one is replaced
 * with JIT_IR_COPY targeting the earlier definition's dst_reg.
 *
 * Requires jit_ir_build_cfg() + jit_ir_compute_dominators() to have been
 * called first (dominator information is used to determine which definition
 * dominates a given use).
 */
void jit_ir_gvn(jit_ir_region_t *r);

/**
 * Constant-folding pass.
 *
 * For each instruction whose opcode is non-side-effecting and whose source
 * operands all reference JIT_IR_LIT constants propagated through copies,
 * evaluate the instruction at compile time and replace it with a JIT_IR_COPY
 * to the result.  Currently handles:
 *  - JIT_IR_LEN (constant n → data pointer + length pre-decoded)
 *  - JIT_IR_RPOS / JIT_IR_RTAB / JIT_IR_POS / JIT_IR_TAB with constant
 *    offset from a preceding LIT-producing instruction
 *
 * This pass is intentionally conservative — only fold when correctness is
 * provable.
 */
void jit_ir_constant_fold(jit_ir_region_t *r);

/**
 * Loop-Invariant Code Motion (LICM) pass.
 *
 * Requires jit_ir_find_loops() to have been called first.  For each loop
 * (blocks with loop_depth > 0), identifies instructions that:
 *  1. Are pure (no side effects, no call-out)
 *  2. Have all source operands defined outside the loop (or loop-invariant
 *     transitively)
 *
 * Such instructions are hoisted to the loop preheader (the block immediately
 * dominating the loop header).
 */
void jit_ir_licm(jit_ir_region_t *r);

/* -------------------------------------------------------------------------
 * Register-allocator interface
 * ------------------------------------------------------------------------- */

/**
 * Linear-scan register-allocation result.
 *
 * After calling jit_ir_alloc_registers(), each virtual register is assigned a
 * physical register slot.  The mapping is stored in @p phys_reg[vreg].
 */
typedef struct {
  int phys_reg[256];    /**< phys_reg[vreg] = physical register index, or -1 if
                             the vreg is unused/spilled */
  uint16_t spill_count; /**< Number of vregs spilled to stack */
} jit_ir_regalloc_t;

/**
 * Run linear-scan register allocation on the region.
 *
 * @return A heap-allocated jit_ir_regalloc_t; caller frees with free().
 *         Returns NULL on allocation failure.
 */
jit_ir_regalloc_t *jit_ir_alloc_registers(jit_ir_region_t *r);

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
