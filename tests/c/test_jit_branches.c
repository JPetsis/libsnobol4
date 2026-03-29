#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "../../core/include/snobol/vm.h"
#include "../../core/include/snobol/jit.h"

/* Forward declare test framework helpers */
void test_suite(const char *name);
void test_assert(bool condition, const char *message);

static void emit_u32(uint8_t *bc, size_t *ip, uint32_t v) {
    bc[(*ip)++] = (uint8_t)((v >> 24) & 0xFF);
    bc[(*ip)++] = (uint8_t)((v >> 16) & 0xFF);
    bc[(*ip)++] = (uint8_t)((v >> 8) & 0xFF);
    bc[(*ip)++] = (uint8_t)(v & 0xFF);
}

static void test_jit_jmp_in_region(void) {
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
    
    snobol_jit_init();
    snobol_jit_reset_stats();
    SnobolJitStats *stats = snobol_jit_get_stats();
    
    SnobolJitContext *ctx = snobol_jit_acquire_context(bc, vm.bc_len);
    vm.jit.ip_counts = ctx->ip_counts;
    vm.jit.traces = ctx->traces;
    vm.jit.stats = stats;

    // Warm up JIT: need 50+ visits to trigger compilation
    for(int i=0; i<100; i++) {
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
}

static void test_jit_split_in_region(void) {
    /* Disable the profitability gate so we can test JIT compilation
     * of SPLIT-containing patterns directly. */
    SnobolJitConfig saved_cfg = *snobol_jit_get_config();
    SnobolJitConfig cfg = saved_cfg;
    cfg.min_useful_ops       = 0;
    cfg.skip_backtrack_heavy = false;
    snobol_jit_set_config(&cfg);
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
    
    snobol_jit_init();
    snobol_jit_reset_stats();
    SnobolJitStats *stats = snobol_jit_get_stats();
    
    SnobolJitContext *ctx = snobol_jit_acquire_context(bc, vm.bc_len);
    vm.jit.ip_counts = ctx->ip_counts;
    vm.jit.traces = ctx->traces;
    vm.jit.stats = stats;

    for(int i=0; i<100; i++) {
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
}

void test_jit_branches_suite(void) {
#ifdef SNOBOL_JIT
    test_suite("JIT Branches");
    test_jit_jmp_in_region();
    test_jit_split_in_region();
#endif
}
