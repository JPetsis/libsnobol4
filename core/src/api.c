/**
 * @file api.c
 * @brief High-level public C API for libsnobol4
 *
 * Implements snobol_context_t, snobol_pattern_t, and snobol_match_t along
 * with their lifecycle and query functions declared in snobol/snobol.h.
 *
 * The pipeline for pattern compilation is:
 *   source text → snobol_lexer → snobol_parser (AST) → compile_ast_to_bytecode_c
 *
 * Pattern matching is done via vm_run().
 */

#include "snobol/snobol.h"
#include "snobol/snobol_internal.h"
#include "snobol/lexer.h"
#include "snobol/parser.h"
#include "snobol/ast.h"
#include "snobol/compiler.h"
#include "snobol/vm.h"

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Opaque type definitions
 * --------------------------------------------------------------------------- */

struct snobol_context {
    int _reserved; /* Future: pattern registry, allocator config, etc. */
};

struct snobol_pattern {
    uint8_t *bc;
    size_t   bc_len;
    bool     case_insensitive; /* stored for future JIT dispatch guard */
};

/* Maximum named variables returned from a match */
#define API_MAX_VARS 64

struct snobol_match {
    bool    success;

    /* Output buffer (from OP_EMIT_* instructions) */
    char   *output;
    size_t  output_len;

    /* Named capture variables: var_values[i] points into subject or is a
     * NUL-terminated copy; var_lens[i] is the byte length.
     * Variable names are stored as decimal strings "1", "2", …, "64".
     * Index 0 corresponds to variable name "1" (1-based). */
    char   *var_values[API_MAX_VARS];
    size_t  var_lens[API_MAX_VARS];
    int     var_count;
};

/* ---------------------------------------------------------------------------
 * Context lifecycle
 * --------------------------------------------------------------------------- */

snobol_context_t* snobol_context_create(void) {
    snobol_context_t *ctx = (snobol_context_t *)snobol_malloc(sizeof(snobol_context_t));
    if (ctx) ctx->_reserved = 0;
    return ctx;
}

void snobol_context_destroy(snobol_context_t* ctx) {
    if (ctx) snobol_free(ctx);
}

/* ---------------------------------------------------------------------------
 * Pattern lifecycle
 * --------------------------------------------------------------------------- */

/**
 * Core compilation helper: lex → parse → compile → allocate pattern.
 */
static snobol_pattern_t* do_compile(const char* source, size_t len,
                                    bool case_insensitive, char** error)
{
    if (error) *error = NULL;

    snobol_lexer_t  *lexer  = snobol_lexer_create(source, len);
    snobol_parser_t *parser = snobol_parser_create();

    ast_node_t *ast = snobol_parser_parse(parser, lexer);

    if (!ast || snobol_parser_has_error(parser)) {
        const char *msg = snobol_parser_get_error(parser);
        if (!msg) msg = "unknown parse error";
        if (error) {
            size_t mlen = strlen(msg) + 1;
            *error = (char *)malloc(mlen);
            if (*error) memcpy(*error, msg, mlen);
        }
        snobol_ast_free(ast);
        snobol_parser_destroy(parser);
        snobol_lexer_destroy(lexer);
        return NULL;
    }

    uint8_t *bc   = NULL;
    size_t   bc_len = 0;
    int rc = compile_ast_to_bytecode_c(ast, case_insensitive, &bc, &bc_len);
    snobol_ast_free(ast);
    snobol_parser_destroy(parser);
    snobol_lexer_destroy(lexer);

    if (rc != 0) {
        if (error) {
            const char *msg = "compilation failed";
            size_t mlen = strlen(msg) + 1;
            *error = (char *)malloc(mlen);
            if (*error) memcpy(*error, msg, mlen);
        }
        return NULL;
    }

    snobol_pattern_t *pat = (snobol_pattern_t *)snobol_malloc(sizeof(snobol_pattern_t));
    if (!pat) {
        compiler_free(bc);
        if (error) {
            *error = (char *)malloc(32);
            if (*error) memcpy(*error, "out of memory", 14);
        }
        return NULL;
    }
    pat->bc              = bc;
    pat->bc_len          = bc_len;
    pat->case_insensitive = case_insensitive;
    return pat;
}

snobol_pattern_t* snobol_pattern_compile_ex(snobol_context_t* ctx,
                                             const char* source, size_t len,
                                             uint32_t flags, char** error)
{
    (void)ctx; /* context owns the pattern conceptually; no registry yet */
    bool case_insensitive = (flags & SNOBOL_FLAG_CASE_INSENSITIVE) != 0;
    /* Unknown flag bits are intentionally ignored (forward-compatible). */
    return do_compile(source, len, case_insensitive, error);
}

snobol_pattern_t* snobol_pattern_compile(snobol_context_t* ctx,
                                          const char* source, size_t len,
                                          char** error)
{
    return snobol_pattern_compile_ex(ctx, source, len, 0, error);
}

void snobol_pattern_free(snobol_pattern_t* pattern) {
    if (!pattern) return;
    compiler_free(pattern->bc);
    snobol_free(pattern);
}

/* ---------------------------------------------------------------------------
 * Pattern matching
 * --------------------------------------------------------------------------- */

snobol_match_t* snobol_pattern_match(snobol_pattern_t* pattern,
                                      const char* subject, size_t len)
{
    if (!pattern || !subject) return NULL;

    snobol_match_t *m = (snobol_match_t *)snobol_malloc(sizeof(snobol_match_t));
    if (!m) return NULL;
    memset(m, 0, sizeof(snobol_match_t));

    /* Set up output buffer */
    snobol_buf out_buf = {0};
    snobol_buf_init(&out_buf);

    /* Initialise VM */
    VM vm;
    memset(&vm, 0, sizeof(VM));
    vm.bc     = pattern->bc;
    vm.bc_len = pattern->bc_len;
    vm.s      = subject;
    vm.len    = len;
    vm.out    = &out_buf;

    /* TODO(jit-case-fold): when case_insensitive is set, JIT is disabled;
     * the interpreter handles case-folded char class matching directly via
     * the charclass case_insensitive flag stored in the bytecode. */

    bool ok = vm_run(&vm);
    m->success = ok;

    if (ok && out_buf.len > 0) {
        m->output = (char *)snobol_malloc(out_buf.len + 1);
        if (m->output) {
            memcpy(m->output, out_buf.data, out_buf.len);
            m->output[out_buf.len] = '\0';
            m->output_len = out_buf.len;
        }
    }

    /* Copy named variables (1-based: var_start[0] = variable "1") */
    int n = (int)vm.var_count;
    if (n > API_MAX_VARS) n = API_MAX_VARS;
    m->var_count = n;
    for (int i = 0; i < n; i++) {
        size_t vs = vm.var_start[i];
        size_t ve = vm.var_end[i];
        if (ve > vs && ve <= len) {
            size_t vlen = ve - vs;
            m->var_values[i] = (char *)snobol_malloc(vlen + 1);
            if (m->var_values[i]) {
                memcpy(m->var_values[i], subject + vs, vlen);
                m->var_values[i][vlen] = '\0';
                m->var_lens[i] = vlen;
            }
        }
    }

    snobol_buf_free(&out_buf);
    vm_free_labels(&vm);
    return m;
}

void snobol_match_free(snobol_match_t* match) {
    if (!match) return;
    snobol_free(match->output);
    for (int i = 0; i < API_MAX_VARS; i++) {
        snobol_free(match->var_values[i]);
    }
    snobol_free(match);
}

/* ---------------------------------------------------------------------------
 * Match result access
 * --------------------------------------------------------------------------- */

bool snobol_match_success(snobol_match_t* match) {
    return match && match->success;
}

const char* snobol_match_get_output(snobol_match_t* match, size_t* len) {
    if (!match || !match->success) {
        if (len) *len = 0;
        return NULL;
    }
    if (len) *len = match->output_len;
    return match->output ? match->output : "";
}

const char* snobol_match_get_variable(snobol_match_t* match, const char* name, size_t* len) {
    if (!match || !match->success || !name) {
        if (len) *len = 0;
        return NULL;
    }
    /* Variable names are 1-based decimal integers: "1" → index 0 */
    char *end;
    long idx = strtol(name, &end, 10);
    if (end == name || idx < 1 || idx > API_MAX_VARS) {
        if (len) *len = 0;
        return NULL;
    }
    int i = (int)(idx - 1);
    if (i >= match->var_count || !match->var_values[i]) {
        if (len) *len = 0;
        return NULL;
    }
    if (len) *len = match->var_lens[i];
    return match->var_values[i];
}

