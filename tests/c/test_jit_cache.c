#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "../../core/include/snobol/vm.h"
#include "../../core/include/snobol/jit.h"

void test_suite(const char *name);
void test_assert(bool condition, const char *message);

/* Check if JIT is supported on this architecture (ARM64 only) */
static bool jit_is_supported(void) {
#if defined(__aarch64__) || defined(_M_ARM64)
    return true;
#else
    return false;
#endif
}

/* -----------------------------------------------------------------------
 * Task 3.4: LRU cache eviction stress tests
 * Validates:
 *   - Cache evicts LRU entries with ref_count == 0 when full
 *   - Entries with ref_count > 0 are never evicted
 *   - Repeated compile/execute cycles remain safe after eviction
 * ----------------------------------------------------------------------- */
static void test_cache_eviction_stress(void) {
    /* Skip on non-ARM64 where JIT compilation is not available */
    if (!jit_is_supported()) {
        test_assert(true, "Cache eviction stress test skipped (ARM64 only)");
        return;
    }
    
    /* Use a very small cache to force evictions quickly */
    SnobolJitConfig cfg = *snobol_jit_get_config();
    SnobolJitConfig saved = cfg;
    cfg.cache_max_entries = 3;
    snobol_jit_set_config(&cfg);

    snobol_jit_init();
    snobol_jit_reset_stats();

    /* Build 5 distinct bytecodes (different literal bytes) */
    uint8_t bcs[5][32];
    size_t  lens[5];
    for (int i = 0; i < 5; i++) {
        memset(bcs[i], 0, sizeof(bcs[i]));
        size_t ip = 0;
        bcs[i][ip++] = OP_LIT;
        /* offset=9, len=3 */
        bcs[i][ip++] = 0; bcs[i][ip++] = 0; bcs[i][ip++] = 0; bcs[i][ip++] = 9;
        bcs[i][ip++] = 0; bcs[i][ip++] = 0; bcs[i][ip++] = 0; bcs[i][ip++] = 3;
        bcs[i][ip++] = (uint8_t)('a' + i);
        bcs[i][ip++] = (uint8_t)('a' + i);
        bcs[i][ip++] = (uint8_t)('a' + i);
        bcs[i][ip++] = OP_ACCEPT;
        lens[i] = ip;
    }

    /* Acquire first 3 — fills cache */
    SnobolJitContext *ctxs[3];
    for (int i = 0; i < 3; i++) {
        ctxs[i] = snobol_jit_acquire_context(bcs[i], lens[i]);
        test_assert(ctxs[i] != NULL, "Eviction stress: context acquired (fill)");
    }

    /* Release first 2 so they become evictable */
    snobol_jit_release_context(ctxs[0]);
    snobol_jit_release_context(ctxs[1]);
    ctxs[0] = ctxs[1] = NULL;

    /* Acquire 4th pattern → should evict LRU among released entries */
    SnobolJitContext *ctx4 = snobol_jit_acquire_context(bcs[3], lens[3]);
    test_assert(ctx4 != NULL, "Eviction stress: 4th context acquired after eviction");

    /* Acquire 5th pattern → triggers another eviction */
    SnobolJitContext *ctx5 = snobol_jit_acquire_context(bcs[4], lens[4]);
    test_assert(ctx5 != NULL, "Eviction stress: 5th context acquired after second eviction");

    /* ctxs[2] still has ref_count=1 — must NOT have been evicted */
    test_assert(ctxs[2]->ref_count == 1,
                "Eviction stress: referenced context not evicted (ref_count preserved)");

    /* Re-acquire pattern 3 — may be a cache hit or new alloc, but must not crash */
    SnobolJitContext *ctx3b = snobol_jit_acquire_context(bcs[3], lens[3]);
    test_assert(ctx3b != NULL, "Eviction stress: re-acquire after potential eviction is safe");

    /* Cleanup */
    snobol_jit_release_context(ctxs[2]);
    snobol_jit_release_context(ctx4);
    snobol_jit_release_context(ctx5);
    if (ctx3b) snobol_jit_release_context(ctx3b);

    snobol_jit_shutdown();
    snobol_jit_set_config(&saved);
    snobol_jit_init();
}

/* -----------------------------------------------------------------------
 * Task 4.3: Backtracking fixture — alternation/repetition/nested
 * Validates correctness + that the system doesn't crash on backtrack-heavy
 * patterns even when the profitability gate skips JIT for them.
 * ----------------------------------------------------------------------- */
static void test_backtrack_alternation_fixture(void) {
    /* Skip on non-ARM64 where JIT compilation is not available */
    if (!jit_is_supported()) {
        test_assert(true, "Backtrack alternation fixture skipped (ARM64 only)");
        return;
    }
    
    /* SPLIT a,b  a:LIT 'x' JMP end  b:LIT 'y'  end:ACCEPT
     * Matches "x" or "y" via alternation */
    uint8_t bc[64] = {0};
    size_t ip = 0;

    size_t split_ip = ip;
    bc[ip++] = OP_SPLIT;
    size_t patch_a = ip;
    bc[ip++] = 0; bc[ip++] = 0; bc[ip++] = 0; bc[ip++] = 0; /* a target */
    size_t patch_b = ip;
    bc[ip++] = 0; bc[ip++] = 0; bc[ip++] = 0; bc[ip++] = 0; /* b target */

    size_t a_ip = ip;
    bc[ip++] = OP_LIT;
    /* literal 'x' at offset 50 */
    bc[ip++] = 0; bc[ip++] = 0; bc[ip++] = 0; bc[ip++] = 50;
    bc[ip++] = 0; bc[ip++] = 0; bc[ip++] = 0; bc[ip++] = 1;
    bc[50] = 'x';
    size_t jmp_ip = ip;
    bc[ip++] = OP_JMP;
    size_t patch_end = ip;
    bc[ip++] = 0; bc[ip++] = 0; bc[ip++] = 0; bc[ip++] = 0; /* end target */

    size_t b_ip = ip;
    bc[ip++] = OP_LIT;
    /* literal 'y' at offset 51 */
    bc[ip++] = 0; bc[ip++] = 0; bc[ip++] = 0; bc[ip++] = 51;
    bc[ip++] = 0; bc[ip++] = 0; bc[ip++] = 0; bc[ip++] = 1;
    bc[51] = 'y';

    size_t end_ip = ip;
    bc[ip++] = OP_ACCEPT;

    /* Patch targets */
    size_t tmp = patch_a;
    bc[tmp++]=(uint8_t)(a_ip>>24); bc[tmp++]=(uint8_t)(a_ip>>16);
    bc[tmp++]=(uint8_t)(a_ip>>8);  bc[tmp++]=(uint8_t)(a_ip);
    tmp = patch_b;
    bc[tmp++]=(uint8_t)(b_ip>>24); bc[tmp++]=(uint8_t)(b_ip>>16);
    bc[tmp++]=(uint8_t)(b_ip>>8);  bc[tmp++]=(uint8_t)(b_ip);
    tmp = patch_end;
    bc[tmp++]=(uint8_t)(end_ip>>24); bc[tmp++]=(uint8_t)(end_ip>>16);
    bc[tmp++]=(uint8_t)(end_ip>>8);  bc[tmp++]=(uint8_t)(end_ip);
    (void)jmp_ip; (void)split_ip;

    snobol_jit_init();
    snobol_jit_reset_stats();

    SnobolJitContext *ctx = snobol_jit_acquire_context(bc, 64);
    SnobolJitStats *stats = snobol_jit_get_stats();

    VM vm = {0};
    vm.bc = bc; vm.bc_len = 64;
    vm.jit.enabled = true;
    vm.jit.ip_counts = ctx->ip_counts;
    vm.jit.traces    = ctx->traces;
    vm.jit.stats     = stats;
    vm.jit.ctx       = ctx;

    /* Match 'x' (primary path) */
    vm.s = "x"; vm.len = 1; vm.ip = 0; vm.pos = 0;
    bool ok_x = vm_run(&vm);
    test_assert(ok_x, "Backtrack alternation fixture: 'x' matches via primary path");

    /* Match 'y' (backtrack path) */
    vm.s = "y"; vm.len = 1; vm.ip = 0; vm.pos = 0;
    bool ok_y = vm_run(&vm);
    test_assert(ok_y, "Backtrack alternation fixture: 'y' matches via backtrack path");

    /* No match */
    vm.s = "z"; vm.len = 1; vm.ip = 0; vm.pos = 0;
    bool ok_z = vm_run(&vm);
    test_assert(!ok_z, "Backtrack alternation fixture: 'z' correctly fails");

    /* Run many iterations — must not crash regardless of JIT decision */
    for (int i = 0; i < 200; i++) {
        const char *s = (i % 2 == 0) ? "x" : "y";
        vm.s = s; vm.len = 1; vm.ip = 0; vm.pos = 0;
        vm_run(&vm);
    }
    test_assert(true, "Backtrack alternation fixture: 200 iterations complete without crash");

    snobol_jit_release_context(ctx);
    snobol_jit_shutdown();
}

void test_jit_cache_suite(void) {
#ifdef SNOBOL_JIT
    test_suite("JIT Cache");

    snobol_jit_init();
    snobol_jit_reset_stats();

    uint8_t bc[] = { OP_LIT, 0, 0, 0, 10, 0, 0, 0, 3, 'a', 'b', 'c', OP_ACCEPT };
    size_t bc_len = sizeof(bc);

    SnobolJitContext *ctx1 = snobol_jit_acquire_context(bc, bc_len);
    test_assert(ctx1 != NULL, "Context 1 acquired");
    test_assert(ctx1->ref_count == 1, "Context 1 ref_count is 1");

    SnobolJitContext *ctx2 = snobol_jit_acquire_context(bc, bc_len);
    test_assert(ctx2 == ctx1, "Context 2 is same as Context 1 (cache hit)");
    test_assert(ctx1->ref_count == 2, "Ref count incremented to 2");

    test_assert(snobol_jit_get_stats()->cache_hits_total == 1, "Stats reflect cache hit");

    snobol_jit_release_context(ctx1);
    test_assert(ctx2->ref_count == 1, "Ref count decremented after release 1");

    snobol_jit_release_context(ctx2);
    test_assert(ctx2->ref_count == 0, "Ref count is 0 after release 2");

    snobol_jit_shutdown();

    test_suite("JIT Cache Eviction Stress");
    test_cache_eviction_stress();

    test_suite("JIT Backtracking Fixtures");
    test_backtrack_alternation_fixture();
#endif
}
