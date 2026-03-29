/**
 * @file test_ast.c
 * @brief Tests for AST version and memory management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "snobol/ast.h"

/* Test framework functions (from test_runner.c) */
extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

static void test_ast_version(void) {
    test_suite("AST: version information");
    
    snobol_ast_version_t version = snobol_ast_get_version();
    
    test_assert(version.major == 1, "major version is 1");
    test_assert(version.minor == 0, "minor version is 0");
    test_assert(version.patch == 0, "patch version is 0");
    test_assert(version.string != NULL, "version string is not NULL");
    test_assert(strcmp(version.string, "1.0.0") == 0, "version string is '1.0.0'");
}

static void test_ast_version_string(void) {
    test_suite("AST: version string");
    
    const char* version_str = snobol_ast_version_string();
    
    test_assert(version_str != NULL, "version string is not NULL");
    test_assert(strcmp(version_str, "1.0.0") == 0, "version string matches");
}

static void test_ast_version_check_compatible(void) {
    test_suite("AST: version check (compatible)");
    
    /* Same major, same minor - compatible */
    test_assert(snobol_ast_version_check(1, 0) == true, "v1.0 is compatible with v1.0");
    
    /* Same major, lower minor - compatible */
    test_assert(snobol_ast_version_check(1, 0) == true, "v1.0 is compatible with v1.0");
}

static void test_ast_version_check_incompatible(void) {
    test_suite("AST: version check (incompatible)");
    
    /* Different major - incompatible */
    test_assert(snobol_ast_version_check(2, 0) == false, "v2.0 is NOT compatible with v1.0");
    
    /* Different major - incompatible */
    test_assert(snobol_ast_version_check(0, 9) == false, "v0.9 is NOT compatible with v1.0");
}

static void test_ast_version_macro(void) {
    test_suite("AST: version macro");
    
    test_assert(SNOBOL_AST_VERSION_MAJOR == 1, "SNOBOL_AST_VERSION_MAJOR is 1");
    test_assert(SNOBOL_AST_VERSION_MINOR == 0, "SNOBOL_AST_VERSION_MINOR is 0");
    test_assert(SNOBOL_AST_VERSION_PATCH == 0, "SNOBOL_AST_VERSION_PATCH is 0");
    test_assert(strcmp(SNOBOL_AST_VERSION_STRING, "1.0.0") == 0, "SNOBOL_AST_VERSION_STRING is '1.0.0'");
}

static void test_ast_version_check_macro(void) {
    test_suite("AST: version check macro");
    
    test_assert(SNOBOL_AST_VERSION_CHECK(1, 0) == 1, "macro: v1.0 is compatible");
    test_assert(SNOBOL_AST_VERSION_CHECK(1, 1) == 0, "macro: v1.1 is NOT compatible (future minor)");
    test_assert(SNOBOL_AST_VERSION_CHECK(2, 0) == 0, "macro: v2.0 is NOT compatible");
}

static void test_ast_create_and_free(void) {
    test_suite("AST: create and free");
    
    /* Create a simple literal node */
    ast_node_t* node = snobol_ast_create_lit("test", 4);
    
    test_assert(node != NULL, "create_lit returns non-NULL");
    test_assert(node->type == AST_LITERAL, "node type is AST_LITERAL");
    test_assert(node->data.literal.len == 4, "literal length is 4");
    test_assert(memcmp(node->data.literal.text, "test", 4) == 0, "literal text matches");
    
    /* Free the node */
    snobol_ast_free(node);
    test_assert(true, "snobol_ast_free completes without crash");
}

static void test_ast_create_and_free_complex(void) {
    test_suite("AST: create and free complex tree");
    
    /* Create: 'A' | 'B' */
    ast_node_t* left = snobol_ast_create_lit("A", 1);
    ast_node_t* right = snobol_ast_create_lit("B", 1);
    ast_node_t* alt = snobol_ast_create_alt(left, right);
    
    test_assert(alt != NULL, "create_alt returns non-NULL");
    test_assert(alt->type == AST_ALT, "node type is AST_ALT");
    test_assert(alt->data.alt.left == left, "left child is correct");
    test_assert(alt->data.alt.right == right, "right child is correct");
    
    /* Free the entire tree (should free children recursively) */
    snobol_ast_free(alt);
    test_assert(true, "snobol_ast_free frees entire tree without crash");
}

static void test_ast_null_safety(void) {
    test_suite("AST: NULL safety");
    
    /* snobol_ast_free should handle NULL gracefully */
    snobol_ast_free(NULL);
    test_assert(true, "snobol_ast_free(NULL) does not crash");
}

static void test_ast_type_names(void) {
    test_suite("AST: type names");
    
    const char* lit_name = snobol_ast_type_name(AST_LITERAL);
    test_assert(lit_name != NULL, "type_name returns non-NULL for LITERAL");
    test_assert(strcmp(lit_name, "LITERAL") == 0, "LITERAL type name is correct");
    
    const char* alt_name = snobol_ast_type_name(AST_ALT);
    test_assert(alt_name != NULL, "type_name returns non-NULL for ALT");
    test_assert(strcmp(alt_name, "ALT") == 0, "ALT type name is correct");
    
    const char* unknown_name = snobol_ast_type_name((ast_type_t)999);
    test_assert(unknown_name != NULL, "type_name returns non-NULL for unknown type");
    test_assert(strcmp(unknown_name, "UNKNOWN") == 0, "unknown type name is 'UNKNOWN'");
}

static void test_ast_create_all_types(void) {
    test_suite("AST: create all node types");
    
    /* Test each creation function */
    ast_node_t* node;
    
    node = snobol_ast_create_lit("test", 4);
    test_assert(node != NULL && node->type == AST_LITERAL, "create_lit works");
    snobol_ast_free(node);
    
    ast_node_t* left = snobol_ast_create_lit("A", 1);
    ast_node_t* right = snobol_ast_create_lit("B", 1);
    node = snobol_ast_create_alt(left, right);
    test_assert(node != NULL && node->type == AST_ALT, "create_alt works");
    snobol_ast_free(node);
    
    /* Test concat separately with fresh nodes */
    left = snobol_ast_create_lit("A", 1);
    right = snobol_ast_create_lit("B", 1);
    ast_node_t** parts = (ast_node_t**)malloc(2 * sizeof(ast_node_t*));
    parts[0] = left;
    parts[1] = right;
    node = snobol_ast_create_concat(parts, 2);
    test_assert(node != NULL && node->type == AST_CONCAT, "create_concat works");
    snobol_ast_free(node);  /* This frees parts array AND children */
    
    node = snobol_ast_create_arbno(snobol_ast_create_lit("X", 1));
    test_assert(node != NULL && node->type == AST_ARBNO, "create_arbno works");
    snobol_ast_free(node);
    
    node = snobol_ast_create_span("a-z", 3);
    test_assert(node != NULL && node->type == AST_SPAN, "create_span works");
    snobol_ast_free(node);
    
    node = snobol_ast_create_any("aeiou", 5);
    test_assert(node != NULL && node->type == AST_ANY, "create_any works");
    snobol_ast_free(node);
    
    node = snobol_ast_create_cap(1, snobol_ast_create_lit("X", 1));
    test_assert(node != NULL && node->type == AST_CAP, "create_cap works");
    snobol_ast_free(node);
    
    node = snobol_ast_create_repeat(snobol_ast_create_lit("X", 1), 2, 5);
    test_assert(node != NULL && node->type == AST_REPETITION, "create_repeat works");
    snobol_ast_free(node);
    
    node = snobol_ast_create_label("LOOP", snobol_ast_create_lit("X", 1));
    test_assert(node != NULL && node->type == AST_LABEL, "create_label works");
    snobol_ast_free(node);
}

void test_ast_suite(void) {
    test_ast_version();
    test_ast_version_string();
    test_ast_version_check_compatible();
    test_ast_version_check_incompatible();
    test_ast_version_macro();
    test_ast_version_check_macro();
    test_ast_create_and_free();
    test_ast_create_and_free_complex();
    test_ast_null_safety();
    test_ast_type_names();
    test_ast_create_all_types();
}
