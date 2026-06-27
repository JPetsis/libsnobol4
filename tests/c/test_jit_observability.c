#include "../../core/include/snobol/jit.h"
#include "../../core/include/snobol/vm.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Forward declare test framework helpers */
void test_suite(const char *name);
void test_assert(bool condition, const char *message);

void test_jit_observability_suite(void) {
#ifdef SNOBOL_JIT
  test_suite("JIT Observability");

  snobol_jit_init();
  snobol_jit_reset_stats();
  SnobolJitStats *stats = snobol_jit_get_stats();

  test_assert(stats->method_attempts_total == 0,
              "Initial method_attempts_total is 0");
  test_assert(stats->method_successes_total == 0,
              "Initial method_successes_total is 0");

  // Simulate JIT increment
  stats->method_attempts_total++;
  test_assert(snobol_jit_get_stats()->method_attempts_total == 1,
              "method_attempts_total increments");

  snobol_jit_reset_stats();
  test_assert(snobol_jit_get_stats()->method_attempts_total == 0,
              "reset_stats works");

  snobol_jit_shutdown();
#endif
}
