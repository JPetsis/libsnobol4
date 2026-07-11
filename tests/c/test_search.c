/**
 * test_search.c - Tests for search-engine tier dispatch and caching.
 *
 * Covers:
 *  - Tier-5 alternation trie is built once and reused across repeated
 *    snobol_pattern_search() calls on the same pattern (trie caching).
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
  test_assert(cache2 == cache1, "trie cache reused (not rebuilt) on 2nd search");

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

void test_search_suite(void) {
  test_suite("Search: tier caching");
  test_trie_cache_hit();
}
