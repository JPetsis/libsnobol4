/**
 * test_api_match.c – Tests for snobol_match() one-shot API
 *
 * Verify the one-shot convenience API produces correct results for
 * success, failure, parse errors, captures, and output.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "snobol/snobol.h"

extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

void test_api_match_suite(void) {
    test_suite("API: snobol_match()");

    /* Simple literal match */
    {
        snobol_match_result_t *r = snobol_match("'hello'", 7, "hello world", 11, 0);
        test_assert(r != NULL, "snobol_match returns non-NULL for valid pattern");
        if (r) {
            test_assert(r->success, "'hello' matches 'hello world'");
            test_assert(r->error == NULL, "no error on success");
            snobol_match_result_free(r);
        }
    }

    /* No match */
    {
        snobol_match_result_t *r = snobol_match("'xyz'", 5, "hello world", 11, 0);
        test_assert(r != NULL, "snobol_match returns non-NULL for non-match");
        if (r) {
            test_assert(!r->success, "'xyz' does not match 'hello world'");
            test_assert(r->error == NULL, "non-match is not an error");
            snobol_match_result_free(r);
        }
    }

    /* Empty pattern → parse error */
    {
        snobol_match_result_t *r = snobol_match("", 0, "hello", 5, 0);
        test_assert(r != NULL, "snobol_match returns non-NULL even on parse error");
        if (r) {
            test_assert(!r->success, "empty pattern returns failure");
            test_assert(r->error != NULL, "parse error populates error field");
            if (r->error) {
                test_assert(strlen(r->error) > 0, "error message is non-empty");
            }
            snobol_match_result_free(r);
        }
    }

    /* Invalid pattern syntax */
    {
        snobol_match_result_t *r = snobol_match("(unclosed", 9, "test", 4, 0);
        test_assert(r != NULL, "snobol_match returns non-NULL on syntax error");
        if (r) {
            test_assert(!r->success, "syntax error -> failure");
            test_assert(r->error != NULL, "syntax error populates error field");
            snobol_match_result_free(r);
        }
    }

    /* Case-insensitive flag */
    {
        snobol_match_result_t *r1 = snobol_match("'ABC'", 5, "abc", 3, 0);
        snobol_match_result_t *r2 = snobol_match("'ABC'", 5, "abc", 3, SNOBOL_FLAG_CASE_INSENSITIVE);
        test_assert(r1 != NULL && r2 != NULL, "both case variants return non-NULL");
        if (r1) {
            test_assert(!r1->success, "'ABC' (case sensitive) does not match 'abc'");
            snobol_match_result_free(r1);
        }
        if (r2) {
            test_assert(r2->success, "'ABC' (case insensitive) matches 'abc'");
            snobol_match_result_free(r2);
        }
    }

    /* NULL result is safe to free */
    snobol_match_result_free(NULL);
    test_assert(true, "snobol_match_result_free(NULL) is safe");

    /* Empty subject */
    {
        snobol_match_result_t *r = snobol_match("'x'", 3, "", 0, 0);
        test_assert(r != NULL, "empty subject returns non-NULL");
        if (r) {
            test_assert(!r->success, "'x' does not match empty subject");
            snobol_match_result_free(r);
        }
    }

    /* Alternation */
    {
        snobol_match_result_t *r = snobol_match("'a' | 'b'", 9, "b", 1, 0);
        test_assert(r != NULL, "alternation pattern returns non-NULL");
        if (r) {
            test_assert(r->success, "'a' | 'b' matches 'b'");
            snobol_match_result_free(r);
        }
    }

    /* Repeated call with same pattern produces consistent results */
    {
        snobol_match_result_t *r1 = snobol_match("'foo'", 5, "foobar", 6, 0);
        snobol_match_result_t *r2 = snobol_match("'foo'", 5, "foobar", 6, 0);
        test_assert(r1 != NULL && r2 != NULL, "repeated calls return non-NULL");
        if (r1 && r2) {
            test_assert(r1->success && r2->success,
                        "repeated calls both match successfully");
            snobol_match_result_free(r1);
            snobol_match_result_free(r2);
        }
    }

    /* Output field (no EMIT in pattern -> output is NULL or empty) */
    {
        snobol_match_result_t *r = snobol_match("'hello'", 7, "hello world", 11, 0);
        test_assert(r != NULL, "output field test returns non-NULL");
        if (r) {
            test_assert(r->success, "match succeeds");
            test_assert(r->output == NULL || r->output_len == 0,
                        "no EMIT -> output is NULL or empty");
            snobol_match_result_free(r);
        }
    }

    /* Captures field valid on success */
    {
        snobol_match_result_t *r = snobol_match("'hello'", 7, "hello", 5, 0);
        test_assert(r != NULL, "captures test returns non-NULL");
        if (r) {
            test_assert(r->success, "match succeeds");
            test_assert(r->capture_count == 0,
                        "simple literal has 0 captures");
            snobol_match_result_free(r);
        }
    }

    /* Capture-via-source test.
     *
     * Pattern source syntax doesn't support $vN (that's an AST-builder-only
     * feature), so the only way to get OP_ASSIGN via the source path is
     * through compiled patterns.  The C-level capture regression test
     * lives in test_compiler.c (it builds the AST directly and runs the VM).
     * Here we just confirm that snobol_match() doesn't crash on
     * captures[] when the source pattern has no captures. */
    {
        snobol_match_result_t *r = snobol_match("'a' 'b' 'c'", 11, "abc", 3, 0);
        test_assert(r != NULL, "non-capture source returns non-NULL");
        if (r) {
            test_assert(r->success, "non-capture source matches");
            test_assert(r->capture_count == 0,
                        "non-capture source has capture_count == 0");
            test_assert(r->captures == NULL || r->captures[0] == NULL,
                        "captures[] is NULL or empty");
            snobol_match_result_free(r);
        }
    }
}
