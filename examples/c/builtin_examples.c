/**
 * @file builtin_examples.c
 * @brief libsnobol4 built-in string and comparison function examples
 *
 * Demonstrates the C API for the built-in string transformation functions
 * (SIZE, TRIM, DUPL, REVERSE, SUBSTR, REPLACE, REPLACE_CHAR, LPAD, RPAD,
 * CHAR, ORD, UPPER, LOWER) and comparison predicates (IDENT, DIFFER,
 * LEXEQ, LEXLT, LEXGT, INTEGER, REAL, NUMERIC).
 *
 * All string functions operate on UTF-8 encoded bytes.
 * Codepoint semantics: positions and lengths are in Unicode codepoints,
 * not bytes, unless otherwise noted.
 *
 * Compile:
 *   gcc -o builtin_examples builtin_examples.c -lsnobol4
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <snobol/vm.h>        /* snobol_buf */
#include <snobol/string_fn.h> /* string built-ins */
#include <snobol/type_fn.h>   /* comparison built-ins */

/* Helper: print a snobol_buf as a null-terminated C string */
static void print_buf(const char *label, const snobol_buf *buf)
{
    printf("  %-20s \"%.*s\"\n", label, (int)buf->len, buf->data);
}

int main(void)
{
    snobol_buf out;
    snobol_buf_init(&out);

    printf("libsnobol4 Built-in Function Examples\n");
    printf("======================================\n\n");

    /* ------------------------------------------------------------------ */
    /* SIZE: count Unicode codepoints                                      */
    /* ------------------------------------------------------------------ */
    printf("SIZE\n");
    {
        const char *ascii   = "hello";
        const char *unicode = "caf\xC3\xA9";  /* "café" – 4 codepoints, 5 bytes */
        printf("  SIZE(\"hello\")  = %zu  (bytes: %zu)\n",
               snobol_size(ascii, 5), (size_t)5);
        printf("  SIZE(\"café\")   = %zu  (bytes: %zu)\n",
               snobol_size(unicode, 5), (size_t)5);
    }
    printf("\n");

    /* ------------------------------------------------------------------ */
    /* TRIM: remove trailing whitespace                                    */
    /* ------------------------------------------------------------------ */
    printf("TRIM\n");
    {
        const char *s = "hello   \t\n";
        snobol_trim(s, strlen(s), &out);
        print_buf("TRIM(\"hello   \\t\\n\")", &out);
    }
    printf("\n");

    /* ------------------------------------------------------------------ */
    /* DUPL: duplicate a string n times                                    */
    /* ------------------------------------------------------------------ */
    printf("DUPL\n");
    {
        snobol_dupl("ab", 2, 4, &out);
        print_buf("DUPL(\"ab\", 4)", &out);
    }
    printf("\n");

    /* ------------------------------------------------------------------ */
    /* REVERSE: reverse by codepoints                                      */
    /* ------------------------------------------------------------------ */
    printf("REVERSE\n");
    {
        snobol_reverse("hello", 5, &out);
        print_buf("REVERSE(\"hello\")", &out);

        /* Unicode: "café" reversed → "éfac" */
        const char *cafe = "caf\xC3\xA9";
        snobol_reverse(cafe, 5, &out);
        printf("  %-20s (%zu bytes)\n", "REVERSE(\"café\")", out.len);
    }
    printf("\n");

    /* ------------------------------------------------------------------ */
    /* SUBSTR: extract by 1-based codepoint position                      */
    /* ------------------------------------------------------------------ */
    printf("SUBSTR\n");
    {
        snobol_substr("hello world", 7, 5, 5, &out);
        print_buf("SUBSTR(\"hello world\",7,5)", &out);
    }
    printf("\n");

    /* ------------------------------------------------------------------ */
    /* REPLACE: replace all occurrences                                    */
    /* ------------------------------------------------------------------ */
    printf("REPLACE\n");
    {
        const char *s    = "foo bar foo baz foo";
        const char *from = "foo";
        const char *to   = "qux";
        snobol_replace(s, strlen(s), from, strlen(from), to, strlen(to), &out);
        print_buf("REPLACE(str, \"foo\",\"qux\")", &out);
    }
    printf("\n");

    /* ------------------------------------------------------------------ */
    /* REPLACE_CHAR: character translation (like tr)                       */
    /* ------------------------------------------------------------------ */
    printf("REPLACE_CHAR\n");
    {
        const char *s    = "hello world";
        const char *from = "aeiou";
        const char *to   = "AEIOU";
        snobol_replace_char(s, strlen(s), from, strlen(from), to, strlen(to), &out);
        print_buf("replace_char(str,vowels)", &out);
    }
    printf("\n");

    /* ------------------------------------------------------------------ */
    /* LPAD / RPAD                                                         */
    /* ------------------------------------------------------------------ */
    printf("LPAD / RPAD\n");
    {
        snobol_lpad("hi", 2, 10, ' ', &out);
        print_buf("LPAD(\"hi\", 10, ' ')", &out);

        snobol_rpad("hi", 2, 10, '-', &out);
        print_buf("RPAD(\"hi\", 10, '-')", &out);
    }
    printf("\n");

    /* ------------------------------------------------------------------ */
    /* CHAR / ORD                                                          */
    /* ------------------------------------------------------------------ */
    printf("CHAR / ORD\n");
    {
        snobol_char_fn(65, &out);   /* 'A' */
        print_buf("CHAR(65)", &out);

        snobol_char_fn(0x1F98A, &out); /* 🦊 fox emoji */
        printf("  %-20s (%zu bytes)\n", "CHAR(0x1F98A) '🦊'", out.len);

        uint32_t cp = 0;
        snobol_ord("A", 1, &cp);
        printf("  %-20s %u\n", "ORD(\"A\")", cp);

        snobol_ord("\xF0\x9F\xA6\x8A", 4, &cp); /* 🦊 */
        printf("  %-20s %u (0x%X)\n", "ORD(\"🦊\")", cp, cp);
    }
    printf("\n");

    /* ------------------------------------------------------------------ */
    /* UPPER / LOWER (v1: ASCII fast path)                                */
    /* ------------------------------------------------------------------ */
    printf("UPPER / LOWER  (v1.0: ASCII a-z/A-Z only)\n");
    {
        snobol_upper("Hello World!", 12, &out);
        print_buf("UPPER(\"Hello World!\")", &out);

        snobol_lower("Hello World!", 12, &out);
        print_buf("LOWER(\"Hello World!\")", &out);
    }
    printf("\n");

    /* ------------------------------------------------------------------ */
    /* Comparison predicates                                               */
    /* ------------------------------------------------------------------ */
    printf("Comparison predicates\n");
    {
        printf("  IDENT(\"abc\",\"abc\")   = %s\n",
               snobol_ident("abc", 3, "abc", 3) ? "true" : "false");
        printf("  DIFFER(\"abc\",\"xyz\")  = %s\n",
               snobol_differ("abc", 3, "xyz", 3) ? "true" : "false");
        printf("  LEXLT(\"abc\",\"abd\")   = %s\n",
               snobol_lexlt("abc", 3, "abd", 3) ? "true" : "false");
        printf("  LEXGT(\"xyz\",\"abc\")   = %s\n",
               snobol_lexgt("xyz", 3, "abc", 3) ? "true" : "false");
    }
    printf("\n");

    /* ------------------------------------------------------------------ */
    /* Type predicates                                                     */
    /* ------------------------------------------------------------------ */
    printf("Type predicates\n");
    {
        printf("  INTEGER(\"42\")        = %s\n",
               snobol_integer("42", 2) ? "true" : "false");
        printf("  INTEGER(\"42.5\")      = %s\n",
               snobol_integer("42.5", 4) ? "true" : "false");
        printf("  REAL(\"3.14\")         = %s\n",
               snobol_real("3.14", 4) ? "true" : "false");
        printf("  REAL(\"1.2e-3\")       = %s\n",
               snobol_real("1.2e-3", 6) ? "true" : "false");
        printf("  NUMERIC(\"100\")       = %s\n",
               snobol_numeric("100", 3) ? "true" : "false");
        printf("  NUMERIC(\"hello\")     = %s\n",
               snobol_numeric("hello", 5) ? "true" : "false");
    }
    printf("\n");

    snobol_buf_free(&out);
    return 0;
}

