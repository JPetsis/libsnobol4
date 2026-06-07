#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "../../core/include/snobol/vm.h"
#include "../../core/include/snobol/jit.h"

/* Forward declare test framework helpers */
void test_suite(const char *name);
void test_assert(bool condition, const char *message);

/* Check if JIT is supported on this architecture (ARM64 only) */
static bool jit_is_supported(void) {
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64) \
    || defined(__arm__) || defined(__thumb__) || defined(__ARM_ARCH_7A__)
    return true;
#else
    return false;
#endif
}

#ifdef SNOBOL_JIT
static void emit_u32(uint8_t *bc, size_t *ip, uint32_t v) {
    bc[(*ip)++] = (uint8_t)((v >> 24) & 0xFF);
    bc[(*ip)++] = (uint8_t)((v >> 16) & 0xFF);
    bc[(*ip)++] = (uint8_t)((v >> 8) & 0xFF);
    bc[(*ip)++] = (uint8_t)(v & 0xFF);
}

static void test_jit_jmp_in_region(void) {
    /* Skip test on non-ARM64 architectures where JIT is not implemented */
    if (!jit_is_supported()) {
        test_assert(true, "JIT JMP test skipped (ARM64 only)");
        return;
    }
    
    // Initialize JIT and set config BEFORE creating VM
    snobol_jit_init();
    snobol_jit_reset_stats();
    
    // Disable profitability gate for testing - force JIT compilation
    SnobolJitConfig saved_cfg = *snobol_jit_get_config();
    SnobolJitConfig test_cfg = saved_cfg;
    test_cfg.hotness_threshold = 5;      // Lower threshold for faster testing
    test_cfg.min_useful_ops = 0;         // Allow any pattern
    test_cfg.skip_backtrack_heavy = false; // Don't skip backtrack patterns
    snobol_jit_set_config(&test_cfg);
    
    uint8_t bc[512] = {0};
    size_t ip = 0;

    // Program: ANY('x') JMP target LIT 'b' target: LIT 'a' ACCEPT
    // Subject: "xa"

    bc[ip++] = OP_ANY;
    bc[ip++] = 0; bc[ip++] = 1; // set_id = 1

    size_t jmp_ip = ip;
    bc[ip++] = OP_JMP;
    emit_u32(bc, &ip, 0); // target placeholder

    bc[ip++] = OP_LIT;
    emit_u32(bc, &ip, 500);
    emit_u32(bc, &ip, 1);
    bc[500] = 'b';

    size_t target_ip = ip;
    bc[ip++] = OP_LIT;
    emit_u32(bc, &ip, 501);
    emit_u32(bc, &ip, 1);
    bc[501] = 'a';
    bc[ip++] = OP_ACCEPT;

    size_t set_pos = 200;
    size_t offset_pos = 396;
    size_t count_pos = 400;

    size_t p = offset_pos;
    bc[p++] = 0; bc[p++] = 0; bc[p++] = 0; bc[p++] = (uint8_t)set_pos;
    p = count_pos;
    bc[p++] = 0; bc[p++] = 0; bc[p++] = 0; bc[p++] = 1;
    p = set_pos;
    bc[p++] = 0; bc[p++] = 1;
    bc[p++] = 0; bc[p++] = 0;
    bc[p++] = 0; bc[p++] = 0; bc[p++] = 0; bc[p++] = 'x';
    bc[p++] = 0; bc[p++] = 0; bc[p++] = 0; bc[p++] = 'x';

    ip = jmp_ip + 1;
    emit_u32(bc, &ip, (uint32_t)target_ip);

    VM vm = {0};
    vm.bc = bc;
    vm.bc_len = 404;
    vm.s = "xa";
    vm.len = 2;
    vm.jit.enabled = true;

    SnobolJitStats *stats = snobol_jit_get_stats();

    SnobolJitContext *ctx = snobol_jit_acquire_context(bc, vm.bc_len);
    vm.jit.ip_counts = ctx->ip_counts;
    vm.jit.traces = ctx->traces;
    vm.jit.stats = stats;
    vm.jit.ctx = ctx;

    // Warm up JIT: need 5+ visits to trigger compilation (with lowered threshold)
    for(int i=0; i<20; i++) {
        vm.ip = 0; vm.pos = 0;
        vm_run(&vm);
    }
    
    test_assert(stats->compilations_total > 0, "Should have compiled region");
    
    // Now test that entries are counted
    // Reset stats but NOT ip_counts (to preserve compiled traces)
    stats->entries_total = 0;
    stats->cache_hits_total = 0;
    
    // Run multiple iterations to enter JIT
    bool ok = false;
    for(int i=0; i<10; i++) {
        vm.ip = 0; vm.pos = 0;
        ok = vm_run(&vm);
    }
    
    if (!ok) {
        printf("  DEBUG: JMP test failed. Final IP: %zu, Pos: %zu\n", vm.ip, vm.pos);
    }
    
    test_assert(ok, "JMP in-region: match should succeed");
    if (stats->entries_total < 1) {
        printf("  DEBUG: entries_total = %llu\n", (unsigned long long)stats->entries_total);
    }
    test_assert(stats->entries_total >= 1, "Should enter JIT at least once");

    snobol_jit_release_context(ctx);
    snobol_jit_set_config(&saved_cfg);
    snobol_jit_shutdown();
}

static void test_jit_split_in_region(void) {
    /* Skip test on non-ARM64 architectures where JIT is not implemented */
    if (!jit_is_supported()) {
        test_assert(true, "JIT SPLIT test skipped (ARM64 only)");
        return;
    }

    uint8_t bc[512] = {0};
    size_t ip = 0;

    // Program: SPLIT a, b  a: LIT 'a' JMP end  b: LIT 'b'  end: ACCEPT
    // Subject: "b"

    size_t split_ip = ip;
    bc[ip++] = OP_SPLIT;
    emit_u32(bc, &ip, 0); // a
    emit_u32(bc, &ip, 0); // b

    size_t a_ip = ip;
    bc[ip++] = OP_LIT;
    emit_u32(bc, &ip, 500);
    emit_u32(bc, &ip, 1);
    bc[500] = 'a';

    size_t jmp_ip = ip;
    bc[ip++] = OP_JMP;
    emit_u32(bc, &ip, 0); // end

    size_t b_ip = ip;
    bc[ip++] = OP_LIT;
    emit_u32(bc, &ip, 501);
    emit_u32(bc, &ip, 1);
    bc[501] = 'b';

    size_t end_ip = ip;
    bc[ip++] = OP_ACCEPT;

    ip = split_ip + 1;
    emit_u32(bc, &ip, (uint32_t)a_ip);
    emit_u32(bc, &ip, (uint32_t)b_ip);
    ip = jmp_ip + 1;
    emit_u32(bc, &ip, (uint32_t)end_ip);

    VM vm = {0};
    vm.bc = bc;
    vm.bc_len = 512;
    vm.s = "b";
    vm.len = 1;
    vm.jit.enabled = true;

    // Initialize JIT first, then override config for testing
    snobol_jit_init();
    snobol_jit_reset_stats();
    
    /* Disable the profitability gate so we can test JIT compilation
     * of SPLIT-containing patterns directly. */
    SnobolJitConfig saved_cfg = *snobol_jit_get_config();
    SnobolJitConfig cfg = saved_cfg;
    cfg.min_useful_ops       = 0;
    cfg.hotness_threshold    = 5;
    cfg.skip_backtrack_heavy = false;
    snobol_jit_set_config(&cfg);
    
    SnobolJitStats *stats = snobol_jit_get_stats();

    SnobolJitContext *ctx = snobol_jit_acquire_context(bc, vm.bc_len);
    vm.jit.ip_counts = ctx->ip_counts;
    vm.jit.traces = ctx->traces;
    vm.jit.stats = stats;

    for(int i=0; i<20; i++) {
        vm.ip = 0; vm.pos = 0;
        vm_run(&vm);
    }
    
    test_assert(stats->compilations_total > 0, "Should have compiled region with SPLIT");
    
    snobol_jit_reset_stats();
    vm.ip = 0; vm.pos = 0;
    bool ok = vm_run(&vm);
    
    if (!ok) {
        printf("  DEBUG: SPLIT test failed. Final IP: %zu, Pos: %zu\n", vm.ip, vm.pos);
    }
    
    test_assert(ok, "SPLIT in-region: match should succeed");
    test_assert(stats->choice_push_total > 0, "Should have pushed a choice");
    test_assert(stats->choice_bytes_total > 0, "Should have recorded choice bytes");

    snobol_jit_release_context(ctx);
    snobol_jit_set_config(&saved_cfg);
    snobol_jit_shutdown();
}

/* Backtracking across a JIT/interpreter boundary.
 * JIT pushes a choice for branch b, taken branch a fails, interpreter pops
 * the choice and jumps to b — the resulting match must be correct. */
static void test_jit_split_backtrack_boundary(void) {
    if (!jit_is_supported()) {
        test_assert(true, "JIT SPLIT backtrack boundary test skipped (ARM64 only)");
        return;
    }

    /* Bytecode:
     *   SPLIT a, b          ; push choice(b), take a
     *   a: LIT 'x'          ; will fail on subject "b"
     *      JMP end
     *   b: LIT 'b'          ; will succeed
     *   end: ACCEPT
     * Subject: "b"
     * Expected: match succeeds (backtrack from a to b), cap consumed 'b'
     */
    uint8_t bc[512] = {0};
    size_t ip = 0;

    size_t split_ip = ip;
    bc[ip++] = OP_SPLIT;
    emit_u32(bc, &ip, 0); /* a placeholder */
    emit_u32(bc, &ip, 0); /* b placeholder */

    size_t a_ip = ip;
    bc[ip++] = OP_LIT;
    emit_u32(bc, &ip, 500); /* literal offset */
    emit_u32(bc, &ip, 1);   /* literal len */
    bc[500] = 'x';

    size_t jmp_ip = ip;
    bc[ip++] = OP_JMP;
    emit_u32(bc, &ip, 0); /* end placeholder */

    size_t b_ip = ip;
    bc[ip++] = OP_LIT;
    emit_u32(bc, &ip, 501);
    emit_u32(bc, &ip, 1);
    bc[501] = 'b';

    size_t end_ip = ip;
    bc[ip++] = OP_ACCEPT;

    /* Patch targets */
    size_t p = split_ip + 1; emit_u32(bc, &p, (uint32_t)a_ip);
    p = split_ip + 5;        emit_u32(bc, &p, (uint32_t)b_ip);
    p = jmp_ip + 1;          emit_u32(bc, &p, (uint32_t)end_ip);

    snobol_jit_init();
    snobol_jit_reset_stats();

    SnobolJitConfig saved_cfg = *snobol_jit_get_config();
    SnobolJitConfig cfg = saved_cfg;
    cfg.min_useful_ops       = 0;
    cfg.hotness_threshold    = 5;
    cfg.skip_backtrack_heavy = false;
    snobol_jit_set_config(&cfg);

    SnobolJitStats *stats = snobol_jit_get_stats();
    SnobolJitContext *ctx  = snobol_jit_acquire_context(bc, 512);

    VM vm = {0};
    vm.bc     = bc;
    vm.bc_len = 512;
    vm.s      = "b";
    vm.len    = 1;
    vm.jit.enabled   = true;
    vm.jit.ip_counts = ctx->ip_counts;
    vm.jit.traces    = ctx->traces;
    vm.jit.stats     = stats;
    vm.jit.ctx       = ctx;

    /* Warm up to trigger JIT compilation */
    for (int i = 0; i < 20; i++) {
        vm.ip = 0; vm.pos = 0;
        vm_run(&vm);
    }

    test_assert(stats->compilations_total > 0,
                "SPLIT backtrack: should have compiled a region");

    /* Final run: JIT executes SPLIT (pushes choice for b), takes a (fails on 'x'),
     * interpreter backtracks to b (succeeds on 'b'). */
    snobol_jit_reset_stats();
    vm.ip = 0; vm.pos = 0;
    bool ok = vm_run(&vm);

    test_assert(ok, "SPLIT backtrack: match via non-taken branch should succeed");
    test_assert(vm.pos == 1, "SPLIT backtrack: pos should be 1 after consuming 'b'");

    snobol_jit_release_context(ctx);
    snobol_jit_set_config(&saved_cfg);
    snobol_jit_shutdown();
}

/* Backward SPLIT terminates the region.
 * A SPLIT whose non-taken branch (b) is a backward offset must NOT be included
 * in the compiled region.  The region should stop before the SPLIT, and
 * the overall match result must still be correct (interpreter handles SPLIT). */
static void test_jit_split_backward_terminates_region(void) {
    if (!jit_is_supported()) {
        test_assert(true, "JIT backward SPLIT test skipped (ARM64 only)");
        return;
    }

    /* Bytecode layout:
     *   [0]  OP_LEN 1          ; 5 bytes: opcode + u32
     *   [5]  OP_SPLIT a=10, b=0 ; 9 bytes: opcode + u32 + u32
     *   [10] OP_ACCEPT
     *
     * b=0 < split_ip=5 → backward branch → guard fires → region ends before SPLIT.
     * Interpreter processes SPLIT: push choice(0, pos), set ip=10 (ACCEPT) → match.
     * Subject "a" (len=1): LEN 1 succeeds (pos→1), SPLIT takes a=10 (ACCEPT). */
    uint8_t bc[32] = {0};
    size_t ip = 0;

    bc[ip++] = OP_LEN;
    emit_u32(bc, &ip, 1); /* LEN 1 → advance pos by 1; ip=5 */

    /* SPLIT at ip=5: a=10 (forward), b=0 (backward → <= split_ip=5) */
    bc[ip++] = OP_SPLIT; /* ip=5 */
    emit_u32(bc, &ip, 10); /* a = 10 */
    emit_u32(bc, &ip, 0);  /* b = 0 < 5 → backward → must terminate region */
    /* ip=14 now; bc[10] was just written as 0 by emit_u32 — overwrite: */
    bc[10] = OP_ACCEPT;

    snobol_jit_init();
    snobol_jit_reset_stats();

    SnobolJitConfig saved_cfg = *snobol_jit_get_config();
    SnobolJitConfig cfg = saved_cfg;
    cfg.min_useful_ops       = 1;
    cfg.hotness_threshold    = 5;
    cfg.skip_backtrack_heavy = false;
    snobol_jit_set_config(&cfg);

    SnobolJitStats *stats = snobol_jit_get_stats();
    SnobolJitContext *ctx  = snobol_jit_acquire_context(bc, 32);

    VM vm = {0};
    vm.bc     = bc;
    vm.bc_len = 32;
    vm.s      = "a";
    vm.len    = 1;
    vm.jit.enabled   = true;
    vm.jit.ip_counts = ctx->ip_counts;
    vm.jit.traces    = ctx->traces;
    vm.jit.stats     = stats;
    vm.jit.ctx       = ctx;

    /* Warm up to trigger JIT compilation of the LEN region */
    for (int i = 0; i < 20; i++) {
        vm.ip = 0; vm.pos = 0;
        vm_run(&vm);
    }

    test_assert(stats->compilations_total > 0,
                "backward SPLIT: JIT should compile region for LEN before SPLIT");

    /* Correctness check: backward SPLIT handled by interpreter → match */
    snobol_jit_reset_stats();
    vm.ip = 0; vm.pos = 0;
    bool ok = vm_run(&vm);
    test_assert(ok, "backward SPLIT: match still succeeds via interpreter SPLIT");

    snobol_jit_release_context(ctx);
    snobol_jit_set_config(&saved_cfg);
    snobol_jit_shutdown();
}
#endif /* SNOBOL_JIT */

void test_jit_branches_suite(void) {
#ifdef SNOBOL_JIT
    test_suite("JIT Branches");
    test_jit_jmp_in_region();
    test_jit_split_in_region();
    test_jit_split_backtrack_boundary();
    test_jit_split_backward_terminates_region();
#endif
}
