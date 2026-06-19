#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/include/snobol/snobol.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0) return 0;

    snobol_context_t *ctx = snobol_context_create();
    if (!ctx) return 0;

    char *error = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile(
        ctx, (const char *)data, size, &error);

    if (pat) {
        snobol_pattern_free(pat);
    } else {
        free(error);
    }

    snobol_context_destroy(ctx);
    return 0;
}
