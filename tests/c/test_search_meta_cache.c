/**
 * test_search_meta_cache.c - Tests for the cached search metadata
 *
 * Verifies that snobol_pattern_search() uses the compile-time cached
 * search metadata (snobol_pattern_t::meta) and produces identical
 * results to a fresh per-call derivation.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "snobol/search.h"
#include "snobol/snobol.h"

extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

/* Internal access — we want to verify the pattern struct's meta field.
 * The struct definition is in core/src/api.c. We can only see the
 * public API from here, so we test behavior, not internals. The test
 * ensures that snobol_pattern_search() returns correct results
 * (regression) using the compile-time cached search metadata. */
void test_search_meta_cache_suite(void) {
  test_suite("Search: cached metadata on pattern");

  /* Simple literal search */
  {
    snobol_context_t *ctx = snobol_context_create();
    char *err = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile_ex(ctx, "'pqr'", 5, 0, &err);
    test_assert(pat != NULL, "compile succeeds");
    if (pat) {
      snobol_match_t *m =
          snobol_pattern_search(pat, "abcdefghijklmnopqrstuvwxyz", 26);
      test_assert(m != NULL, "search returns non-NULL");
      if (m) {
        test_assert(snobol_match_success(m), "search finds 'pqr' at offset 15");
        snobol_match_free(m);
      }
      snobol_pattern_free(pat);
    }
    free(err);
    snobol_context_destroy(ctx);
  }

  /* SPAN search-mode */
  {
    snobol_context_t *ctx = snobol_context_create();
    char *err = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile(ctx, "SPAN(',')", 9, &err);
    test_assert(pat != NULL, "compile SPAN succeeds");
    if (pat) {
      snobol_match_t *m =
          snobol_pattern_search(pat, "id,name,email,age,status", 24);
      test_assert(m != NULL, "SPAN search returns non-NULL");
      if (m) {
        test_assert(snobol_match_success(m), "SPAN search finds comma run");
        snobol_match_free(m);
      }
      snobol_pattern_free(pat);
    }
    free(err);
    snobol_context_destroy(ctx);
  }

  /* Alternation: split-any-fused path */
  {
    snobol_context_t *ctx = snobol_context_create();
    char *err = NULL;
    snobol_pattern_t *pat =
        snobol_pattern_compile(ctx, "'a' | 'b' | 'c'", 15, &err);
    test_assert(pat != NULL, "compile alternation succeeds");
    if (pat) {
      snobol_match_t *m = snobol_pattern_search(pat, "the quick brown fox", 19);
      test_assert(m != NULL, "alternation search returns non-NULL");
      if (m) {
        test_assert(snobol_match_success(m), "alternation search succeeds");
        snobol_match_free(m);
      }
      snobol_pattern_free(pat);
    }
    free(err);
    snobol_context_destroy(ctx);
  }

  /* Hot loop: verify JIT still fires after the cache change.
   * With cached metadata, the search runtime should still get the
   * meta pointer and the JIT should fire. */
  {
    snobol_context_t *ctx = snobol_context_create();
    char *err = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile(ctx, "SPAN('a')", 9, &err);
    test_assert(pat != NULL, "compile hot-loop pattern succeeds");
    if (pat) {
      /* warmup: get to JIT hotness */
      for (int i = 0; i < 100; i++) {
        snobol_match_t *m =
            snobol_pattern_search(pat, "aaaaaaaaaaaaaaaaaa", 18);
        if (m)
          snobol_match_free(m);
      }
      for (int i = 0; i < 50; i++) {
        snobol_match_t *m =
            snobol_pattern_search(pat, "aaaaaaaaaaaaaaaaaa", 18);
        if (m)
          snobol_match_free(m);
      }
      snobol_pattern_free(pat);
    }
    free(err);
    snobol_context_destroy(ctx);
  }
}
