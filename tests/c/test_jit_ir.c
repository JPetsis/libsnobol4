/**
 * @file test_jit_ir.c
 * @brief Unit tests for the SNOBOL4 JIT neutral IR layer.
 *
 * Tests:
 *  - IR region builder (append, vreg allocation, use-count)
 *  - Dead-code elimination (DCE) pass
 *  - Copy-propagation pass
 *  - 256-virtual-register limit marking region non-compilable
 *  - IR dump (smoke test: no crash, output written to /dev/null)
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/include/snobol/jit_ir.h"

void test_suite(const char *name);
void test_assert(bool condition, const char *message);

#ifdef SNOBOL_JIT

static void test_ir_region_alloc(void) {
  test_suite("IR region allocation");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  test_assert(r->count == 0, "initial count = 0");
  test_assert(!r->non_compilable, "initially compilable");
  jit_ir_region_free(r);
  test_assert(true, "region freed without crash");
}

static void test_ir_append_basic(void) {
  test_suite("IR append");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  bool ok = jit_ir_append(r, JIT_IR_NOP, JIT_IR_FLAG_PURE, 0, 0, 0, 0);
  test_assert(ok, "append returned true");
  test_assert(r->count == 1, "count = 1 after append");
  test_assert(r->instrs[0].opcode == JIT_IR_NOP, "opcode correct");
  jit_ir_region_free(r);
}

static void test_ir_vreg_alloc(void) {
  test_suite("IR vreg allocation");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  uint8_t v1 = jit_ir_alloc_vreg(r);
  uint8_t v2 = jit_ir_alloc_vreg(r);
  test_assert(v1 == 1, "first vreg = 1");
  test_assert(v2 == 2, "second vreg = 2");
  test_assert(!r->non_compilable, "still compilable");
  jit_ir_region_free(r);
}

static void test_ir_vreg_use_count(void) {
  test_suite("IR vreg use count");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  uint8_t v1 = jit_ir_alloc_vreg(r);
  test_assert(r->use_count[v1] == 0, "initial use count = 0");
  jit_ir_append(r, JIT_IR_NOP, JIT_IR_FLAG_PURE, 0, JIT_IR_VREG_NONE, v1, 0);
  test_assert(r->use_count[v1] == 1, "use count incremented to 1");
  jit_ir_region_free(r);
}

/* DCE -------------------------------------------------------------------- */

static void test_ir_dce_removes_unused_pure(void) {
  test_suite("DCE: removes unused pure instruction");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  uint8_t v1 = jit_ir_alloc_vreg(r);
  jit_ir_append(r, JIT_IR_NOP, JIT_IR_FLAG_PURE, 0, v1, 0, 0);
  test_assert(r->count == 1, "one instruction before DCE");
  test_assert(r->use_count[v1] == 0, "v1 use count = 0");
  jit_ir_dce(r);
  test_assert(r->count == 0, "DCE removes dead pure NOP");
  jit_ir_region_free(r);
}

static void test_ir_dce_keeps_side_effecting(void) {
  test_suite("DCE: retains side-effecting instruction");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  jit_ir_append(r, JIT_IR_ACCEPT, JIT_IR_FLAG_SIDE_EFFECT, 0, JIT_IR_VREG_NONE,
                0, 0);
  jit_ir_dce(r);
  test_assert(r->count == 1, "DCE keeps side-effecting ACCEPT");
  jit_ir_region_free(r);
}

static void test_ir_dce_keeps_used_pure(void) {
  test_suite("DCE: keeps pure instruction whose output is used");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  uint8_t v1 = jit_ir_alloc_vreg(r);
  jit_ir_append(r, JIT_IR_NOP, JIT_IR_FLAG_PURE, 0, v1, 0, 0);
  jit_ir_append(r, JIT_IR_ACCEPT, JIT_IR_FLAG_SIDE_EFFECT, 1, JIT_IR_VREG_NONE,
                v1, 0);
  test_assert(r->use_count[v1] == 1, "v1 has 1 use");
  jit_ir_dce(r);
  test_assert(r->count == 2, "both instructions survive DCE");
  jit_ir_region_free(r);
}

static void test_ir_dce_multiple_dead(void) {
  test_suite("DCE: removes multiple dead pure NOPs");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  for (int i = 0; i < 5; i++) {
    uint8_t vr = jit_ir_alloc_vreg(r);
    jit_ir_append(r, JIT_IR_NOP, JIT_IR_FLAG_PURE, (size_t)i, vr, 0, 0);
  }
  test_assert(r->count == 5, "5 instructions before DCE");
  jit_ir_dce(r);
  test_assert(r->count == 0, "all 5 dead NOPs removed by DCE");
  jit_ir_region_free(r);
}

/* Copy-propagation -------------------------------------------------------- */

static void test_ir_copy_prop_folds_copy(void) {
  test_suite("Copy-propagation: fold COPY into consumer");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  uint8_t v1 = jit_ir_alloc_vreg(r);
  uint8_t v2 = jit_ir_alloc_vreg(r);
  jit_ir_append(r, JIT_IR_NOP, JIT_IR_FLAG_PURE, 0, v1, 0, 0);
  jit_ir_append(r, JIT_IR_COPY, JIT_IR_FLAG_PURE, 1, v2, v1, 0);
  jit_ir_append(r, JIT_IR_ACCEPT, JIT_IR_FLAG_SIDE_EFFECT, 2, JIT_IR_VREG_NONE,
                v2, 0);
  test_assert(r->count == 3, "3 instructions before copy-prop");
  test_assert(r->use_count[v2] == 1, "v2 used once in ACCEPT");
  jit_ir_copy_propagation(r);
  test_assert(r->count == 2, "COPY removed after copy-propagation");
  bool found_nop = false, found_accept = false;
  for (size_t i = 0; i < r->count; i++) {
    if (r->instrs[i].opcode == JIT_IR_NOP)
      found_nop = true;
    if (r->instrs[i].opcode == JIT_IR_ACCEPT)
      found_accept = true;
  }
  test_assert(found_nop, "NOP(v1) still present");
  test_assert(found_accept, "ACCEPT still present");
  jit_ir_region_free(r);
}

static void test_ir_copy_prop_noop_when_no_copies(void) {
  test_suite("Copy-propagation: no-op when no COPY instructions");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  jit_ir_append(r, JIT_IR_ACCEPT, JIT_IR_FLAG_SIDE_EFFECT, 0, JIT_IR_VREG_NONE,
                0, 0);
  jit_ir_append(r, JIT_IR_FAIL, JIT_IR_FLAG_SIDE_EFFECT, 1, JIT_IR_VREG_NONE, 0,
                0);
  jit_ir_copy_propagation(r);
  test_assert(r->count == 2, "count unchanged (no COPY instructions)");
  jit_ir_region_free(r);
}

/* 256 vreg limit ---------------------------------------------------------- */

static void test_ir_vreg_limit_256(void) {
  test_suite("IR vreg limit (256 max)");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  bool all_valid = true;
  for (int i = 0; i < 255; i++) {
    uint8_t v = jit_ir_alloc_vreg(r);
    if (v == JIT_IR_VREG_NONE) {
      all_valid = false;
      break;
    }
  }
  test_assert(all_valid, "255 vregs allocated without overflow");
  test_assert(!r->non_compilable, "region compilable after 255 vregs");
  uint8_t v_over = jit_ir_alloc_vreg(r);
  test_assert(v_over == JIT_IR_VREG_NONE, "256th vreg returns VREG_NONE");
  test_assert(r->non_compilable, "region marked non-compilable");
  jit_ir_region_free(r);
}

/* Dump smoke test --------------------------------------------------------- */

static void test_ir_dump_smoke(void) {
  test_suite("IR dump (no-crash smoke test)");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  jit_ir_append(r, JIT_IR_NOP, JIT_IR_FLAG_PURE, 0, 1, 0, 0);
  jit_ir_append(r, JIT_IR_ACCEPT, JIT_IR_FLAG_SIDE_EFFECT, 1, 0, 0, 0);
  FILE *sink = fopen("/dev/null", "w");
  if (sink) {
    jit_ir_dump(r, sink);
    fclose(sink);
  }
  test_assert(true, "jit_ir_dump completed without crash");
  jit_ir_region_free(r);
}

/* CFG construction ------------------------------------------------------- */

static void test_ir_cfg_build(void) {
  test_suite("CFG: basic block construction");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  /* Single block: NOP, ACCEPT */
  jit_ir_append(r, JIT_IR_NOP, JIT_IR_FLAG_PURE, 0, 1, 0, 0);
  jit_ir_append(r, JIT_IR_ACCEPT, JIT_IR_FLAG_SIDE_EFFECT, 1, 0, 0, 0);
  jit_ir_build_cfg(r);
  test_assert(r->block_count >= 1, "at least one block constructed");
  test_assert(r->blocks != NULL, "blocks array allocated");
  jit_ir_region_free(r);
}

static void test_ir_cfg_with_label(void) {
  test_suite("CFG: label creates new block");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  jit_ir_append(r, JIT_IR_NOP, JIT_IR_FLAG_PURE, 0, 1, 0, 0);
  uint8_t lv = jit_ir_alloc_vreg(r);
  jit_ir_append(r, JIT_IR_LABEL, JIT_IR_FLAG_PURE | JIT_IR_FLAG_PSEUDO, 2, lv,
                0, 0);
  r->instrs[r->count - 1].u.label.label_id = 5;
  jit_ir_append(r, JIT_IR_ACCEPT, JIT_IR_FLAG_SIDE_EFFECT, 3, 0, 0, 0);
  jit_ir_build_cfg(r);
  test_assert(r->block_count >= 2, "label creates second block");
  jit_ir_region_free(r);
}

/* Dominators ------------------------------------------------------------- */

static void test_ir_dominators(void) {
  test_suite("Dominators: compute on simple CFG");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  jit_ir_append(r, JIT_IR_NOP, JIT_IR_FLAG_PURE, 0, 1, 0, 0);
  jit_ir_append(r, JIT_IR_ACCEPT, JIT_IR_FLAG_SIDE_EFFECT, 1, 0, 0, 0);
  jit_ir_build_cfg(r);
  jit_ir_compute_dominators(r);
  test_assert(r->blocks[0].idom == 0, "entry block dominates itself");
  jit_ir_region_free(r);
}

/* GVN -------------------------------------------------------------------- */

static void test_ir_gvn_dedup(void) {
  test_suite("GVN: deduplicates identical pure instructions");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  /* Two identical NOPs with different dst vregs — GVN should turn the
   * second into a COPY of the first. */
  uint8_t v1 = jit_ir_alloc_vreg(r);
  uint8_t v2 = jit_ir_alloc_vreg(r);
  jit_ir_append(r, JIT_IR_NOP, JIT_IR_FLAG_PURE, 0, v1, 0, 0);
  jit_ir_append(r, JIT_IR_NOP, JIT_IR_FLAG_PURE, 1, v2, 0, 0);
  jit_ir_build_cfg(r);
  jit_ir_compute_dominators(r);
  jit_ir_gvn(r);
  /* After GVN, second NOP should become a COPY */
  bool found_copy = false;
  for (size_t i = 0; i < r->count; i++) {
    if (r->instrs[i].opcode == JIT_IR_COPY)
      found_copy = true;
  }
  test_assert(found_copy, "GVN converted duplicate to COPY");
  jit_ir_region_free(r);
}

/* Constant folding ------------------------------------------------------- */

static void test_ir_constant_fold(void) {
  test_suite("Constant folding: smoke test (no crash)");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  jit_ir_append(r, JIT_IR_LEN, JIT_IR_FLAG_PURE, 0, 0, 0, 0);
  r->instrs[0].u.len.n = 3;
  jit_ir_constant_fold(r);
  test_assert(true, "constant_fold completed without crash");
  jit_ir_region_free(r);
}

/* LICM ------------------------------------------------------------------- */

static void test_ir_licm(void) {
  test_suite("LICM: smoke test (no crash)");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  jit_ir_append(r, JIT_IR_NOP, JIT_IR_FLAG_PURE, 0, 1, 0, 0);
  jit_ir_append(r, JIT_IR_ACCEPT, JIT_IR_FLAG_SIDE_EFFECT, 1, 0, 0, 0);
  jit_ir_build_cfg(r);
  jit_ir_compute_dominators(r);
  jit_ir_find_loops(r);
  jit_ir_licm(r);
  test_assert(true, "LICM completed without crash");
  jit_ir_region_free(r);
}

/* Register allocator ----------------------------------------------------- */

static void test_ir_regalloc_basic(void) {
  test_suite("Register allocator: basic allocation");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  /* Create two vregs with overlapping live ranges */
  uint8_t v1 = jit_ir_alloc_vreg(r);
  uint8_t v2 = jit_ir_alloc_vreg(r);
  jit_ir_append(r, JIT_IR_NOP, JIT_IR_FLAG_PURE, 0, v1, 0, 0);
  jit_ir_append(r, JIT_IR_ACCEPT, JIT_IR_FLAG_SIDE_EFFECT, 1, 0, v1, 0);
  jit_ir_append(r, JIT_IR_NOP, JIT_IR_FLAG_PURE, 2, v2, 0, 0);
  jit_ir_append(r, JIT_IR_ACCEPT, JIT_IR_FLAG_SIDE_EFFECT, 3, 0, v2, 0);
  jit_ir_regalloc_t *ra = jit_ir_alloc_registers(r);
  test_assert(ra != NULL, "regalloc returned non-NULL");
  test_assert(ra->phys_reg[v1] >= 0, "v1 assigned a physical register");
  test_assert(ra->phys_reg[v2] >= 0, "v2 assigned a physical register");
  test_assert(ra->spill_count == 0, "no spills expected for 2 vregs");
  free(ra);
  jit_ir_region_free(r);
}

static void test_ir_regalloc_spill(void) {
  test_suite("Register allocator: spill under pressure");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  /* Create 16 overlapping live ranges by defining all vregs first, then
   * using each later — this forces the linear-scan allocator to hold all
   * 16 vregs simultaneously, exceeding the available phys-reg count. */
  uint8_t vregs[16];
  for (int i = 0; i < 16; i++) {
    vregs[i] = jit_ir_alloc_vreg(r);
    if (vregs[i] == JIT_IR_VREG_NONE)
      break;
    jit_ir_append(r, JIT_IR_NOP, JIT_IR_FLAG_PURE, (size_t)i, vregs[i], 0, 0);
  }
  /* Now use each vreg at a later instruction — keeps them all live. */
  for (int i = 0; i < 16; i++) {
    if (vregs[i] == JIT_IR_VREG_NONE)
      break;
    jit_ir_append(r, JIT_IR_ACCEPT, JIT_IR_FLAG_SIDE_EFFECT, (size_t)(16 + i),
                  0, vregs[i], 0);
  }
  jit_ir_regalloc_t *ra = jit_ir_alloc_registers(r);
  test_assert(ra != NULL, "regalloc returned non-NULL");
  /* Some vregs should be spilled (Phys reg count is < 16) */
  test_assert(ra->spill_count > 0, "spills expected under register pressure");
  free(ra);
  jit_ir_region_free(r);
}

/* Phi node --------------------------------------------------------------- */

static void test_ir_phi_append(void) {
  test_suite("Phi: append with operands");
  jit_ir_region_t *r = jit_ir_region_new();
  test_assert(r != NULL, "region allocated");
  jit_ir_phi_operand_t ops[2] = {{.vreg = 1, .pred_id = 0},
                                  {.vreg = 2, .pred_id = 1}};
  size_t idx = jit_ir_append_phi(r, 0, 3, ops, 2);
  test_assert(idx != (size_t)-1, "phi append returned valid index");
  test_assert(r->count == 1, "one instruction after phi append");
  test_assert(r->instrs[0].opcode == JIT_IR_PHI, "opcode is PHI");
  test_assert(r->instrs[0].dst_reg == 3, "phi dst_reg = 3");
  test_assert(r->instrs[0].u.phi.operand_count == 2, "2 phi operands");
  test_assert(r->phi_operands_count == 2, "phi operands pool has 2 entries");
  test_assert(r->use_count[1] >= 1, "v1 use count incremented");
  test_assert(r->use_count[2] >= 1, "v2 use count incremented");
  jit_ir_region_free(r);
}

#endif /* SNOBOL_JIT */

void test_jit_ir_suite(void) {
#ifdef SNOBOL_JIT
  test_ir_region_alloc();
  test_ir_append_basic();
  test_ir_vreg_alloc();
  test_ir_vreg_use_count();
  test_ir_dce_removes_unused_pure();
  test_ir_dce_keeps_side_effecting();
  test_ir_dce_keeps_used_pure();
  test_ir_dce_multiple_dead();
  test_ir_copy_prop_folds_copy();
  test_ir_copy_prop_noop_when_no_copies();
  test_ir_vreg_limit_256();
  test_ir_dump_smoke();
  test_ir_cfg_build();
  test_ir_cfg_with_label();
  test_ir_dominators();
  test_ir_gvn_dedup();
  test_ir_constant_fold();
  test_ir_licm();
  test_ir_regalloc_basic();
  test_ir_regalloc_spill();
  test_ir_phi_append();
#else
  test_suite("JIT IR (not built)");
  test_assert(true, "JIT not enabled on this platform — IR tests skipped");
#endif
}
