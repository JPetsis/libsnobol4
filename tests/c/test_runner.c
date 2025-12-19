/*
 * test_runner.c - Minimal test harness for SNOBOL4 C core
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Test framework */
typedef struct {
    int passed;
    int failed;
    const char *current_suite;
} TestContext;

static TestContext test_ctx = {0};

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

int main(void) {
    printf("SNOBOL4 C Core Test Runner\n");
    printf("===========================\n");

    /* Run test suites */
    test_vm_suite();

    /* Summary */
    printf("\n===========================\n");
    printf("Results: %d passed, %d failed\n", test_ctx.passed, test_ctx.failed);

    return test_ctx.failed > 0 ? 1 : 0;
}

