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
#else
  test_suite("JIT IR (not built)");
  test_assert(true, "JIT not enabled on this platform — IR tests skipped");
#endif
}
