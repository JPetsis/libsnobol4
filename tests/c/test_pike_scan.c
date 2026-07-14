/**
 * test_pike_scan.c — Pike single-pass scan tests (W1c)
 *
 * Tests the Pike scan function directly (not through tier dispatch).
 * Gated behind SNOBOL_PIKE_SCAN; compiled only when enabled.
 */
#ifdef SNOBOL_PIKE_SCAN

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "snobol/search.h"
#include "snobol/vm.h"
#include "snobol/snobol.h"

extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);
extern bool pike_scan(const uint8_t *bc, size_t bc_len,
                      const char *subject, size_t subject_len,
                      const snobol_search_meta_t *meta,
                      const snobol_range_meta_t *range_meta,
                      size_t range_meta_count,
                      VM *vm,
                      snobol_search_result_t *out_result);

static int pike_test_count = 0, pike_test_pass = 0;

static void pike_assert(bool cond, const char *name) {
  pike_test_count++;
  if (cond) { pike_test_pass++; return; }
  fprintf(stderr, "  PIKE FAIL: %s\n", name);
}

static void pike_test_literal(void) {
  snobol_context_t *ctx = snobol_context_create();
  char *err = NULL;
  snobol_pattern_t *p = snobol_pattern_compile(ctx, "'hello'", 7, &err);
  if (!p) { pike_assert(false, "literal compile"); snobol_context_destroy(ctx); return; }
  const snobol_search_meta_t *meta = snobol_pattern_get_meta(p);
  size_t rc = 0;
  const snobol_range_meta_t *rm = snobol_pattern_get_range_meta(p, &rc);
  snobol_search_result_t r;
  bool ok = pike_scan(snobol_pattern_get_bc(p), snobol_pattern_get_bc_len(p),
                      "say hello world", 15,                       meta, rm, rc, NULL, &r);
  pike_assert(ok, "literal match succeeds");
  pike_assert(r.match_start == 4, "literal match_start == 4");
  pike_assert(r.match_end == 9, "literal match_end == 9");
  snobol_pattern_free(p);
  snobol_context_destroy(ctx);
}

static void pike_test_span(void) {
  snobol_context_t *ctx = snobol_context_create();
  char *err = NULL;
  snobol_pattern_t *p = snobol_pattern_compile(ctx, "SPAN('0-9')", 11, &err);
  if (!p) { pike_assert(false, "span compile"); snobol_context_destroy(ctx); return; }
  const snobol_search_meta_t *meta = snobol_pattern_get_meta(p);
  size_t rc = 0;
  const snobol_range_meta_t *rm = snobol_pattern_get_range_meta(p, &rc);
  snobol_search_result_t r;
  bool ok = pike_scan(snobol_pattern_get_bc(p), snobol_pattern_get_bc_len(p),
                      "abc123def", 9,                       meta, rm, rc, NULL, &r);
  pike_assert(ok, "span match succeeds");
  pike_assert(r.match_start == 3, "span match_start == 3");
  pike_assert(r.match_end == 6, "span match_end == 6");
  snobol_pattern_free(p);
  snobol_context_destroy(ctx);
}

static void pike_test_alt_capture(void) {
  snobol_context_t *ctx = snobol_context_create();
  char *err = NULL;
  snobol_pattern_t *p = snobol_pattern_compile(ctx, "(@r1('a') | @r1('b'))", 21, &err);
  if (!p) { pike_assert(false, "alt+cap compile"); snobol_context_destroy(ctx); return; }
  const snobol_search_meta_t *meta = snobol_pattern_get_meta(p);
  size_t rc = 0;
  const snobol_range_meta_t *rm = snobol_pattern_get_range_meta(p, &rc);
  snobol_search_result_t r;
  bool ok = pike_scan(snobol_pattern_get_bc(p), snobol_pattern_get_bc_len(p),
                      "b", 1,                       meta, rm, rc, NULL, &r);
  pike_assert(ok, "alt+cap match succeeds");
  pike_assert(r.match_start == 0, "alt+cap match_start == 0");
  pike_assert(r.match_end == 1, "alt+cap match_end == 1");
  snobol_pattern_free(p);
  snobol_context_destroy(ctx);
}

static void pike_test_notany(void) {
  snobol_context_t *ctx = snobol_context_create();
  char *err = NULL;
  snobol_pattern_t *p = snobol_pattern_compile(ctx, "NOTANY('aeiou')", 15, &err);
  if (!p) { pike_assert(false, "notany compile"); snobol_context_destroy(ctx); return; }
  const snobol_search_meta_t *meta = snobol_pattern_get_meta(p);
  size_t rc = 0;
  const snobol_range_meta_t *rm = snobol_pattern_get_range_meta(p, &rc);
  snobol_search_result_t r;
  bool ok = pike_scan(snobol_pattern_get_bc(p), snobol_pattern_get_bc_len(p),
                      "frog", 4,                       meta, rm, rc, NULL, &r);
  pike_assert(ok, "notany match succeeds");
  pike_assert(r.match_start == 0, "notany match_start == 0");
  pike_assert(r.match_end == 1, "notany match_end == 1");
  snobol_pattern_free(p);
  snobol_context_destroy(ctx);
}

void test_pike_scan_suite(void) {
  test_suite("Search: Pike Scan");
  pike_test_count = 0; pike_test_pass = 0;
  pike_test_literal();
  pike_test_span();
  pike_test_alt_capture();
  pike_test_notany();
  test_assert(pike_test_pass == pike_test_count,
              "pike scan: all tests pass");
}

#endif /* SNOBOL_PIKE_SCAN */
