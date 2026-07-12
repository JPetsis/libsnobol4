#include "bench_shared.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Forward declarations */
BENCH_SUITE(literal);
BENCH_SUITE(alternation);
BENCH_SUITE(alt_flat);
BENCH_SUITE(tokenization);
BENCH_SUITE(substitution);
BENCH_SUITE(complex_http);
BENCH_SUITE(delimiter);

static bench_results_t results[16];
static int result_count = 0;

static void run_one(void (*suite)(bench_results_t *)) {
    if (result_count >= 16) return;
    bench_results_t *r = &results[result_count++];
    memset(r, 0, sizeof(*r));
    suite(r);
}

int main(void) {
    int maj, min, pat;
    snobol_version(&maj, &min, &pat);

    printf("libsnobol4 C Microbenchmarks\n");
    printf("============================\n");
    printf("Version: %d.%d.%d\n", maj, min, pat);
#ifdef SNOBOL_JIT
    printf("JIT:     enabled (search-mode)\n");
#endif
#ifdef HAVE_PCRE2
    printf("PCRE2:   available\n");
#else
    printf("PCRE2:   NOT available (install libpcre2-dev)\n");
#endif
    printf("Iterations per scenario: %d\n\n", BENCH_ITERATIONS);

    run_one(bench_literal_suite);
    run_one(bench_alternation_suite);
    run_one(bench_alt_flat_suite);
    run_one(bench_tokenization_suite);
    run_one(bench_substitution_suite);
    run_one(bench_complex_http_suite);
    run_one(bench_delimiter_suite);

    /* Header */
    printf("%-30s %16s %16s %10s %16s %10s %16s %10s\n",
           "Scenario", "snobol4 (ns)", "literal (ns)", "l/s4",
           "search (ns)", "s/s4",
           "pcre2 (ns)", "ratio");
    printf("%-30s %16s %16s %10s %16s %10s %16s %10s\n",
           "-------", "------------", "-----------", "-----",
           "-----------", "-----",
           "-----------", "-----");

    for (int i = 0; i < result_count; i++) {
        bench_results_t *r = &results[i];
        double snobol_us = (double)r->snobol_ns / 1000.0;
        double literal_us = r->literal_ns > 0 ? (double)r->literal_ns / 1000.0 : 0.0;
        double search_us = r->search_ns > 0 ? (double)r->search_ns / 1000.0 : 0.0;
        double ratio = 0.0;
        char ratio_str[32] = "N/A";
        char lit_ratio_str[32] = "N/A";
        char search_ratio_str[32] = "N/A";

        /* snobol4 match vs literal API */
        if (r->literal_ns > 0) {
            double lr = snobol_us / literal_us;
            snprintf(lit_ratio_str, sizeof(lit_ratio_str), "%.2f", lr);
        }

        /* snobol4 match time vs search time */
        if (r->search_ns > 0) {
            double sr = snobol_us / search_us;
            snprintf(search_ratio_str, sizeof(search_ratio_str), "%.2f", sr);
        }

        /* snobol4 vs pcre2 */
        if (r->pcre2_ns > 0) {
            double pcre2_us = (double)r->pcre2_ns / 1000.0;
            ratio = snobol_us / pcre2_us;
            snprintf(ratio_str, sizeof(ratio_str), "%.2f", ratio);
        }

        printf("%-30s %8.0f us (%lld) %8.0f us (%lld) %10s %8.0f us (%lld) %10s %8.0f us (%lld) %10s\n",
               r->label,
               snobol_us, (long long)r->snobol_ns,
               literal_us, (long long)r->literal_ns,
               lit_ratio_str,
               search_us, (long long)r->search_ns,
               search_ratio_str,
               (r->pcre2_ns > 0 ? (double)r->pcre2_ns / 1000.0 : 0.0),
               (long long)r->pcre2_ns,
               ratio_str);

        /* ops/sec for each engine */
        double s4_ops = (double)BENCH_ITERATIONS / (snobol_us / 1e6);
        printf("  %30s ops/sec (snobol4):  %.0f\n", "", s4_ops);

        if (r->literal_ns > 0) {
            double literal_ops = (double)BENCH_ITERATIONS / (literal_us / 1e6);
            printf("  %30s ops/sec (literal):  %.0f\n", "", literal_ops);
        }

        if (r->search_ns > 0) {
            double search_ops = (double)BENCH_ITERATIONS / (search_us / 1e6);
            printf("  %30s ops/sec (search):   %.0f\n", "", search_ops);
        }

        if (r->pcre2_ns > 0) {
            double pcre2_ops = (double)BENCH_ITERATIONS / ((double)r->pcre2_ns / 1e9);
            printf("  %30s ops/sec (pcre2):    %.0f\n", "", pcre2_ops);
        }
        printf("\n");
    }

    printf("---\n");
    printf("Ratio = snobol4 time / pcre2 time.  <1.0 = snobol4 faster.\n");
    printf("s/s4  = snobol4 match time / search-mode time.  >1.0 = search faster.\n");
    printf("l/s4  = snobol4 match time / literal API time.  >1.0 = literal API faster.\n");
    return 0;
}
