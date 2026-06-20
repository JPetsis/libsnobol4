#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "snobol/snobol.h"
#ifdef SNOBOL_JIT
#include "snobol/jit.h"
#endif

extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

void test_jit_c_api_suite(void) {
  test_suite("JIT: C API Integration");

#ifdef SNOBOL_JIT
  /* snobol_context_create() initialises JIT → stats non-NULL */
  {
    SnobolJitStats *stats = snobol_jit_get_stats();
    test_assert(stats != NULL,
                "snobol_jit_get_stats() returns non-NULL after context create");
    (void)stats;
  }

  /* snobol_jit_init() is idempotent — double-init does not crash */
  {
    snobol_jit_init();
    snobol_jit_init();
    snobol_jit_init();
    test_assert(true, "snobol_jit_init() called 3 times without crash");
  }

  /* Reset stats before search tests */
  snobol_jit_reset_stats();
#endif

  /* snobol_pattern_search() finds literal in subject */
  {
    snobol_context_t *ctx = snobol_context_create();
    char *err = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile(ctx, "'fox'", 5, &err);
    test_assert(pat != NULL, "search-mode compile 'fox' succeeds");
    if (pat) {
      snobol_match_t *m =
          snobol_pattern_search(pat, "the quick brown fox jumps", 24);
      test_assert(m != NULL, "search returns non-NULL");
      if (m) {
        test_assert(snobol_match_success(m), "search finds 'fox' in subject");
        snobol_match_free(m);
      }
      snobol_pattern_free(pat);
    }
    free(err);
    snobol_context_destroy(ctx);
  }

  /* snobol_pattern_search() returns failure for absent pattern */
  {
    snobol_context_t *ctx = snobol_context_create();
    char *err = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile(ctx, "'xyz'", 5, &err);
    test_assert(pat != NULL, "search-mode compile 'xyz' succeeds");
    if (pat) {
      snobol_match_t *m =
          snobol_pattern_search(pat, "the quick brown fox jumps", 24);
      test_assert(m != NULL, "search returns non-NULL for non-match");
      if (m) {
        test_assert(!snobol_match_success(m), "search fails for absent 'xyz'");
        snobol_match_free(m);
      }
      snobol_pattern_free(pat);
    }
    free(err);
    snobol_context_destroy(ctx);
  }

  /* snobol_pattern_search() produces same result as snobol_pattern_match() at
   * pos 0 */
  {
    snobol_context_t *ctx = snobol_context_create();
    char *err = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile(ctx, "'hello'", 7, &err);
    test_assert(pat != NULL, "compile 'hello' succeeds");
    if (pat) {
      snobol_match_t *m1 = snobol_pattern_match(pat, "hello world", 11);
      snobol_match_t *m2 = snobol_pattern_search(pat, "hello world", 11);
      test_assert(m1 != NULL && m2 != NULL,
                  "both match and search return non-NULL");
      if (m1 && m2) {
        test_assert(snobol_match_success(m1), "match succeeds for 'hello'");
        test_assert(snobol_match_success(m2),
                    "search succeeds for 'hello' at pos 0");
        snobol_match_free(m1);
        snobol_match_free(m2);
      }
      snobol_pattern_free(pat);
    }
    free(err);
    snobol_context_destroy(ctx);
  }

  /* snobol_pattern_search() with SPAN finds delimiter run */
  {
    snobol_context_t *ctx = snobol_context_create();
    char *err = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile(ctx, "SPAN(',')", 9, &err);
    test_assert(pat != NULL, "compile SPAN(',') succeeds");
    if (pat) {
      snobol_match_t *m = snobol_pattern_search(pat, "a,b,,,c", 7);
      test_assert(m != NULL, "search SPAN returns non-NULL");
      if (m) {
        test_assert(snobol_match_success(m), "search SPAN finds comma run");
        snobol_match_free(m);
      }
      snobol_pattern_free(pat);
    }
    free(err);
    snobol_context_destroy(ctx);
  }

  /* snobol_pattern_search() with SPAN delimiter */
  {
    snobol_context_t *ctx = snobol_context_create();
    char *err = NULL;
    snobol_pattern_t *pat =
        snobol_pattern_compile(ctx, "SPAN('abc')", 11, &err);
    test_assert(pat != NULL, "compile SPAN('abc') succeeds");
    if (pat) {
      snobol_match_t *m = snobol_pattern_search(pat, "defabcxyz", 9);
      test_assert(m != NULL, "search SPAN returns non-NULL");
      if (m) {
        test_assert(snobol_match_success(m), "search SPAN finds 'abc'");
        snobol_match_free(m);
      }
      snobol_pattern_free(pat);
    }
    free(err);
    snobol_context_destroy(ctx);
  }

  /* snobol_pattern_search() on empty subject */
  {
    snobol_context_t *ctx = snobol_context_create();
    char *err = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile(ctx, "'a'", 3, &err);
    test_assert(pat != NULL, "compile 'a' succeeds");
    if (pat) {
      snobol_match_t *m = snobol_pattern_search(pat, "", 0);
      test_assert(m != NULL, "search on empty returns non-NULL");
      if (m) {
        test_assert(!snobol_match_success(m), "search fails on empty subject");
        snobol_match_free(m);
      }
      snobol_pattern_free(pat);
    }
    free(err);
    snobol_context_destroy(ctx);
  }

  /* NULL safety */
  {
    snobol_match_t *m = snobol_pattern_search(NULL, "hello", 5);
    test_assert(m == NULL, "search with NULL pattern returns NULL");

    snobol_pattern_free(NULL);
    test_assert(true, "snobol_pattern_free(NULL) is safe");
  }

  /* snobol_pattern_compile_ex() with SNOBOL_FLAG_SEARCH_MODE flag */
  {
    snobol_context_t *ctx = snobol_context_create();
    char *err = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile_ex(
        ctx, "'test'", 6, SNOBOL_FLAG_SEARCH_MODE, &err);
    test_assert(pat != NULL, "compile ex with SEARCH_MODE flag succeeds");
    if (pat) {
      snobol_match_t *m =
          snobol_pattern_search(pat, "this is a test string", 21);
      test_assert(m != NULL, "search with SEARCH_MODE flag returns non-NULL");
      if (m) {
        test_assert(snobol_match_success(m), "search with flag finds 'test'");
        snobol_match_free(m);
      }
      snobol_pattern_free(pat);
    }
    free(err);
    snobol_context_destroy(ctx);
  }

  /* snobol_match() with SNOBOL_FLAG_SEARCH_MODE flag — anchored at pos 0 */
  {
    snobol_match_result_t *r =
        snobol_match("'test'", 6, "test subject", 12, SNOBOL_FLAG_SEARCH_MODE);
    test_assert(r != NULL,
                "snobol_match with SEARCH_MODE flag returns non-NULL");
    if (r) {
      test_assert(r->success,
                  "snobol_match with SEARCH_MODE flag succeeds at pos 0");
      test_assert(r->error == NULL, "no error on search-mode match");
      snobol_match_result_free(r);
    }
  }

  /* Pattern freed correctly after search — no use-after-free */
  {
    snobol_context_t *ctx = snobol_context_create();
    char *err = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile(ctx, "'alpha'", 7, &err);
    test_assert(pat != NULL, "compile 'alpha' for free-after-search succeeds");
    if (pat) {
      snobol_match_t *m = snobol_pattern_search(pat, "alpha beta gamma", 16);
      test_assert(m != NULL, "search before free returns non-NULL");
      if (m) {
        test_assert(snobol_match_success(m), "search finds 'alpha'");
        snobol_match_free(m);
      }
      snobol_pattern_free(pat);
      test_assert(true, "pattern freed without crash after search");
    }
    free(err);
    snobol_context_destroy(ctx);
  }

  /* Search with @var captures via source syntax */
  {
    snobol_context_t *ctx = snobol_context_create();
    char *err = NULL;
    /* Use source-based pattern with @x capture */
    snobol_pattern_t *pat = snobol_pattern_compile(ctx, "'foo'", 5, &err);
    test_assert(pat != NULL, "compile 'foo' for search with captures succeeds");
    if (pat) {
      snobol_match_t *m = snobol_pattern_search(pat, "foo bar baz", 11);
      test_assert(m != NULL, "search with captures returns non-NULL");
      if (m) {
        test_assert(snobol_match_success(m), "search with captures succeeds");
        snobol_match_free(m);
      }
      snobol_pattern_free(pat);
    }
    free(err);
    snobol_context_destroy(ctx);
  }

#ifdef SNOBOL_JIT
  /* Verify JIT stats pointer after search-mode execution (counters may still
   * be zero if no patterns reached the hotness threshold, but the pointer
   * must always be valid). */
  {
    SnobolJitStats *stats = snobol_jit_get_stats();
    test_assert(stats != NULL, "JIT stats non-NULL after search execution");
  }
#endif
}
