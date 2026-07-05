#include "bench_shared.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SUBJ_LEN 1024

void bench_delimiter_suite(bench_results_t *out) {
    char subject[SUBJ_LEN + 1];
    memset(subject, ',', SUBJ_LEN);
    subject[SUBJ_LEN] = '\0';

    int64_t snobol_ns = 0, pcre2_ns = 0;

    /* snobol4: SPAN(',') anchored match on all-comma subject */
    {
        snobol_context_t *ctx = snobol_context_create();
        char *err = NULL;
        snobol_pattern_t *pat =
            snobol_pattern_compile(ctx, "SPAN(',')", 9, &err);
        if (!pat) {
            fprintf(stderr, "compile SPAN failed: %s\n", err ? err : "??");
            free(err);
            snobol_context_destroy(ctx);
            out->label = "Delimiter (compile FAILED)";
            return;
        }
        free(err);

        int64_t start = bench_ns();
        for (int i = 0; i < BENCH_ITERATIONS; i++) {
            snobol_match_t *m = snobol_pattern_match(pat, subject, SUBJ_LEN);
            snobol_match_free(m);
        }
        snobol_ns = bench_ns() - start;

        snobol_pattern_free(pat);
        snobol_context_destroy(ctx);
    }

    out->snobol_ns = snobol_ns;
    out->search_ns = -1;

    /* pcre2: same workload for comparison */
#ifdef HAVE_PCRE2
    {
        int errcode;
        PCRE2_SIZE erroffset;
        pcre2_code *re =
            pcre2_compile((PCRE2_SPTR)",+", 2, 0, &errcode, &erroffset, NULL);
        if (!re) {
            out->pcre2_ns = -1;
        } else {
            pcre2_match_data *md =
                pcre2_match_data_create_from_pattern(re, NULL);
            int64_t start = bench_ns();
            for (int i = 0; i < BENCH_ITERATIONS; i++) {
                pcre2_match(re, (PCRE2_SPTR)subject, SUBJ_LEN, 0, 0, md, NULL);
            }
            pcre2_ns = bench_ns() - start;
            pcre2_match_data_free(md);
            pcre2_code_free(re);
        }
    }
#else
    out->pcre2_ns = -1;
#endif

    out->name = "delimiter";
    out->label = "Delimiter (SPAN commas, 1KB)";
}
