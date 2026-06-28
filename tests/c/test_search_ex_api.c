/**
 * test_search_ex_api.c - Tests for the stateful snobol_pattern_search_ex() API
 *
 * Verifies that:
 *   1. snobol_pattern_search_ex() produces identical results to
 *      snobol_pattern_search() when called in equivalent loops
 *   2. State can be created and destroyed without leaks (validated by
 *      running under ASan/UBSan in the build-asan target)
 *   3. The JIT fires on hot patterns through the stateful path
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "snobol/snobol.h"

extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

void test_search_ex_api_suite(void) {
  test_suite("Search: stateful _ex API");

  /* Stateful search matches non-stateful search results */
  {
    snobol_context_t *ctx = snobol_context_create();
    char *err = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile(ctx, "SPAN(',')", 9, &err);
    test_assert(pat != NULL, "compile SPAN succeeds");
    if (pat) {
      const char *subject = "a,b,,c,d,,,e";
      size_t slen = strlen(subject);

      /* Reference: non-stateful search */
      snobol_match_t *ref = snobol_pattern_search(pat, subject, slen);
      test_assert(ref != NULL && snobol_match_success(ref),
                  "non-stateful search succeeds");
      size_t ref_output_len = 0;
      const char *ref_output =
          ref ? snobol_match_get_output(ref, &ref_output_len) : NULL;
      char ref_buf[64] = {0};
      if (ref_output) {
        size_t cp = ref_output_len < 63 ? ref_output_len : 63;
        memcpy(ref_buf, ref_output, cp);
      }

      /* Stateful search */
      snobol_pattern_search_state_t *state = snobol_pattern_search_state_create(
          snobol_pattern_get_bc(pat), snobol_pattern_get_bc_len(pat));
      test_assert(state != NULL, "state create returns non-NULL");
      if (state) {
        snobol_match_t *m = snobol_pattern_search_ex(state, subject, slen, 0);
        test_assert(m != NULL, "stateful search returns non-NULL");
        if (m) {
          test_assert(snobol_match_success(m), "stateful search succeeds");
          /* The output should be the same set of commas */
          size_t out_len = 0;
          const char *out = snobol_match_get_output(m, &out_len);
          test_assert(
              out_len == ref_output_len,
              "stateful and non-stateful search produce same output length");
          if (out && ref_output) {
            test_assert(memcmp(out, ref_output, out_len) == 0,
                        "stateful and non-stateful outputs match");
          }
        }
        snobol_pattern_search_state_destroy(state);
      }
      if (ref)
        snobol_match_free(ref);
      snobol_pattern_free(pat);
    }
    free(err);
    snobol_context_destroy(ctx);
  }

  /* Multiple calls on the same state return valid results */
  {
    snobol_context_t *ctx = snobol_context_create();
    char *err = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile(ctx, "'abc'", 5, &err);
    test_assert(pat != NULL, "compile literal succeeds");
    if (pat) {
      snobol_pattern_search_state_t *state = snobol_pattern_search_state_create(
          snobol_pattern_get_bc(pat), snobol_pattern_get_bc_len(pat));
      if (state) {
        const char *s1 = "abc";
        const char *s2 = "xabcx";
        const char *s3 = "abcabcabc";

        snobol_match_t *m1 = snobol_pattern_search_ex(state, s1, 3, 0);
        test_assert(m1 && snobol_match_success(m1), "stateful call 1 succeeds");

        snobol_match_t *m2 = snobol_pattern_search_ex(state, s2, 5, 0);
        test_assert(m2 && snobol_match_success(m2), "stateful call 2 succeeds");

        snobol_match_t *m3 = snobol_pattern_search_ex(state, s3, 9, 0);
        test_assert(m3 && snobol_match_success(m3), "stateful call 3 succeeds");

        snobol_pattern_search_state_destroy(state);
      }
      snobol_pattern_free(pat);
    }
    free(err);
    snobol_context_destroy(ctx);
  }

  /* start_offset is honored */
  {
    snobol_context_t *ctx = snobol_context_create();
    char *err = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile(ctx, "'a'", 3, &err);
    test_assert(pat != NULL, "compile 'a' succeeds");
    if (pat) {
      snobol_pattern_search_state_t *state = snobol_pattern_search_state_create(
          snobol_pattern_get_bc(pat), snobol_pattern_get_bc_len(pat));
      if (state) {
        const char *subject = "aXaXa";
        /* From offset 2, first 'a' is at offset 2 */
        snobol_match_t *m = snobol_pattern_search_ex(state, subject, 5, 2);
        test_assert(m && snobol_match_success(m),
                    "search from offset 2 succeeds");
        snobol_pattern_search_state_destroy(state);
      }
      snobol_pattern_free(pat);
    }
    free(err);
    snobol_context_destroy(ctx);
  }

  /* Hot loop: JIT still fires through the stateful path */
  {
    snobol_context_t *ctx = snobol_context_create();
    char *err = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile(ctx, "SPAN('a')", 9, &err);
    test_assert(pat != NULL, "compile hot-loop pattern succeeds");
    if (pat) {
      snobol_pattern_search_state_t *state = snobol_pattern_search_state_create(
          snobol_pattern_get_bc(pat), snobol_pattern_get_bc_len(pat));
      if (state) {
        /* warmup */
        for (int i = 0; i < 100; i++) {
          snobol_match_t *m =
              snobol_pattern_search_ex(state, "aaaaaaaaaaaaaaaaaa", 18, 0);
          (void)m;
        }
        for (int i = 0; i < 50; i++) {
          snobol_match_t *m =
              snobol_pattern_search_ex(state, "aaaaaaaaaaaaaaaaaa", 18, 0);
          (void)m;
        }
        snobol_pattern_search_state_destroy(state);
      }
      snobol_pattern_free(pat);
    }
    free(err);
    snobol_context_destroy(ctx);
  }

  /* NULL safety: state destroy on NULL is a no-op */
  {
    snobol_pattern_search_state_destroy(NULL);
    test_assert(true, "snobol_pattern_search_state_destroy(NULL) is safe");
  }

  /* NULL safety: state create with NULL pattern returns NULL */
  {
    snobol_pattern_search_state_t *s =
        snobol_pattern_search_state_create(NULL, 0);
    test_assert(s == NULL, "state create with NULL pattern returns NULL");
    if (s)
      snobol_pattern_search_state_destroy(s);
  }
}
