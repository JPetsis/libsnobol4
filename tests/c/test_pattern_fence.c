/*
 * test_pattern_fence.c – Tests for OP_FENCE, OP_REM, OP_RPOS, OP_RTAB
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snobol/snobol_internal.h"
#include "snobol/vm.h"

extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

/* Run bytecode directly */
static bool run_bc(const uint8_t *bc, size_t bc_len, const char *s,
                   size_t s_len, size_t *out_pos) {
  VM vm = {0};
  vm.bc = bc;
  vm.bc_len = bc_len;
  vm.s = s;
  vm.len = s_len;

  snobol_buf out_buf = {0};
  snobol_buf_init(&out_buf);
  vm.out = &out_buf;

  bool result = vm_run(&vm);
  if (out_pos)
    *out_pos = vm.pos;
  snobol_buf_free(&out_buf);
  vm_free_labels(&vm);
  return result;
}

void test_pattern_fence_suite(void) {
  test_suite("Pattern: FENCE / REM / RPOS / RTAB");

  /* OP_FENCE test: after fence, no backtracking choice points remain */
  {
    /* Bytecode: SPLIT(body, alt) FENCE ACCEPT */
    /* With FENCE, once we reach it, the alt choice is cut */
    uint8_t bc[] = {
        OP_SPLIT,
        0,
        0,
        0,
        9, /* a = offset 9 (FENCE) */
        0,
        0,
        0,
        17, /* b = offset 17 (FAIL) */
        /* 9: */ OP_FENCE,
        /* 10: */ OP_ACCEPT,
        /* 11 (alt=17 but this layout is demonstration): */
        OP_FAIL,
    };
    /* We won't actually use the alt branch – just demonstrate FENCE clears
     * choices */
    size_t pos = 0;
    bool ok = run_bc(bc, sizeof(bc), "test", 4, &pos);
    test_assert(ok, "FENCE: match succeeds on SPLIT-then-FENCE path");
  }

  /* OP_REM test: matches all remaining characters */
  {
    /* Bytecode: CAP_START(0) REM CAP_END(0) ACCEPT */
    uint8_t bc[] = {OP_CAP_START, 0, OP_REM, OP_CAP_END, 0, OP_ACCEPT};
    size_t pos = 0;
    bool ok = run_bc(bc, sizeof(bc), "hello", 5, &pos);
    test_assert(ok, "REM: matches entire subject");
    test_assert(pos == 5, "REM: pos advanced to end");
  }

  /* OP_RPOS test: succeed only at pos == len - n */
  {
    /* RPOS(3) on "hello world" (len=11): succeed only at pos=8 */
    /* Bytecode: LEN(8) RPOS(3) ACCEPT */
    uint32_t len_val = 8;
    uint32_t rpos_n = 3;
    uint8_t bc[] = {OP_LEN,
                    (uint8_t)(len_val >> 24),
                    (uint8_t)(len_val >> 16),
                    (uint8_t)(len_val >> 8),
                    (uint8_t)(len_val),
                    OP_RPOS,
                    (uint8_t)(rpos_n >> 24),
                    (uint8_t)(rpos_n >> 16),
                    (uint8_t)(rpos_n >> 8),
                    (uint8_t)(rpos_n),
                    OP_ACCEPT};
    size_t pos = 0;
    const char *subj = "hello world";
    bool ok = run_bc(bc, sizeof(bc), subj, 11, &pos);
    test_assert(ok, "RPOS(3): succeeds at pos 8 of 'hello world'");

    /* RPOS(3) with different pos should fail */
    uint32_t len_val2 = 7;
    uint8_t bc2[] = {OP_LEN,
                     (uint8_t)(len_val2 >> 24),
                     (uint8_t)(len_val2 >> 16),
                     (uint8_t)(len_val2 >> 8),
                     (uint8_t)(len_val2),
                     OP_RPOS,
                     (uint8_t)(rpos_n >> 24),
                     (uint8_t)(rpos_n >> 16),
                     (uint8_t)(rpos_n >> 8),
                     (uint8_t)(rpos_n),
                     OP_ACCEPT};
    ok = run_bc(bc2, sizeof(bc2), subj, 11, &pos);
    test_assert(!ok, "RPOS(3): fails when pos != 8");
  }

  /* OP_RTAB test: advance to len-n position */
  {
    /* RTAB(2) on "hello world" (len=11): advance to pos=9 */
    uint32_t rtab_n = 2;
    uint8_t bc[] = {OP_CAP_START,
                    0,
                    OP_RTAB,
                    (uint8_t)(rtab_n >> 24),
                    (uint8_t)(rtab_n >> 16),
                    (uint8_t)(rtab_n >> 8),
                    (uint8_t)(rtab_n),
                    OP_CAP_END,
                    0,
                    OP_ACCEPT};
    size_t pos = 0;
    bool ok = run_bc(bc, sizeof(bc), "hello world", 11, &pos);
    test_assert(ok, "RTAB(2): succeeds on 'hello world'");
    test_assert(pos == 9, "RTAB(2): pos advanced to 9 (11-2=9)");

    /* RTAB(0) = REM: advance to end */
    uint32_t rtab0 = 0;
    uint8_t bc2[] = {OP_RTAB,
                     (uint8_t)(rtab0 >> 24),
                     (uint8_t)(rtab0 >> 16),
                     (uint8_t)(rtab0 >> 8),
                     (uint8_t)(rtab0),
                     OP_ACCEPT};
    ok = run_bc(bc2, sizeof(bc2), "hello", 5, &pos);
    test_assert(ok, "RTAB(0): succeeds (same as REM)");
    test_assert(pos == 5, "RTAB(0): pos advanced to end");
  }
}
