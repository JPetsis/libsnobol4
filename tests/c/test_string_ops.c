/*
 * test_string_ops.c – Tests for SUBSTR and REPLACE
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "snobol/string_fn.h"
#include "snobol/vm.h"

extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

void test_string_ops_suite(void) {
  test_suite("String: SUBSTR / REPLACE / REPLACE_CHAR / LPAD / RPAD");

  snobol_buf b = {0};
  snobol_buf_init(&b);

  /* --- SUBSTR --- */
  /* "hello world", pos=7, len=5 → "world" */
  (void)snobol_substr("hello world", 11, 7, 5, &b);
  test_assert(b.len == 5 && memcmp(b.data, "world", 5) == 0,
              "SUBSTR: 'hello world'[7,5] = 'world'");

  /* pos=1, len=5 → "hello" */
  (void)snobol_substr("hello world", 11, 1, 5, &b);
  test_assert(b.len == 5 && memcmp(b.data, "hello", 5) == 0,
              "SUBSTR: 'hello world'[1,5] = 'hello'");

  /* Unicode SUBSTR: "αβγδε" pos=2, len=3 → "βγδ" */
  const char *greek = "\xCE\xB1\xCE\xB2\xCE\xB3\xCE\xB4\xCE\xB5"; /* αβγδε */
  (void)snobol_substr(greek, 10, 2, 3, &b);
  test_assert(b.len == 6 && memcmp(b.data, "\xCE\xB2\xCE\xB3\xCE\xB4", 6) == 0,
              "SUBSTR: 'αβγδε'[2,3] = 'βγδ' (codepoint positions)");

  /* pos=0 is invalid (1-based) */
  bool ok = snobol_substr("hello", 5, 0, 3, &b);
  test_assert(!ok, "SUBSTR: pos=0 returns false (1-based indexing)");

  /* --- REPLACE --- */
  (void)snobol_replace("hello hello", 11, "ll", 2, "xx", 2, &b);
  test_assert(b.len == 11 && memcmp(b.data, "hexxo hexxo", 11) == 0,
              "REPLACE: 'll'→'xx' in 'hello hello'");

  /* No occurrences: output equals input */
  (void)snobol_replace("hello", 5, "xyz", 3, "abc", 3, &b);
  test_assert(b.len == 5 && memcmp(b.data, "hello", 5) == 0,
              "REPLACE: no match → input unchanged");

  /* Empty from: no replacement */
  (void)snobol_replace("hello", 5, "", 0, "x", 1, &b);
  test_assert(b.len == 5 && memcmp(b.data, "hello", 5) == 0,
              "REPLACE: empty from → no replacement");

  /* --- REPLACE_CHAR --- */
  (void)snobol_replace_char("hello", 5, "el", 2, "xy", 2, &b);
  test_assert(b.len == 5 && memcmp(b.data, "hxyyo", 5) == 0,
              "REPLACE_CHAR: e→x, l→y in 'hello'");

  /* Rot13 */
  (void)snobol_replace_char("hello", 5, "abcdefghijklmnopqrstuvwxyz", 26,
                            "nopqrstuvwxyzabcdefghijklm", 26, &b);
  test_assert(b.len == 5 && memcmp(b.data, "uryyb", 5) == 0,
              "REPLACE_CHAR: rot13 of 'hello' = 'uryyb'");

  /* --- LPAD --- */
  (void)snobol_lpad("5", 1, 3, '0', &b);
  test_assert(b.len == 3 && memcmp(b.data, "005", 3) == 0,
              "LPAD: '5' padded to width 3 with '0' = '005'");

  /* Already wide enough */
  (void)snobol_lpad("hello", 5, 3, ' ', &b);
  test_assert(b.len == 5 && memcmp(b.data, "hello", 5) == 0,
              "LPAD: already at width → unchanged");

  /* --- RPAD --- */
  (void)snobol_rpad("hi", 2, 5, ' ', &b);
  test_assert(b.len == 5 && memcmp(b.data, "hi   ", 5) == 0,
              "RPAD: 'hi' padded to width 5 with space");

  snobol_buf_free(&b);
}
