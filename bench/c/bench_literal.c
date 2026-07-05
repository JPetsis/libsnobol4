#include "bench_shared.h"
#include <string.h>
#include <stdio.h>

static const char *subject_1k =
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";

#define SUBJ_LEN 1040

void bench_literal_suite(bench_results_t *out) {
    size_t subj_len = strlen(subject_1k);
    int64_t snobol_ns = 0, pcre2_ns = 0;

    /* snobol4: compile once, match 100K times */
    {
        snobol_context_t *ctx = snobol_context_create();
        char *err = NULL;
        snobol_pattern_t *pat = snobol_pattern_compile(ctx, "'pqr'", 5, &err);
        if (!pat) { fprintf(stderr, "snobol compile failed: %s\n", err ? err : "??"); free(err); snobol_context_destroy(ctx); return; }
        free(err);

        int64_t start = bench_ns();
        for (int i = 0; i < BENCH_ITERATIONS; i++) {
            snobol_match_t *m = snobol_pattern_match(pat, subject_1k, subj_len);
            bool ok = snobol_match_success(m);
            (void)ok;
            snobol_match_free(m);
        }
        snobol_ns = bench_ns() - start;

        snobol_pattern_free(pat);
        snobol_context_destroy(ctx);
    }

    out->snobol_ns = snobol_ns;

    /* snobol4 literal-match API: snobol_pattern_match_literal()
     * zero-allocation anchored literal match, bypasses VM setup entirely. */
    {
        snobol_context_t *ctx = snobol_context_create();
        char *err = NULL;
        snobol_pattern_t *pat = snobol_pattern_compile(ctx, "'pqr'", 5, &err);
        if (!pat) { fprintf(stderr, "snobol lit compile failed: %s\n", err ? err : "??"); free(err); snobol_context_destroy(ctx); out->literal_ns = -1; }
        else {
            free(err);
            int64_t start = bench_ns();
            for (int i = 0; i < BENCH_ITERATIONS; i++) {
                snobol_literal_match_t lm = snobol_pattern_match_literal(pat, subject_1k, subj_len);
                (void)lm;
            }
            out->literal_ns = bench_ns() - start;
            snobol_pattern_free(pat);
            snobol_context_destroy(ctx);
        }
    }

    /* snobol4 search-mode (JIT): compile once, search 100K times */
    {
        snobol_context_t *ctx = snobol_context_create();
        char *err = NULL;
        snobol_pattern_t *pat = snobol_pattern_compile(ctx, "'pqr'", 5, &err);
        if (!pat) { fprintf(stderr, "snobol search compile failed: %s\n", err ? err : "??"); free(err); snobol_context_destroy(ctx); return; }
        free(err);

        int64_t start = bench_ns();
        for (int i = 0; i < BENCH_ITERATIONS; i++) {
            snobol_match_t *m = snobol_pattern_search(pat, subject_1k, subj_len);
            bool ok = snobol_match_success(m);
            (void)ok;
            snobol_match_free(m);
        }
        out->search_ns = bench_ns() - start;

        snobol_pattern_free(pat);
        snobol_context_destroy(ctx);
    }

#ifdef HAVE_PCRE2
    {
        pcre2_code *re;
        int errcode;
        PCRE2_SIZE erroffset;
        re = pcre2_compile((PCRE2_SPTR)"pqr", PCRE2_ZERO_TERMINATED, 0, &errcode, &erroffset, NULL);
        if (!re) { fprintf(stderr, "pcre2 compile failed\n"); out->pcre2_ns = -1; return; }
        pcre2_match_data *md = pcre2_match_data_create_from_pattern(re, NULL);

        int64_t start = bench_ns();
        for (int i = 0; i < BENCH_ITERATIONS; i++) {
            int rc = pcre2_match(re, (PCRE2_SPTR)subject_1k, subj_len, 0, 0, md, NULL);
            (void)rc;
        }
        pcre2_ns = bench_ns() - start;

        pcre2_match_data_free(md);
        pcre2_code_free(re);
    }

    out->pcre2_ns = pcre2_ns;
#else
    out->pcre2_ns = -1;
#endif

    out->name = "literal";
    out->label = "Literal substring (pqr)";
}
