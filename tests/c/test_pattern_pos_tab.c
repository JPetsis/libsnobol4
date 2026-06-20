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

void test_pattern_pos_suite(void) {
  test_suite("Pattern: POS");

  /* POS(3) on "abcde": match at position 3 */
  {
    uint32_t pos_n = 3;
    uint32_t len_n = 1;
    uint8_t bc[] = {OP_POS,
                    (uint8_t)(pos_n >> 24),
                    (uint8_t)(pos_n >> 16),
                    (uint8_t)(pos_n >> 8),
                    (uint8_t)(pos_n),
                    OP_ACCEPT};
    /* At pos=0, POS(3) should fail */
    size_t pos = 0;
    bool ok = run_bc(bc, sizeof(bc), "abcde", 5, &pos);
    test_assert(!ok, "POS(3): fails at position 0");

    /* With LEN(3) first to advance to pos=3, then POS(3) should succeed */
    uint8_t bc2[] = {OP_LEN,
                     (uint8_t)(len_n >> 24),
                     (uint8_t)(len_n >> 16),
                     (uint8_t)(len_n >> 8),
                     (uint8_t)(len_n),
                     OP_POS,
                     (uint8_t)(pos_n >> 24),
                     (uint8_t)(pos_n >> 16),
                     (uint8_t)(pos_n >> 8),
                     (uint8_t)(pos_n),
                     OP_ACCEPT};
    pos = 0;
    ok = run_bc(bc2, sizeof(bc2), "abcde", 5, &pos);
    test_assert(!ok, "POS(3): fails at position 1");

    /* With LEN(3) → LEN(1) only succeeds at pos=3 */
    uint32_t len3 = 3;
    uint8_t bc3[] = {OP_LEN,
                     (uint8_t)(len3 >> 24),
                     (uint8_t)(len3 >> 16),
                     (uint8_t)(len3 >> 8),
                     (uint8_t)(len3),
                     OP_POS,
                     (uint8_t)(pos_n >> 24),
                     (uint8_t)(pos_n >> 16),
                     (uint8_t)(pos_n >> 8),
                     (uint8_t)(pos_n),
                     OP_LEN,
                     (uint8_t)(len_n >> 24),
                     (uint8_t)(len_n >> 16),
                     (uint8_t)(len_n >> 8),
                     (uint8_t)(len_n),
                     OP_ACCEPT};
    pos = 0;
    ok = run_bc(bc3, sizeof(bc3), "abcde", 5, &pos);
    test_assert(ok, "POS(3): succeeds after LEN(3) at pos=3");
    test_assert(pos == 4, "POS(3): pos=4 after matching 'd'");
  }

  /* POS beyond string length should never match */
  {
    uint32_t pos_n = 10;
    uint8_t bc[] = {OP_POS,
                    (uint8_t)(pos_n >> 24),
                    (uint8_t)(pos_n >> 16),
                    (uint8_t)(pos_n >> 8),
                    (uint8_t)(pos_n),
                    OP_ACCEPT};
    size_t pos = 0;
    bool ok = run_bc(bc, sizeof(bc), "abc", 3, &pos);
    test_assert(!ok, "POS(10): fails on 'abc' (beyond length)");
  }

  /* POS(0) should match at position 0 */
  {
    uint32_t pos_n = 0;
    uint8_t bc[] = {OP_POS,
                    (uint8_t)(pos_n >> 24),
                    (uint8_t)(pos_n >> 16),
                    (uint8_t)(pos_n >> 8),
                    (uint8_t)(pos_n),
                    OP_ACCEPT};
    size_t pos = 0;
    bool ok = run_bc(bc, sizeof(bc), "abc", 3, &pos);
    test_assert(ok, "POS(0): succeeds at position 0");
  }
}

void test_pattern_tab_suite(void) {
  test_suite("Pattern: TAB");

  /* TAB(2) LEN(2) on "abcde": advance to pos=2, match "cd" */
  {
    uint32_t tab_n = 2;
    uint32_t len_n = 2;
    uint8_t bc[] = {OP_TAB,
                    (uint8_t)(tab_n >> 24),
                    (uint8_t)(tab_n >> 16),
                    (uint8_t)(tab_n >> 8),
                    (uint8_t)(tab_n),
                    OP_LEN,
                    (uint8_t)(len_n >> 24),
                    (uint8_t)(len_n >> 16),
                    (uint8_t)(len_n >> 8),
                    (uint8_t)(len_n),
                    OP_ACCEPT};
    size_t pos = 0;
    bool ok = run_bc(bc, sizeof(bc), "abcde", 5, &pos);
    test_assert(ok, "TAB(2): advances cursor and succeeds");
    test_assert(pos == 4, "TAB(2): pos=4 after LEN(2)");
  }

  /* TAB(0) should keep pos at 0 (beginning) */
  {
    uint32_t tab_n = 0;
    uint32_t len_n = 2;
    uint8_t bc[] = {OP_TAB,
                    (uint8_t)(tab_n >> 24),
                    (uint8_t)(tab_n >> 16),
                    (uint8_t)(tab_n >> 8),
                    (uint8_t)(tab_n),
                    OP_LEN,
                    (uint8_t)(len_n >> 24),
                    (uint8_t)(len_n >> 16),
                    (uint8_t)(len_n >> 8),
                    (uint8_t)(len_n),
                    OP_ACCEPT};
    size_t pos = 0;
    bool ok = run_bc(bc, sizeof(bc), "abcde", 5, &pos);
    test_assert(ok, "TAB(0): keeps pos at beginning");
    test_assert(pos == 2, "TAB(0): pos=2 after LEN(2)");
  }

  /* TAB(10) on "abc" should fail (beyond string length) */
  {
    uint32_t tab_n = 10;
    uint8_t bc[] = {OP_TAB,
                    (uint8_t)(tab_n >> 24),
                    (uint8_t)(tab_n >> 16),
                    (uint8_t)(tab_n >> 8),
                    (uint8_t)(tab_n),
                    OP_ACCEPT};
    size_t pos = 0;
    bool ok = run_bc(bc, sizeof(bc), "abc", 3, &pos);
    test_assert(!ok, "TAB(10): fails on 'abc' (beyond length)");
  }

  /* TAB from beyond target position should fail */
  {
    uint32_t len_n = 3;
    uint32_t tab_n = 1;
    uint8_t bc[] = {OP_LEN,
                    (uint8_t)(len_n >> 24),
                    (uint8_t)(len_n >> 16),
                    (uint8_t)(len_n >> 8),
                    (uint8_t)(len_n),
                    OP_TAB,
                    (uint8_t)(tab_n >> 24),
                    (uint8_t)(tab_n >> 16),
                    (uint8_t)(tab_n >> 8),
                    (uint8_t)(tab_n),
                    OP_ACCEPT};
    size_t pos = 0;
    bool ok = run_bc(bc, sizeof(bc), "abcde", 5, &pos);
    test_assert(!ok, "TAB(1): fails when pos(3) > target(1)");
  }
}
