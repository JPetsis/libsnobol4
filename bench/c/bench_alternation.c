#include "bench_shared.h"
#include <string.h>
#include <stdio.h>

static const char *subject_csv =
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status";

/* SPAN(',') matches consecutive comma delimiters in the CSV.
 * This exercises the same O(n) charclass scanning code path as BREAK. */
void bench_alternation_suite(bench_results_t *out) {
    size_t subj_len = strlen(subject_csv);

    /* snobol4: SPAN(',') — comma delimiter scanning */
    {
        snobol_context_t *ctx = snobol_context_create();
        char *err = NULL;
        snobol_pattern_t *pat = snobol_pattern_compile(ctx, "SPAN(',')", 9, &err);
        if (!pat) { fprintf(stderr, "snobol compile failed: %s\n", err ? err : "??"); free(err); snobol_context_destroy(ctx); return; }
        free(err);

        int64_t start = bench_ns();
        for (int i = 0; i < BENCH_ITERATIONS; i++) {
            snobol_match_t *m = snobol_pattern_match(pat, subject_csv, subj_len);
            snobol_match_free(m);
        }
        out->snobol_ns = bench_ns() - start;

        snobol_pattern_free(pat);
        snobol_context_destroy(ctx);
    }

#ifdef HAVE_PCRE2
    {
        pcre2_code *re;
        int errcode;
        PCRE2_SIZE erroffset;
        re = pcre2_compile((PCRE2_SPTR)"[^,]*", PCRE2_ZERO_TERMINATED, 0, &errcode, &erroffset, NULL);
        if (!re) { fprintf(stderr, "pcre2 compile failed\n"); out->pcre2_ns = -1; return; }
        pcre2_match_data *md = pcre2_match_data_create_from_pattern(re, NULL);

        int64_t start = bench_ns();
        for (int i = 0; i < BENCH_ITERATIONS; i++) {
            int rc = pcre2_match(re, (PCRE2_SPTR)subject_csv, subj_len, 0, 0, md, NULL);
            (void)rc;
        }
        out->pcre2_ns = bench_ns() - start;

        pcre2_match_data_free(md);
        pcre2_code_free(re);
    }
#else
    out->pcre2_ns = -1;
#endif

    out->label = "SPAN delimiter skip (CSV comma)";
}
