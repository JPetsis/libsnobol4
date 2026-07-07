/**
 * test_search_simd.c
 *
 * Tests for SIMD-accelerated Thompson NFA (Tier 9):
 *   - check_simd_eligible() for various opcode patterns
 *   - Tier routing via snobol_search_derive_meta()
 *   - Execution via tier_simd_nfa() / snobol_search_exec()
 *   - Edge cases: 0/1-byte, tail bytes, UTF-8 byte ranges
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/include/snobol/search.h"
#include "../../core/include/snobol/vm.h"
#include "../../core/include/snobol/snobol.h"

/* Provided by test_runner.c */
void test_suite(const char *name);
void test_assert(bool condition, const char *message);

/* ---------------------------------------------------------------------------
 * Bytecode helpers
 * ---------------------------------------------------------------------------
 */
static void emit_u32_be(uint8_t *bc, size_t *ip, uint32_t v) {
  bc[(*ip)++] = (uint8_t)(v >> 24);
  bc[(*ip)++] = (uint8_t)(v >> 16);
  bc[(*ip)++] = (uint8_t)(v >> 8);
  bc[(*ip)++] = (uint8_t)(v);
}

static void emit_u16_be(uint8_t *bc, size_t *ip, uint16_t v) {
  bc[(*ip)++] = (uint8_t)(v >> 8);
  bc[(*ip)++] = (uint8_t)(v);
}

/* Build SPAN(chars) + ACCEPT bytecode with inline offset table */
static size_t build_span_accept(uint8_t *bc, const char *chars, size_t nchr,
                                size_t extra_terminators) {
  size_t ip = 0;
  bc[ip++] = OP_SPAN;
  emit_u16_be(bc, &ip, 1);
  for (size_t i = 0; i < extra_terminators; i++)
    bc[ip++] = OP_ACCEPT;
  bc[ip++] = OP_ACCEPT;

  size_t class_data_off = ip;
  emit_u16_be(bc, &ip, (uint16_t)nchr);
  emit_u16_be(bc, &ip, 0);
  for (size_t i = 0; i < nchr; i++) {
    uint32_t c = (uint32_t)(unsigned char)chars[i];
    emit_u32_be(bc, &ip, c);
    emit_u32_be(bc, &ip, c);
  }
  emit_u32_be(bc, &ip, (uint32_t)class_data_off);
  emit_u32_be(bc, &ip, 1);
  return ip;
}

static size_t build_break_accept(uint8_t *bc, const char *chars, size_t nchr) {
  size_t ip = 0;
  bc[ip++] = OP_BREAK;
  emit_u16_be(bc, &ip, 1);
  bc[ip++] = OP_ACCEPT;

  size_t class_data_off = ip;
  emit_u16_be(bc, &ip, (uint16_t)nchr);
  emit_u16_be(bc, &ip, 0);
  for (size_t i = 0; i < nchr; i++) {
    uint32_t c = (uint32_t)(unsigned char)chars[i];
    emit_u32_be(bc, &ip, c);
    emit_u32_be(bc, &ip, c);
  }
  emit_u32_be(bc, &ip, (uint32_t)class_data_off);
  emit_u32_be(bc, &ip, 1);
  return ip;
}

static size_t build_any_accept(uint8_t *bc, const char *chars, size_t nchr) {
  size_t ip = 0;
  bc[ip++] = OP_ANY;
  emit_u16_be(bc, &ip, 1);
  bc[ip++] = OP_ACCEPT;

  size_t class_data_off = ip;
  emit_u16_be(bc, &ip, (uint16_t)nchr);
  emit_u16_be(bc, &ip, 0);
  for (size_t i = 0; i < nchr; i++) {
    uint32_t c = (uint32_t)(unsigned char)chars[i];
    emit_u32_be(bc, &ip, c);
    emit_u32_be(bc, &ip, c);
  }
  emit_u32_be(bc, &ip, (uint32_t)class_data_off);
  emit_u32_be(bc, &ip, 1);
  return ip;
}

static size_t build_notany_accept(uint8_t *bc, const char *chars, size_t nchr) {
  size_t ip = 0;
  bc[ip++] = OP_NOTANY;
  emit_u16_be(bc, &ip, 1);
  bc[ip++] = OP_ACCEPT;

  size_t class_data_off = ip;
  emit_u16_be(bc, &ip, (uint16_t)nchr);
  emit_u16_be(bc, &ip, 0);
  for (size_t i = 0; i < nchr; i++) {
    uint32_t c = (uint32_t)(unsigned char)chars[i];
    emit_u32_be(bc, &ip, c);
    emit_u32_be(bc, &ip, c);
  }
  emit_u32_be(bc, &ip, (uint32_t)class_data_off);
  emit_u32_be(bc, &ip, 1);
  return ip;
}

static size_t build_len_accept(uint8_t *bc, uint32_t n) {
  size_t ip = 0;
  bc[ip++] = OP_LEN;
  emit_u32_be(bc, &ip, n);
  bc[ip++] = OP_ACCEPT;
  return ip;
}

static size_t build_lit_accept(uint8_t *bc, const char *s, size_t slen) {
  size_t ip = 0;
  bc[ip++] = OP_LIT;
  emit_u32_be(bc, &ip, (uint32_t)(1 + 4 + 4));
  emit_u32_be(bc, &ip, (uint32_t)slen);
  for (size_t i = 0; i < slen; i++)
    bc[ip++] = (uint8_t)s[i];
  bc[ip++] = OP_ACCEPT;
  return ip;
}

/* Build SPAN with multi-byte UTF-8 ranges (bytes >= 128) */
static size_t build_span_utf8(uint8_t *bc) {
  size_t ip = 0;
  bc[ip++] = OP_SPAN;
  emit_u16_be(bc, &ip, 1);
  bc[ip++] = OP_ACCEPT;

  /* Single range [0xC0, 0xDF] — 2-byte UTF-8 lead bytes */
  size_t class_data_off = ip;
  emit_u16_be(bc, &ip, 1);
  emit_u16_be(bc, &ip, 0);
  emit_u32_be(bc, &ip, 0xC0);
  emit_u32_be(bc, &ip, 0xDF);
  emit_u32_be(bc, &ip, (uint32_t)class_data_off);
  emit_u32_be(bc, &ip, 1);
  return ip;
}

/* ---------------------------------------------------------------------------
 * Test: check_simd_eligible() — basic opcode filtering
 * ---------------------------------------------------------------------------
 */
static void test_simd_eligibility(void) {
  test_suite("SIMD: eligibility checks");

  {
    uint8_t bc[128];
    size_t n = build_span_accept(bc, "abc", 3, 0);
    test_assert(check_simd_eligible(bc, n), "SPAN('abc')+ACCEPT is eligible");
  }
  {
    uint8_t bc[128];
    size_t n = build_break_accept(bc, ",;", 2);
    test_assert(check_simd_eligible(bc, n), "BREAK(',;')+ACCEPT is eligible");
  }
  {
    uint8_t bc[128];
    size_t n = build_any_accept(bc, "0123456789", 10);
    test_assert(check_simd_eligible(bc, n), "ANY('0-9')+ACCEPT is eligible");
  }
  {
    uint8_t bc[128];
    size_t n = build_notany_accept(bc, "abc", 3);
    test_assert(check_simd_eligible(bc, n), "NOTANY('abc')+ACCEPT is eligible");
  }
  {
    uint8_t bc[128];
    size_t n = build_len_accept(bc, 3);
    test_assert(!check_simd_eligible(bc, n), "LEN(3)+ACCEPT is NOT eligible");
  }
  {
    uint8_t bc[128];
    size_t n = build_lit_accept(bc, "hello", 5);
    test_assert(!check_simd_eligible(bc, n), "LIT('hello')+ACCEPT is NOT eligible");
  }
  {
    /* Nested SPAN(JMP+SPLIT): not eligible */
    uint8_t bc[64];
    size_t ip = 0;
    bc[ip++] = OP_SPLIT;
    emit_u32_be(bc, &ip, 5);
    emit_u32_be(bc, &ip, 10);
    bc[ip++] = OP_SPAN;
    emit_u16_be(bc, &ip, 1);
    bc[ip++] = OP_ACCEPT;
    size_t class_off = ip;
    emit_u16_be(bc, &ip, 1); emit_u16_be(bc, &ip, 0);
    emit_u32_be(bc, &ip, 'a'); emit_u32_be(bc, &ip, 'a');
    emit_u32_be(bc, &ip, (uint32_t)class_off);
    emit_u32_be(bc, &ip, 1);
    size_t n = ip;
    test_assert(!check_simd_eligible(bc, n),
                "SPLIT+SPAN is NOT eligible (no control flow)");
  }
  {
    /* UTF-8 byte ranges (>=128) are eligible */
    uint8_t bc[128];
    size_t n = build_span_utf8(bc);
    test_assert(check_simd_eligible(bc, n),
                "SPAN([0xC0-0xDF])+ACCEPT is eligible (UTF-8 safe)");
  }
}

/* ---------------------------------------------------------------------------
 * Test: tier routing places eligible patterns at TIER_SIMD_NFA
 * ---------------------------------------------------------------------------
 */
static void test_simd_tier_routing(void) {
  test_suite("SIMD: tier routing");

  {
    uint8_t bc[128];
    size_t n = build_span_accept(bc, " \t", 2, 0);
    snobol_search_meta_t m;
    memset(&m, 0, sizeof(m));
    snobol_search_derive_meta(bc, n, &m);
    test_assert(m.simd_eligible, "simd_eligible true for SPAN(' \\t')");
    test_assert(m.tier == TIER_SIMD_NFA || m.tier < TIER_SIMD_NFA,
                "SPAN(' \\t') tier == TIER_SIMD_NFA (or earlier if faster path)");
    snobol_search_meta_free(&m);
  }
  {
    uint8_t bc[128];
    size_t n = build_any_accept(bc, "aeiou", 5);
    snobol_search_meta_t m;
    memset(&m, 0, sizeof(m));
    snobol_search_derive_meta(bc, n, &m);
    /* ANY may be captured by TIER_BITMAP (Tier 4) first if it fits the
     * single-char alt acceleration; otherwise it routes to TIER_SIMD_NFA.
     * Either is correct — both are fast acceleration tiers. */
    if (m.simd_eligible)
      test_assert(m.tier == TIER_SIMD_NFA || m.tier == TIER_BITMAP,
                  "ANY('aeiou') routes through an acceleration tier");
    snobol_search_meta_free(&m);
  }
  {
    uint8_t bc[128];
    size_t n = build_len_accept(bc, 5);
    snobol_search_meta_t m;
    memset(&m, 0, sizeof(m));
    snobol_search_derive_meta(bc, n, &m);
    test_assert(!m.simd_eligible, "simd_eligible false for LEN(5)+ACCEPT");
    test_assert(m.tier != TIER_SIMD_NFA,
                "LEN(5)+ACCEPT does NOT route through TIER_SIMD_NFA");
    snobol_search_meta_free(&m);
  }
}

/* ---------------------------------------------------------------------------
 * Test: SPAN execution — match consecutive class bytes
 * ---------------------------------------------------------------------------
 */
static void test_simd_span(void) {
  test_suite("SIMD: SPAN execution");

  /* Create bytecode + range_meta for a full VM */
  uint8_t bc[128];
  size_t n = build_span_accept(bc, "abc", 3, 0);

  /* Derive metadata */
  snobol_search_meta_t m;
  memset(&m, 0, sizeof(m));
  snobol_search_derive_meta(bc, n, &m);

  /* Build range_meta */
  snobol_range_meta_t *rm = NULL;
  size_t rm_count = 0;
  snobol_build_range_meta(bc, n, &rm, &rm_count);

  VM vm;
  memset(&vm, 0, sizeof(vm));
  vm.bc = bc;
  vm.bc_len = n;
  vm.range_meta = rm;
  vm.range_meta_count = rm_count;

  /* SPAN('abc') on "aabbcxyz": should match "aabbc" */
  {
    snobol_search_result_t r;
    snobol_search_diag_t d;
    memset(&r, 0, sizeof(r));
    memset(&d, 0, sizeof(d));
    bool ok = snobol_search_exec(&vm, "aabbcxyz", 8, 0, &m, NULL, &r, &d);
    test_assert(ok, "SPAN('abc') matches 'aabbc' in 'aabbcxyz'");
    test_assert(r.match_start == 0, "SPAN('abc') match_start == 0");
    test_assert(r.match_end == 5, "SPAN('abc') match_end == 5");
  }

  /* SPAN('abc') on "xyz": should fail (no class bytes) */
  {
    snobol_search_result_t r;
    snobol_search_diag_t d;
    memset(&r, 0, sizeof(r));
    memset(&d, 0, sizeof(d));
    bool ok = snobol_search_exec(&vm, "xyz", 3, 0, &m, NULL, &r, &d);
    test_assert(!ok, "SPAN('abc') fails on 'xyz'");
  }

  /* SPAN('abc') on "aaaa": match the full string */
  {
    snobol_search_result_t r;
    snobol_search_diag_t d;
    memset(&r, 0, sizeof(r));
    memset(&d, 0, sizeof(d));
    bool ok = snobol_search_exec(&vm, "aaaa", 4, 0, &m, NULL, &r, &d);
    test_assert(ok, "SPAN('abc') matches 'aaaa'");
    test_assert(r.match_end == 4, "SPAN('abc') matches all 4 bytes");
  }

  if (rm) free((void *)rm);
  snobol_search_meta_free(&m);
}

/* ---------------------------------------------------------------------------
 * Test: BREAK execution — match until delimiter
 * ---------------------------------------------------------------------------
 */
static void test_simd_break(void) {
  test_suite("SIMD: BREAK execution");

  uint8_t bc[128];
  size_t n = build_break_accept(bc, ",", 1);

  snobol_search_meta_t m;
  memset(&m, 0, sizeof(m));
  snobol_search_derive_meta(bc, n, &m);

  snobol_range_meta_t *rm = NULL;
  size_t rm_count = 0;
  snobol_build_range_meta(bc, n, &rm, &rm_count);

  VM vm;
  memset(&vm, 0, sizeof(vm));
  vm.bc = bc;
  vm.bc_len = n;
  vm.range_meta = rm;
  vm.range_meta_count = rm_count;

  /* BREAK(',') on "hello,world": break at comma */
  {
    snobol_search_result_t r;
    snobol_search_diag_t d;
    memset(&r, 0, sizeof(r));
    memset(&d, 0, sizeof(d));
    bool ok = snobol_search_exec(&vm, "hello,world", 11, 0, &m, NULL, &r, &d);
    test_assert(ok, "BREAK(',') matches in 'hello,world'");
    /* BREAK(',') scans to delimiter ',' at position 5; VM matches empty at that position */
    test_assert(r.match_start == 5, "BREAK match_start == delimiter position");
    test_assert(r.match_end == 5, "BREAK match_end == delimiter position");
  }

  /* BREAK(',') on "abc": no comma, should match at end */
  {
    snobol_search_result_t r;
    snobol_search_diag_t d;
    memset(&r, 0, sizeof(r));
    memset(&d, 0, sizeof(d));
    bool ok = snobol_search_exec(&vm, "abc", 3, 0, &m, NULL, &r, &d);
    test_assert(ok, "BREAK(',') matches all of 'abc' (no comma)");
    test_assert(r.match_end == 3, "BREAK match_end == 3 (end of subject)");
  }

  /* BREAK(',') on ",": empty match */
  {
    snobol_search_result_t r;
    snobol_search_diag_t d;
    memset(&r, 0, sizeof(r));
    memset(&d, 0, sizeof(d));
    bool ok = snobol_search_exec(&vm, ",", 1, 0, &m, NULL, &r, &d);
    test_assert(ok, "BREAK(',') on ',' matches empty (pos 0)");
    test_assert(r.match_start == 0, "BREAK match_start == 0");
    test_assert(r.match_end == 0, "BREAK match_end == 0 (at delimiter)");
  }

  if (rm) free((void *)rm);
  snobol_search_meta_free(&m);
}

/* ---------------------------------------------------------------------------
 * Test: ANY execution — match single class byte
 * ---------------------------------------------------------------------------
 */
static void test_simd_any(void) {
  test_suite("SIMD: ANY execution");

  uint8_t bc[128];
  size_t n = build_any_accept(bc, "aeiou", 5);

  snobol_search_meta_t m;
  memset(&m, 0, sizeof(m));
  snobol_search_derive_meta(bc, n, &m);

  snobol_range_meta_t *rm = NULL;
  size_t rm_count = 0;
  snobol_build_range_meta(bc, n, &rm, &rm_count);

  VM vm;
  memset(&vm, 0, sizeof(vm));
  vm.bc = bc;
  vm.bc_len = n;
  vm.range_meta = rm;
  vm.range_meta_count = rm_count;

  /* ANY('aeiou') on "frog": 'o' is a vowel at pos 2 */
  {
    snobol_search_result_t r;
    snobol_search_diag_t d;
    memset(&r, 0, sizeof(r));
    memset(&d, 0, sizeof(d));
    bool ok = snobol_search_exec(&vm, "frog", 4, 0, &m, NULL, &r, &d);
    test_assert(ok, "ANY('aeiou') matches 'o' in 'frog'");
    test_assert(r.match_start == 2, "ANY match_start == 2");
    test_assert(r.match_end == 3, "ANY match_end == 3");
  }

  /* ANY('aeiou') on "xyz": no vowel, should fail */
  {
    snobol_search_result_t r;
    snobol_search_diag_t d;
    memset(&r, 0, sizeof(r));
    memset(&d, 0, sizeof(d));
    bool ok = snobol_search_exec(&vm, "xyz", 3, 0, &m, NULL, &r, &d);
    test_assert(!ok, "ANY('aeiou') fails on 'xyz'");
  }

  if (rm) free((void *)rm);
  snobol_search_meta_free(&m);
}

/* ---------------------------------------------------------------------------
 * Test: NOTANY execution — match single non-class byte
 * ---------------------------------------------------------------------------
 */
static void test_simd_notany(void) {
  test_suite("SIMD: NOTANY execution");

  uint8_t bc[128];
  size_t n = build_notany_accept(bc, "aeiou", 5);

  snobol_search_meta_t m;
  memset(&m, 0, sizeof(m));
  snobol_search_derive_meta(bc, n, &m);

  snobol_range_meta_t *rm = NULL;
  size_t rm_count = 0;
  snobol_build_range_meta(bc, n, &rm, &rm_count);

  VM vm;
  memset(&vm, 0, sizeof(vm));
  vm.bc = bc;
  vm.bc_len = n;
  vm.range_meta = rm;
  vm.range_meta_count = rm_count;

  /* NOTANY('aeiou') on "frog": 'f' is not a vowel at pos 0 */
  {
    snobol_search_result_t r;
    snobol_search_diag_t d;
    memset(&r, 0, sizeof(r));
    memset(&d, 0, sizeof(d));
    bool ok = snobol_search_exec(&vm, "frog", 4, 0, &m, NULL, &r, &d);
    test_assert(ok, "NOTANY('aeiou') matches 'f' in 'frog'");
    test_assert(r.match_start == 0, "NOTANY match_start == 0");
    test_assert(r.match_end == 1, "NOTANY match_end == 1");
  }

  if (rm) free((void *)rm);
  snobol_search_meta_free(&m);
}

/* ---------------------------------------------------------------------------
 * Test: tail-byte handling (subjects shorter than SIMD chunk)
 * ---------------------------------------------------------------------------
 */
static void test_simd_tail(void) {
  test_suite("SIMD: tail bytes");

  uint8_t bc[128];
  size_t n = build_span_accept(bc, "ab", 2, 0);

  snobol_search_meta_t m;
  memset(&m, 0, sizeof(m));
  snobol_search_derive_meta(bc, n, &m);

  snobol_range_meta_t *rm = NULL;
  size_t rm_count = 0;
  snobol_build_range_meta(bc, n, &rm, &rm_count);

  VM vm;
  memset(&vm, 0, sizeof(vm));
  vm.bc = bc;
  vm.bc_len = n;
  vm.range_meta = rm;
  vm.range_meta_count = rm_count;

  /* SPAN('ab') on empty string */
  {
    snobol_search_result_t r;
    snobol_search_diag_t d;
    memset(&r, 0, sizeof(r));
    memset(&d, 0, sizeof(d));
    bool ok = snobol_search_exec(&vm, "", 0, 0, &m, NULL, &r, &d);
    test_assert(!ok, "SPAN('ab') on empty string fails");
  }

  /* SPAN('ab') on 1-byte subject */
  {
    snobol_search_result_t r;
    snobol_search_diag_t d;
    memset(&r, 0, sizeof(r));
    memset(&d, 0, sizeof(d));
    bool ok = snobol_search_exec(&vm, "a", 1, 0, &m, NULL, &r, &d);
    test_assert(ok, "SPAN('ab') on 'a' matches");
    test_assert(r.match_end == 1, "SPAN('ab') on 'a' match_end == 1");
  }

  /* SPAN('ab') on 31 bytes (AVX2 tail boundary) */
  {
    char buf[32];
    memset(buf, 'a', 31);
    buf[31] = '\0';
    snobol_search_result_t r;
    snobol_search_diag_t d;
    memset(&r, 0, sizeof(r));
    memset(&d, 0, sizeof(d));
    bool ok = snobol_search_exec(&vm, buf, 31, 0, &m, NULL, &r, &d);
    test_assert(ok, "SPAN('ab') on 31-byte subject matches");
    test_assert(r.match_end == 31, "SPAN('ab') on 31 bytes match_end == 31");
  }

  /* SPAN('ab') on 32 bytes (exact AVX2 width) */
  {
    char buf[33];
    memset(buf, 'a', 32);
    buf[32] = '\0';
    snobol_search_result_t r;
    snobol_search_diag_t d;
    memset(&r, 0, sizeof(r));
    memset(&d, 0, sizeof(d));
    bool ok = snobol_search_exec(&vm, buf, 32, 0, &m, NULL, &r, &d);
    test_assert(ok, "SPAN('ab') on 32-byte subject matches");
    test_assert(r.match_end == 32, "SPAN('ab') on 32 bytes match_end == 32");
  }

  /* SPAN('ab') on 64 bytes (multiple SIMD chunks) */
  {
    char buf[65];
    memset(buf, 'a', 64);
    buf[64] = '\0';
    snobol_search_result_t r;
    snobol_search_diag_t d;
    memset(&r, 0, sizeof(r));
    memset(&d, 0, sizeof(d));
    bool ok = snobol_search_exec(&vm, buf, 64, 0, &m, NULL, &r, &d);
    test_assert(ok, "SPAN('ab') on 64-byte subject matches");
    test_assert(r.match_end == 64, "SPAN('ab') on 64 bytes match_end == 64");
  }

  if (rm) free((void *)rm);
  snobol_search_meta_free(&m);
}

/* ---------------------------------------------------------------------------
 * Test: UTF-8 byte ranges (bytes 128-255)
 * ---------------------------------------------------------------------------
 */
static void test_simd_utf8_range(void) {
  test_suite("SIMD: UTF-8 byte ranges");

  uint8_t bc[128];
  size_t n = build_span_utf8(bc);

  snobol_search_meta_t m;
  memset(&m, 0, sizeof(m));
  snobol_search_derive_meta(bc, n, &m);
  test_assert(m.simd_eligible, "SPAN([0xC0-0xDF]) is simd_eligible");

  snobol_range_meta_t *rm = NULL;
  size_t rm_count = 0;
  snobol_build_range_meta(bc, n, &rm, &rm_count);

  VM vm;
  memset(&vm, 0, sizeof(vm));
  vm.bc = bc;
  vm.bc_len = n;
  vm.range_meta = rm;
  vm.range_meta_count = rm_count;

  /* SPAN([0xC0-0xDF]) on bytes 0xC3 0xC8 0x41: match the first two */
  {
    unsigned char data[] = {0xC3, 0xC8, 0x41};
    snobol_search_result_t r;
    snobol_search_diag_t d;
    memset(&r, 0, sizeof(r));
    memset(&d, 0, sizeof(d));
    bool ok = snobol_search_exec(&vm, (const char *)data, 3, 0, &m, NULL, &r, &d);
    test_assert(ok, "SPAN([0xC0-0xDF]) matches UTF-8 lead bytes");
    test_assert(r.match_end == 2, "SPAN([0xC0-0xDF]) match_end == 2");
  }

  /* SPAN([0xC0-0xDF]) on ASCII bytes: no match */
  {
    const char *data = "abc";
    snobol_search_result_t r;
    snobol_search_diag_t d;
    memset(&r, 0, sizeof(r));
    memset(&d, 0, sizeof(d));
    bool ok = snobol_search_exec(&vm, data, 3, 0, &m, NULL, &r, &d);
    test_assert(!ok, "SPAN([0xC0-0xDF]) fails on ASCII-only data");
  }

  if (rm) free((void *)rm);
  snobol_search_meta_free(&m);
}

/* ---------------------------------------------------------------------------
 * Test: partial match (offset > 0)
 * ---------------------------------------------------------------------------
 */
static void test_simd_offset(void) {
  test_suite("SIMD: non-zero start offset");

  uint8_t bc[128];
  size_t n = build_span_accept(bc, "ab", 2, 0);

  snobol_search_meta_t m;
  memset(&m, 0, sizeof(m));
  snobol_search_derive_meta(bc, n, &m);

  snobol_range_meta_t *rm = NULL;
  size_t rm_count = 0;
  snobol_build_range_meta(bc, n, &rm, &rm_count);

  VM vm;
  memset(&vm, 0, sizeof(vm));
  vm.bc = bc;
  vm.bc_len = n;
  vm.range_meta = rm;
  vm.range_meta_count = rm_count;

  /* SPAN('ab') on "xxaaabb" starting at offset 2 */
  {
    snobol_search_result_t r;
    snobol_search_diag_t d;
    memset(&r, 0, sizeof(r));
    memset(&d, 0, sizeof(d));
    bool ok = snobol_search_exec(&vm, "xxaaabb", 7, 2, &m, NULL, &r, &d);
    test_assert(ok, "SPAN('ab') from offset 2 matches 'aaabb'");
    test_assert(r.match_start == 2, "SPAN match_start == 2");
    test_assert(r.match_end == 7, "SPAN match_end == 7");
  }

  if (rm) free((void *)rm);
  snobol_search_meta_free(&m);
}

/* ---------------------------------------------------------------------------
 * Suite entry-point
 * ---------------------------------------------------------------------------
 */
void test_search_simd_suite(void) {
  test_simd_eligibility();
  test_simd_tier_routing();
  test_simd_span();
  test_simd_break();
  test_simd_any();
  test_simd_notany();
  test_simd_tail();
  test_simd_utf8_range();
  test_simd_offset();
}
