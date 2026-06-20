/**
 * test_search_jit.c
 *
 * JIT-focused regression tests:
 *   - Verify correctness when search fast paths and compiled JIT regions
 * interact.
 *   - Verify search-mode profitability is distinct from anchored-mode.
 *   - Verify search-mode diagnostics (counters) are populated.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/include/snobol/search.h"
#include "../../core/include/snobol/vm.h"
#ifdef SNOBOL_JIT
#include "../../core/include/snobol/jit.h"
#endif

void test_suite(const char *name);
void test_assert(bool condition, const char *message);

/* ---------------------------------------------------------------------------
 * Bytecode builder (reused from test_search_runtime.c style)
 * ---------------------------------------------------------------------------
 */

static void jt_emit_u32(uint8_t *bc, size_t *ip, uint32_t v) {
  bc[(*ip)++] = (uint8_t)((v >> 24) & 0xFF);
  bc[(*ip)++] = (uint8_t)((v >> 16) & 0xFF);
  bc[(*ip)++] = (uint8_t)((v >> 8) & 0xFF);
  bc[(*ip)++] = (uint8_t)(v & 0xFF);
}

/** Build: OP_LIT(s) OP_ACCEPT */
static size_t jt_build_lit(uint8_t *bc, const char *s, size_t slen) {
  size_t ip = 0;
  bc[ip++] = OP_LIT;
  uint32_t off = (uint32_t)(1 + 4 + 4);
  jt_emit_u32(bc, &ip, off);
  jt_emit_u32(bc, &ip, (uint32_t)slen);
  for (size_t i = 0; i < slen; i++)
    bc[ip++] = (uint8_t)s[i];
  bc[ip++] = OP_ACCEPT;
  return ip;
}

/* ---------------------------------------------------------------------------
 * Test helpers
 * ---------------------------------------------------------------------------
 */

/** Run search N times and return number of successes */
static int run_search_n(const uint8_t *bc, size_t bc_len, const char *subject,
                        int n) {
  snobol_search_meta_t meta;
  snobol_search_derive_meta(bc, bc_len, &meta);

  int found = 0;
  for (int i = 0; i < n; i++) {
    VM vm;
    memset(&vm, 0, sizeof(vm));
    vm.bc = bc;
    vm.bc_len = bc_len;
#ifdef SNOBOL_JIT
    SnobolJitContext *ctx = snobol_jit_acquire_context(bc, bc_len);
    if (ctx) {
      vm.jit.ip_counts = ctx->ip_counts;
      vm.jit.traces = ctx->traces;
      vm.jit.ctx = ctx;
      vm.jit.enabled = true;
      vm.jit.stats = snobol_jit_get_stats();
    }
#endif
    snobol_search_result_t result;
    bool ok = snobol_search_exec(&vm, subject, strlen(subject), 0, &meta,
                                 &result, NULL);
    if (ok)
      found++;
#ifdef SNOBOL_JIT
    if (vm.jit.ctx)
      snobol_jit_release_context(vm.jit.ctx);
#endif
  }
  return found;
}

/* ---------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------------
 */

#ifdef SNOBOL_JIT

static void test_search_jit_correctness_through_hotness(void) {
  test_suite("JIT search: correctness maintained through JIT hotness");

  snobol_jit_init();
  snobol_jit_reset_stats();

  uint8_t bc[64];
  size_t bc_len = jt_build_lit(bc, "fox", 3);

  /* Run enough times to exceed hotness threshold */
  SnobolJitConfig cfg = *snobol_jit_get_config();
  int runs = (int)(cfg.hotness_threshold * 2 + 10);

  int successes = run_search_n(bc, bc_len, "the quick brown fox", runs);

  test_assert(
      successes == runs,
      "JIT search: all runs find 'fox' correctly after JIT compilation");

  snobol_jit_shutdown();
  snobol_jit_init();
}

static void test_search_mode_profitability_lower_threshold(void) {
  test_suite("JIT: search-mode uses lower hotness threshold");

  /* Save config */
  SnobolJitConfig saved = *snobol_jit_get_config();

  /* Set search_min_useful_ops = 1 so single-op patterns compile in search mode
   */
  SnobolJitConfig cfg = saved;
  cfg.search_min_useful_ops = 1;
  cfg.min_useful_ops = 2; /* anchored mode requires 2 */
  snobol_jit_set_config(&cfg);

  uint8_t bc[64];
  size_t bc_len = jt_build_lit(bc, "x", 1);

  VM vm;
  memset(&vm, 0, sizeof(vm));
  vm.bc = bc;
  vm.bc_len = bc_len;

  /* Search-mode should allow compilation with min_useful_ops=1 */
  bool search_ok =
      snobol_jit_should_compile(&vm, 0, snobol_jit_get_config(), true);
  /* Anchored-mode should deny (min_useful_ops=2 but we only have 1 useful op)
   */
  bool anchored_ok =
      snobol_jit_should_compile(&vm, 0, snobol_jit_get_config(), false);

  test_assert(search_ok,
              "search_mode=true allows single-op pattern compilation");
  test_assert(!anchored_ok,
              "search_mode=false denies single-op with min_useful_ops=2");

  /* Restore config */
  snobol_jit_set_config(&saved);
}

/** Build OP_ANY / OP_BREAK / OP_BREAKX / OP_SPAN over a one-char ASCII set.
 * Reuses the same charclass table layout as test_search_runtime.c. */
static size_t jt_build_char_class_op(uint8_t *bc, uint8_t op, char c) {
  size_t ip = 0;
  /* op  set_id(u16=1)  ACCEPT */
  bc[ip++] = op;
  bc[ip++] = 0;
  bc[ip++] = 1;         /* set_id = 1 */
  bc[ip++] = OP_ACCEPT; /* ip = 4 */

  /* charclass data at ip=4: range_count(u16)=1, case_flag(u16)=0, range[c,c](8
   * bytes) */
  size_t class_data_off = ip;
  bc[ip++] = 0;
  bc[ip++] = 1; /* range_count = 1 */
  bc[ip++] = 0;
  bc[ip++] = 0; /* case_flag = 0 */
  uint32_t cv = (uint32_t)(unsigned char)c;
  /* range start (u32 BE) */
  bc[ip++] = (uint8_t)((cv >> 24) & 0xFF);
  bc[ip++] = (uint8_t)((cv >> 16) & 0xFF);
  bc[ip++] = (uint8_t)((cv >> 8) & 0xFF);
  bc[ip++] = (uint8_t)(cv & 0xFF);
  /* range end (u32 BE) — same as start for single char */
  bc[ip++] = (uint8_t)((cv >> 24) & 0xFF);
  bc[ip++] = (uint8_t)((cv >> 16) & 0xFF);
  bc[ip++] = (uint8_t)((cv >> 8) & 0xFF);
  bc[ip++] = (uint8_t)(cv & 0xFF);
  /* offset table: 1 entry pointing at class_data_off */
  bc[ip++] = (uint8_t)((class_data_off >> 24) & 0xFF);
  bc[ip++] = (uint8_t)((class_data_off >> 16) & 0xFF);
  bc[ip++] = (uint8_t)((class_data_off >> 8) & 0xFF);
  bc[ip++] = (uint8_t)(class_data_off & 0xFF);
  /* table count = 1 */
  bc[ip++] = 0;
  bc[ip++] = 0;
  bc[ip++] = 0;
  bc[ip++] = 1;
  return ip;
}

static void test_search_jit_bailout_counter(void) {
  test_suite("JIT: search candidate bailout counter is attributed");

  snobol_jit_init();
  snobol_jit_reset_stats();

  /* OP_ANY("z") OP_ACCEPT — a single char-class op that will fail at every
   * non-'z' position.  Because this pattern has no literal prefix, the
   * literal-accelerated fast path is NOT used; instead every position in the
   * subject drives a separate vm_exec call (via the automaton/fallback path).
   * That guarantees ip_counts[0] accumulates quickly and JIT compilation fires
   * after just a few calls.  Once compiled, every failing call bails at ip=0
   * with search_mode=true → bailout_search_candidate_total increments. */
  uint8_t bc[128];
  size_t bc_len = jt_build_char_class_op(bc, OP_ANY, 'z');

  SnobolJitConfig saved = *snobol_jit_get_config();
  SnobolJitConfig cfg = saved;
  cfg.search_hotness_threshold = 3; /* compile after just 3 calls */
  cfg.search_min_useful_ops = 1;    /* OP_ANY counts as 1 useful op */
  snobol_jit_set_config(&cfg);

  SnobolJitContext *ctx = snobol_jit_acquire_context(bc, bc_len);
  SnobolJitStats *stats = snobol_jit_get_stats();

  /* Force automaton/vm_exec path so the JIT fires at each position.
   * Since snobol_search_derive_meta() now recognises a root OP_ANY with an
   * ASCII-only charclass and routes it to the fast bitmap path (skipping
   * vm_exec entirely), we must override the meta to force Tier-4 execution
   * where vm_exec is called at every candidate position and the JIT can fire.
   */
  snobol_search_meta_t meta;
  memset(&meta, 0, sizeof(meta));
  meta.automaton_eligible = true; /* Tier 4: call vm_exec at every position */

  /* 5 outer searches × ~15 positions each = ~75 vm_exec calls total.
   * After 3 calls JIT fires; every subsequent call bails at ip=0 in search
   * mode, incrementing bailout_search_candidate_total. */
  for (int i = 0; i < 5; i++) {
    VM vm;
    memset(&vm, 0, sizeof(vm));
    vm.bc = bc;
    vm.bc_len = bc_len;
    vm.jit.ip_counts = ctx->ip_counts;
    vm.jit.traces = ctx->traces;
    vm.jit.ctx = ctx;
    vm.jit.enabled = true;
    vm.jit.stats = stats;

    snobol_search_result_t result;
    (void)snobol_search_exec(&vm, "aabbccddeeffgg", 14, 0, &meta, &result,
                             NULL);
    /* No match expected — every position fails */
  }

  test_assert(stats->bailouts_total > 0,
              "JIT: bailouts recorded after search workloads");
  test_assert(stats->bailout_search_candidate_total > 0,
              "JIT: search_candidate bailouts attributed");

  snobol_jit_release_context(ctx);
  snobol_jit_shutdown();
  snobol_jit_set_config(&saved);
  snobol_jit_init();
}

/* Search_candidate_rejects must NOT be incremented when the JIT
 * bails out for reasons other than a clean candidate rejection (i.e., partial
 * progress / left-region bailout).  We verify that a bailout with ip !=
 * entry_ip does NOT touch search_candidate_rejects. */
static void test_search_candidate_rejects_attribution(void) {
  test_suite("JIT: search_candidate_rejects attribution");

  snobol_jit_init();
  snobol_jit_reset_stats();

  /* Build: OP_ANY('a') OP_ACCEPT — a simple candidate-rejection workload.
   * On a subject with no 'a', every candidate bails at ip==entry_ip:
   *   → bailout_search_candidate_total and search_candidate_rejects increment.
   * On a subject WITH 'a', the trace SUCCEEDS (ip advances to ACCEPT then
   * returns), so search_candidate_rejects stays 0 for that execution (not a
   * bailout at all). We run on "bbb" which has no 'a', so all exits should be
   * candidate rejects. */
  uint8_t bc[128];
  size_t bc_len = jt_build_char_class_op(bc, OP_ANY, 'a');

  SnobolJitConfig saved = *snobol_jit_get_config();
  SnobolJitConfig cfg = saved;
  cfg.search_hotness_threshold = 2;
  cfg.search_min_useful_ops = 1;
  snobol_jit_set_config(&cfg);

  SnobolJitContext *ctx = snobol_jit_acquire_context(bc, bc_len);
  SnobolJitStats *stats = snobol_jit_get_stats();

  /* Force automaton/vm_exec path — same reason as in
   * test_search_jit_bailout_counter: the bitmap-accelerated Tier 3 path for
   * OP_ANY would bypass vm_exec, preventing JIT compilation and bailout
   * counting.  We want vm_exec called at every position so ip_counts[0]
   * accumulates and the JIT fires. */
  snobol_search_meta_t meta;
  memset(&meta, 0, sizeof(meta));
  meta.automaton_eligible = true; /* Tier 4: vm_exec at every position */

  for (int i = 0; i < 5; i++) {
    VM vm;
    memset(&vm, 0, sizeof(vm));
    vm.bc = bc;
    vm.bc_len = bc_len;
    vm.jit.ip_counts = ctx->ip_counts;
    vm.jit.traces = ctx->traces;
    vm.jit.ctx = ctx;
    vm.jit.enabled = true;
    vm.jit.stats = stats;

    snobol_search_result_t result;
    (void)snobol_search_exec(&vm, "bbb", 3, 0, &meta, &result, NULL);
  }

  /* All bailouts on "bbb" must be candidate rejects (ip == entry_ip) */
  uint64_t rejects = stats->search_candidate_rejects;
  uint64_t partials = stats->bailout_partial_total;

  /* search_candidate_rejects must be positive (we had bailouts on 'b'
   * positions) */
  test_assert(rejects > 0,
              "search_candidate_rejects > 0 for pure candidate rejections");

  /* bailout_partial_total must be 0: no bailout with partial progress */
  test_assert(partials == 0,
              "no partial-progress bailouts for simple ANY pattern");

  /* Verify: search_candidate_rejects == bailout_search_candidate_total */
  test_assert(
      rejects == stats->bailout_search_candidate_total,
      "search_candidate_rejects matches bailout_search_candidate_total");

  snobol_jit_release_context(ctx);
  snobol_jit_shutdown();
  snobol_jit_set_config(&saved);
  snobol_jit_init();
}

#endif /* SNOBOL_JIT */

static void test_search_no_jit_fallback(void) {
  test_suite("Search: correct result without JIT");

  uint8_t bc[64];
  size_t bc_len = jt_build_lit(bc, "hello", 5);

  VM vm;
  memset(&vm, 0, sizeof(vm));
  vm.bc = bc;
  vm.bc_len = bc_len;
  /* JIT disabled */

  snobol_search_meta_t meta;
  snobol_search_derive_meta(bc, bc_len, &meta);

  snobol_search_result_t result;
  bool ok =
      snobol_search_exec(&vm, "say hello world", 15, 0, &meta, &result, NULL);
  test_assert(ok, "No-JIT search finds 'hello'");
  test_assert(result.match_start == 4, "No-JIT match_start == 4");
  test_assert(result.match_end == 9, "No-JIT match_end == 9");
}

/* ---------------------------------------------------------------------------
 * Suite entry point
 * ---------------------------------------------------------------------------
 */

void test_search_jit_suite(void) {
  test_search_no_jit_fallback();

#ifdef SNOBOL_JIT
  test_search_jit_correctness_through_hotness();
  test_search_mode_profitability_lower_threshold();
  test_search_jit_bailout_counter();
  test_search_candidate_rejects_attribution();
#endif
}
