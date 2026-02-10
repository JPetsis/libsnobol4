#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "../../snobol4-php/snobol_vm.h"
#include "../../snobol4-php/snobol_jit.h"

/* Forward declare test framework helpers */
void test_suite(const char *name);
void test_assert(bool condition, const char *message);

void test_jit_observability_suite(void) {
#ifdef SNOBOL_JIT
    test_suite("JIT Observability");

    snobol_jit_init();
    snobol_jit_reset_stats();
    SnobolJitStats *stats = snobol_jit_get_stats();
    
    test_assert(stats->entries_total == 0, "Initial entries_total is 0");
    test_assert(stats->compilations_total == 0, "Initial compilations_total is 0");

    // Simulate JIT increment
    stats->entries_total++;
    test_assert(snobol_jit_get_stats()->entries_total == 1, "entries_total increments");

    snobol_jit_reset_stats();
    test_assert(snobol_jit_get_stats()->entries_total == 0, "reset_stats works");
#endif
}