/**
 * test_search.c - Tests for search-engine tier dispatch and caching.
 *
 * Covers:
 *  - Tier-5 alternation trie is built once and reused across repeated
 *    snobol_pattern_search() calls on the same pattern (trie caching).
 *  - Tier-3 2-byte literal-prefix fast-path uses paired memchr (not memmem).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/include/snobol/search.h"
#include "../../core/include/snobol/vm.h"
#include "../../core/include/snobol/snobol.h"

extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

/* Note on the memmem check: search_literal_accelerated() selects its candidate
 * scan by literal_prefix_len — prefix_len == 1 and == 2 use memchr, while the
 * memmem branch is guarded by prefix_len > 2. A pattern that routes to
 * TIER_PREFIX with literal_prefix_len == 2 therefore statically cannot take the
 * memmem branch. We assert that routing below as the proof that the 2-byte path
 * uses paired memchr (no memmem). (A dynamic memmem interposer is intentionally
 * avoided: macOS's two-level namespace prevents a main-executable override of
 * libc memmem from intercepting the static lib's calls.) */

/* Verify the Tier-5 alternation trie is built exactly once and reused
 * across repeated snobol_pattern_search() calls on the same pattern. */
static void test_trie_cache_hit(void) {
  snobol_context_t *ctx = snobol_context_create();
  test_assert(ctx != NULL, "context created");

  char *err = NULL;
  snobol_pattern_t *pat =
      snobol_pattern_compile_ex(ctx, "'cat'|'car'|'cab'", 17, 0, &err);
  test_assert(pat != NULL, "bushy alt-literal pattern compiles");
  free(err);

  if (!pat) {
    snobol_context_destroy(ctx);
    return;
  }

  /* First search builds and caches the trie. */
  snobol_match_t *m = snobol_pattern_search(pat, "the car is here", 15);
  test_assert(m != NULL, "first search returns a result");
  if (m) {
    test_assert(snobol_match_success(m), "'car' matches");
    snobol_match_free(m);
  }
  snobol_auto_trie_t *cache1 = snobol_pattern_get_trie_cache(pat);
  test_assert(cache1 != NULL, "trie cache built after first search");

  /* Second search must reuse the same cached trie (identical pointer). */
  m = snobol_pattern_search(pat, "a cabinet", 10);
  test_assert(m != NULL, "second search returns a result");
  if (m) {
    test_assert(snobol_match_success(m), "'cab' matches");
    snobol_match_free(m);
  }
  snobol_auto_trie_t *cache2 = snobol_pattern_get_trie_cache(pat);
  test_assert(cache2 == cache1,
              "trie cache reused (not rebuilt) on 2nd search");

  /* Many more searches keep the same cache pointer. */
  for (int i = 0; i < 20; i++) {
    m = snobol_pattern_search(pat, "xcatsy", 6);
    if (m)
      snobol_match_free(m);
  }
  test_assert(snobol_pattern_get_trie_cache(pat) == cache1,
              "trie cache stable across many searches");

  /* Results stay correct after caching. */
  m = snobol_pattern_search(pat, "no match at all", 15);
  if (m) {
    test_assert(!snobol_match_success(m), "no false match after caching");
    snobol_match_free(m);
  }

  snobol_pattern_free(pat);
  snobol_context_destroy(ctx);
}

/* Oracle for pattern "'ab' SPAN('xy')": first index i where the literal "ab"
 * appears AND a character from {x,y} immediately follows it. In this engine
 * SPAN matches exactly one character from the set, so the match span is
 * [i, i+3). Returns true on success. */
static bool oracle_2byte(const char *s, size_t len, size_t *out_start,
                         size_t *out_end) {
  for (size_t i = 0; i + 3 <= len; i++) {
    if (s[i] == 'a' && s[i + 1] == 'b') {
      char c = s[i + 2];
      if (c == 'x' || c == 'y') {
        *out_start = i;
        *out_end = i + 3;
        return true;
      }
    }
  }
  return false;
}

/* Verify the Tier-3 2-byte literal-prefix fast-path (search_literal_accelerated
 * with literal_prefix_len == 2, ~search.c:1588) uses paired memchr and never
 * memmem, and produces correct results. A bare 'ab' is literal-only
 * (TIER_LITERAL) and instead goes through search_literal_only() which calls
 * memmem directly, so it does NOT exercise this path. */
static void test_2byte_prefix_memchr_path(void) {
  snobol_context_t *ctx = snobol_context_create();
  test_assert(ctx != NULL, "context created");

  const char *pat_src = "'ab' SPAN('xy')";
  char *err = NULL;
  snobol_pattern_t *pat =
      snobol_pattern_compile_ex(ctx, pat_src, strlen(pat_src), 0, &err);
  test_assert(pat != NULL, "2-byte-prefix pattern compiles");
  free(err);
  if (!pat) {
    snobol_context_destroy(ctx);
    return;
  }

  const snobol_search_meta_t *meta = snobol_pattern_get_meta(pat);
  test_assert(meta != NULL, "meta present");
  test_assert(meta->tier == TIER_PREFIX, "pattern routes to TIER_PREFIX");
  test_assert(meta->literal_prefix_len == 2, "literal prefix length is 2");

  struct {
    const char *subj;
    size_t len;
  } cases[] = {
      {"abx", 3},      {"aby", 3},   {"ab", 2},   {"abz", 3},
      {"xxabxyyy", 8}, {"ababx", 5}, {"zzz", 3},  {"aaxbbx", 6},
      {"aabx", 4},     {"", 0},      {"aaaa", 4}, {"abxaabx", 7},
  };

  for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
    size_t exp_start = 0, exp_end = 0;
    bool exp = oracle_2byte(cases[k].subj, cases[k].len, &exp_start, &exp_end);

    snobol_match_t *m = snobol_pattern_search(pat, cases[k].subj, cases[k].len);
    test_assert(m != NULL, "search returns a result");
    bool got = m ? snobol_match_success(m) : false;
    test_assert(got == exp, "correct success for 2-byte prefix case");
    if (got && m) {
      size_t st = snobol_match_get_position(m);
      size_t ln = snobol_match_get_length(m);
      test_assert(st == exp_start, "correct match start");
      test_assert(st + ln == exp_end, "correct match end");
    }
    if (m)
      snobol_match_free(m);
  }

  snobol_pattern_free(pat);
  snobol_context_destroy(ctx);
}

void test_search_suite(void) {
  test_suite("Search: tier caching");
  test_trie_cache_hit();
  test_2byte_prefix_memchr_path();
}
