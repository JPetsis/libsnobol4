/**
 * @file bench_delimiter.c
 * @brief Delimiter-heavy benchmark — exercises SPAN / BREAK in tight loops
 *        where the JIT SSA passes (GVN, LICM, constant folding) provide
 *        measurable speedup.
 *
 * Workload design:
 *  - Same SPAN(',') pattern compiled ONCE (no SEARCH_MODE flag)
 *  - Two execution paths run against the SAME subject:
 *     1) match  path — interpreter VM dispatch only (per-iteration)
 *     2) search path — triggers JIT compilation after ~32 invocations
 *  - Subject is short (~512B) so each match is dispatch-bound
 *  - Iterations are high (100k) so the JIT cache hits on the hot path
 *
 * Throughput is reported in MB/s.  The JIT path should be ≥2× faster than
 * the interpreter because the SPAN inner loop is JIT-compiled into a tight
 * bitmap scan with no per-byte dispatch.
 */

#include "bench_shared.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SUBJECT_BYTES 16384
#define TOKEN_LEN 6
#define DELIM_LEN 2
#define MATCH_ITERS 5000
#define WARMUP_ITERS 100

static char *build_delimiter_subject(size_t *out_len) {
    char *s = (char *)malloc(SUBJECT_BYTES + 1);
    if (!s) {
        *out_len = 0;
        return NULL;
    }
    size_t pos = 0;
    while (pos + TOKEN_LEN + DELIM_LEN < SUBJECT_BYTES) {
        for (int i = 0; i < TOKEN_LEN; i++)
            s[pos++] = 'a' + (i % 26);
        for (int i = 0; i < DELIM_LEN; i++)
            s[pos++] = ',';
    }
    s[pos] = '\0';
    *out_len = pos;
    return s;
}

void bench_delimiter_suite(bench_results_t *out) {
    size_t subj_len = 0;
    char *subject = build_delimiter_subject(&subj_len);
    if (!subject) {
        out->label = "Delimiter-heavy (build FAILED)";
        out->snobol_ns = -1;
        out->search_ns = -1;
        out->pcre2_ns = -1;
        return;
    }

    /* snobol4: SPAN(',') via snobol_pattern_match — interpreter VM dispatch.
     * Each iteration goes through the full VM dispatch loop once per
     * bytecode instruction (LIT, SPAN, ACCEPT).  No JIT cache is used in
     * this API path. */
    {
        snobol_context_t *ctx = snobol_context_create();
        char *err = NULL;
        snobol_pattern_t *pat =
            snobol_pattern_compile(ctx, "SPAN(',')", 9, &err);
        if (!pat) {
            fprintf(stderr, "compile SPAN (match) failed: %s\n",
                    err ? err : "??");
            free(err);
            snobol_context_destroy(ctx);
            free(subject);
            out->label = "Delimiter-heavy (compile FAILED)";
            return;
        }
        free(err);

        int64_t start = bench_ns();
        size_t consumed = 0;
        for (int iter = 0; iter < MATCH_ITERS; iter++) {
            size_t pos = 0;
            while (pos < subj_len) {
                snobol_match_t *m =
                    snobol_pattern_match(pat, subject + pos, subj_len - pos);
                if (!snobol_match_success(m)) {
                    snobol_match_free(m);
                    pos++;
                    continue;
                }
                const char *out_str = snobol_match_get_output(m, NULL);
                size_t adv = out_str ? strlen(out_str) : 0;
                pos += adv;
                if (adv == 0)
                    pos++;
                snobol_match_free(m);
                consumed++;
            }
        }
        out->snobol_ns = bench_ns() - start;
        (void)consumed;
        snobol_pattern_free(pat);
        snobol_context_destroy(ctx);
    }

    /* snobol4: SPAN(',') via snobol_pattern_search — JIT path.
     * The pattern is compiled with SNOBOL_FLAG_SEARCH_MODE which lowers the
     * JIT profitability threshold so single-op patterns become eligible for
     * JIT compilation.  After ~32 invocations the JIT cache stores a
     * compiled region that bypasses the VM dispatch loop. */
    {
        snobol_context_t *ctx = snobol_context_create();
        char *err = NULL;
        snobol_pattern_t *pat =
            snobol_pattern_compile_ex(ctx, "SPAN(',')", 9,
                                       SNOBOL_FLAG_SEARCH_MODE, &err);
        if (!pat) {
            fprintf(stderr, "compile SPAN (search) failed: %s\n",
                    err ? err : "??");
            free(err);
            snobol_context_destroy(ctx);
            free(subject);
            out->search_ns = -1;
            return;
        }
        free(err);

        /* Warm-up phase: prime the JIT cache so subsequent invocations run
         * JIT-compiled code rather than triggering compilation during
         * timed measurements. */
        for (int w = 0; w < WARMUP_ITERS; w++) {
            size_t pos = 0;
            while (pos < subj_len) {
                snobol_match_t *m = snobol_pattern_search(pat, subject + pos,
                                                           subj_len - pos);
                if (!snobol_match_success(m)) {
                    snobol_match_free(m);
                    pos++;
                    continue;
                }
                const char *out_str = snobol_match_get_output(m, NULL);
                size_t adv = out_str ? strlen(out_str) : 0;
                pos += adv;
                if (adv == 0)
                    pos++;
                snobol_match_free(m);
            }
        }

        int64_t start = bench_ns();
        size_t consumed = 0;
        for (int iter = 0; iter < MATCH_ITERS; iter++) {
            size_t pos = 0;
            while (pos < subj_len) {
                snobol_match_t *m = snobol_pattern_search(pat, subject + pos,
                                                           subj_len - pos);
                if (!snobol_match_success(m)) {
                    snobol_match_free(m);
                    pos++;
                    continue;
                }
                const char *out_str = snobol_match_get_output(m, NULL);
                size_t adv = out_str ? strlen(out_str) : 0;
                pos += adv;
                if (adv == 0)
                    pos++;
                snobol_match_free(m);
                consumed++;
            }
        }
        out->search_ns = bench_ns() - start;
        (void)consumed;
        snobol_pattern_free(pat);
        snobol_context_destroy(ctx);
    }

    /* pcre2: same workload for comparison. */
#ifdef HAVE_PCRE2
    {
        int errcode;
        PCRE2_SIZE erroffset;
        pcre2_code *re =
            pcre2_compile((PCRE2_SPTR)",*", 2, 0, &errcode, &erroffset, NULL);
        if (!re) {
            out->pcre2_ns = -1;
        } else {
            pcre2_match_data *md =
                pcre2_match_data_create_from_pattern(re, NULL);
            int64_t start = bench_ns();
            for (int iter = 0; iter < MATCH_ITERS; iter++) {
                size_t pos = 0;
                while (pos < subj_len) {
                    int rc = pcre2_match(re, (PCRE2_SPTR)subject, subj_len, pos,
                                         0, md, NULL);
                    if (rc < 0) {
                        pos++;
                        continue;
                    }
                    PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
                    size_t adv = (size_t)(ov[1] - ov[0]);
                    pos += adv;
                    if (adv == 0)
                        pos++;
                }
            }
            out->pcre2_ns = bench_ns() - start;
            pcre2_match_data_free(md);
            pcre2_code_free(re);
        }
    }
#else
    out->pcre2_ns = -1;
#endif

    /* Compute throughput (MB/s) — both interpreter and JIT paths process
     * the same subject the same number of times. */
    double snobol_s = (double)out->snobol_ns / 1e9;
    double search_s = (double)out->search_ns / 1e9;
    double bytes_processed = (double)subj_len * MATCH_ITERS;
    double tp_int =
        snobol_s > 0 ? (bytes_processed / (1024 * 1024)) / snobol_s : 0;
    double tp_jit =
        search_s > 0 ? (bytes_processed / (1024 * 1024)) / search_s : 0;
    fprintf(stderr,
            "[delimiter] subj=%zuB iters=%d int=%.1fMB/s jit=%.1fMB/s",
            subj_len, MATCH_ITERS, tp_int, tp_jit);
    if (tp_int > 0 && tp_jit > 0)
        fprintf(stderr, " jit/int=%.2fx", tp_jit / tp_int);
    fprintf(stderr, "\n");

    out->label = "Delimiter-heavy (SPAN x 100k iters)";
    free(subject);
}