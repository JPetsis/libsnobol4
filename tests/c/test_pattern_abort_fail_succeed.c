#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snobol/snobol_internal.h"
#include "snobol/vm.h"

extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

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

/*
 * Helper: patch a 4-byte big-endian u32 at a given byte offset.
 */
static void patch_u32(uint8_t *bc, size_t off, uint32_t v) {
  bc[off + 0] = (uint8_t)((v >> 24) & 0xff);
  bc[off + 1] = (uint8_t)((v >> 16) & 0xff);
  bc[off + 2] = (uint8_t)((v >> 8) & 0xff);
  bc[off + 3] = (uint8_t)(v & 0xff);
}

void test_pattern_abort_suite(void) {
  test_suite("Pattern: ABORT");

  /* ABORT after matching 'a' on "abc" should terminate with failure */
  {
    /* Bytecode layout:
     *   0: OP_LIT  off=9  len=1  9:'a'
     *  10: OP_ABORT
     */
    uint8_t bc[] = {OP_LIT, 0, 0, 0, 9, 0, 0, 0, 1, 'a', OP_ABORT};
    size_t pos = 0;
    bool ok = run_bc(bc, sizeof(bc), "abc", 3, &pos);
    test_assert(!ok, "ABORT: 'a' ABORT on 'abc' terminates with failure");
  }

  /* ABORT in alternation should prevent backtracking */
  {
    /* Bytecode layout:
     *   0: OP_SPLIT  a=9  b=20
     *   9: OP_LIT  off=18  len=1  18:'a'
     *  19: OP_ABORT
     *  20: OP_LIT  off=29  len=1  29:'b'
     *  30: OP_ACCEPT
     */
    uint8_t bc[] = {OP_SPLIT, 0, 0,  0, 9, 0, 0, 0,   20,       OP_LIT, 0,
                    0,        0, 18, 0, 0, 0, 1, 'a', OP_ABORT, OP_LIT, 0,
                    0,        0, 29, 0, 0, 0, 1, 'b', OP_ACCEPT};
    size_t pos = 0;
    bool ok = run_bc(bc, sizeof(bc), "abc", 3, &pos);
    test_assert(!ok, "ABORT: prevents backtracking to alt branch");
  }
}

void test_pattern_fail_suite(void) {
  test_suite("Pattern: FAIL");

  /* 'a' FAIL on "abc": matches 'a', then FAIL forces backtrack */
  {
    /* Bytecode layout:
     *   0: OP_LIT  off=9  len=1  9:'a'
     *  10: OP_FAIL
     *  11: OP_LIT  off=20 len=1 20:'x'
     *  21: OP_ACCEPT
     */
    uint8_t bc[] = {OP_LIT, 0, 0, 0, 9,  0, 0, 0, 1, 'a', OP_FAIL,
                    OP_LIT, 0, 0, 0, 20, 0, 0, 0, 1, 'x', OP_ACCEPT};
    size_t pos = 0;
    bool ok = run_bc(bc, sizeof(bc), "abc", 3, &pos);
    test_assert(!ok, "FAIL: 'a' FAIL on 'abc' eventually fails");
  }

  /* FAIL with a choice point should backtrack.
   * Both arms match the same literal at the same position;
   * arm-a matches first, then FAIL forces backtrack to arm-b. */
  {
    /* Bytecode layout:
     *   0: OP_SPLIT  a=9  b=20
     *   9: OP_LIT  off=18  len=1  18:'a'
     *  19: OP_FAIL
     *  20: OP_LIT  off=29  len=1  29:'a'
     *  30: OP_ACCEPT
     */
    uint8_t bc[] = {OP_SPLIT, 0, 0,  0, 9, 0, 0, 0,   20,       OP_LIT, 0,
                    0,        0, 18, 0, 0, 0, 1, 'a', OP_FAIL,  OP_LIT, 0,
                    0,        0, 29, 0, 0, 0, 1, 'a', OP_ACCEPT};
    size_t pos = 0;
    bool ok = run_bc(bc, sizeof(bc), "abc", 3, &pos);
    test_assert(ok, "FAIL: backtracking to alt branch succeeds");
    test_assert(pos == 1, "FAIL: alt branch matched 'a' at pos=1");
  }
}

void test_pattern_succeed_suite(void) {
  test_suite("Pattern: SUCCEED");

  /* 'a' SUCCEED LEN(10) on "abc": succeeds immediately at pos=1 */
  {
    /* Bytecode layout:
     *   0: OP_LIT  off=9  len=1  9:'a'
     *  10: OP_SUCCEED
     *  11: OP_LIT  off=20 len=10 20-29: padding (never executed)
     *  30: OP_ACCEPT
     */
    uint8_t bc[31];
    memset(bc, 0, sizeof(bc));
    bc[0] = OP_LIT;
    patch_u32(bc, 1, 9);
    patch_u32(bc, 5, 1);
    bc[9] = 'a';
    bc[10] = OP_SUCCEED;
    bc[11] = OP_LIT;
    patch_u32(bc, 12, 20);
    patch_u32(bc, 16, 10);
    /* padding at 20-29 is zeros (never executed) */
    bc[30] = OP_ACCEPT;
    size_t pos = 0;
    bool ok = run_bc(bc, sizeof(bc), "abc", 3, &pos);
    test_assert(ok, "SUCCEED: skips remaining pattern and succeeds");
    test_assert(pos == 1, "SUCCEED: position stays at 1");
  }

  /* SUCCEED at start of string succeeds immediately */
  {
    /* Bytecode layout:
     *   0: OP_SUCCEED
     *   1: OP_LIT   off=10  len=10  10-19: padding (never executed)
     *  20: OP_ACCEPT
     */
    uint8_t bc[21];
    memset(bc, 0, sizeof(bc));
    bc[0] = OP_SUCCEED;
    bc[1] = OP_LIT;
    patch_u32(bc, 2, 10);
    patch_u32(bc, 6, 10);
    /* padding at 10-19 is zeros (never executed) */
    bc[20] = OP_ACCEPT;
    size_t pos = 0;
    bool ok = run_bc(bc, sizeof(bc), "abc", 3, &pos);
    test_assert(ok, "SUCCEED: succeeds immediately at pos=0");
    test_assert(pos == 0, "SUCCEED: position stays at 0");
  }
}
