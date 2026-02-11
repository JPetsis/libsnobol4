#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <setjmp.h>

/* Test framework */
typedef struct {
    int passed;
    int failed;
    const char *current_suite;
} TestContext;

static TestContext test_ctx = {0};
static jmp_buf test_jump;

static void signal_handler(int sig) {
    printf("\n  ✗ CRASHED with signal %d (%s) in suite: %s\n", 
           sig, (sig == SIGILL ? "SIGILL" : (sig == SIGSEGV ? "SIGSEGV" : "SIGBUS")),
           test_ctx.current_suite);
    test_ctx.failed++;
    longjmp(test_jump, 1);
}

void test_suite(const char *name) {
    test_ctx.current_suite = name;
    printf("\n=== %s ===\n", name);
}

void test_assert(bool condition, const char *message) {
    if (condition) {
        test_ctx.passed++;
        printf("  ✓ %s\n", message);
    } else {
        test_ctx.failed++;
        printf("  ✗ %s\n", message);
    }
}

/* Forward declarations for test suites */
void test_vm_suite(void);
void test_backtracking_suite(void);
void test_catastrophic_suite(void);
void test_jit_observability_suite(void);
void test_jit_cache_suite(void);
void test_jit_branches_suite(void);
int test_stress_backtracking_main(void);

int main(void) {
    printf("SNOBOL4 C Core Test Runner\n");
    printf("===========================\n");

    signal(SIGILL, signal_handler);
    signal(SIGSEGV, signal_handler);
    signal(SIGBUS, signal_handler);

    if (setjmp(test_jump) == 0) {
        test_vm_suite();
    }
    if (setjmp(test_jump) == 0) {
        test_backtracking_suite();
    }
    if (setjmp(test_jump) == 0) {
        test_catastrophic_suite();
    }
    if (setjmp(test_jump) == 0) {
        test_jit_observability_suite();
    }
    if (setjmp(test_jump) == 0) {
        test_jit_cache_suite();
    }
    if (setjmp(test_jump) == 0) {
        test_jit_branches_suite();
    }

    printf("\n=== Stress Tests ===\n");
    if (setjmp(test_jump) == 0) {
        int rc = test_stress_backtracking_main();
        if (rc == 0) {
            test_ctx.passed++;
        } else {
            test_ctx.failed++;
        }
    }

    /* Summary */
    printf("\n===========================\n");
    printf("Results: %d passed, %d failed\n", test_ctx.passed, test_ctx.failed);

    return test_ctx.failed > 0 ? 1 : 0;
}

// NOTE: In DEBUG_BACKTRACK mode we intentionally allow stderr output so we can
// see VM state dumps when diagnosing backtracking.
