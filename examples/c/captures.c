/**
 * @file captures.c
 * @brief libsnobol4 capture example
 *
 * Demonstrates pattern captures with the libsnobol4 C API.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <snobol/snobol.h>

int main(void) {
    printf("libsnobol4 Captures Example\n");
    printf("===========================\n\n");

    /* Example: Capture digits from a string */
    printf("Example: Extract digits from 'id:12345'\n");
    printf("  Pattern: 'id:' @r1(SPAN('0-9'))\n");
    printf("  Expected capture: v1 = '12345'\n\n");

    /* Example: Multiple captures */
    printf("Example: Multiple captures\n");
    printf("  Pattern: @r1(SPAN('a-z')) ':' @r2(SPAN('0-9'))\n");
    printf("  Subject: 'abc:123'\n");
    printf("  Expected: v1 = 'abc', v2 = '123'\n\n");

    /* Example: Nested captures */
    printf("Example: Nested captures\n");
    printf("  Pattern: @r1('hello' @r2('world'))\n");
    printf("  Subject: 'hello world'\n");
    printf("  Expected: v1 = 'hello world', v2 = 'world'\n\n");

    printf("Capture registers are accessed via snobol_match_get_variable().\n");
    printf("Register N is accessed as variable 'vN' (e.g., v0, v1, v2).\n");

    return 0;
}
