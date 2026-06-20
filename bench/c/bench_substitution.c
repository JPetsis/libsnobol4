#include "bench_shared.h"
#include <string.h>
#include <stdio.h>

static const char *subject_text =
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. "
    "The quick brown fox jumps over the lazy dog. ";

void bench_substitution_suite(bench_results_t *out) {
    size_t subj_len = strlen(subject_text);

    /* snobol4: compile 'fox', match each occurrence (simulates search-replace) */
    {
        snobol_context_t *ctx = snobol_context_create();
        char *err = NULL;
        snobol_pattern_t *pat = snobol_pattern_compile(ctx, "'fox'", 5, &err);
        if (!pat) { fprintf(stderr, "snobol compile failed: %s\n", err ? err : "??"); free(err); snobol_context_destroy(ctx); return; }
        free(err);

        int64_t start = bench_ns();
        for (int i = 0; i < BENCH_ITERATIONS; i++) {
            size_t pos = 0;
            while (pos < subj_len) {
                snobol_match_t *m = snobol_pattern_match(pat, subject_text + pos, subj_len - pos);
                bool ok = snobol_match_success(m);
                if (ok) pos += 3; else break;
                snobol_match_free(m);
            }
        }
        out->snobol_ns = bench_ns() - start;

        snobol_pattern_free(pat);
        snobol_context_destroy(ctx);
    }

    /* snobol4 search-mode (JIT): search for 'fox' at pos 0 each time */
    {
        snobol_context_t *ctx = snobol_context_create();
        char *err = NULL;
        snobol_pattern_t *pat = snobol_pattern_compile(ctx, "'fox'", 5, &err);
        if (!pat) { fprintf(stderr, "snobol search compile failed: %s\n", err ? err : "??"); free(err); snobol_context_destroy(ctx); return; }
        free(err);

        int64_t start = bench_ns();
        for (int i = 0; i < BENCH_ITERATIONS; i++) {
            snobol_match_t *m = snobol_pattern_search(pat, subject_text, subj_len);
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
        re = pcre2_compile((PCRE2_SPTR)"fox", PCRE2_ZERO_TERMINATED, 0, &errcode, &erroffset, NULL);
        if (!re) { fprintf(stderr, "pcre2 compile failed\n"); out->pcre2_ns = -1; return; }
        pcre2_match_data *md = pcre2_match_data_create_from_pattern(re, NULL);

        int64_t start = bench_ns();
        for (int i = 0; i < BENCH_ITERATIONS; i++) {
            size_t pos = 0;
            while (pos < subj_len) {
                int rc = pcre2_match(re, (PCRE2_SPTR)subject_text, subj_len, pos, 0, md, NULL);
                if (rc >= 0) pos += 3; else break;
            }
        }
        out->pcre2_ns = bench_ns() - start;

        pcre2_match_data_free(md);
        pcre2_code_free(re);
    }
#else
    out->pcre2_ns = -1;
#endif

    out->name = "substitution";
    out->label = "Search+Replace scan (fox)";
}
