#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/include/snobol/snobol.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 4) return 0;

    uint32_t split;
    if (size > 1024) {
        split = 256;
    } else {
        split = size / 2;
    }
    if (split == 0) split = 1;
    if (split >= size) split = size / 2;
    if (split == 0) return 0;

    size_t pat_len = split;
    size_t sub_len = size - split;

    snobol_context_t *ctx = snobol_context_create();
    if (!ctx) return 0;

    char *error = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile(
        ctx, (const char *)data, pat_len, &error);

    if (pat) {
        const char *subject = (const char *)(data + split);
        snobol_match_t *result = snobol_pattern_match(pat, subject, sub_len);
        if (result) {
            snobol_match_free(result);
        }
        snobol_pattern_free(pat);
    } else {
        free(error);
    }

    snobol_context_destroy(ctx);
    return 0;
}
