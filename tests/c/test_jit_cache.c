#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "../../snobol4-php/snobol_vm.h"
#include "../../snobol4-php/snobol_jit.h"

void test_suite(const char *name);
void test_assert(bool condition, const char *message);

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
    // ctx2 is still in global cache so it's not freed yet, but ref_count is 0
    test_assert(ctx2->ref_count == 0, "Ref count is 0 after release 2");

    snobol_jit_shutdown();
#endif
}
