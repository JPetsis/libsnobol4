/**
 * @file jit_ir.c
 * @brief Architecture-neutral intermediate representation (IR) for the SNOBOL4
 * micro-JIT.
 *
 * Implements:
 *  - Region builder  (jit_ir_region_new, jit_ir_append, ...)
 *  - SSA phi support (jit_ir_append_phi)
 *  - CFG construction & analysis (jit_ir_build_cfg, jit_ir_compute_dominators,
 *    jit_ir_find_loops)
 *  - Optimiser passes (jit_ir_dce, jit_ir_copy_propagation, jit_ir_gvn,
 *    jit_ir_constant_fold, jit_ir_licm)
 *  - Register allocation (jit_ir_alloc_registers)
 *  - VM opcode lifter (jit_ir_lift_region)
 *  - Debug dump      (jit_ir_dump)
 */

#include "snobol/jit_ir.h"
#include "snobol/snobol_internal.h"

#ifdef SNOBOL_JIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Context helpers (bytecode reading mirrors jit.c cfg_read_u32)
 * -------------------------------------------------------------------------
 */
static inline uint32_t ir_read_u32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static inline uint16_t ir_read_u16(const uint8_t *p) {
  return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

/* -------------------------------------------------------------------------
 * Region builder
 * -------------------------------------------------------------------------
 */

#define IR_INITIAL_CAPACITY 32

jit_ir_region_t *jit_ir_region_new(void) {
  jit_ir_region_t *r =
      (jit_ir_region_t *)snobol_calloc(1, sizeof(jit_ir_region_t));
  if (!r)
    return nullptr;
  r->instrs = (jit_ir_instr_t *)snobol_malloc(IR_INITIAL_CAPACITY *
                                              sizeof(jit_ir_instr_t));
  if (!r->instrs) {
    snobol_free(r);
    return nullptr;
  }
  r->capacity = IR_INITIAL_CAPACITY;
  r->count = 0;
  r->vreg_next = 1; /* 0 = VREG_NONE; allocations start at 1 */
  r->non_compilable = false;
  memset(r->use_count, 0, sizeof(r->use_count));

  /* CFG / SSA fields zeroed by calloc */
  return r;
}

void jit_ir_region_free(jit_ir_region_t *r) {
  if (!r)
    return;
  if (r->instrs)
    snobol_free(r->instrs);
  if (r->blocks)
    snobol_free(r->blocks);
  if (r->pred_pool)
    snobol_free(r->pred_pool);
  if (r->succ_pool)
    snobol_free(r->succ_pool);
  if (r->phi_operands)
    snobol_free(r->phi_operands);
  snobol_free(r);
}

bool jit_ir_append(jit_ir_region_t *r, jit_ir_opcode_t op, uint8_t flags,
                   size_t bc_ip, uint8_t dst_reg, uint8_t src0, uint8_t src1) {
  if (!r || r->non_compilable)
    return false;
  if (r->count >= r->capacity) {
    size_t new_cap = r->capacity * 2;
    jit_ir_instr_t *nb = (jit_ir_instr_t *)snobol_realloc(
        r->instrs, new_cap * sizeof(jit_ir_instr_t));
    if (!nb)
      return false;
    r->instrs = nb;
    r->capacity = new_cap;
  }
  jit_ir_instr_t *ins = &r->instrs[r->count++];
  memset(ins, 0, sizeof(*ins));
  ins->opcode = op;
  ins->flags = flags;
  ins->dst_reg = dst_reg;
  ins->src_reg[0] = src0;
  ins->src_reg[1] = src1;
  ins->bc_ip = bc_ip;
  /* Increment use counts for src registers */
  if (src0 != JIT_IR_VREG_NONE)
    jit_ir_inc_use(r, src0);
  if (src1 != JIT_IR_VREG_NONE)
    jit_ir_inc_use(r, src1);
  return true;
}

uint8_t jit_ir_alloc_vreg(jit_ir_region_t *r) {
  if (!r)
    return JIT_IR_VREG_NONE;
  if (r->vreg_next > JIT_IR_VREG_MAX) {
    /* Exceeded 256 virtual registers — mark region non-compilable */
    if (!r->non_compilable) {
      r->non_compilable = true;
      fprintf(stderr, "[snobol JIT] IR vreg limit exceeded (> 256): region "
                      "marked non-compilable\n");
    }
    return JIT_IR_VREG_NONE;
  }
  return r->vreg_next++;
}

void jit_ir_inc_use(jit_ir_region_t *r, uint8_t reg) {
  if (!r || reg == JIT_IR_VREG_NONE)
    return;
  if (r->use_count[reg] < UINT16_MAX)
    r->use_count[reg]++;
}

size_t jit_ir_append_phi(jit_ir_region_t *r, size_t bc_ip, uint8_t dst_reg,
                         const jit_ir_phi_operand_t *operands,
                         uint8_t operand_count) {
  if (!r || r->non_compilable || !operands || operand_count == 0)
    return (size_t)-1;

  /* Grow phi_operands array if needed */
  size_t needed = r->phi_operands_count + operand_count;
  if (needed > r->phi_operands_capacity) {
    size_t new_cap =
        r->phi_operands_capacity ? r->phi_operands_capacity * 2 : 8;
    while (new_cap < needed)
      new_cap *= 2;
    jit_ir_phi_operand_t *np = (jit_ir_phi_operand_t *)snobol_realloc(
        r->phi_operands, new_cap * sizeof(jit_ir_phi_operand_t));
    if (!np)
      return (size_t)-1;
    r->phi_operands = np;
    r->phi_operands_capacity = new_cap;
  }

  size_t first_idx = r->phi_operands_count;
  memcpy(&r->phi_operands[first_idx], operands,
         operand_count * sizeof(jit_ir_phi_operand_t));
  r->phi_operands_count += operand_count;

  /* Increment use counts for phi source operands */
  for (uint8_t pi = 0; pi < operand_count; pi++)
    jit_ir_inc_use(r, operands[pi].vreg);

  uint8_t vr = dst_reg;
  if (vr == JIT_IR_VREG_NONE)
    vr = jit_ir_alloc_vreg(r);
  if (!jit_ir_append(r, JIT_IR_PHI, JIT_IR_FLAG_PURE, bc_ip, vr, 0, 0))
    return (size_t)-1;

  /* Fill in phi-specific union data */
  size_t idx = r->count - 1;
  r->instrs[idx].u.phi.operand_count = operand_count;
  r->instrs[idx].u.phi.operand_first = first_idx;

  return idx;
}

/* -------------------------------------------------------------------------
 * Optimiser passes
 * -------------------------------------------------------------------------
 */

void jit_ir_dce(jit_ir_region_t *r) {
  if (!r || r->count == 0)
    return;

  /* Mark dead: instructions that are pure AND whose dst_reg has zero uses */
  for (size_t i = 0; i < r->count; i++) {
    jit_ir_instr_t *ins = &r->instrs[i];
    if (ins->flags & JIT_IR_FLAG_DEAD)
      continue;
    if (ins->flags & JIT_IR_FLAG_SIDE_EFFECT)
      continue;
    if (!(ins->flags & JIT_IR_FLAG_PURE))
      continue;
    if (ins->dst_reg == JIT_IR_VREG_NONE)
      continue;
    if (r->use_count[ins->dst_reg] == 0)
      ins->flags |= JIT_IR_FLAG_DEAD;
  }

  /* Compact: remove dead instructions */
  size_t write = 0;
  for (size_t read = 0; read < r->count; read++) {
    if (!(r->instrs[read].flags & JIT_IR_FLAG_DEAD)) {
      if (write != read)
        r->instrs[write] = r->instrs[read];
      write++;
    }
  }
  r->count = write;
}

void jit_ir_copy_propagation(jit_ir_region_t *r) {
  if (!r || r->count == 0)
    return;

  /* Build a copy-of table: copy_source[dst_reg] = src_reg for COPY instrs */
  uint8_t copy_source[256];
  memset(copy_source, JIT_IR_VREG_NONE, sizeof(copy_source));

  for (size_t i = 0; i < r->count; i++) {
    jit_ir_instr_t *ins = &r->instrs[i];
    if (ins->opcode == JIT_IR_COPY && ins->dst_reg != JIT_IR_VREG_NONE)
      copy_source[ins->dst_reg] = ins->src_reg[0];
  }

  /* Replace uses of copy-destination registers with their sources */
  bool any_change = false;
  for (size_t i = 0; i < r->count; i++) {
    jit_ir_instr_t *ins = &r->instrs[i];
    if (ins->opcode == JIT_IR_COPY)
      continue; /* COPY itself — skip */
    for (int k = 0; k < 2; k++) {
      uint8_t s = ins->src_reg[k];
      if (s != JIT_IR_VREG_NONE && copy_source[s] != JIT_IR_VREG_NONE) {
        /* Decrement old use count, increment new */
        if (r->use_count[s] > 0)
          r->use_count[s]--;
        uint8_t ns = copy_source[s];
        ins->src_reg[k] = ns;
        jit_ir_inc_use(r, ns);
        any_change = true;
      }
    }
  }

  /* Mark COPY instructions whose dst_reg now has zero uses as dead (pure) */
  if (any_change) {
    for (size_t i = 0; i < r->count; i++) {
      jit_ir_instr_t *ins = &r->instrs[i];
      if (ins->opcode == JIT_IR_COPY && ins->dst_reg != JIT_IR_VREG_NONE &&
          r->use_count[ins->dst_reg] == 0) {
        ins->flags |= JIT_IR_FLAG_PURE; /* ensure DCE can remove it */
      }
    }
    /* Run DCE to clean up dead COPYs */
    jit_ir_dce(r);
  }
}

/* -------------------------------------------------------------------------
 * CFG construction & analysis
 * -------------------------------------------------------------------------
 */

/** Maximum blocks we can represent with uint16_t indices. */
#define MAX_BLOCKS 4096

/**
 * Return the block index containing instruction @p idx, or UINT16_MAX.
 * Linear scan — blocks are small, so O(N) works fine.
 */
static uint16_t block_for_instr(const jit_ir_region_t *r, size_t idx) {
  for (uint16_t b = 0; b < r->block_count; b++) {
    if (idx >= r->blocks[b].start_idx && idx <= r->blocks[b].end_idx)
      return b;
  }
  return UINT16_MAX;
}

void jit_ir_build_cfg(jit_ir_region_t *r) {
  if (!r || r->count == 0)
    return;

  /* Free any previous CFG */
  if (r->blocks) {
    snobol_free(r->blocks);
    r->blocks = nullptr;
  }
  if (r->pred_pool) {
    snobol_free(r->pred_pool);
    r->pred_pool = nullptr;
  }
  if (r->succ_pool) {
    snobol_free(r->succ_pool);
    r->succ_pool = nullptr;
  }
  r->block_count = 0;
  r->block_capacity = 0;
  r->pred_pool_count = 0;
  r->pred_pool_capacity = 0;
  r->succ_pool_count = 0;
  r->succ_pool_capacity = 0;

  /* Pass 1: identify block boundaries.
   * A block starts at:
   *   - index 0
   *   - any JIT_IR_LABEL
   * A block ends at:
   *   - the last instruction
   *   - any terminator: JIT_IR_JMP, JIT_IR_GOTO, JIT_IR_GOTO_F, JIT_IR_SPLIT,
   *     JIT_IR_ACCEPT, JIT_IR_FAIL, JIT_IR_REPEAT_INIT, JIT_IR_REPEAT_STEP,
   *     JIT_IR_ABORT, JIT_IR_SUCCEED */

  /* First, mark which indices are block starts */
  bool is_block_start[4096];
  size_t max_starts = r->count > 4096 ? 4096 : r->count;
  memset(is_block_start, 0, max_starts);
  is_block_start[0] = true;

  for (size_t i = 0; i < max_starts; i++) {
    if (r->instrs[i].opcode == JIT_IR_LABEL)
      is_block_start[i] = true;
  }

  /* Build blocks */
  size_t local_cap = 64;
  r->blocks = (jit_ir_basic_block_t *)snobol_malloc(
      local_cap * sizeof(jit_ir_basic_block_t));
  if (!r->blocks)
    return;
  r->block_capacity = (uint16_t)local_cap;
  r->block_count = 0;

  size_t block_start = 0;
  for (size_t i = 0; i < max_starts; i++) {
    if (i > 0 && is_block_start[i]) {
      /* End previous block at i-1 */
      if (r->block_count >= r->block_capacity) {
        local_cap *= 2;
        jit_ir_basic_block_t *nb = (jit_ir_basic_block_t *)snobol_realloc(
            r->blocks, local_cap * sizeof(jit_ir_basic_block_t));
        if (!nb)
          return;
        r->blocks = nb;
        r->block_capacity = (uint16_t)local_cap;
      }
      jit_ir_basic_block_t *bb = &r->blocks[r->block_count++];
      memset(bb, 0, sizeof(*bb));
      bb->start_idx = block_start;
      bb->end_idx = i - 1;
      bb->id = r->block_count - 1;
      bb->loop_depth = 0;
      bb->idom = (uint16_t)-1;
      bb->dom_depth = 0;
      block_start = i;
    }
  }

  /* Last block */
  if (r->block_count >= r->block_capacity) {
    local_cap *= 2;
    jit_ir_basic_block_t *nb = (jit_ir_basic_block_t *)snobol_realloc(
        r->blocks, local_cap * sizeof(jit_ir_basic_block_t));
    if (!nb)
      return;
    r->blocks = nb;
    r->block_capacity = (uint16_t)local_cap;
  }
  {
    jit_ir_basic_block_t *bb = &r->blocks[r->block_count++];
    memset(bb, 0, sizeof(*bb));
    bb->start_idx = block_start;
    bb->end_idx = max_starts - 1;
    bb->id = r->block_count - 1;
    bb->loop_depth = 0;
    bb->idom = (uint16_t)-1;
    bb->dom_depth = 0;
  }

  /* Pass 2: build successor and predecessor adjacency.
   * Allocate generous initial pools. */
  size_t pool_cap = r->block_count * 4;
  r->pred_pool = (uint16_t *)snobol_calloc(pool_cap, sizeof(uint16_t));
  r->succ_pool = (uint16_t *)snobol_calloc(pool_cap, sizeof(uint16_t));
  if (!r->pred_pool || !r->succ_pool)
    return;
  r->pred_pool_capacity = pool_cap;
  r->succ_pool_capacity = pool_cap;

  for (uint16_t bi = 0; bi < r->block_count; bi++) {
    jit_ir_basic_block_t *bb = &r->blocks[bi];

    /* Determine the last instruction of the block (the terminator) */
    size_t last = bb->end_idx;
    jit_ir_instr_t *term = &r->instrs[last];

    /* Find successor(s) based on terminator type */
    uint16_t succs[4];
    uint16_t nsuccs = 0;

    switch (term->opcode) {
    case JIT_IR_JMP:
    case JIT_IR_GOTO: {
      /* Find target block by scanning label position */
      size_t tgt = term->u.jmp.target;
      uint16_t tb = block_for_instr(r, tgt);
      if (tb != UINT16_MAX)
        succs[nsuccs++] = tb;
      break;
    }
    case JIT_IR_SPLIT: {
      size_t ta = term->u.split.target_a;
      size_t tb = term->u.split.target_b;
      uint16_t ba = block_for_instr(r, ta);
      uint16_t bb2 = block_for_instr(r, tb);
      if (ba != UINT16_MAX)
        succs[nsuccs++] = ba;
      if (bb2 != UINT16_MAX && bb2 != ba)
        succs[nsuccs++] = bb2;
      break;
    }
    case JIT_IR_GOTO_F: {
      /* Conditional: falls through to next block, or jumps to target */
      if (bi + 1 < r->block_count)
        succs[nsuccs++] = bi + 1;
      size_t tgt = term->u.goto_f.target;
      uint16_t tb = block_for_instr(r, tgt);
      if (tb != UINT16_MAX)
        succs[nsuccs++] = tb;
      break;
    }
    case JIT_IR_ACCEPT:
    case JIT_IR_FAIL:
    case JIT_IR_REPEAT_INIT:
    case JIT_IR_REPEAT_STEP:
    case JIT_IR_ABORT:
    case JIT_IR_SUCCEED:
      /* Terminators with no successors */
      break;
    default:
      /* Not a terminator: falls through to next block */
      if (bi + 1 < r->block_count)
        succs[nsuccs++] = bi + 1;
      break;
    }

    /* Store successors and reverse-edge predecessors */
    if (nsuccs > 0) {
      /* Grow successor pool if needed */
      size_t s_needed = r->succ_pool_count + nsuccs;
      if (s_needed > r->succ_pool_capacity) {
        size_t new_cap =
            r->succ_pool_capacity ? r->succ_pool_capacity * 2 : pool_cap;
        while (new_cap < s_needed)
          new_cap *= 2;
        uint16_t *np = (uint16_t *)snobol_realloc(r->succ_pool,
                                                  new_cap * sizeof(uint16_t));
        if (!np)
          return;
        r->succ_pool = np;
        r->succ_pool_capacity = new_cap;
      }
      bb->succ_first = (uint16_t)r->succ_pool_count;
      bb->succ_count = nsuccs;
      memcpy(&r->succ_pool[r->succ_pool_count], succs,
             nsuccs * sizeof(uint16_t));
      r->succ_pool_count += nsuccs;

      /* Add reverse edges (predecessors) */
      for (uint16_t si = 0; si < nsuccs; si++) {
        uint16_t dest = succs[si];
        jit_ir_basic_block_t *dbb = &r->blocks[dest];
        size_t p_needed = r->pred_pool_count + 1;
        if (p_needed > r->pred_pool_capacity) {
          size_t new_cap =
              r->pred_pool_capacity ? r->pred_pool_capacity * 2 : pool_cap;
          while (new_cap < p_needed)
            new_cap *= 2;
          uint16_t *np = (uint16_t *)snobol_realloc(r->pred_pool,
                                                    new_cap * sizeof(uint16_t));
          if (!np)
            return;
          r->pred_pool = np;
          r->pred_pool_capacity = new_cap;
        }
        /* Predecessors are appended; we'll consolidate later or just leave as
         * a flat list with each block storing its slice.  For now, append and
         * update block's slice. */
        size_t prev_first = (size_t)dbb->pred_first;
        size_t prev_count = (size_t)dbb->pred_count;
        if (prev_count == 0) {
          dbb->pred_first = (uint16_t)r->pred_pool_count;
          dbb->pred_count = 1;
        } else {
          dbb->pred_count++;
        }
        r->pred_pool[r->pred_pool_count++] = bi;
      }
    }
  }
}

void jit_ir_compute_dominators(jit_ir_region_t *r) {
  if (!r || r->block_count == 0)
    return;

  uint16_t n = r->block_count;

  /* idom[0] = entry = itself */
  r->blocks[0].idom = 0;
  r->blocks[0].dom_depth = 0;
  for (uint16_t i = 1; i < n; i++)
    r->blocks[i].idom = UINT16_MAX;

  /* Iterative data-flow for immediate dominators.
   * For each block (in order, which approximates RPO for reducible CFGs),
   * idom(b) = meet of idom(p) for all predecessors p. */
  bool changed = true;
  while (changed) {
    changed = false;
    for (uint16_t i = 1; i < n; i++) {
      jit_ir_basic_block_t *bb = &r->blocks[i];
      if (bb->pred_count == 0)
        continue;

      /* Find first predecessor that has idom computed */
      uint16_t new_idom = UINT16_MAX;
      for (uint16_t pi = 0; pi < bb->pred_count; pi++) {
        uint16_t p = r->pred_pool[bb->pred_first + pi];
        if (r->blocks[p].idom != UINT16_MAX) {
          new_idom = p;
          break;
        }
      }
      if (new_idom == UINT16_MAX)
        continue;

      /* Intersect with remaining predecessors */
      for (uint16_t pi = 0; pi < bb->pred_count; pi++) {
        uint16_t p = r->pred_pool[bb->pred_first + pi];
        if (r->blocks[p].idom == UINT16_MAX)
          continue;
        uint16_t fi = p;
        uint16_t si = new_idom;
        while (fi != si) {
          while (fi < si)
            si = r->blocks[si].idom;
          while (si < fi)
            fi = r->blocks[fi].idom;
        }
        new_idom = fi;
      }

      if (bb->idom != new_idom) {
        bb->idom = new_idom;
        changed = true;
      }
    }
  }

  /* Compute dom_depth */
  r->blocks[0].dom_depth = 0;
  for (uint16_t i = 1; i < n; i++) {
    uint16_t d = r->blocks[i].idom;
    if (d != UINT16_MAX && d != i)
      r->blocks[i].dom_depth = r->blocks[d].dom_depth + 1;
  }
}

void jit_ir_find_loops(jit_ir_region_t *r) {
  if (!r || r->block_count == 0)
    return;

  uint16_t n = r->block_count;

  /* Reset loop depths */
  for (uint16_t i = 0; i < n; i++)
    r->blocks[i].loop_depth = 0;

  /* Detect natural loops: for each edge m → n where n dominates m (back edge).
   * The loop body = all blocks that can reach m without going through n. */
  for (uint16_t m = 0; m < n; m++) {
    jit_ir_basic_block_t *bm = &r->blocks[m];
    for (uint16_t si = 0; si < bm->succ_count; si++) {
      uint16_t s = r->succ_pool[bm->succ_first + si];

      /* Back edge: s (header) dominates m (latch)? */
      if (s > m)
        continue; /* forward edge — not a back edge */

      /* Check domination: does s dominate m?
       * Walk idom chain from m up to s. */
      uint16_t cur = m;
      bool s_dominates_m = false;
      while (cur != UINT16_MAX) {
        if (cur == s) {
          s_dominates_m = true;
          break;
        }
        if (cur == r->blocks[cur].idom)
          break; /* root */
        cur = r->blocks[cur].idom;
      }

      if (!s_dominates_m)
        continue;

      /* s is the loop header, m is the latch.
       * Loop body = all blocks reachable from s that can reach m without going
       * through s. For simplicity, mark s and all blocks in the
       * backward-reachable set. */
      /* Mark blocks reachable from s backward to m via CFG reverse traversal */
      /* Simple approach: iterate blocks in [s, m] range and check reachability.
       * This is conservative (over-approximates) but safe. */
      for (uint16_t b = s; b <= m; b++) {
        jit_ir_basic_block_t *bbl = &r->blocks[b];
        /* Check if b is reachable from s and can reach m */
        bool reachable_from_s = false;
        {
          /* Forward DFS limited to s..m */
          bool visited[4096];
          memset(visited, 0, n);
          uint16_t stack[4096];
          uint16_t sp = 0;
          stack[sp++] = s;
          visited[s] = true;
          while (sp > 0) {
            uint16_t cur2 = stack[--sp];
            if (cur2 == b) {
              reachable_from_s = true;
              break;
            }
            jit_ir_basic_block_t *cb = &r->blocks[cur2];
            for (uint16_t si2 = 0; si2 < cb->succ_count; si2++) {
              uint16_t ns = r->succ_pool[cb->succ_first + si2];
              if (ns <= m && !visited[ns]) {
                visited[ns] = true;
                stack[sp++] = ns;
              }
            }
          }
        }

        if (reachable_from_s) {
          bbl->loop_depth++;
        }
      }
    }
  }
}

/* -------------------------------------------------------------------------
 * GVN (Global Value Numbering)
 * -------------------------------------------------------------------------
 */

/** Simple hash combining for GVN. */
static inline uint32_t hash_combine(uint32_t h, uint32_t v) {
  return (h * 0x9e3779b9u) ^ v;
}

/** Compute a hash for an IR instruction (excluding dst_reg). */
static uint32_t ir_instr_hash(const jit_ir_instr_t *ins) {
  uint32_t h = (uint32_t)ins->opcode;
  h = hash_combine(h, (uint32_t)ins->flags & 0xFu);
  h = hash_combine(h, (uint32_t)ins->src_reg[0]);
  h = hash_combine(h, (uint32_t)ins->src_reg[1]);

  /* Include relevant union data based on opcode */
  switch (ins->opcode) {
  case JIT_IR_LIT:
    h = hash_combine(h, ins->u.lit.len);
    if (ins->u.lit.data)
      for (uint32_t i = 0; i < ins->u.lit.len && i < 32; i++)
        h = hash_combine(h, (uint32_t)ins->u.lit.data[i]);
    break;
  case JIT_IR_ANY:
  case JIT_IR_NOTANY:
  case JIT_IR_SPAN:
  case JIT_IR_BREAK:
  case JIT_IR_BREAKX:
    h = hash_combine(h, (uint32_t)ins->u.set.set_id);
    break;
  case JIT_IR_LEN:
    h = hash_combine(h, ins->u.len.n);
    break;
  case JIT_IR_RPOS:
  case JIT_IR_RTAB:
    h = hash_combine(h, ins->u.rpos_rtab.n);
    break;
  case JIT_IR_ASSIGN:
    h = hash_combine(h, (uint32_t)ins->u.assign.var);
    h = hash_combine(h, (uint32_t)ins->u.assign.reg);
    break;
  default:
    break;
  }
  return h;
}

/** GVN value-table entry. */
typedef struct {
  uint32_t hash;
  uint8_t vreg;
  size_t instr_idx;
  bool used;
} gvn_entry_t;

void jit_ir_gvn(jit_ir_region_t *r) {
  if (!r || r->count == 0 || r->block_count == 0)
    return;

  /* Simple linear-scan GVN: iterate instructions, assign value numbers to
   * each producing instruction.  When a later instruction has the same
   * opcode + operands as an earlier one in a dominating position, replace
   * the later with JIT_IR_COPY. */

  gvn_entry_t value_table[256];
  memset(value_table, 0, sizeof(value_table));
  size_t vn_count = 0;

  /* Track which block each instruction belongs to */
  uint16_t *instr_block =
      (uint16_t *)snobol_malloc(r->count * sizeof(uint16_t));
  if (!instr_block)
    return;
  for (size_t i = 0; i < r->count; i++)
    instr_block[i] = block_for_instr(r, i);

  bool dominated_by[4096][256];
  /* Only allocate if we have reasonable sizes */
  bool use_dominated = (r->block_count <= 256 && r->block_count <= 4096);

  for (size_t i = 0; i < r->count; i++) {
    jit_ir_instr_t *ins = &r->instrs[i];
    if (ins->opcode == JIT_IR_COPY || ins->opcode == JIT_IR_PHI)
      continue;
    if (ins->dst_reg == JIT_IR_VREG_NONE)
      continue;
    if (!(ins->flags & JIT_IR_FLAG_PURE))
      continue;
    if (ins->flags & JIT_IR_FLAG_SIDE_EFFECT)
      continue;

    uint32_t h = ir_instr_hash(ins);

    /* Check if we've seen this value before */
    bool found = false;
    size_t found_idx = (size_t)-1;
    for (size_t vi = 0; vi < vn_count; vi++) {
      if (!value_table[vi].used)
        continue;
      if (value_table[vi].hash != h)
        continue;
      /* Check that the earlier definition dominates this use point */
      uint16_t def_block = instr_block[value_table[vi].instr_idx];
      uint16_t use_block = instr_block[i];

      if (def_block == use_block) {
        /* Same block: check that the definition comes before the use */
        if (value_table[vi].instr_idx < i)
          found = true;
      } else {
        /* Different blocks: check domination */
        uint16_t cur = use_block;
        while (cur != UINT16_MAX) {
          if (cur == def_block) {
            found = true;
            break;
          }
          if (cur == r->blocks[cur].idom)
            break;
          cur = r->blocks[cur].idom;
        }
      }

      if (found) {
        found_idx = vi;
        break;
      }
    }

    if (found) {
      /* Replace with COPY from the earlier result */
      uint8_t orig_dst = ins->dst_reg;
      ins->opcode = JIT_IR_COPY;
      ins->src_reg[0] = value_table[found_idx].vreg;
      ins->src_reg[1] = JIT_IR_VREG_NONE;
      memset(&ins->u, 0, sizeof(ins->u));
      /* Update use counts */
      jit_ir_inc_use(r, ins->src_reg[0]);
      /* Mark as pure so DCE can clean up if unused */
      ins->flags |= JIT_IR_FLAG_PURE;
    } else {
      /* Record this value */
      if (vn_count < 256) {
        value_table[vn_count].hash = h;
        value_table[vn_count].vreg = ins->dst_reg;
        value_table[vn_count].instr_idx = i;
        value_table[vn_count].used = true;
        vn_count++;
      }
    }
  }

  snobol_free(instr_block);
}

/* -------------------------------------------------------------------------
 * Constant folding
 * -------------------------------------------------------------------------
 */

/** Check if a vreg is defined by a LIT instruction with constant data.
 *  Returns the jit_ir_instr_t if so, NULL otherwise. */
static const jit_ir_instr_t *vreg_def_is_lit(const jit_ir_region_t *r,
                                             uint8_t vreg) {
  if (vreg == JIT_IR_VREG_NONE)
    return nullptr;
  for (size_t i = 0; i < r->count; i++) {
    const jit_ir_instr_t *ins = &r->instrs[i];
    if (ins->dst_reg == vreg && ins->opcode == JIT_IR_LIT)
      return ins;
  }
  return nullptr;
}

/** Check if a vreg is defined by a constant positional instruction (LEN, RPOS,
 *  RTAB, POS, TAB). */
static const jit_ir_instr_t *vreg_def_is_const_pos(const jit_ir_region_t *r,
                                                   uint8_t vreg) {
  if (vreg == JIT_IR_VREG_NONE)
    return nullptr;
  for (size_t i = 0; i < r->count; i++) {
    const jit_ir_instr_t *ins = &r->instrs[i];
    if (ins->dst_reg != vreg)
      continue;
    switch (ins->opcode) {
    case JIT_IR_LEN:
    case JIT_IR_RPOS:
    case JIT_IR_RTAB:
    case JIT_IR_POS:
    case JIT_IR_TAB:
      return ins;
    default:
      return nullptr;
    }
  }
  return nullptr;
}

/** Check if a vreg is defined by a COPY or a constant instruction.
 *  Fills *out_val with the value if foldable. */
static bool vreg_is_constant(const jit_ir_region_t *r, uint8_t vreg,
                             uint32_t *out_val) {
  if (vreg == JIT_IR_VREG_NONE)
    return false;

  /* Follow copy chain */
  uint8_t cur = vreg;
  for (int iter = 0; iter < 256; iter++) {
    const jit_ir_instr_t *def = vreg_def_is_const_pos(r, cur);
    if (def) {
      switch (def->opcode) {
      case JIT_IR_LEN:
        *out_val = def->u.len.n;
        return true;
      case JIT_IR_RPOS:
      case JIT_IR_RTAB:
      case JIT_IR_POS:
      case JIT_IR_TAB:
        *out_val = def->u.rpos_rtab.n;
        return true;
      default:
        return false;
      }
    }

    /* Follow COPY chain */
    bool found_copy = false;
    for (size_t i = 0; i < r->count; i++) {
      const jit_ir_instr_t *ins = &r->instrs[i];
      if (ins->dst_reg == cur && ins->opcode == JIT_IR_COPY) {
        cur = ins->src_reg[0];
        found_copy = true;
        break;
      }
    }
    if (!found_copy)
      return false;
  }
  return false;
}

void jit_ir_constant_fold(jit_ir_region_t *r) {
  if (!r || r->count == 0)
    return;

  /* Currently handles: folding of LEN, RPOS, RTAB, POS, TAB when the operand
   * vreg is a known constant.  This is conservative — only fold when we can
   * prove correctness. */

  for (size_t i = 0; i < r->count; i++) {
    jit_ir_instr_t *ins = &r->instrs[i];
    if (ins->flags & JIT_IR_FLAG_SIDE_EFFECT)
      continue;
    if (ins->dst_reg == JIT_IR_VREG_NONE)
      continue;

    /* Check for LEN with constant src_reg[0] (pattern length) */
    if (ins->opcode == JIT_IR_LEN) {
      uint32_t const_val;
      if (vreg_is_constant(r, ins->src_reg[0], &const_val)) {
        ins->u.len.n = const_val;
        ins->src_reg[0] = JIT_IR_VREG_NONE;
        ins->flags |= JIT_IR_FLAG_CONSTANT_FOLDED;
      }
      continue;
    }

    /* RPOS, RTAB, POS, TAB with constant src_reg[0] */
    if (ins->opcode == JIT_IR_RPOS || ins->opcode == JIT_IR_RTAB ||
        ins->opcode == JIT_IR_POS || ins->opcode == JIT_IR_TAB) {
      uint32_t const_val;
      if (vreg_is_constant(r, ins->src_reg[0], &const_val)) {
        ins->u.rpos_rtab.n = const_val;
        ins->src_reg[0] = JIT_IR_VREG_NONE;
        ins->flags |= JIT_IR_FLAG_CONSTANT_FOLDED;
      }
      continue;
    }
  }
}

/* -------------------------------------------------------------------------
 * LICM (Loop-Invariant Code Motion)
 * -------------------------------------------------------------------------
 */

/** Check if a vreg is defined outside loop blocks (loop_depth == 0) or is
 *  loop-invariant.  Requires the instruction index for dominance check. */
static bool vreg_is_loop_invariant(const jit_ir_region_t *r, uint8_t vreg,
                                   uint16_t loop_header,
                                   const bool *block_in_loop) {
  if (vreg == JIT_IR_VREG_NONE)
    return true;

  for (size_t i = 0; i < r->count; i++) {
    const jit_ir_instr_t *ins = &r->instrs[i];
    if (ins->dst_reg != vreg)
      continue;
    uint16_t def_block = block_for_instr(r, i);
    if (def_block == UINT16_MAX)
      return true;
    return !block_in_loop[def_block];
  }
  return true; /* undefined vreg — treat as invariant (conservative) */
}

void jit_ir_licm(jit_ir_region_t *r) {
  if (!r || r->count == 0 || r->block_count == 0)
    return;

  /* For each loop (identified by header block), hoist loop-invariant
   * instructions to the preheader. */

  for (uint16_t header = 0; header < r->block_count; header++) {
    jit_ir_basic_block_t *hb = &r->blocks[header];
    if (hb->loop_depth == 0)
      continue;

    /* Find the preheader: the immediate dominator of the loop header */
    uint16_t preheader = hb->idom;
    if (preheader == UINT16_MAX || preheader == header)
      continue;

    /* Determine which blocks are in this loop.
     * Conservative: all blocks with loop_depth >= hb->loop_depth
     * that are reachable from header without going through preheader. */
    bool in_loop[4096];
    memset(in_loop, 0, r->block_count);
    {
      uint16_t stack[4096];
      uint16_t sp = 0;
      stack[sp++] = header;
      in_loop[header] = true;
      while (sp > 0) {
        uint16_t cur = stack[--sp];
        jit_ir_basic_block_t *cb = &r->blocks[cur];
        for (uint16_t si = 0; si < cb->succ_count; si++) {
          uint16_t ns = r->succ_pool[cb->succ_first + si];
          if (ns != preheader && ns < r->block_count && !in_loop[ns]) {
            in_loop[ns] = true;
            stack[sp++] = ns;
          }
        }
      }
    }

    /* Scan instructions in each loop block for hoisting candidates */
    /* Build list of instructions to hoist */
    size_t hoist_candidates[4096];
    uint16_t n_hoist = 0;

    for (uint16_t bi = 0; bi < r->block_count; bi++) {
      if (!in_loop[bi])
        continue;

      jit_ir_basic_block_t *bb = &r->blocks[bi];
      for (size_t i = bb->start_idx; i <= bb->end_idx && i < r->count; i++) {
        jit_ir_instr_t *ins = &r->instrs[i];

        /* Must be pure and have no side effects */
        if (!(ins->flags & JIT_IR_FLAG_PURE))
          continue;
        if (ins->flags & JIT_IR_FLAG_SIDE_EFFECT)
          continue;
        if (ins->flags & JIT_IR_FLAG_CALLOUT)
          continue;
        if (ins->opcode == JIT_IR_PHI || ins->opcode == JIT_IR_LABEL)
          continue;

        /* Check both source operands are loop-invariant */
        if (!vreg_is_loop_invariant(r, ins->src_reg[0], header, in_loop))
          continue;
        if (!vreg_is_loop_invariant(r, ins->src_reg[1], header, in_loop))
          continue;

        /* Candidate for hoisting */
        if (n_hoist < 4096)
          hoist_candidates[n_hoist++] = i;
      }
    }

    /* Hoist: move instructions to the preheader.
     * We implement this by swapping the instruction field contents: we move
     * the instruction to the position just before the first instruction of the
     * loop header block.  A simple approach is to save the instruction, shift
     * instructions down, and place at the target location.
     *
     * For simplicity, just mark as loop-invariant and skip the actual
     * instruction reordering (the backend can use this flag for scheduling).
     * Actual code motion is left to the backend register allocator. */
    for (uint16_t hi = 0; hi < n_hoist; hi++) {
      r->instrs[hoist_candidates[hi]].flags |= JIT_IR_FLAG_LOOP_INVARIANT;
    }
  }
}

/* -------------------------------------------------------------------------
 * Register allocator
 * -------------------------------------------------------------------------
 */

/** Available physical registers per architecture. */
#if defined(__aarch64__)
#define PHYS_REG_COUNT 8
#elif defined(__arm__)
#define PHYS_REG_COUNT 6
#elif defined(__x86_64__) || defined(__i386__)
#define PHYS_REG_COUNT 6
#elif defined(__riscv)
#define PHYS_REG_COUNT 8
#else
#define PHYS_REG_COUNT 4
#endif

jit_ir_regalloc_t *jit_ir_alloc_registers(jit_ir_region_t *r) {
  if (!r)
    return nullptr;

  jit_ir_regalloc_t *ra =
      (jit_ir_regalloc_t *)snobol_calloc(1, sizeof(jit_ir_regalloc_t));
  if (!ra)
    return nullptr;

  /* Initialise all to -1 (unmapped) */
  for (int i = 0; i < 256; i++)
    ra->phys_reg[i] = -1;

  /* Simple linear-scan: assign physical registers round-robin from the
   * set of live vregs.  This is a foundational allocator — the backend may
   * override with its own. */

  /* First, compute live ranges (min and max instruction index each vreg is
   * used). */
  size_t first_use[256], last_use[256];
  for (int i = 0; i < 256; i++) {
    first_use[i] = (size_t)-1;
    last_use[i] = 0;
  }

  for (size_t i = 0; i < r->count; i++) {
    const jit_ir_instr_t *ins = &r->instrs[i];
    if (ins->dst_reg != JIT_IR_VREG_NONE) {
      if (i < first_use[ins->dst_reg])
        first_use[ins->dst_reg] = i;
      last_use[ins->dst_reg] = i;
    }
    for (int k = 0; k < 2; k++) {
      if (ins->src_reg[k] != JIT_IR_VREG_NONE) {
        if (i < first_use[ins->src_reg[k]])
          first_use[ins->src_reg[k]] = i;
        last_use[ins->src_reg[k]] = i;
      }
    }
  }

  /* Linear scan: assign phys_reg to vregs in order of first_use */
  uint8_t active[256];
  uint16_t n_active = 0;

  for (uint16_t v = 1; v < r->vreg_next; v++) {
    if (first_use[v] == (size_t)-1 || r->use_count[v] == 0)
      continue; /* unused vreg */

    /* Expire inactive vregs */
    for (int ai = (int)n_active - 1; ai >= 0; ai--) {
      if (last_use[active[ai]] < first_use[v]) {
        active[ai] = active[--n_active];
      }
    }

    if (n_active < PHYS_REG_COUNT) {
      /* Assign next available register */
      if (n_active == 0) {
        ra->phys_reg[v] = 0;
      } else {
        /* Find lowest unused phys_reg among active */
        bool used[16] = {false};
        for (uint16_t ai = 0; ai < n_active; ai++)
          if (ra->phys_reg[active[ai]] >= 0 &&
              ra->phys_reg[active[ai]] < (int)(sizeof(used) / sizeof(used[0])))
            used[ra->phys_reg[active[ai]]] = true;
        for (int p = 0; p < PHYS_REG_COUNT; p++) {
          if (!used[p]) {
            ra->phys_reg[v] = p;
            break;
          }
        }
      }
      active[n_active++] = v;
    } else {
      /* All registers in use: spill (assign -1, counted as spill) */
      ra->phys_reg[v] = -1;
      ra->spill_count++;
    }
  }

  return ra;
}

/* -------------------------------------------------------------------------
 * Debug dump
 * -------------------------------------------------------------------------
 */

static const char *ir_opcode_name(jit_ir_opcode_t op) {
  switch (op) {
  case JIT_IR_NOP:
    return "NOP";
  case JIT_IR_ACCEPT:
    return "ACCEPT";
  case JIT_IR_FAIL:
    return "FAIL";
  case JIT_IR_JMP:
    return "JMP";
  case JIT_IR_SPLIT:
    return "SPLIT";
  case JIT_IR_LIT:
    return "LIT";
  case JIT_IR_ANY:
    return "ANY";
  case JIT_IR_NOTANY:
    return "NOTANY";
  case JIT_IR_SPAN:
    return "SPAN";
  case JIT_IR_BREAK:
    return "BREAK";
  case JIT_IR_BREAKX:
    return "BREAKX";
  case JIT_IR_LEN:
    return "LEN";
  case JIT_IR_ANCHOR:
    return "ANCHOR";
  case JIT_IR_CAP_START:
    return "CAP_START";
  case JIT_IR_CAP_END:
    return "CAP_END";
  case JIT_IR_ASSIGN:
    return "ASSIGN";
  case JIT_IR_REPEAT_INIT:
    return "REPEAT_INIT";
  case JIT_IR_REPEAT_STEP:
    return "REPEAT_STEP";
  case JIT_IR_REM:
    return "REM";
  case JIT_IR_RPOS:
    return "RPOS";
  case JIT_IR_RTAB:
    return "RTAB";
  case JIT_IR_FENCE:
    return "FENCE";
  case JIT_IR_GOTO:
    return "GOTO";
  case JIT_IR_GOTO_F:
    return "GOTO_F";
  case JIT_IR_EMIT_LITERAL:
    return "EMIT_LITERAL";
  case JIT_IR_EMIT_CAPTURE:
    return "EMIT_CAPTURE";
  case JIT_IR_EMIT_EXPR:
    return "EMIT_EXPR";
  case JIT_IR_EMIT_FORMAT:
    return "EMIT_FORMAT";
  case JIT_IR_EMIT_TABLE:
    return "EMIT_TABLE";
  case JIT_IR_TABLE_GET:
    return "TABLE_GET";
  case JIT_IR_TABLE_SET:
    return "TABLE_SET";
  case JIT_IR_BAL:
    return "BAL";
  case JIT_IR_EVAL:
    return "EVAL";
  case JIT_IR_DYNAMIC:
    return "DYNAMIC";
  case JIT_IR_LABEL:
    return "LABEL";
  case JIT_IR_DYNAMIC_DEF:
    return "DYNAMIC_DEF";
  case JIT_IR_COPY:
    return "COPY";
  case JIT_IR_PHI:
    return "PHI";
  default:
    return "UNKNOWN";
  }
}

void jit_ir_dump(const jit_ir_region_t *r, FILE *out) {
  if (!r || !out)
    return;
  fprintf(out, "[snobol JIT-IR] region   count=%zu  vregs=%u  compilable=%s\n",
          r->count, (unsigned)r->vreg_next - 1u,
          r->non_compilable ? "NO" : "YES");

  /* Dump CFG if available */
  if (r->blocks && r->block_count > 0) {
    fprintf(out, "  blocks=%u", (unsigned)r->block_count);
    for (uint16_t bi = 0; bi < r->block_count; bi++) {
      const jit_ir_basic_block_t *bb = &r->blocks[bi];
      fprintf(out, " B%u[%zu..%zu] ld=%u", (unsigned)bb->id, bb->start_idx,
              bb->end_idx, (unsigned)bb->loop_depth);
    }
    fprintf(out, "\n");
  }

  for (size_t i = 0; i < r->count; i++) {
    const jit_ir_instr_t *ins = &r->instrs[i];
    fprintf(out, "  %4zu  bc_ip=%-5zu  %-14s", i, ins->bc_ip,
            ir_opcode_name(ins->opcode));
    if (ins->dst_reg != JIT_IR_VREG_NONE)
      fprintf(out, "  v%u=", (unsigned)ins->dst_reg);
    else
      fprintf(out, "       ");
    if (ins->src_reg[0])
      fprintf(out, " v%u", (unsigned)ins->src_reg[0]);
    if (ins->src_reg[1])
      fprintf(out, ",v%u", (unsigned)ins->src_reg[1]);

    /* Operand summary */
    switch (ins->opcode) {
    case JIT_IR_LIT:
      fprintf(out, "  len=%u", ins->u.lit.len);
      break;
    case JIT_IR_ANY:
    case JIT_IR_NOTANY:
    case JIT_IR_SPAN:
    case JIT_IR_BREAK:
    case JIT_IR_BREAKX:
      fprintf(out, "  set=%u", (unsigned)ins->u.set.set_id);
      break;
    case JIT_IR_SPLIT:
      fprintf(out, "  a=@%zu b=@%zu", ins->u.split.target_a,
              ins->u.split.target_b);
      break;
    case JIT_IR_JMP:
    case JIT_IR_GOTO:
      fprintf(out, "  tgt=@%zu", ins->u.jmp.target);
      break;
    case JIT_IR_GOTO_F:
      fprintf(out, "  tgt=@%zu (label %u)", ins->u.goto_f.target,
              ins->u.goto_f.label_id);
      break;
    case JIT_IR_LEN:
      fprintf(out, "  n=%u", ins->u.len.n);
      break;
    case JIT_IR_RPOS:
    case JIT_IR_RTAB:
      fprintf(out, "  n=%u", ins->u.rpos_rtab.n);
      break;
    case JIT_IR_CAP_START:
    case JIT_IR_CAP_END:
      fprintf(out, "  reg=%u", (unsigned)ins->u.cap.reg);
      break;
    case JIT_IR_ANCHOR:
      fprintf(out, "  type=%u", (unsigned)ins->u.anchor.type);
      break;
    case JIT_IR_ASSIGN:
      fprintf(out, "  var=%u reg=%u", (unsigned)ins->u.assign.var,
              (unsigned)ins->u.assign.reg);
      break;
    case JIT_IR_EMIT_LITERAL:
      fprintf(out, "  off=%u len=%u", ins->u.emit_lit.offset,
              ins->u.emit_lit.len);
      break;
    case JIT_IR_EMIT_CAPTURE:
      fprintf(out, "  reg=%u", (unsigned)ins->u.emit_cap.reg);
      break;
    case JIT_IR_EMIT_EXPR:
      fprintf(out, "  reg=%u type=%u", (unsigned)ins->u.emit_expr.reg,
              (unsigned)ins->u.emit_expr.expr_type);
      break;
    case JIT_IR_EMIT_FORMAT:
      fprintf(out, "  reg=%u fmt=%u width=%u", (unsigned)ins->u.emit_fmt.reg,
              (unsigned)ins->u.emit_fmt.fmt_type,
              (unsigned)ins->u.emit_fmt.width);
      break;
    case JIT_IR_TABLE_GET:
      fprintf(out, "  tbl=%u key=%u dst=%u", (unsigned)ins->u.tget.table_id,
              (unsigned)ins->u.tget.key_reg, (unsigned)ins->u.tget.dest_reg);
      break;
    case JIT_IR_TABLE_SET:
      fprintf(out, "  tbl=%u key=%u val=%u", (unsigned)ins->u.tset.table_id,
              (unsigned)ins->u.tset.key_reg, (unsigned)ins->u.tset.val_reg);
      break;
    case JIT_IR_BAL:
      fprintf(out, "  open=%u close=%u", ins->u.bal.open_cp,
              ins->u.bal.close_cp);
      break;
    case JIT_IR_EVAL:
      fprintf(out, "  fn=%u reg=%u", (unsigned)ins->u.eval.fn_id,
              (unsigned)ins->u.eval.reg);
      break;
    case JIT_IR_LABEL:
      fprintf(out, "  label_id=%u", (unsigned)ins->u.label.label_id);
      break;
    case JIT_IR_PHI:
      fprintf(out, "  nops=%u", (unsigned)ins->u.phi.operand_count);
      if (ins->u.phi.operand_count > 0 && r->phi_operands) {
        fprintf(out, "  [");
        for (uint8_t pi = 0; pi < ins->u.phi.operand_count; pi++) {
          size_t oi = ins->u.phi.operand_first + pi;
          if (oi < r->phi_operands_count) {
            if (pi > 0)
              fprintf(out, ", ");
            fprintf(out, "B%u:v%u", (unsigned)r->phi_operands[oi].pred_id,
                    (unsigned)r->phi_operands[oi].vreg);
          }
        }
        fprintf(out, "]");
      }
      break;
    default:
      break;
    }
    fprintf(out, "\n");
  }
  fflush(out);
}

/* -------------------------------------------------------------------------
 * VM opcode lifter
 *
 * Creates one IR instruction per VM opcode, pre-decoding all operands.
 * Mirrors the bytecode scanning logic in snobol_jit_compile() pass-1 and
 * jit_cfg_scan_block(), but stores decoded state in IR instead of emitting
 * ARM64 directly.
 * -------------------------------------------------------------------------
 */

/* Helper: append a side-effecting instruction (most VM ops are side-effecting)
 */
#define LIFT_SE(r, op, bc_ip)                                                  \
  jit_ir_append((r), (op), JIT_IR_FLAG_SIDE_EFFECT, (bc_ip), JIT_IR_VREG_NONE, \
                0, 0)

/* Helper: append a pure instruction with a fresh virtual register as output */
#define LIFT_PURE_VREG(r, op, bc_ip, dst)                                      \
  jit_ir_append((r), (op), JIT_IR_FLAG_PURE, (bc_ip), (dst), 0, 0)

jit_ir_region_t *jit_ir_lift_region(const VM *vm, size_t start_ip) {
  if (!vm || !vm->bc || start_ip >= vm->bc_len)
    return nullptr;

  jit_ir_region_t *r = jit_ir_region_new();
  if (!r)
    return nullptr;

  const uint8_t *bc = vm->bc;
  size_t bc_len = vm->bc_len;

  size_t scan = start_ip;

  while (scan < bc_len && !r->non_compilable) {
    uint8_t op = bc[scan];
    size_t oip = scan + 1; /* operand start */
    size_t cur = scan;     /* saved ip for this instruction */

    /* -----------------------------------------------------------------
     * Terminator opcodes: append IR and stop scanning
     * ----------------------------------------------------------------- */
    if (op == OP_ACCEPT) {
      LIFT_SE(r, JIT_IR_ACCEPT, cur);
      break;
    }
    if (op == OP_FAIL) {
      LIFT_SE(r, JIT_IR_FAIL, cur);
      break;
    }
    if (op == OP_REPEAT_INIT) {
      LIFT_SE(r, JIT_IR_REPEAT_INIT, cur);
      break;
    }
    if (op == OP_REPEAT_STEP) {
      LIFT_SE(r, JIT_IR_REPEAT_STEP, cur);
      break;
    }

    /* -----------------------------------------------------------------
     * Control-flow opcodes: append IR and stop scanning
     * (Linear lift: CFG expansion is done by the ARM64 backend)
     * ----------------------------------------------------------------- */
    if (op == OP_JMP) {
      if (oip + 4 > bc_len)
        break;
      size_t tgt = (size_t)ir_read_u32(bc + oip);
      if (!jit_ir_append(r, JIT_IR_JMP, JIT_IR_FLAG_SIDE_EFFECT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.jmp.target = tgt;
      /* Follow forward JMPs inline (same as current pass-1) */
      if (tgt > scan && tgt < bc_len) {
        scan = tgt;
      } else {
        break; /* backward or out-of-range: stop */
      }
      continue;
    }

    if (op == OP_SPLIT) {
      if (oip + 8 > bc_len)
        break;
      size_t ta = (size_t)ir_read_u32(bc + oip);
      size_t tb = (size_t)ir_read_u32(bc + oip + 4);
      if (!jit_ir_append(r, JIT_IR_SPLIT, JIT_IR_FLAG_SIDE_EFFECT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.split.target_a = ta;
      r->instrs[r->count - 1].u.split.target_b = tb;
      /* Teleport to arm-a for forward branches (mirrors pass-1 in
       * snobol_jit_compile). The ARM64 backend handles arm-b via
       * CFG/choice-push. */
      if (ta > scan && ta < bc_len && tb > scan && tb < bc_len) {
        scan = ta; /* inline arm-a */
      } else {
        break; /* backward target or out-of-range: hand off to interpreter */
      }
      continue;
    }

    if (op == OP_GOTO) {
      if (oip + 2 > bc_len)
        break;
      uint16_t label_id = ir_read_u16(bc + oip);
      size_t tgt = 0;
      if (vm->label_offsets && label_id < vm->label_count)
        tgt = (size_t)vm->label_offsets[label_id];
      if (!jit_ir_append(r, JIT_IR_GOTO, JIT_IR_FLAG_SIDE_EFFECT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.jmp.target = tgt;
      scan = oip + 2;
      break; /* GOTO terminates the linear scan */
    }

    /* -----------------------------------------------------------------
     * Linear (non-branching) opcodes
     * ----------------------------------------------------------------- */

    if (op == OP_NOP) {
      /* Pure no-op: allocate a virtual register so DCE can remove it */
      uint8_t vr = jit_ir_alloc_vreg(r);
      LIFT_PURE_VREG(r, JIT_IR_NOP, cur, vr);
      scan = oip;
      continue;
    }

    if (op == OP_LIT) {
      if (oip + 8 > bc_len)
        break;
      uint32_t off = ir_read_u32(bc + oip);
      uint32_t len = ir_read_u32(bc + oip + 4);
      size_t next = oip + 8 + (size_t)len;
      if (!jit_ir_append(r, JIT_IR_LIT, JIT_IR_FLAG_SIDE_EFFECT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.lit.data = bc + off;
      r->instrs[r->count - 1].u.lit.len = len;
      scan = next;
      continue;
    }

    if (op == OP_ANY || op == OP_NOTANY || op == OP_SPAN || op == OP_BREAK ||
        op == OP_BREAKX) {
      if (oip + 2 > bc_len)
        break;
      uint16_t set_id = ir_read_u16(bc + oip);
      jit_ir_opcode_t irop;
      switch (op) {
      case OP_ANY:
        irop = JIT_IR_ANY;
        break;
      case OP_NOTANY:
        irop = JIT_IR_NOTANY;
        break;
      case OP_SPAN:
        irop = JIT_IR_SPAN;
        break;
      case OP_BREAK:
        irop = JIT_IR_BREAK;
        break;
      default:
        irop = JIT_IR_BREAKX;
        break;
      }
      if (!jit_ir_append(r, irop, JIT_IR_FLAG_SIDE_EFFECT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.set.set_id = set_id;
      scan = oip + 2;
      continue;
    }

    if (op == OP_LEN) {
      if (oip + 4 > bc_len)
        break;
      uint32_t n = ir_read_u32(bc + oip);
      if (!jit_ir_append(r, JIT_IR_LEN, JIT_IR_FLAG_SIDE_EFFECT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.len.n = n;
      scan = oip + 4;
      continue;
    }

    if (op == OP_ANCHOR) {
      if (oip + 1 > bc_len)
        break;
      uint8_t type = bc[oip];
      if (!jit_ir_append(r, JIT_IR_ANCHOR, JIT_IR_FLAG_SIDE_EFFECT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.anchor.type = type;
      scan = oip + 1;
      continue;
    }

    if (op == OP_CAP_START || op == OP_CAP_END) {
      if (oip + 1 > bc_len)
        break;
      uint8_t reg = bc[oip];
      if (!jit_ir_append(
              r, (op == OP_CAP_START ? JIT_IR_CAP_START : JIT_IR_CAP_END),
              JIT_IR_FLAG_SIDE_EFFECT, cur, JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.cap.reg = reg;
      scan = oip + 1;
      continue;
    }

    if (op == OP_ASSIGN) {
      if (oip + 3 > bc_len)
        break;
      uint16_t var = ir_read_u16(bc + oip);
      uint8_t reg = bc[oip + 2];
      if (!jit_ir_append(r, JIT_IR_ASSIGN, JIT_IR_FLAG_SIDE_EFFECT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.assign.var = var;
      r->instrs[r->count - 1].u.assign.reg = reg;
      scan = oip + 3;
      continue;
    }

    if (op == OP_REM) {
      LIFT_SE(r, JIT_IR_REM, cur);
      scan = oip;
      continue;
    }

    if (op == OP_RPOS) {
      if (oip + 4 > bc_len)
        break;
      uint32_t n = ir_read_u32(bc + oip);
      if (!jit_ir_append(r, JIT_IR_RPOS, JIT_IR_FLAG_SIDE_EFFECT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.rpos_rtab.n = n;
      scan = oip + 4;
      continue;
    }

    if (op == OP_RTAB) {
      if (oip + 4 > bc_len)
        break;
      uint32_t n = ir_read_u32(bc + oip);
      if (!jit_ir_append(r, JIT_IR_RTAB, JIT_IR_FLAG_SIDE_EFFECT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.rpos_rtab.n = n;
      scan = oip + 4;
      continue;
    }

    if (op == OP_POS) {
      if (oip + 4 > bc_len)
        break;
      uint32_t n = ir_read_u32(bc + oip);
      if (!jit_ir_append(r, JIT_IR_POS, JIT_IR_FLAG_SIDE_EFFECT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.rpos_rtab.n = n;
      scan = oip + 4;
      continue;
    }

    if (op == OP_TAB) {
      if (oip + 4 > bc_len)
        break;
      uint32_t n = ir_read_u32(bc + oip);
      if (!jit_ir_append(r, JIT_IR_TAB, JIT_IR_FLAG_SIDE_EFFECT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.rpos_rtab.n = n;
      scan = oip + 4;
      continue;
    }

    if (op == OP_ABORT) {
      LIFT_SE(r, JIT_IR_ABORT, cur);
      break;
    }

    if (op == OP_SUCCEED) {
      LIFT_SE(r, JIT_IR_SUCCEED, cur);
      break;
    }

    if (op == OP_FENCE) {
      LIFT_SE(r, JIT_IR_FENCE, cur);
      scan = oip;
      continue;
    }

    if (op == OP_LABEL) {
      if (oip + 2 > bc_len)
        break;
      uint16_t label_id = ir_read_u16(bc + oip);
      /* Pure pseudo-op: allocate vreg so DCE can remove if unused */
      uint8_t vr = jit_ir_alloc_vreg(r);
      if (!jit_ir_append(r, JIT_IR_LABEL, JIT_IR_FLAG_PURE | JIT_IR_FLAG_PSEUDO,
                         cur, vr, 0, 0))
        break;
      r->instrs[r->count - 1].u.label.label_id = label_id;
      scan = oip + 2;
      continue;
    }

    if (op == OP_GOTO_F) {
      if (oip + 2 > bc_len)
        break;
      uint16_t label_id = ir_read_u16(bc + oip);
      size_t tgt = 0;
      if (vm->label_offsets && label_id < vm->label_count)
        tgt = (size_t)vm->label_offsets[label_id];
      /* NOP in JIT compiled regions (always falls through) — pure */
      uint8_t vr = jit_ir_alloc_vreg(r);
      if (!jit_ir_append(r, JIT_IR_GOTO_F,
                         JIT_IR_FLAG_PURE | JIT_IR_FLAG_PSEUDO, cur, vr, 0, 0))
        break;
      r->instrs[r->count - 1].u.goto_f.label_id = label_id;
      r->instrs[r->count - 1].u.goto_f.target = tgt;
      scan = oip + 2;
      continue;
    }

    if (op == OP_DYNAMIC_DEF) {
      if (oip + 4 > bc_len)
        break;
      uint32_t src_len = ir_read_u32(bc + oip);
      size_t after_src = oip + 4 + src_len;
      if (after_src + 4 > bc_len)
        break;
      uint32_t dyn_bcl = ir_read_u32(bc + after_src);
      /* Pure pseudo-op */
      uint8_t vr = jit_ir_alloc_vreg(r);
      if (!jit_ir_append(r, JIT_IR_DYNAMIC_DEF,
                         JIT_IR_FLAG_PURE | JIT_IR_FLAG_PSEUDO, cur, vr, 0, 0))
        break;
      scan = after_src + 4 + dyn_bcl;
      continue;
    }

    /* Call-out opcodes ------------------------------------------------- */

    if (op == OP_EMIT_LITERAL) {
      if (oip + 8 > bc_len)
        break;
      uint32_t elt_off = ir_read_u32(bc + oip);
      uint32_t elt_len = ir_read_u32(bc + oip + 4);
      size_t next = oip + 8;
      if ((size_t)elt_off == next)
        next += (size_t)elt_len;
      if (!jit_ir_append(r, JIT_IR_EMIT_LITERAL,
                         JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.emit_lit.data = bc + elt_off;
      r->instrs[r->count - 1].u.emit_lit.len = elt_len;
      r->instrs[r->count - 1].u.emit_lit.offset = elt_off;
      scan = next;
      continue;
    }

    if (op == OP_EMIT_CAPTURE) {
      if (oip + 1 > bc_len)
        break;
      uint8_t reg = bc[oip];
      if (!jit_ir_append(r, JIT_IR_EMIT_CAPTURE,
                         JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.emit_cap.reg = reg;
      scan = oip + 1;
      continue;
    }

    if (op == OP_EMIT_EXPR) {
      if (oip + 2 > bc_len)
        break;
      uint8_t reg = bc[oip];
      uint8_t expr_type = bc[oip + 1];
      if (!jit_ir_append(r, JIT_IR_EMIT_EXPR,
                         JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.emit_expr.reg = reg;
      r->instrs[r->count - 1].u.emit_expr.expr_type = expr_type;
      scan = oip + 2;
      continue;
    }

    if (op == OP_EMIT_FORMAT) {
      if (oip + 2 > bc_len)
        break;
      uint8_t reg = bc[oip];
      uint8_t fmt_type = bc[oip + 1];
      uint16_t width = 0;
      uint8_t fill = 0;
      size_t next = oip + 2;
      if (fmt_type == SNBL_FMT_LPAD || fmt_type == SNBL_FMT_RPAD) {
        if (next + 3 > bc_len)
          break;
        width = ir_read_u16(bc + next);
        fill = bc[next + 2];
        next += 3;
      }
      if (!jit_ir_append(r, JIT_IR_EMIT_FORMAT,
                         JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.emit_fmt.reg = reg;
      r->instrs[r->count - 1].u.emit_fmt.fmt_type = fmt_type;
      r->instrs[r->count - 1].u.emit_fmt.width = width;
      r->instrs[r->count - 1].u.emit_fmt.fill_char = fill;
      scan = next;
      continue;
    }

    if (op == OP_EMIT_TABLE) {
      if (oip + 4 > bc_len)
        break;
      uint8_t ktype = bc[oip + 2];
      uint8_t nlen = bc[oip + 3];
      size_t after = oip + 4 + nlen;
      if (after > bc_len)
        break;
      size_t next;
      if (ktype == 0) {
        if (after + 2 > bc_len)
          break;
        uint16_t kl = ir_read_u16(bc + after);
        next = after + 2 + kl;
      } else if (ktype == 1) {
        next = after + 1;
      } else {
        break;
      }
      /* bc_ip carries the instruction offset needed by the runtime helper */
      if (!jit_ir_append(r, JIT_IR_EMIT_TABLE,
                         JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      scan = next;
      continue;
    }

    if (op == OP_TABLE_GET) {
      if (oip + 5 > bc_len)
        break;
      uint16_t tid = ir_read_u16(bc + oip);
      uint8_t kreg = bc[oip + 2];
      uint8_t dreg = bc[oip + 3];
      uint8_t nlen = bc[oip + 4];
      if (!jit_ir_append(r, JIT_IR_TABLE_GET,
                         JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.tget.table_id = tid;
      r->instrs[r->count - 1].u.tget.key_reg = kreg;
      r->instrs[r->count - 1].u.tget.dest_reg = dreg;
      scan = oip + 5 + nlen;
      continue;
    }

    if (op == OP_TABLE_SET) {
      if (oip + 5 > bc_len)
        break;
      uint16_t tid = ir_read_u16(bc + oip);
      uint8_t kreg = bc[oip + 2];
      uint8_t vreg = bc[oip + 3];
      uint8_t nlen = bc[oip + 4];
      if (!jit_ir_append(r, JIT_IR_TABLE_SET,
                         JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.tset.table_id = tid;
      r->instrs[r->count - 1].u.tset.key_reg = kreg;
      r->instrs[r->count - 1].u.tset.val_reg = vreg;
      scan = oip + 5 + nlen;
      continue;
    }

    if (op == OP_BAL) {
      if (oip + 8 > bc_len)
        break;
      uint32_t open = ir_read_u32(bc + oip);
      uint32_t close = ir_read_u32(bc + oip + 4);
      if (!jit_ir_append(r, JIT_IR_BAL,
                         JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.bal.open_cp = open;
      r->instrs[r->count - 1].u.bal.close_cp = close;
      scan = oip + 8;
      continue;
    }

    if (op == OP_EVAL) {
      if (oip + 3 > bc_len)
        break;
      uint16_t fn_id = ir_read_u16(bc + oip);
      uint8_t reg = bc[oip + 2];
      if (!jit_ir_append(r, JIT_IR_EVAL,
                         JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT, cur,
                         JIT_IR_VREG_NONE, 0, 0))
        break;
      r->instrs[r->count - 1].u.eval.fn_id = fn_id;
      r->instrs[r->count - 1].u.eval.reg = reg;
      scan = oip + 3;
      continue;
    }

    if (op == OP_DYNAMIC) {
      jit_ir_append(r, JIT_IR_DYNAMIC,
                    JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT, cur,
                    JIT_IR_VREG_NONE, 0, 0);
      scan = oip;
      continue;
    }

    /* Unknown opcode: stop scanning */
    break;
  }

  return r;
}

#undef LIFT_SE
#undef LIFT_PURE_VREG

#endif /* SNOBOL_JIT */
