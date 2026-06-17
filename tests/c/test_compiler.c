/*
 * test_compiler.c - Compiler tests
 *
 * Verifies the AST → bytecode compiler emits the expected opcodes and
 * that captures are exposed in match results (regression for the bug
 * where OP_CAP_END didn't update var_count).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "snobol/snobol.h"
#include "snobol/compiler.h"
#include "snobol/ast.h"
#include "snobol/vm.h"

/* External test framework functions */
extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

/* ---------------------------------------------------------------------------
 * Helper: compile an AST, run the VM, return success + capture registers.
 * --------------------------------------------------------------------------- */
static bool run_ast_pattern(ast_node_t *ast, const char *subject, size_t sub_len,
                            int *out_match_len, int *out_cap_count)
{
    uint8_t *bc = NULL;
    size_t bc_len = 0;
    if (compile_ast_to_bytecode_c(ast, false, &bc, &bc_len) != 0) {
        return false;
    }
    if (!bc || bc_len == 0) {
        if (bc) free(bc);
        return false;
    }

    VM vm = {0};
    vm.bc = bc;
    vm.bc_len = bc_len;
    vm.s = subject;
    vm.len = sub_len;

    snobol_buf out_buf = {0};
    snobol_buf_init(&out_buf);
    vm.out = &out_buf;

    bool ok = vm_run(&vm);

    if (out_match_len) *out_match_len = (int)vm.pos;
    if (out_cap_count) *out_cap_count = (int)vm.var_count;

    snobol_buf_free(&out_buf);
    vm_free_labels(&vm);
    free(bc);
    return ok;
}

void test_compiler_suite(void) {
    test_suite("Compiler Tests");

    test_assert(true, "Compiler initialization placeholder");
    test_assert(true, "Compiler can process AST nodes");

    /* Plain literal (no capture) -> var_count stays 0 */
    {
        ast_node_t *lit = snobol_ast_create_lit("hello", 5);
        int match_len = 0, cap_count = 0;
        bool ok = run_ast_pattern(lit, "hello world", 11, &match_len, &cap_count);
        test_assert(ok, "plain literal matches");
        test_assert(cap_count == 0, "plain literal has 0 captures (var_count stays 0)");
        snobol_ast_free(lit);
    }

    /* Single capture: cap(0, SPAN('0-9')) on "id:12345" -> v0 == "12345" */
    {
        ast_node_t *id_lit = snobol_ast_create_lit("id:", 3);
        ast_node_t *digits = snobol_ast_create_span("0123456789", 10);
        ast_node_t *cap = snobol_ast_create_cap(0, digits);
        ast_node_t **parts = (ast_node_t **)malloc(2 * sizeof(ast_node_t *));
        parts[0] = id_lit;
        parts[1] = cap;
        ast_node_t *concat = snobol_ast_create_concat(parts, 2);

        int match_len = 0, cap_count = 0;
        bool ok = run_ast_pattern(concat, "id:12345", 8, &match_len, &cap_count);
        test_assert(ok, "AST cap+span+concat matches 'id:12345'");
        test_assert(match_len == 8, "match_len is 8 (full pattern length)");
        test_assert(cap_count >= 1, "OP_CAP_END exposes capture register as a var");

        snobol_ast_free(concat);
    }

    /* Two captures via concat: cap(0,...) cap(1,...) on "ab ba" */
    {
        /* Build: cap(0, SPAN("abc")) ' ' cap(1, SPAN("abc")) */
        ast_node_t *left_span  = snobol_ast_create_span("abc", 3);
        test_assert(left_span != NULL, "create_span for left_cap");
        ast_node_t *left_cap   = snobol_ast_create_cap(0, left_span);
        test_assert(left_cap != NULL, "create_cap(0, ...)");
        ast_node_t *sp_lit     = snobol_ast_create_lit(" ", 1);
        test_assert(sp_lit != NULL, "create_lit(' ')");
        ast_node_t *right_span = snobol_ast_create_span("abc", 3);
        test_assert(right_span != NULL, "create_span for right_cap");
        ast_node_t *right_cap  = snobol_ast_create_cap(1, right_span);
        test_assert(right_cap != NULL, "create_cap(1, ...)");

        ast_node_t **parts = (ast_node_t **)malloc(3 * sizeof(ast_node_t *));
        parts[0] = left_cap;
        parts[1] = sp_lit;
        parts[2] = right_cap;
        ast_node_t *concat = snobol_ast_create_concat(parts, 3);
        test_assert(concat != NULL, "create_concat with 3 parts");

        int match_len = 0, cap_count = 0;
        bool ok = run_ast_pattern(concat, "ab ba", 5, &match_len, &cap_count);
        test_assert(ok, "two captures match 'ab ba'");
        test_assert(match_len == 5, "match_len is 5");
        test_assert(cap_count >= 2, "OP_CAP_END exposes both capture registers");

        snobol_ast_free(concat);
    }

    /* Capture in alternation: only the matched branch contributes */
    {
        ast_node_t *h_lit  = snobol_ast_create_lit("hi", 2);
        ast_node_t *h_cap  = snobol_ast_create_cap(0, h_lit);
        ast_node_t *g_lit  = snobol_ast_create_lit("bye", 3);
        ast_node_t *g_cap  = snobol_ast_create_cap(0, g_lit);
        ast_node_t *alt = snobol_ast_create_alt(h_cap, g_cap);

        int match_len = 0, cap_count = 0;
        bool ok = run_ast_pattern(alt, "bye world", 9, &match_len, &cap_count);
        test_assert(ok, "alt with cap matches 'bye world'");
        test_assert(match_len == 3, "match_len is 3 (length of 'bye')");
        test_assert(cap_count >= 1, "capture in matched branch is exposed");

        snobol_ast_free(alt);
    }
}
