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
    uint64_t jit_entries;
    uint64_t jit_bailouts;
    uint64_t jit_search_entries;
    uint64_t jit_choice_push;
    uint64_t jit_choice_pop;
    uint64_t jit_compile_time_ns;
    uint64_t jit_exec_time_ns;
    uint64_t jit_interp_time_ns;
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
static void snapshot_jit(uint64_t *entries, uint64_t *bailouts,
                         uint64_t *search_entries, uint64_t *choice_push,
                         uint64_t *choice_pop, uint64_t *compile_time_ns,
                         uint64_t *exec_time_ns, uint64_t *interp_time_ns) {
#ifdef SNOBOL_JIT
    SnobolJitStats *s = snobol_jit_get_stats();
    if (s) {
        *entries         = s->entries_total;
        *bailouts        = s->bailouts_total;
        *search_entries  = s->search_entries_total;
        *choice_push     = s->choice_push_total;
        *choice_pop      = s->choice_pop_total;
        *compile_time_ns = s->compile_time_ns_total;
        *exec_time_ns    = s->exec_time_ns_total;
        *interp_time_ns  = s->interp_time_ns_total;
    } else
#endif
    {
        *entries = *bailouts = *search_entries = 0;
        *choice_push = *choice_pop = 0;
        *compile_time_ns = *exec_time_ns = *interp_time_ns = 0;
    }
}

/* Capture per-scenario JIT deltas. */
static void capture_deltas(probe_result_t *r,
                           uint64_t e0, uint64_t b0, uint64_t s0,
                           uint64_t p0, uint64_t cp0,
                           uint64_t ct0, uint64_t et0, uint64_t it0) {
    uint64_t e1, b1, s1, p1, cp1, ct1, et1, it1;
    snapshot_jit(&e1, &b1, &s1, &p1, &cp1, &ct1, &et1, &it1);
    r->jit_entries         = e1 - e0;
    r->jit_bailouts        = b1 - b0;
    r->jit_search_entries  = s1 - s0;
    r->jit_choice_push     = p1 - p0;
    r->jit_choice_pop      = cp1 - cp0;
    r->jit_compile_time_ns = ct1 - ct0;
    r->jit_exec_time_ns    = et1 - et0;
    r->jit_interp_time_ns  = it1 - it0;
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

    uint64_t e0, b0, s0, p0, cp0, ct0, et0, it0;
    snapshot_jit(&e0, &b0, &s0, &p0, &cp0, &ct0, &et0, &it0);

    int64_t start = bench_ns();
    for (int64_t i = 0; i < iters; i++) {
        snobol_match_t *m = snobol_pattern_match(pat, SUBJECT_NO_PQR, slen);
        snobol_match_free(m);
    }
    int64_t end = bench_ns();

    r->iters = iters;
    r->total_ns = end - start;
    r->ns_per_iter = (iters > 0) ? (r->total_ns / iters) : 0;
    capture_deltas(r, e0, b0, s0, p0, cp0, ct0, et0, it0);

    snobol_pattern_free(pat);
    snobol_context_destroy(ctx);
}

static void run_literal_ok(int64_t iters, probe_result_t *r) {
    snobol_context_t *ctx = snobol_context_create();
    snobol_pattern_t *pat = compile_or_die(ctx, "'pqr'", 5);
    size_t slen = strlen(SUBJECT_WITH_PQR);

    uint64_t e0, b0, s0, p0, cp0, ct0, et0, it0;
    snapshot_jit(&e0, &b0, &s0, &p0, &cp0, &ct0, &et0, &it0);

    int64_t start = bench_ns();
    for (int64_t i = 0; i < iters; i++) {
        snobol_match_t *m = snobol_pattern_match(pat, SUBJECT_WITH_PQR, slen);
        snobol_match_free(m);
    }
    int64_t end = bench_ns();

    r->iters = iters;
    r->total_ns = end - start;
    r->ns_per_iter = (iters > 0) ? (r->total_ns / iters) : 0;
    capture_deltas(r, e0, b0, s0, p0, cp0, ct0, et0, it0);

    snobol_pattern_free(pat);
    snobol_context_destroy(ctx);
}

static void run_span_comma(int64_t iters, probe_result_t *r) {
    snobol_context_t *ctx = snobol_context_create();
    snobol_pattern_t *pat = compile_or_die(ctx, "SPAN(',')", 9);
    size_t slen = strlen(SUBJECT_CSV);

    uint64_t e0, b0, s0, p0, cp0, ct0, et0, it0;
    snapshot_jit(&e0, &b0, &s0, &p0, &cp0, &ct0, &et0, &it0);

    int64_t start = bench_ns();
    for (int64_t i = 0; i < iters; i++) {
        snobol_match_t *m = snobol_pattern_match(pat, SUBJECT_CSV, slen);
        snobol_match_free(m);
    }
    int64_t end = bench_ns();

    r->iters = iters;
    r->total_ns = end - start;
    r->ns_per_iter = (iters > 0) ? (r->total_ns / iters) : 0;
    capture_deltas(r, e0, b0, s0, p0, cp0, ct0, et0, it0);

    snobol_pattern_free(pat);
    snobol_context_destroy(ctx);
}

static void run_span_search(int64_t iters, probe_result_t *r) {
    snobol_context_t *ctx = snobol_context_create();
    snobol_pattern_t *pat = compile_or_die(ctx, "SPAN(',')", 9);
    size_t slen = strlen(SUBJECT_CSV);

    uint64_t e0, b0, s0, p0, cp0, ct0, et0, it0;
    snapshot_jit(&e0, &b0, &s0, &p0, &cp0, &ct0, &et0, &it0);

    int64_t start = bench_ns();
    for (int64_t i = 0; i < iters; i++) {
        snobol_match_t *m = snobol_pattern_search(pat, SUBJECT_CSV, slen);
        snobol_match_free(m);
    }
    int64_t end = bench_ns();

    r->iters = iters;
    r->total_ns = end - start;
    r->ns_per_iter = (iters > 0) ? (r->total_ns / iters) : 0;
    capture_deltas(r, e0, b0, s0, p0, cp0, ct0, et0, it0);

    snobol_pattern_free(pat);
    snobol_context_destroy(ctx);
}

static void run_alternation(int64_t iters, probe_result_t *r) {
    snobol_context_t *ctx = snobol_context_create();
    /* Fused by SPLIT→ANY into one OP_ANY with {a,b,c} charclass */
    snobol_pattern_t *pat = compile_or_die(ctx, "'a' | 'b' | 'c'", 15);
    size_t slen = strlen(SUBJECT_MIXED);

    uint64_t e0, b0, s0, p0, cp0, ct0, et0, it0;
    snapshot_jit(&e0, &b0, &s0, &p0, &cp0, &ct0, &et0, &it0);

    int64_t start = bench_ns();
    for (int64_t i = 0; i < iters; i++) {
        snobol_match_t *m = snobol_pattern_match(pat, SUBJECT_MIXED, slen);
        snobol_match_free(m);
    }
    int64_t end = bench_ns();

    r->iters = iters;
    r->total_ns = end - start;
    r->ns_per_iter = (iters > 0) ? (r->total_ns / iters) : 0;
    capture_deltas(r, e0, b0, s0, p0, cp0, ct0, et0, it0);

    snobol_pattern_free(pat);
    snobol_context_destroy(ctx);
}

static void run_alt_search(int64_t iters, probe_result_t *r) {
    snobol_context_t *ctx = snobol_context_create();
    snobol_pattern_t *pat = compile_or_die(ctx, "'a' | 'b' | 'c'", 15);
    size_t slen = strlen(SUBJECT_MIXED);

    uint64_t e0, b0, s0, p0, cp0, ct0, et0, it0;
    snapshot_jit(&e0, &b0, &s0, &p0, &cp0, &ct0, &et0, &it0);

    int64_t start = bench_ns();
    for (int64_t i = 0; i < iters; i++) {
        snobol_match_t *m = snobol_pattern_search(pat, SUBJECT_MIXED, slen);
        snobol_match_free(m);
    }
    int64_t end = bench_ns();

    r->iters = iters;
    r->total_ns = end - start;
    r->ns_per_iter = (iters > 0) ? (r->total_ns / iters) : 0;
    capture_deltas(r, e0, b0, s0, p0, cp0, ct0, et0, it0);

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

    uint64_t e0, b0, s0, p0, cp0, ct0, et0, it0;
    snapshot_jit(&e0, &b0, &s0, &p0, &cp0, &ct0, &et0, &it0);

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
    capture_deltas(r, e0, b0, s0, p0, cp0, ct0, et0, it0);

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
    /* Columns: scenario, ns/iter, iters, jit_entries, jit_bailouts,
     * search_entries, choice_push, choice_pop, exec_ns, interp_ns */
    printf("%-16s %10s %8s %10s %10s %10s %10s %10s %12s %12s\n",
           "scenario", "ns/iter", "iters", "jit_ent", "jit_bail",
           "s_entries", "choice_p", "choice_pop", "exec_ns", "interp_ns");
    printf("%-16s %10s %8s %10s %10s %10s %10s %10s %12s %12s\n",
           "-------", "-------", "-----", "-------", "-------",
           "---------", "---------", "---------", "-------", "--------");

    for (size_t i = 0; i < n; i++) {
        const probe_result_t *r = &results[i];
        printf("%-16s %10" PRId64 " %8" PRId64 " %10" PRIu64 " %10" PRIu64
               " %10" PRIu64 " %10" PRIu64 " %10" PRIu64 " %12" PRIu64
               " %12" PRIu64 "\n",
               r->name,
               r->ns_per_iter,
               r->iters,
               r->jit_entries,
               r->jit_bailouts,
               r->jit_search_entries,
               r->jit_choice_push,
               r->jit_choice_pop,
               r->jit_exec_time_ns,
               r->jit_interp_time_ns);
    }
    printf("\n");
    printf("Legend:\n");
    printf("  ns/iter      : wall time per match attempt (lower = faster)\n");
    printf("  iters        : match attempts executed in the scenario\n");
    printf("  jit_ent      : JIT-compiled trace entries (delta)\n");
    printf("  jit_bail     : JIT bailout count (delta)\n");
    printf("  s_entries    : search-mode trace entries (delta)\n");
    printf("  choice_p/pop : VM choice stack push/pop count (delta)\n");
    printf("  exec_ns      : time inside JIT-compiled traces (delta, ns)\n");
    printf("  interp_ns    : time in interpreter dispatch (delta, ns)\n");
    printf("\n");
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

    return 0;
}
