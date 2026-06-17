/**
 * @file one_shot_match.c
 * @brief Example using the snobol_match() one-shot convenience API
 *
 * Demonstrates the simplified compile-and-match pipeline that bundles
 * lexing, parsing, compiling, and VM execution into a single call.
 * Best for one-off matches; for repeated matching of the same pattern
 * use the multi-step API instead.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <snobol/snobol.h>

static void demo(const char *pattern, const char *subject) {
    size_t pat_len = strlen(pattern);
    size_t sub_len = strlen(subject);

    printf("Pattern: %s\n", pattern);
    printf("Subject: %s\n", subject);

    snobol_match_result_t *r = snobol_match(
        pattern, pat_len, subject, sub_len, 0);

    if (!r) {
        printf("  -> snobol_match() returned NULL\n\n");
        return;
    }

    if (r->success) {
        printf("  -> match succeeded\n");
        for (int i = 0; i < r->capture_count; i++) {
            if (r->captures[i]) {
                printf("     capture %d: \"%s\"\n", i, r->captures[i]);
            }
        }
        if (r->output && r->output_len > 0) {
            printf("     output: \"%s\"\n", r->output);
        }
    } else if (r->error) {
        printf("  -> error: %s\n", r->error);
    } else {
        printf("  -> no match\n");
    }
    printf("\n");

    snobol_match_result_free(r);
}

int main(void) {
    int major, minor, patch;
    snobol_version(&major, &minor, &patch);
    printf("libsnobol4 %d.%d.%d — one_shot_match example\n\n",
           major, minor, patch);

    demo("'hello'", "hello world");
    demo("'xyz'",   "hello world");
    demo("",        "anything");
    demo("'a' | 'b' | 'c'", "b");
    return 0;
}
