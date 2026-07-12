#include "bench_shared.h"
#include <string.h>
#include <stdio.h>

/* Build a ~48KB subject by repeating a template that embeds the alternation
 * literals, then ensure it starts with one literal so an anchored match
 * succeeds. Returns a malloc'd buffer the caller must free. */
static char *build_subject(const char *prefix, const char *tmpl, size_t *out_len) {
    size_t tmpl_len = strlen(tmpl);
    size_t cap = 48 * 1024;
    char *buf = (char *)malloc(cap + 1);
    if (!buf) return NULL;
    size_t off = 0;
    /* Force an anchored match at position 0. */
    size_t plen = strlen(prefix);
    memcpy(buf, prefix, plen);
    off += plen;
    while (off + tmpl_len < cap) {
        memcpy(buf + off, tmpl, tmpl_len);
        off += tmpl_len;
    }
    buf[off] = '\0';
    *out_len = off;
    return buf;
}

/* Benchmark one alternation-literal pattern: snobol4 anchored match,
 * snobol4 search-mode, and PCRE2 unanchored match. */
static void bench_alt(const char *snobol_pat, size_t snobol_pat_len,
                      const char *pcre_pat, const char *prefix, const char *tmpl,
                      bench_results_t *out, const char *label) {
    size_t subj_len = 0;
    char *subject = build_subject(prefix, tmpl, &subj_len);
    if (!subject) { out->label = label; out->snobol_ns = -1; return; }

    /* snobol4 anchored match (VM path) */
    {
        snobol_context_t *ctx = snobol_context_create();
        char *err = NULL;
        snobol_pattern_t *pat = snobol_pattern_compile(ctx, snobol_pat, snobol_pat_len, &err);
        if (!pat) { fprintf(stderr, "snobol compile failed: %s\n", err ? err : "??"); free(err); snobol_context_destroy(ctx); free(subject); out->label = label; out->snobol_ns = -1; return; }
        free(err);

        int64_t start = bench_ns();
        for (int i = 0; i < BENCH_ITERATIONS; i++) {
            snobol_match_t *m = snobol_pattern_match(pat, subject, subj_len);
            snobol_match_free(m);
        }
        out->snobol_ns = bench_ns() - start;

        snobol_pattern_free(pat);
        snobol_context_destroy(ctx);
    }

    /* snobol4 search-mode (find first occurrence anywhere) */
    {
        snobol_context_t *ctx = snobol_context_create();
        char *err = NULL;
        snobol_pattern_t *pat = snobol_pattern_compile(ctx, snobol_pat, snobol_pat_len, &err);
        if (!pat) { fprintf(stderr, "snobol search compile failed: %s\n", err ? err : "??"); free(err); snobol_context_destroy(ctx); free(subject); out->label = label; out->snobol_ns = -1; return; }
        free(err);

        int64_t start = bench_ns();
        for (int i = 0; i < BENCH_ITERATIONS; i++) {
            snobol_match_t *m = snobol_pattern_search(pat, subject, subj_len);
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
        re = pcre2_compile((PCRE2_SPTR)pcre_pat, PCRE2_ZERO_TERMINATED, 0, &errcode, &erroffset, NULL);
        if (!re) { fprintf(stderr, "pcre2 compile failed\n"); out->pcre2_ns = -1; free(subject); out->label = label; return; }
        pcre2_match_data *md = pcre2_match_data_create_from_pattern(re, NULL);

        int64_t start = bench_ns();
        for (int i = 0; i < BENCH_ITERATIONS; i++) {
            int rc = pcre2_match(re, (PCRE2_SPTR)subject, subj_len, 0, 0, md, NULL);
            (void)rc;
        }
        out->pcre2_ns = bench_ns() - start;

        pcre2_match_data_free(md);
        pcre2_code_free(re);
    }
#else
    out->pcre2_ns = -1;
#endif

    out->literal_ns = -1;
    out->label = label;
    free(subject);
}

/* Bushy alternation: 8 literals sharing a 2-char prefix ("ca").
 * Routes to TIER_ALT_LIT (tier 5); benefits from trie cache (Priority 2),
 * start-bitmap + BMH skip (Priority 1) and the cost model (Priority 4). */
void bench_alternation_suite(bench_results_t *out) {
    static const char *snobol_pat = "'cat'|'car'|'cab'|'cap'|'can'|'cam'|'cad'|'caf'";
    static const char *pcre_pat = "cat|car|cab|cap|can|cam|cad|caf";
    static const char *tmpl = "catalog carpet cabin captain candy camera cadet cafe dog bird tree river moon star ";
    bench_alt(snobol_pat, strlen(snobol_pat), pcre_pat, "cat ", tmpl, out,
              "Alt-literals bushy (tier 5)");
}

/* Flat alternation: 8 literals with no shared prefix.
 * Routes to TIER_GENERAL (tier 8); Priority 1's flat-trie fallback fixes the
 * 125x regression that previously mis-dispatched it into the tier-5 loop. */
void bench_alt_flat_suite(bench_results_t *out) {
    static const char *snobol_pat = "'apple'|'orange'|'banana'|'grape'|'melon'|'cherry'|'peach'|'lemon'";
    static const char *pcre_pat = "apple|orange|banana|grape|melon|cherry|peach|lemon";
    static const char *tmpl = "apple orange banana grape melon cherry peach lemon wolf fox bear deer wind rain snow ";
    bench_alt(snobol_pat, strlen(snobol_pat), pcre_pat, "apple ", tmpl, out,
              "Alt-literals flat (tier 8)");
}
