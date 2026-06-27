/*
 * bench_probe.c — Diagnostic probe for libsnobol4 C API
 *
 * Exercises representative patterns in tight loops and reports a per-scenario
 * table of timings + JIT stats deltas. Used to attribute per-iteration cost
 * between the interpreter and JIT-compiled paths.
 *
 * Uses only the PUBLIC C API — no core/ modifications, no internal headers.
 *
 * Build: cmake -B build -DBUILD_BENCH_C=ON && cmake --build build --target snobol4_probe
 * Run:   ./build/bench/c/snobol4_probe
 * Tune:  PROBE_ITERS=1000000 ./build/bench/c/snobol4_probe
 */

#include "bench_shared.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#ifdef SNOBOL_JIT
#include <snobol/jit.h>
#endif

/* Per-scenario result */
typedef struct {
    const char *name;       /* scenario id */
    int64_t iters;          /* iterations executed */
    int64_t total_ns;       /* wall time for the loop */
    int64_t ns_per_iter;    /* total_ns / iters */

    /* JIT stat deltas (across the loop). All zero if JIT disabled. */
    uint64_t jit_method_attempts;
    uint64_t jit_method_successes;
    uint64_t jit_method_fallbacks;
} probe_result_t;

/* Comma subject (matches bench_alternation.c) */
static const char *SUBJECT_CSV =
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

/* 1KB subject with 'pqr' at offset 16 */
static const char *SUBJECT_WITH_PQR =
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

/* 1KB subject with NO 'pqr' */
static const char *SUBJECT_NO_PQR =
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz";

/* Whitespace-separated subject for tokenize (mimics bench/tokenize.php) */
static const char *SUBJECT_WHITESPACE =
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    "a b c d e f g h i j k l m n o p q r s t u v w x y z ";

/* Mixed subject for alternation (a/b/c interleaved with other chars) */
static const char *SUBJECT_MIXED =
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    "the a quick b brown c fox a jumps b over c the a lazy b dog c ";

/* Compile a pattern; abort on failure. */
static snobol_pattern_t *compile_or_die(snobol_context_t *ctx,
                                         const char *src, size_t len) {
    char *err = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile(ctx, src, len, &err);
    if (!pat) {
        fprintf(stderr, "compile failed: %s\n", err ? err : "(no detail)");
        free(err);
        abort();
    }
    free(err);
    return pat;
}

/* Snapshot JIT stats. Returns zeros if JIT is not compiled in. */
static void snapshot_jit(uint64_t *attempts, uint64_t *successes,
                         uint64_t *fallbacks) {
#ifdef SNOBOL_JIT
    SnobolJitStats *s = snobol_jit_get_stats();
    if (s) {
        *attempts  = s->method_attempts_total;
        *successes = s->method_successes_total;
        *fallbacks = s->method_fallbacks_total;
    } else
#endif
    {
        *attempts = *successes = *fallbacks = 0;
    }
}

/* Capture per-scenario JIT deltas. */
static void capture_deltas(probe_result_t *r,
                           uint64_t a0, uint64_t s0, uint64_t f0) {
    uint64_t a1, s1, f1;
    snapshot_jit(&a1, &s1, &f1);
    r->jit_method_attempts  = a1 - a0;
    r->jit_method_successes = s1 - s0;
    r->jit_method_fallbacks = f1 - f0;
}

/* Reset JIT stats. No-op if JIT is not compiled in. */
static void reset_jit_stats(void) {
#ifdef SNOBOL_JIT
    snobol_jit_reset_stats();
#endif
}

/* ---------------------------------------------------------------------------
 * Scenario runners
 *
 * Each runner takes (iterations, subject, subject_len) and writes to *r.
 * --------------------------------------------------------------------------- */

static void run_literal_fail(int64_t iters, probe_result_t *r) {
    snobol_context_t *ctx = snobol_context_create();
    snobol_pattern_t *pat = compile_or_die(ctx, "'pqr'", 5);
    size_t slen = strlen(SUBJECT_NO_PQR);

    uint64_t a0, s0, f0;
    snapshot_jit(&a0, &s0, &f0);

    int64_t start = bench_ns();
    for (int64_t i = 0; i < iters; i++) {
        snobol_match_t *m = snobol_pattern_match(pat, SUBJECT_NO_PQR, slen);
        snobol_match_free(m);
    }
    int64_t end = bench_ns();

    r->iters = iters;
    r->total_ns = end - start;
    r->ns_per_iter = (iters > 0) ? (r->total_ns / iters) : 0;
    capture_deltas(r, a0, s0, f0);

    snobol_pattern_free(pat);
    snobol_context_destroy(ctx);
}

static void run_literal_ok(int64_t iters, probe_result_t *r) {
    snobol_context_t *ctx = snobol_context_create();
    snobol_pattern_t *pat = compile_or_die(ctx, "'pqr'", 5);
    size_t slen = strlen(SUBJECT_WITH_PQR);

    uint64_t a0, s0, f0;
    snapshot_jit(&a0, &s0, &f0);

    int64_t start = bench_ns();
    for (int64_t i = 0; i < iters; i++) {
        snobol_match_t *m = snobol_pattern_match(pat, SUBJECT_WITH_PQR, slen);
        snobol_match_free(m);
    }
    int64_t end = bench_ns();

    r->iters = iters;
    r->total_ns = end - start;
    r->ns_per_iter = (iters > 0) ? (r->total_ns / iters) : 0;
    capture_deltas(r, a0, s0, f0);

    snobol_pattern_free(pat);
    snobol_context_destroy(ctx);
}

static void run_span_comma(int64_t iters, probe_result_t *r) {
    snobol_context_t *ctx = snobol_context_create();
    snobol_pattern_t *pat = compile_or_die(ctx, "SPAN(',')", 9);
    size_t slen = strlen(SUBJECT_CSV);

    uint64_t a0, s0, f0;
    snapshot_jit(&a0, &s0, &f0);

    int64_t start = bench_ns();
    for (int64_t i = 0; i < iters; i++) {
        snobol_match_t *m = snobol_pattern_match(pat, SUBJECT_CSV, slen);
        snobol_match_free(m);
    }
    int64_t end = bench_ns();

    r->iters = iters;
    r->total_ns = end - start;
    r->ns_per_iter = (iters > 0) ? (r->total_ns / iters) : 0;
    capture_deltas(r, a0, s0, f0);

    snobol_pattern_free(pat);
    snobol_context_destroy(ctx);
}

static void run_span_search(int64_t iters, probe_result_t *r) {
    snobol_context_t *ctx = snobol_context_create();
    snobol_pattern_t *pat = compile_or_die(ctx, "SPAN(',')", 9);
    size_t slen = strlen(SUBJECT_CSV);

    uint64_t a0, s0, f0;
    snapshot_jit(&a0, &s0, &f0);

    int64_t start = bench_ns();
    for (int64_t i = 0; i < iters; i++) {
        snobol_match_t *m = snobol_pattern_search(pat, SUBJECT_CSV, slen);
        snobol_match_free(m);
    }
    int64_t end = bench_ns();

    r->iters = iters;
    r->total_ns = end - start;
    r->ns_per_iter = (iters > 0) ? (r->total_ns / iters) : 0;
    capture_deltas(r, a0, s0, f0);

    snobol_pattern_free(pat);
    snobol_context_destroy(ctx);
}

static void run_alternation(int64_t iters, probe_result_t *r) {
    snobol_context_t *ctx = snobol_context_create();
    /* Fused by SPLIT→ANY into one OP_ANY with {a,b,c} charclass */
    snobol_pattern_t *pat = compile_or_die(ctx, "'a' | 'b' | 'c'", 15);
    size_t slen = strlen(SUBJECT_MIXED);

    uint64_t a0, s0, f0;
    snapshot_jit(&a0, &s0, &f0);

    int64_t start = bench_ns();
    for (int64_t i = 0; i < iters; i++) {
        snobol_match_t *m = snobol_pattern_match(pat, SUBJECT_MIXED, slen);
        snobol_match_free(m);
    }
    int64_t end = bench_ns();

    r->iters = iters;
    r->total_ns = end - start;
    r->ns_per_iter = (iters > 0) ? (r->total_ns / iters) : 0;
    capture_deltas(r, a0, s0, f0);

    snobol_pattern_free(pat);
    snobol_context_destroy(ctx);
}

static void run_alt_search(int64_t iters, probe_result_t *r) {
    snobol_context_t *ctx = snobol_context_create();
    snobol_pattern_t *pat = compile_or_die(ctx, "'a' | 'b' | 'c'", 15);
    size_t slen = strlen(SUBJECT_MIXED);

    uint64_t a0, s0, f0;
    snapshot_jit(&a0, &s0, &f0);

    int64_t start = bench_ns();
    for (int64_t i = 0; i < iters; i++) {
        snobol_match_t *m = snobol_pattern_search(pat, SUBJECT_MIXED, slen);
        snobol_match_free(m);
    }
    int64_t end = bench_ns();

    r->iters = iters;
    r->total_ns = end - start;
    r->ns_per_iter = (iters > 0) ? (r->total_ns / iters) : 0;
    capture_deltas(r, a0, s0, f0);

    snobol_pattern_free(pat);
    snobol_context_destroy(ctx);
}

/* Mimics what Pattern::searchSplit does: loop snobol_pattern_search at
 * advancing offsets, advance by match length. Counts inner iterations
 * (search calls) as `iters` (one per token boundary). */
static void run_tokenize(int64_t iters, probe_result_t *r) {
    snobol_context_t *ctx = snobol_context_create();
    snobol_pattern_t *pat = compile_or_die(ctx, "' '", 3);
    size_t slen = strlen(SUBJECT_WHITESPACE);

    uint64_t a0, s0, f0;
    snapshot_jit(&a0, &s0, &f0);

    int64_t total_search_calls = 0;
    int64_t start = bench_ns();
    for (int64_t i = 0; i < iters && total_search_calls < (int64_t)1e9; i++) {
        /* one full pass of the subject, splitting on ' ' */
        size_t pos = 0;
        while (pos <= slen) {
            snobol_match_t *m = snobol_pattern_search(pat, SUBJECT_WHITESPACE, slen);
            total_search_calls++;
            if (!snobol_match_success(m)) {
                snobol_match_free(m);
                break;
            }
            /* advance past the match (single space → len 1) */
            snobol_match_free(m);
            pos += 1;
        }
    }
    int64_t end = bench_ns();

    r->iters = total_search_calls;  /* report actual search calls, not outer iters */
    r->total_ns = end - start;
    r->ns_per_iter = (total_search_calls > 0) ? (r->total_ns / total_search_calls) : 0;
    capture_deltas(r, a0, s0, f0);

    snobol_pattern_free(pat);
    snobol_context_destroy(ctx);
}

/* ---------------------------------------------------------------------------
 * Output
 * --------------------------------------------------------------------------- */

static void print_header(void) {
    printf("\n");
    printf("libsnobol4 diagnostic probe — per-scenario timing + JIT stat deltas\n");
    printf("==================================================================\n");
#ifdef SNOBOL_JIT
    printf("JIT: ENABLED (public stats available via snobol_jit_get_stats)\n");
#else
    printf("JIT: DISABLED (rebuild with -DSNOBOL_JIT=ON to enable)\n");
#endif
    printf("\n");
}

static void print_table(const probe_result_t *results, size_t n) {
    /* Columns: scenario, ns/iter, iters, jit_attempts, jit_ok, jit_fb */
    printf("%-16s %10s %8s %12s %12s %12s\n",
           "scenario", "ns/iter", "iters", "jit_attempts", "jit_ok", "jit_fb");
    printf("%-16s %10s %8s %12s %12s %12s\n",
           "-------", "-------", "-----", "-----------", "------", "------");

    for (size_t i = 0; i < n; i++) {
        const probe_result_t *r = &results[i];
        printf("%-16s %10" PRId64 " %8" PRId64 " %12" PRIu64 " %12" PRIu64
               " %12" PRIu64 "\n",
               r->name,
               r->ns_per_iter,
               r->iters,
               r->jit_method_attempts,
               r->jit_method_successes,
               r->jit_method_fallbacks);
    }
    printf("\n");
    printf("Legend:\n");
    printf("  ns/iter       : wall time per match attempt (lower = faster)\n");
    printf("  iters         : match attempts executed in the scenario\n");
    printf("  jit_attempts  : method-JIT compile attempts (delta)\n");
    printf("  jit_ok        : method-JIT successful compilations (delta)\n");
    printf("  jit_fb        : method-JIT fallbacks to interpreter (delta)\n");
    printf("\n");
}

/* ---------------------------------------------------------------------------
 * Baseline regression guard
 *
 * Reads bench/results/search_perf_baseline.json and asserts each
 * scenario's ns_per_iter is within 10% of the baseline. Exits non-zero
 * on regression.
 * --------------------------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Minimal JSON parser for our specific schema — finds a scenario
 * key in the c_probe object and extracts its ns_per_iter value. We
 * parse brace-delimited scopes to avoid matching ns_per_iter in
 * nested objects (e.g. c_api_extensions.comparison_to_before). */
typedef struct {
    char name[64];
    long long ns_per_iter;
    int  found;
} baseline_row_t;

static int parse_baseline_row(const char *json, const char *scenario, baseline_row_t *out) {
    /* The c_probe object is the authoritative source. Find it first. */
    const char *cprobe = strstr(json, "\"c_probe\"");
    if (!cprobe) return 0;
    const char *obj_start = strchr(cprobe, '{');
    if (!obj_start) return 0;
    /* Walk forward to find the matching '}' for the c_probe object. */
    int depth = 1;
    const char *q = obj_start + 1;
    while (*q && depth > 0) {
        if (*q == '{') depth++;
        else if (*q == '}') depth--;
        if (depth == 0) break;
        q++;
    }
    if (depth != 0) return 0;
    /* Now search for "<scenario>": { within c_probe only. */
    char key[128];
    snprintf(key, sizeof(key), "\"%s\"", scenario);
    const char *p = strstr(obj_start, key);
    if (!p || p > q) return 0;
    /* Find the scenario's object start. */
    const char *s_start = strchr(p, '{');
    if (!s_start || s_start > q) return 0;
    int sd = 1;
    const char *s_end = s_start + 1;
    while (s_end < q && sd > 0) {
        if (*s_end == '{') sd++;
        else if (*s_end == '}') sd--;
        if (sd == 0) break;
        s_end++;
    }
    if (sd != 0) return 0;
    /* Now find ns_per_iter within the scenario's brace-balanced object. */
    const char *nsp = strstr(s_start, "\"ns_per_iter\"");
    if (!nsp || nsp > s_end) return 0;
    const char *colon = strchr(nsp, ':');
    if (!colon) return 0;
    long long v = strtoll(colon + 1, NULL, 10);
    if (v <= 0) return 0;
    strncpy(out->name, scenario, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';
    out->ns_per_iter = v;
    out->found = 1;
    return 1;
}

static int read_file(const char *path, char **out) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    *out = (char *)malloc((size_t)sz + 1);
    if (!*out) { fclose(f); return 0; }
    fread(*out, 1, (size_t)sz, f);
    (*out)[sz] = '\0';
    fclose(f);
    return 1;
}

static int assert_against_baseline(const probe_result_t *results, size_t n) {
    /* Try a few likely paths */
    const char *env_path = getenv("PROBE_BASELINE_PATH");
    const char *paths[5] = {0};
    int npaths = 0;
    if (env_path) paths[npaths++] = env_path;
    paths[npaths++] = "bench/results/search_perf_baseline.json";
    paths[npaths++] = "../bench/results/search_perf_baseline.json";
    paths[npaths++] = "../../bench/results/search_perf_baseline.json";
    paths[npaths] = NULL;
    char *json = NULL;
    int found_path = -1;
    for (int i = 0; paths[i]; i++) {
        if (read_file(paths[i], &json)) {
            found_path = i;
            break;
        }
    }
    if (!json) {
        fprintf(stderr, "PROBE_BASELINE=1 but no baseline file found\n");
        return 2;
    }
    printf("\n=== Baseline regression guard (PROBE_BASELINE=1) ===\n");
    printf("Baseline file: %s\n", paths[found_path]);
    printf("%-16s %12s %12s %12s\n",
           "scenario", "baseline", "observed", "delta%");
    printf("%-16s %12s %12s %12s\n",
           "-------", "--------", "--------", "------");

    int regressions = 0;
    int speedups = 0;
    for (size_t i = 0; i < n; i++) {
        baseline_row_t row;
        if (!parse_baseline_row(json, results[i].name, &row)) {
            /* No baseline entry — skip (not a regression) */
            continue;
        }
        long long base = row.ns_per_iter;
        long long obs  = results[i].ns_per_iter;
        double delta_pct = (base > 0) ? ((double)(obs - base) / base * 100.0) : 0.0;
        printf("%-16s %12lld %12lld %+11.1f%%",
               results[i].name, base, obs, delta_pct);
        if (delta_pct > 25.0) {
            printf("  REGRESSION\n");
            regressions++;
        } else if (delta_pct < -10.0) {
            printf("  speedup\n");
            speedups++;
        } else {
            printf("  ok\n");
        }
    }
    free(json);
    printf("\n%d regressions, %d speedups, %zu scenarios checked\n",
           regressions, speedups, n);
    if (regressions > 0) {
        printf("FAILED: %d scenarios regressed by more than 25%%\n", regressions);
        return 1;
    }
    printf("OK: no regressions exceeding 25%% threshold\n");
    return 0;
}

/* ---------------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------------- */

int main(void) {
    int64_t iters = 100000;
    const char *env_iters = getenv("PROBE_ITERS");
    if (env_iters && *env_iters) {
        long long v = atoll(env_iters);
        if (v > 0) iters = v;
    }
    /* Tokenize needs more outer iterations to make the timing meaningful;
     * scale them down by 10x so the probe stays fast. */
    int64_t tokenize_iters = iters / 10;
    if (tokenize_iters < 1) tokenize_iters = 1;

    print_header();
    printf("Iterations per scenario: %" PRId64 " (override with PROBE_ITERS)\n",
           iters);
    printf("Tokenize uses %" PRId64 " outer iters (multi-pass of subject).\n\n",
           tokenize_iters);

    probe_result_t results[7];
    memset(results, 0, sizeof(results));

    /* Run each scenario with a fresh JIT-stats baseline so deltas are clean. */
    struct {
        const char *name;
        void (*run)(int64_t, probe_result_t *);
        int64_t iter_count;
    } scenarios[] = {
        { "literal_fail",   run_literal_fail,   iters            },
        { "literal_ok",     run_literal_ok,     iters            },
        { "span_comma",     run_span_comma,     iters            },
        { "span_search",    run_span_search,    iters            },
        { "alternation",    run_alternation,    iters            },
        { "alt_search",     run_alt_search,     iters            },
        { "tokenize",       run_tokenize,       tokenize_iters   },
    };
    size_t n = sizeof(scenarios) / sizeof(scenarios[0]);

    for (size_t i = 0; i < n; i++) {
        reset_jit_stats();
        results[i].name = scenarios[i].name;
        scenarios[i].run(scenarios[i].iter_count, &results[i]);
    }

    print_table(results, n);

    /* Optional baseline regression guard. If a baseline file exists
     * at bench/results/search_perf_baseline.json and PROBE_BASELINE=1,
     * assert each scenario's ns_per_iter is within 10% of the baseline. */
    if (getenv("PROBE_BASELINE") && atoi(getenv("PROBE_BASELINE")) == 1) {
        return assert_against_baseline(results, n);
    }

    return 0;
}
