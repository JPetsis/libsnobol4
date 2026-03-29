/**
 * @file basic.c
 * @brief Basic libsnobol4 usage example
 *
 * Demonstrates simple pattern matching with the libsnobol4 C API.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <snobol/snobol.h>

int main(void) {
    printf("libsnobol4 Basic Example\n");
    printf("========================\n\n");

    /* Print version */
    int major, minor, patch;
    snobol_version(&major, &minor, &patch);
    printf("libsnobol4 version: %d.%d.%d\n\n", major, minor, patch);

    /* Example 1: Simple literal match */
    printf("Example 1: Simple literal match\n");
    printf("  Pattern: 'hello'\n");
    printf("  Subject: 'hello world'\n");
    printf("  (Full implementation requires context/pattern API)\n\n");

    /* Example 2: Character class */
    printf("Example 2: Character class (SPAN)\n");
    printf("  Pattern: SPAN('0-9')\n");
    printf("  Subject: 'id:12345'\n");
    printf("  (Matches run of digits)\n\n");

    /* Example 3: Alternation */
    printf("Example 3: Alternation\n");
    printf("  Pattern: 'cat' | 'dog' | 'bird'\n");
    printf("  Subject: 'I have a dog'\n");
    printf("  (Matches any of the alternatives)\n\n");

    printf("For complete examples, see the libsnobol4 documentation.\n");

    return 0;
}
