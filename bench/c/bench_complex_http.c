#include "bench_shared.h"
#include <string.h>
#include <stdio.h>

static const char *http_request =
    "GET /api/v2/users?page=1&limit=100 HTTP/1.1\r\n"
    "Host: example.com\r\n"
    "User-Agent: Mozilla/5.0\r\n"
    "Accept: application/json\r\n"
    "Authorization: Bearer xyz123abc\r\n"
    "\r\n";

void bench_complex_http_suite(bench_results_t *out) {
    size_t subj_len = strlen(http_request);

    /* snobol4: match HTTP request line with captures (method, path, version) */
    {
        snobol_context_t *ctx = snobol_context_create();
        char *err = NULL;
        /* SNOBOL: anchored match of method+path+version */
        snobol_pattern_t *pat = snobol_pattern_compile(ctx,
            "SPAN('A-Z') ' ' SPAN('/?&a-z0-9=.') ' ' 'HTTP/' SPAN('0-9.')",
            60, &err);
        if (!pat) { fprintf(stderr, "snobol compile failed: %s\n", err ? err : "??"); free(err); snobol_context_destroy(ctx); return; }
        free(err);

        int64_t start = bench_ns();
        for (int i = 0; i < BENCH_ITERATIONS; i++) {
            snobol_match_t *m = snobol_pattern_match(pat, http_request, subj_len);
            bool ok = snobol_match_success(m);
            (void)ok;
            snobol_match_free(m);
        }
        out->snobol_ns = bench_ns() - start;

        snobol_pattern_free(pat);
        snobol_context_destroy(ctx);
    }

    /* snobol4 search-mode: same pattern via snobol_pattern_search */
    {
        snobol_context_t *ctx = snobol_context_create();
        char *err = NULL;
        snobol_pattern_t *pat = snobol_pattern_compile(ctx,
            "SPAN('A-Z') ' ' SPAN('/?&a-z0-9=.') ' ' 'HTTP/' SPAN('0-9.')",
            60, &err);
        if (!pat) { fprintf(stderr, "snobol search compile failed: %s\n", err ? err : "??"); free(err); snobol_context_destroy(ctx); return; }
        free(err);

        int64_t start = bench_ns();
        for (int i = 0; i < BENCH_ITERATIONS; i++) {
            snobol_match_t *m = snobol_pattern_search(pat, http_request, subj_len);
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
        re = pcre2_compile((PCRE2_SPTR)"^[A-Z]+ [/?&a-z0-9=.]+ HTTP/[0-9.]+",
            PCRE2_ZERO_TERMINATED, 0, &errcode, &erroffset, NULL);
        if (!re) { fprintf(stderr, "pcre2 compile failed\n"); out->pcre2_ns = -1; return; }
        pcre2_match_data *md = pcre2_match_data_create_from_pattern(re, NULL);

        int64_t start = bench_ns();
        for (int i = 0; i < BENCH_ITERATIONS; i++) {
            int rc = pcre2_match(re, (PCRE2_SPTR)http_request, subj_len, 0, 0, md, NULL);
            (void)rc;
        }
        out->pcre2_ns = bench_ns() - start;

        pcre2_match_data_free(md);
        pcre2_code_free(re);
    }
#else
    out->pcre2_ns = -1;
#endif

    out->name = "complex_http";
    out->label = "HTTP request line (method+path+version)";
}
