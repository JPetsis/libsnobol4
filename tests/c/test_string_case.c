/*
 * test_string_case.c – Tests for UPPER and LOWER
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "snobol/vm.h"
#include "snobol/string_fn.h"

extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

void test_string_case_suite(void) {
    test_suite("String: UPPER / LOWER");

    snobol_buf b = {0};
    snobol_buf_init(&b);

    /* --- UPPER (ASCII fast path) --- */
    (void)snobol_upper("hello", 5, &b);
    test_assert(b.len == 5 && memcmp(b.data, "HELLO", 5) == 0,
                "UPPER: 'hello' → 'HELLO'");

    (void)snobol_upper("ALREADY", 7, &b);
    test_assert(b.len == 7 && memcmp(b.data, "ALREADY", 7) == 0,
                "UPPER: already uppercase unchanged");

    (void)snobol_upper("Hello World!", 12, &b);
    test_assert(b.len == 12 && memcmp(b.data, "HELLO WORLD!", 12) == 0,
                "UPPER: mixed case → all uppercase");

    (void)snobol_upper("", 0, &b);
    test_assert(b.len == 0, "UPPER: empty string");

    /* Non-ASCII bytes are preserved unchanged (v1 ASCII-only) */
    const char *with_utf8 = "caf\xC3\xA9"; /* café */
    (void)snobol_upper(with_utf8, 5, &b);
    test_assert(b.len == 5 &&
                (unsigned char)b.data[0] == 'C' &&
                (unsigned char)b.data[1] == 'A' &&
                (unsigned char)b.data[2] == 'F' &&
                (unsigned char)b.data[3] == 0xC3 &&
                (unsigned char)b.data[4] == 0xA9,
                "UPPER: non-ASCII bytes preserved (v1 ASCII-only limitation)");

    /* --- LOWER (ASCII fast path) --- */
    (void)snobol_lower("HELLO", 5, &b);
    test_assert(b.len == 5 && memcmp(b.data, "hello", 5) == 0,
                "LOWER: 'HELLO' → 'hello'");

    (void)snobol_lower("already", 7, &b);
    test_assert(b.len == 7 && memcmp(b.data, "already", 7) == 0,
                "LOWER: already lowercase unchanged");

    (void)snobol_lower("Hello World!", 12, &b);
    test_assert(b.len == 12 && memcmp(b.data, "hello world!", 12) == 0,
                "LOWER: mixed case → all lowercase");

    (void)snobol_lower("", 0, &b);
    test_assert(b.len == 0, "LOWER: empty string");

    snobol_buf_free(&b);
}

