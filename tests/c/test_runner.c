#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <setjmp.h>
#include <time.h>

/* ---------------------------------------------------------------------------
 * Portable monotonic clock
 *
 * POSIX clock_gettime(CLOCK_MONOTONIC) is not available on Windows/MSVC.
 * Use QueryPerformanceCounter there; fall back to clock_gettime everywhere else.
 * --------------------------------------------------------------------------- */
#ifdef _WIN32
#  include <windows.h>
static void snobol_clock_gettime(struct timespec *ts) {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    ts->tv_sec  = (time_t)(counter.QuadPart / freq.QuadPart);
    ts->tv_nsec = (long)((counter.QuadPart % freq.QuadPart) * 1000000000LL
                         / freq.QuadPart);
}
#  define SNOBOL_CLOCK_GETTIME(ts)  snobol_clock_gettime(ts)
#else
#  define SNOBOL_CLOCK_GETTIME(ts)  clock_gettime(CLOCK_MONOTONIC, ts)
#endif

/* ── Test framework ──────────────────────────────────────────────────────── */

typedef struct {
    int passed;
    int failed;
    const char *current_suite;
} TestContext;

#define MAX_SUITES   64
#define NAME_WIDTH   36   /* fixed width for suite name column          */
#define COL_SEP      "  " /* two-space column separator                  */
#define RULE_WIDTH   80   /* total width of separator lines              */

typedef struct {
    const char *name;
    int         passed;
    int         failed;
    double      time_ms;
} SuiteResult;

static TestContext test_ctx      = {0};
static jmp_buf     test_jump;
static SuiteResult suite_results[MAX_SUITES];
static int         suite_count   = 0;

/* ── Internal helpers ────────────────────────────────────────────────────── */

static double elapsed_ms(struct timespec t0, struct timespec t1) {
    return (t1.tv_sec  - t0.tv_sec)  * 1000.0
         + (t1.tv_nsec - t0.tv_nsec) / 1.0e6;
}

static void print_rule(char ch) {
    for (int i = 0; i < RULE_WIDTH; i++) putchar(ch);
    putchar('\n');
}

static void signal_handler(int sig) {
    printf("\n  [CRASH] signal %d (%s) in suite: %s\n",
           sig,
           (sig == SIGILL  ? "SIGILL"  :
            sig == SIGSEGV ? "SIGSEGV" : "UNKNOWN"),
           test_ctx.current_suite);
    test_ctx.failed++;
    longjmp(test_jump, 1);
}

/* ── Public API (called by individual test files) ────────────────────────── */

/* Called multiple times within a suite to label sub-groups */
void test_suite(const char *name) {
    test_ctx.current_suite = name;
    printf("  -- %s\n", name);
}

void test_assert(bool condition, const char *message) {
    if (condition) {
        test_ctx.passed++;
        printf("     ✓  %s\n", message);
    } else {
        test_ctx.failed++;
        printf("     ✗  %s\n", message);
    }
    fflush(stdout);
}

/* ── RUN_SUITE macro ─────────────────────────────────────────────────────── */
/*
 * Usage: RUN_SUITE("Human Name", function_name);
 *
 * Prints:
 *   ▸ Human Name
 *     ·· sub-section (from test_suite() calls inside fn)
 *        ✓  assertion
 *   ↳ N passed · M failed · X.XX ms
 */
#define RUN_SUITE(display_name, fn) do {                                \
    int _p0 = test_ctx.passed, _f0 = test_ctx.failed;                  \
    struct timespec _t0, _t1;                                           \
    printf("\n▸ %s\n", (display_name));                                 \
    SNOBOL_CLOCK_GETTIME(&_t0);                                         \
    if (setjmp(test_jump) == 0) { fn(); }                               \
    SNOBOL_CLOCK_GETTIME(&_t1);                                         \
    int    _sp = test_ctx.passed - _p0;                                 \
    int    _sf = test_ctx.failed  - _f0;                                \
    double _ms = elapsed_ms(_t0, _t1);                                  \
    if (_sf > 0)                                                            \
        printf("  ↳  %d passed  |  \033[31m%d failed\033[0m  |  %.2f ms\n", \
               _sp, _sf, _ms);                                              \
    else                                                                    \
        printf("  ↳  %d passed  |  0 failed  |  %.2f ms\n", _sp, _ms);     \
    if (suite_count < MAX_SUITES)                                       \
        suite_results[suite_count++] =                                  \
            (SuiteResult){ (display_name), _sp, _sf, _ms };             \
} while(0)

/* ── Forward declarations ────────────────────────────────────────────────── */
void test_vm_suite(void);
void test_table_suite(void);
void test_dynamic_pattern_suite(void);
void test_table_ops_suite(void);
void test_template_ops_suite(void);
void test_control_flow_suite(void);
void test_backtracking_suite(void);
void test_catastrophic_suite(void);
void test_jit_observability_suite(void);
void test_jit_cache_suite(void);
void test_jit_branches_suite(void);
void test_jit_profitability_suite(void);
void test_jit_cfg_suite(void);
void test_lexer_suite(void);
void test_parser_suite(void);
void test_ast_suite(void);
int  test_stress_backtracking_main(void);
/* Built-in function test suites */
void test_pattern_breakx_suite(void);
void test_pattern_bal_suite(void);
void test_pattern_fence_suite(void);
void test_string_size_suite(void);
void test_string_transform_suite(void);
void test_string_ops_suite(void);
void test_string_char_suite(void);
void test_string_case_suite(void);
void test_comparison_suite(void);
void test_builtin_dispatch_suite(void);
void test_search_runtime_suite(void);
void test_search_jit_suite(void);
void test_fusion_suite(void);
void test_compact_choice_suite(void);

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void) {
    print_rule('=');
    printf("  SNOBOL4 C Core Test Suite\n");
    print_rule('=');

    signal(SIGILL,  signal_handler);
    signal(SIGSEGV, signal_handler);
#ifdef SIGBUS
    signal(SIGBUS,  signal_handler);
#endif

    /* Core VM / data structures */
    RUN_SUITE("VM",                         test_vm_suite);
    RUN_SUITE("Tables",                     test_table_suite);
    RUN_SUITE("Dynamic Patterns",           test_dynamic_pattern_suite);
    RUN_SUITE("Table Ops",                  test_table_ops_suite);
    RUN_SUITE("Template Ops",               test_template_ops_suite);

    /* Pattern matching engine */
    RUN_SUITE("Control Flow",               test_control_flow_suite);
    RUN_SUITE("Backtracking",               test_backtracking_suite);
    RUN_SUITE("Catastrophic Backtracking",  test_catastrophic_suite);

    /* JIT */
    RUN_SUITE("JIT: Observability",         test_jit_observability_suite);
    RUN_SUITE("JIT: Cache",                 test_jit_cache_suite);
    RUN_SUITE("JIT: Branches",              test_jit_branches_suite);
    RUN_SUITE("JIT: Profitability",         test_jit_profitability_suite);
    RUN_SUITE("JIT: CFG Multi-Block",       test_jit_cfg_suite);

    /* Lexer / Parser / AST */
    RUN_SUITE("Lexer",                      test_lexer_suite);
    RUN_SUITE("Parser",                     test_parser_suite);
    RUN_SUITE("AST",                        test_ast_suite);

    /* Built-in functions */
    RUN_SUITE("Pattern: BREAKX",            test_pattern_breakx_suite);
    RUN_SUITE("Pattern: BAL",               test_pattern_bal_suite);
    RUN_SUITE("Pattern: FENCE/REM/RPOS/RTAB", test_pattern_fence_suite);
    RUN_SUITE("String: SIZE",               test_string_size_suite);
    RUN_SUITE("String: TRIM/DUPL/REVERSE",  test_string_transform_suite);
    RUN_SUITE("String: SUBSTR/REPLACE/PAD", test_string_ops_suite);
    RUN_SUITE("String: CHAR/ORD",           test_string_char_suite);
    RUN_SUITE("String: UPPER/LOWER",        test_string_case_suite);
    RUN_SUITE("Comparison predicates",      test_comparison_suite);
    RUN_SUITE("Built-in dispatch (OP_EVAL)", test_builtin_dispatch_suite);

    /* Search runtime */
    RUN_SUITE("Search Runtime",             test_search_runtime_suite);
    RUN_SUITE("Search JIT",                 test_search_jit_suite);
    RUN_SUITE("Fusion: SPLIT/ANY",          test_fusion_suite);
    RUN_SUITE("Compact Choice Stack",       test_compact_choice_suite);

    /* Stress test */
    {
        int _p0 = test_ctx.passed, _f0 = test_ctx.failed;
        struct timespec _t0, _t1;
        printf("\n▸ Stress: Backtracking\n");
        SNOBOL_CLOCK_GETTIME(&_t0);
        int rc = 0;
        if (setjmp(test_jump) == 0) { rc = test_stress_backtracking_main(); }
        SNOBOL_CLOCK_GETTIME(&_t1);
        if (rc == 0) { test_ctx.passed++; } else { test_ctx.failed++; }
        int    _sp = test_ctx.passed - _p0;
        int    _sf = test_ctx.failed  - _f0;
        double _ms = elapsed_ms(_t0, _t1);
        if (_sf > 0)
            printf("  ↳  %d passed  |  \033[31m%d failed\033[0m  |  %.2f ms\n",
                   _sp, _sf, _ms);
        else
            printf("  ↳  %d passed  |  0 failed  |  %.2f ms\n", _sp, _ms);
        if (suite_count < MAX_SUITES)
            suite_results[suite_count++] =
                (SuiteResult){ "Stress: Backtracking", _sp, _sf, _ms };
    }

    /* ── Summary table ───────────────────────────────────────────────────── */
    printf("\n");
    print_rule('=');
    printf("  %-*s  %7s  %7s  %9s\n",
           NAME_WIDTH, "Suite", "Passed", "Failed", "Time(ms)");
    print_rule('-');

    double total_ms = 0.0;
    for (int i = 0; i < suite_count; i++) {
        SuiteResult *r = &suite_results[i];
        if (r->failed > 0)
            printf("  \033[31m✗\033[0m %-*s  %7d  \033[31m%7d\033[0m  %9.2f\n",
                   NAME_WIDTH - 2, r->name, r->passed, r->failed, r->time_ms);
        else
            printf("  ✓ %-*s  %7d  %7d  %9.2f\n",
                   NAME_WIDTH - 2, r->name, r->passed, r->failed, r->time_ms);
        total_ms += r->time_ms;
    }

    print_rule('-');
    printf("  %-*s  %7d  %7d  %9.2f\n",
           NAME_WIDTH, "TOTAL", test_ctx.passed, test_ctx.failed, total_ms);
    print_rule('=');

    if (test_ctx.failed == 0)
        printf("  \033[32m✓  All %d tests passed\033[0m\n", test_ctx.passed);
    else
        printf("  \033[31m✗  %d of %d tests FAILED\033[0m\n",
               test_ctx.failed, test_ctx.passed + test_ctx.failed);

    print_rule('=');
    return test_ctx.failed > 0 ? 1 : 0;
}

// NOTE: In DEBUG_BACKTRACK mode we intentionally allow stderr output so we can
// see VM state dumps when diagnosing backtracking.
