/**
 * test_pattern_case.c – Tests for case-insensitive pattern matching
 *
 * compile_ex with SNOBOL_FLAG_CASE_INSENSITIVE matches "HELLO" against
 *   pattern "hello"; flags=0 behaves identically to snobol_pattern_compile;
 *   unknown flag bits are tolerated.
 * Latin-1 case-insensitive tests.
 * JIT disabled comment is placed in api.c at the TODO(jit-case-fold) site.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "snobol/snobol.h"

extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

/* ---------------------------------------------------------------------------
 * Helper: compile + match a pattern against a subject, return match success.
 * Does NOT require an output or captures — just tests match/no-match.
 * --------------------------------------------------------------------------- */
static bool compile_and_match(const char *pattern_src, const char *subject,
                               uint32_t flags)
{
    snobol_context_t *ctx = snobol_context_create();
    if (!ctx) return false;

    char *err = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile_ex(ctx, pattern_src,
                                                       strlen(pattern_src),
                                                       flags, &err);
    if (!pat) {
        free(err);
        snobol_context_destroy(ctx);
        return false;
    }

    snobol_match_t *m = snobol_pattern_match(pat, subject, strlen(subject));
    bool ok = snobol_match_success(m);

    snobol_match_free(m);
    snobol_pattern_free(pat);
    snobol_context_destroy(ctx);
    return ok;
}

void test_pattern_case_suite(void) {
    test_suite("Pattern: case-insensitive (compile_ex)");

    /* --- ASCII case-insensitive matching --- */
    test_assert(compile_and_match("'hello'", "hello", SNOBOL_FLAG_CASE_INSENSITIVE),
                "CI: 'hello' matches \"hello\"");

    test_assert(compile_and_match("'hello'", "HELLO", SNOBOL_FLAG_CASE_INSENSITIVE),
                "CI: 'hello' matches \"HELLO\" (case-insensitive)");

    test_assert(compile_and_match("'hello'", "HeLLo", SNOBOL_FLAG_CASE_INSENSITIVE),
                "CI: 'hello' matches \"HeLLo\" (case-insensitive)");

    /* --- flags=0 identical to snobol_pattern_compile (case-sensitive) --- */
    test_assert(!compile_and_match("'hello'", "HELLO", 0),
                "CS: 'hello' does NOT match \"HELLO\" with flags=0");

    test_assert(compile_and_match("'hello'", "hello", 0),
                "CS: 'hello' matches \"hello\" with flags=0");

    /* --- Unknown flag bits are tolerated (shouldn't crash or fail) --- */
    /* Pattern is valid; unknown bits ignored; case-insensitive still active */
    test_assert(compile_and_match("'hello'", "HELLO", 0xFFFEu | SNOBOL_FLAG_CASE_INSENSITIVE),
                "CI: unknown flag bits tolerated (flags=0xFFFF)");

    test_suite("Pattern: snobol_pattern_compile (no flags)");

    /* snobol_pattern_compile should behave identically to compile_ex with flags=0 */
    {
        snobol_context_t *ctx = snobol_context_create();
        char *err = NULL;
        snobol_pattern_t *pat = snobol_pattern_compile(ctx, "'hello'", 7, &err);
        test_assert(pat != NULL, "snobol_pattern_compile returns non-NULL");
        if (pat) {
            snobol_match_t *m = snobol_pattern_match(pat, "hello", 5);
            test_assert(snobol_match_success(m), "compile: 'hello' matches \"hello\"");
            snobol_match_free(m);

            m = snobol_pattern_match(pat, "HELLO", 5);
            test_assert(!snobol_match_success(m), "compile: 'hello' does NOT match \"HELLO\" (case-sensitive)");
            snobol_match_free(m);

            snobol_pattern_free(pat);
        }
        free(err);
        snobol_context_destroy(ctx);
    }

    test_suite("Pattern: case-insensitive Latin-1");

    /* literal "café" matches "CAFÉ" case-insensitively */
    /* NB: pattern source uses single-quoted literals */
    /* café UTF-8: 63 61 66 C3 A9 (5 bytes) */
    /* CAFÉ UTF-8: 43 41 46 C3 89 (5 bytes) */
    test_assert(compile_and_match("'caf\xC3\xA9'", "CAF\xC3\x89",
                                   SNOBOL_FLAG_CASE_INSENSITIVE),
                "CI: 'café' matches \"CAFÉ\" (Latin-1 case-insensitive)");

    /* Reverse: 'CAFÉ' matches "café" case-insensitively */
    test_assert(compile_and_match("'CAF\xC3\x89'", "caf\xC3\xA9",
                                   SNOBOL_FLAG_CASE_INSENSITIVE),
                "CI: 'CAFÉ' matches \"café\" (Latin-1 case-insensitive, reverse)");
}


