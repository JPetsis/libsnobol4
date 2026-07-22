#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "snobol/search.h"
#include "snobol/vm.h"
#include "snobol/snobol.h"

void test_suite(const char *name);
void test_assert(bool condition, const char *message);

static void test_prefilter_miss(void) {
  snobol_context_t *ctx = snobol_context_create();
  char *err = NULL;
  /* ('a'+)+ 'b' — required lit is 'b' */
  snobol_pattern_t *p = snobol_pattern_compile(ctx, "('a'+)+ 'b'", 12, &err);
  if (!p) { test_assert(false, "prefilter miss: compile"); snobol_context_destroy(ctx); return; }
  VM vm;
  memset(&vm, 0, sizeof(vm));
  vm.bc = (uint8_t *)snobol_pattern_get_bc(p);
  vm.bc_len = snobol_pattern_get_bc_len(p);
  const snobol_search_meta_t *meta = snobol_pattern_get_meta(p);
  snobol_search_result_t r;
  bool ok = snobol_search_exec(&vm, "aaaaaaaaaa", 10, 0, meta, NULL, &r, NULL);
  test_assert(!ok, "prefilter miss: no match on a-only");
  test_assert(r.prefilter_skip, "prefilter miss: prefilter_skip set");
  snobol_pattern_free(p);
  snobol_context_destroy(ctx);
}

static void test_prefilter_hit(void) {
  snobol_context_t *ctx = snobol_context_create();
  char *err = NULL;
  snobol_pattern_t *p = snobol_pattern_compile(ctx, "('a'+)+ 'b'", 12, &err);
  if (!p) { test_assert(false, "prefilter hit: compile"); snobol_context_destroy(ctx); return; }
  VM vm;
  memset(&vm, 0, sizeof(vm));
  vm.bc = (uint8_t *)snobol_pattern_get_bc(p);
  vm.bc_len = snobol_pattern_get_bc_len(p);
  const snobol_search_meta_t *meta = snobol_pattern_get_meta(p);
  snobol_search_result_t r;
  bool ok = snobol_search_exec(&vm, "aaaaabaaaa", 10, 0, meta, NULL, &r, NULL);
  test_assert(ok, "prefilter hit: match found");
  test_assert(!r.prefilter_skip, "prefilter hit: prefilter_skip not set");
  test_assert(r.match_start <= 5 && r.match_end > 5, "prefilter hit: match contains 'b'");
  snobol_pattern_free(p);
  snobol_context_destroy(ctx);
}

static void test_prefilter_noop(void) {
  snobol_context_t *ctx = snobol_context_create();
  char *err = NULL;
  /* 'a' | 'b' — no single required lit (SPLIT in bytecode prevents it) */
  snobol_pattern_t *p = snobol_pattern_compile(ctx, "'a' | 'b'", 9, &err);
  if (!p) { test_assert(false, "prefilter noop: compile"); snobol_context_destroy(ctx); return; }
  const snobol_search_meta_t *meta = snobol_pattern_get_meta(p);
  VM vm;
  memset(&vm, 0, sizeof(vm));
  vm.bc = (uint8_t *)snobol_pattern_get_bc(p);
  vm.bc_len = snobol_pattern_get_bc_len(p);
  snobol_search_result_t r;
  bool ok = snobol_search_exec(&vm, "c", 1, 0, meta, NULL, &r, NULL);
  test_assert(!ok, "prefilter noop: no match");
  /* prefilter_skip should be false — the pre-filter was skipped */
  test_assert(!r.prefilter_skip, "prefilter noop: no prefilter_skip");
  snobol_pattern_free(p);
  snobol_context_destroy(ctx);
}

void test_search_prefilter_suite(void) {
  test_suite("Search: Required-Byte Prefilter");
  test_prefilter_miss();
  test_prefilter_hit();
  test_prefilter_noop();
}
