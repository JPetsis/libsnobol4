#include "bench_shared.h"
#include <string.h>
#include <stdio.h>

static const char *subject_csv =
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status,"
    "id,name,email,age,status,id,name,email,age,status";

#define TOKEN_COUNT 100

/* Tokenize CSV by matching each field as a literal at successive positions.
 * Uses anchored literal matches ('id', 'name', etc.) to simulate
 * advancing through delimited fields — exercises repeated match dispatch. */
static const char *field_labels[] = {
    "id", "name", "email", "age", "status"
};
#define NFIELD (sizeof(field_labels) / sizeof(field_labels[0]))

static snobol_pattern_t* compile_field_pattern(snobol_context_t *ctx, const char *field) {
    /* Build pattern "^'field'" — anchored match */
    size_t slen = strlen(field);
    char *buf = (char *)malloc(slen + 8);
    if (!buf) return NULL;
    buf[0] = '^';
    buf[1] = '\'';
    memcpy(buf + 2, field, slen);
    buf[2 + slen] = '\'';
    buf[3 + slen] = '\0';
    char *err = NULL;
    snobol_pattern_t *pat = snobol_pattern_compile(ctx, buf, 3 + slen, &err);
    if (!pat) { fprintf(stderr, "snobol compile(%s) failed: %s\n", buf, err ? err : "??"); free(err); free(buf); return NULL; }
    free(err);
    free(buf);
    return pat;
}

void bench_tokenization_suite(bench_results_t *out) {
    size_t subj_len = strlen(subject_csv);

    /* snobol4: anchored literal match in a loop — simulates tokenization */
    {
        snobol_context_t *ctx = snobol_context_create();
        /* Compile one pattern for each field label */
        snobol_pattern_t *pats[NFIELD];
        for (size_t i = 0; i < NFIELD; i++) {
            pats[i] = compile_field_pattern(ctx, field_labels[i]);
            if (!pats[i]) { snobol_context_destroy(ctx); return; }
        }

        int64_t start = bench_ns();
        for (int iter = 0; iter < 1000; iter++) {
            size_t pos = 0;
            for (int t = 0; t < 100 && pos < subj_len; t++) {
                int fi = t % NFIELD;
                snobol_match_t *m = snobol_pattern_match(pats[fi], subject_csv + pos, subj_len - pos);
                bool ok = snobol_match_success(m);
                if (ok) {
                    const char *out_str = snobol_match_get_output(m, NULL);
                    size_t match_len = out_str ? strlen(out_str) : 0;
                    pos += match_len;
                    /* skip following comma */
                    if (pos < subj_len && subject_csv[pos] == ',') pos++;
                } else {
                    /* skip one char and try next position */
                    if (pos < subj_len) pos++;
                }
                snobol_match_free(m);
            }
        }
        out->snobol_ns = bench_ns() - start;

        for (size_t i = 0; i < NFIELD; i++) snobol_pattern_free(pats[i]);
        snobol_context_destroy(ctx);
    }

#ifdef HAVE_PCRE2
    /* pcre2: tokenize via repeated anchored match advancing through subject */
    {
        pcre2_code *re[NFIELD];
        for (size_t i = 0; i < NFIELD; i++) {
            int errcode;
            PCRE2_SIZE erroffset;
            /* Build anchored pattern: ^(id|name|...) */
            char buf[64];
            int n = snprintf(buf, sizeof(buf), "^(%s)", field_labels[i]);
            re[i] = pcre2_compile((PCRE2_SPTR)buf, (PCRE2_SIZE)n, 0, &errcode, &erroffset, NULL);
            if (!re[i]) { fprintf(stderr, "pcre2 compile failed\n"); out->pcre2_ns = -1; return; }
        }
        pcre2_match_data *md[NFIELD];
        for (size_t i = 0; i < NFIELD; i++)
            md[i] = pcre2_match_data_create_from_pattern(re[i], NULL);

        int64_t start = bench_ns();
        for (int iter = 0; iter < 1000; iter++) {
            size_t pos = 0;
            for (int t = 0; t < 100 && pos < subj_len; t++) {
                int fi = t % NFIELD;
                int rc = pcre2_match(re[fi], (PCRE2_SPTR)subject_csv, subj_len, pos, 0, md[fi], NULL);
                if (rc >= 0) {
                    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(md[fi]);
                    pos = ovector[1];
                    if (pos < subj_len && subject_csv[pos] == ',') pos++;
                } else {
                    if (pos < subj_len) pos++;
                }
            }
        }
        out->pcre2_ns = bench_ns() - start;

        for (size_t i = 0; i < NFIELD; i++) {
            pcre2_match_data_free(md[i]);
            pcre2_code_free(re[i]);
        }
    }
#else
    out->pcre2_ns = -1;
#endif

    out->label = "Tokenization (100 tokens x 1000 loops)";
}
