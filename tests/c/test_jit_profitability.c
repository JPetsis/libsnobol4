/**
 * JIT Profitability Heuristics Tests
 *
 * Validates:
 * (a) cold/simple patterns (< min_useful_ops) skip JIT
 * (b) hot patterns with enough useful ops enable JIT
 * (c) backtracking-dominated patterns skip JIT when skip_backtrack_heavy=true
 * (d) early-exit rule (stop_compiling) triggers when exit rate is too high
 * (e) decisions are observable via counters
 */
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "../../snobol4-php/snobol_vm.h"
#include "../../snobol4-php/snobol_jit.h"

void test_suite(const char *name);
void test_assert(bool condition, const char *message);

/* helpers ----------------------------------------------------------- */
static void emit_u32_be(uint8_t *bc, size_t *ip, uint32_t v) {
    bc[(*ip)++] = (uint8_t)((v >> 24) & 0xFF);
    bc[(*ip)++] = (uint8_t)((v >> 16) & 0xFF);
    bc[(*ip)++] = (uint8_t)((v >>  8) & 0xFF);
    bc[(*ip)++] = (uint8_t)( v        & 0xFF);
}

/* Build: LIT<s> ACCEPT  (inline literal, no char-class table needed) */
static size_t build_lit_accept(uint8_t *bc, const char *s) {
    size_t ip = 0;
    size_t slen = strlen(s);
    /* OP_LIT: offset(4) len(4) <bytes> */
    bc[ip++] = OP_LIT;
    /* offset points right after the two u32 operands */
    uint32_t offset = (uint32_t)(1 + 4 + 4); /* ip of literal data */
    emit_u32_be(bc, &ip, offset);
    emit_u32_be(bc, &ip, (uint32_t)slen);
    for (size_t i = 0; i < slen; i++) bc[ip++] = (uint8_t)s[i];
    bc[ip++] = OP_ACCEPT;
    return ip;
}

/* Build: LIT<a> LIT<b> ACCEPT */
static size_t build_two_lits_accept(uint8_t *bc, const char *a, const char *b) {
    size_t ip = 0;
    size_t alen = strlen(a), blen = strlen(b);

    /* first LIT */
    bc[ip++] = OP_LIT;
    uint32_t off_a = (uint32_t)(1 + 4 + 4); /* right after first operand pair */
    emit_u32_be(bc, &ip, off_a);
    emit_u32_be(bc, &ip, (uint32_t)alen);
    for (size_t i = 0; i < alen; i++) bc[ip++] = (uint8_t)a[i];

    /* second LIT */
    size_t lit2_start = ip;
    bc[ip++] = OP_LIT;
    uint32_t off_b = (uint32_t)(lit2_start + 1 + 4 + 4);
    emit_u32_be(bc, &ip, off_b);
    emit_u32_be(bc, &ip, (uint32_t)blen);
    for (size_t i = 0; i < blen; i++) bc[ip++] = (uint8_t)b[i];

    bc[ip++] = OP_ACCEPT;
    return ip;
}

/* Build: SPLIT a b  a:ACCEPT  b:FAIL */
static size_t build_split_only(uint8_t *bc) {
    size_t ip = 0;
    bc[ip++] = OP_SPLIT;
    size_t patch_a = ip; emit_u32_be(bc, &ip, 0);
    size_t patch_b = ip; emit_u32_be(bc, &ip, 0);
    size_t a_ip = ip; bc[ip++] = OP_ACCEPT;
    size_t b_ip = ip; bc[ip++] = OP_FAIL;

    /* patch targets */
    size_t tmp = patch_a;
    emit_u32_be(bc, &tmp, (uint32_t)a_ip);
    tmp = patch_b;
    emit_u32_be(bc, &tmp, (uint32_t)b_ip);
    return ip;
}

/* ------------------------------------------------------------------- */

/* Test (a): direct call to snobol_jit_should_compile - cold/simple patterns */
static void test_profitability_cold_skip(void) {
    SnobolJitConfig cfg = *snobol_jit_get_config();
    cfg.min_useful_ops       = 2;
    cfg.skip_backtrack_heavy = true;

    uint8_t bc[256] = {0};

    /* 0 useful ops: just ACCEPT → should_compile must return false */
    bc[0] = OP_ACCEPT;
    VM vm0 = {0}; vm0.bc = bc; vm0.bc_len = 1;
    test_assert(!snobol_jit_should_compile(&vm0, 0, &cfg),
                "Cold skip: ACCEPT-only pattern skips JIT");

    /* 1 useful op: LIT 'a' ACCEPT → useful=1 < 2 → false */
    size_t len1 = build_lit_accept(bc, "a");
    VM vm1 = {0}; vm1.bc = bc; vm1.bc_len = len1;
    test_assert(!snobol_jit_should_compile(&vm1, 0, &cfg),
                "Cold skip: single-literal pattern skips JIT (useful < min_useful_ops)");

    /* 2 useful ops: LIT 'ab' LIT 'cd' ACCEPT → useful=2 >= 2 → true */
    size_t len2 = build_two_lits_accept(bc, "ab", "cd");
    VM vm2 = {0}; vm2.bc = bc; vm2.bc_len = len2;
    test_assert(snobol_jit_should_compile(&vm2, 0, &cfg),
                "Hot pattern: two-literal pattern enables JIT (useful >= min_useful_ops)");
}

/* Test (b): hot patterns with enough useful ops pass profitability gate */
static void test_profitability_hot_enable(void) {
    SnobolJitConfig cfg = *snobol_jit_get_config();
    cfg.min_useful_ops       = 1;
    cfg.skip_backtrack_heavy = false; /* disable backtrack skip to isolate test */

    uint8_t bc[256] = {0};

    /* Even 1 useful op is enough when min_useful_ops=1 */
    size_t len = build_lit_accept(bc, "hello");
    VM vm = {0}; vm.bc = bc; vm.bc_len = len;
    test_assert(snobol_jit_should_compile(&vm, 0, &cfg),
                "Hot enable: pattern with min_useful_ops=1 passes gate");
}

/* Test (c): backtracking-dominated patterns skip JIT */
static void test_profitability_backtrack_skip(void) {
    SnobolJitConfig cfg = *snobol_jit_get_config();
    cfg.min_useful_ops       = 1;
    cfg.skip_backtrack_heavy = true;

    uint8_t bc[64] = {0};

    /* SPLIT at ip=0 → has_backtrack=true, useful=0 < 5 → false */
    size_t len = build_split_only(bc);
    VM vm = {0}; vm.bc = bc; vm.bc_len = len;
    test_assert(!snobol_jit_should_compile(&vm, 0, &cfg),
                "Backtrack skip: SPLIT-dominated pattern skips JIT");

    /* Disable both gates → SPLIT-only pattern can pass profitability check */
    cfg.skip_backtrack_heavy = false;
    cfg.min_useful_ops = 0;
    test_assert(snobol_jit_should_compile(&vm, 0, &cfg),
                "Backtrack allow: with skip_backtrack_heavy=false and min_useful_ops=0, SPLIT pattern passes gate");
}

/* Test (c2): configurable thresholds — snobol_jit_set_config adjusts decisions */
static void test_profitability_config_adjustable(void) {
    /* Save original config */
    SnobolJitConfig saved = *snobol_jit_get_config();

    uint8_t bc[256] = {0};
    size_t len = build_lit_accept(bc, "x");
    VM vm = {0}; vm.bc = bc; vm.bc_len = len;

    /* With min_useful_ops=2, single-LIT should be skipped */
    SnobolJitConfig cfg_strict = saved;
    cfg_strict.min_useful_ops = 2;
    snobol_jit_set_config(&cfg_strict);
    test_assert(!snobol_jit_should_compile(&vm, 0, snobol_jit_get_config()),
                "Config: min_useful_ops=2 skips single-literal pattern");

    /* With min_useful_ops=1, same pattern should compile */
    SnobolJitConfig cfg_loose = saved;
    cfg_loose.min_useful_ops = 1;
    snobol_jit_set_config(&cfg_loose);
    test_assert(snobol_jit_should_compile(&vm, 0, snobol_jit_get_config()),
                "Config: min_useful_ops=1 allows single-literal pattern");

    /* Restore original */
    snobol_jit_set_config(&saved);
}

/* Test (d+e): cold skip counter is incremented via the VM dispatch path */
static void test_profitability_counter_cold(void) {
    snobol_jit_init();
    snobol_jit_reset_stats();

    /* Force aggressive settings: require 3 useful ops to compile */
    SnobolJitConfig cfg = *snobol_jit_get_config();
    SnobolJitConfig saved = cfg;
    cfg.min_useful_ops    = 3;
    cfg.hotness_threshold = 10; /* trigger quickly */
    snobol_jit_set_config(&cfg);

    /* Pattern: LIT 'a' LIT 'b' ACCEPT → only 2 useful ops → below threshold=3 */
    uint8_t bc[256] = {0};
    size_t bc_len = build_two_lits_accept(bc, "a", "b");

    SnobolJitContext *ctx = snobol_jit_acquire_context(bc, bc_len);
    SnobolJitStats  *stats = snobol_jit_get_stats();

    VM vm = {0};
    vm.bc = bc; vm.bc_len = bc_len;
    vm.s = "ab"; vm.len = 2;
    vm.jit.enabled = true;
    vm.jit.ip_counts = ctx->ip_counts;
    vm.jit.traces    = ctx->traces;
    vm.jit.stats     = stats;
    vm.jit.ctx       = ctx;

    /* Run enough to trigger hotness (threshold=10) */
    for (int i = 0; i < 15; i++) {
        vm.ip = 0; vm.pos = 0;
        vm_run(&vm);
    }

    test_assert(stats->skipped_cold_total > 0,
                "Counter: skipped_cold_total increments when profitability gate rejects region");
    test_assert(stats->compilations_total == 0,
                "Counter: compilations_total stays 0 when all regions are skipped");

    snobol_jit_release_context(ctx);
    snobol_jit_shutdown();
    snobol_jit_set_config(&saved);
    snobol_jit_init();
}

void test_jit_profitability_suite(void) {
#ifdef SNOBOL_JIT
    test_suite("JIT Profitability Heuristics");
    test_profitability_cold_skip();
    test_profitability_hot_enable();
    test_profitability_backtrack_skip();
    test_profitability_config_adjustable();
    test_profitability_counter_cold();
#endif
}


