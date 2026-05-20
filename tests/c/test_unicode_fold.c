/**
 * test_unicode_fold.c – Tests for the snobol_to_upper_cp / snobol_to_lower_cp
 * Unicode fold table functions.
 *
 * Verifies:
 *   - Latin-1 upper/lower round-trip
 *   - German sharp-s multi-char expansion (U+00DF → "SS")
 *   - Out-of-range identity mapping
 *   - ASCII fast path
 *   - Latin Extended-A samples
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "snobol/unicode_fold.h"

extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

void test_unicode_fold_suite(void) {
    test_suite("Unicode fold table");

    uint32_t out[2];
    int      out_len;

    /* --- ASCII fast path --- */
    snobol_to_upper_cp(0x61, out, &out_len);   /* 'a' → 'A' */
    test_assert(out_len == 1 && out[0] == 0x41, "to_upper: 'a' → 'A'");

    snobol_to_upper_cp(0x7A, out, &out_len);   /* 'z' → 'Z' */
    test_assert(out_len == 1 && out[0] == 0x5A, "to_upper: 'z' → 'Z'");

    test_assert(snobol_to_lower_cp(0x41) == 0x61, "to_lower: 'A' → 'a'");
    test_assert(snobol_to_lower_cp(0x5A) == 0x7A, "to_lower: 'Z' → 'z'");

    /* ASCII already-upper / already-lower: identity */
    snobol_to_upper_cp(0x41, out, &out_len);
    test_assert(out_len == 1 && out[0] == 0x41, "to_upper: 'A' → 'A' (identity)");
    test_assert(snobol_to_lower_cp(0x61) == 0x61, "to_lower: 'a' → 'a' (identity)");

    /* --- Latin-1 upper/lower round-trip --- */
    /* U+00E9 é → U+00C9 É */
    snobol_to_upper_cp(0x00E9, out, &out_len);
    test_assert(out_len == 1 && out[0] == 0x00C9, "to_upper: U+00E9 é → U+00C9 É");

    /* U+00C9 É → U+00E9 é */
    test_assert(snobol_to_lower_cp(0x00C9) == 0x00E9, "to_lower: U+00C9 É → U+00E9 é");

    /* U+00FC ü → U+00DC Ü */
    snobol_to_upper_cp(0x00FC, out, &out_len);
    test_assert(out_len == 1 && out[0] == 0x00DC, "to_upper: U+00FC ü → U+00DC Ü");

    /* U+00DC Ü → U+00FC ü */
    test_assert(snobol_to_lower_cp(0x00DC) == 0x00FC, "to_lower: U+00DC Ü → U+00FC ü");

    /* U+00FF ÿ → U+0178 Ÿ */
    snobol_to_upper_cp(0x00FF, out, &out_len);
    test_assert(out_len == 1 && out[0] == 0x0178, "to_upper: U+00FF ÿ → U+0178 Ÿ");

    /* U+0178 Ÿ → U+00FF ÿ */
    test_assert(snobol_to_lower_cp(0x0178) == 0x00FF, "to_lower: U+0178 Ÿ → U+00FF ÿ");

    /* --- Sharp-s multi-char expansion --- */
    snobol_to_upper_cp(0x00DF, out, &out_len);
    test_assert(out_len == 2 && out[0] == 0x0053 && out[1] == 0x0053,
                "to_upper: U+00DF ß → {S, S} (two codepoints)");

    /* Sharp-s to_lower: identity (ß is already lowercase) */
    test_assert(snobol_to_lower_cp(0x00DF) == 0x00DF, "to_lower: U+00DF ß → ß (identity)");

    /* --- Latin Extended-A --- */
    /* U+0161 š → U+0160 Š */
    snobol_to_upper_cp(0x0161, out, &out_len);
    test_assert(out_len == 1 && out[0] == 0x0160, "to_upper: U+0161 š → U+0160 Š");

    /* U+0160 Š → U+0161 š */
    test_assert(snobol_to_lower_cp(0x0160) == 0x0161, "to_lower: U+0160 Š → U+0161 š");

    /* U+017E ž → U+017D Ž */
    snobol_to_upper_cp(0x017E, out, &out_len);
    test_assert(out_len == 1 && out[0] == 0x017D, "to_upper: U+017E ž → U+017D Ž");

    /* U+0101 ā → U+0100 Ā */
    snobol_to_upper_cp(0x0101, out, &out_len);
    test_assert(out_len == 1 && out[0] == 0x0100, "to_upper: U+0101 ā → U+0100 Ā");

    /* --- Out-of-range identity --- */
    snobol_to_upper_cp(0x0400, out, &out_len); /* Cyrillic — beyond coverage */
    test_assert(out_len == 1 && out[0] == 0x0400, "to_upper: U+0400 (Cyrillic) → identity");

    test_assert(snobol_to_lower_cp(0x0400) == 0x0400, "to_lower: U+0400 (Cyrillic) → identity");

    /* --- Non-case characters: identity --- */
    snobol_to_upper_cp(0x00D7, out, &out_len); /* × multiplication sign */
    test_assert(out_len == 1 && out[0] == 0x00D7, "to_upper: U+00D7 × → identity");

    snobol_to_upper_cp(0x00F7, out, &out_len); /* ÷ division sign */
    test_assert(out_len == 1 && out[0] == 0x00F7, "to_upper: U+00F7 ÷ → identity");
}

