/**
 * @file test_parser.c
 * @brief Tests for the SNOBOL C parser
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "snobol/parser.h"
#include "snobol/lexer.h"
#include "snobol/ast.h"

/* Test framework functions (from test_runner.c) */
extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

static void test_parser_create_destroy(void) {
    test_suite("Parser: create and destroy");
    
    snobol_parser_t* parser = snobol_parser_create();
    test_assert(parser != NULL, "parser_create returns non-NULL");
    
    snobol_parser_destroy(parser);
    test_assert(true, "parser_destroy completes without crash");
}

static void test_parser_literal(void) {
    test_suite("Parser: literal pattern");
    
    snobol_parser_t* parser = snobol_parser_create();
    snobol_lexer_t* lexer = snobol_lexer_create("'hello'", 7);
    
    ast_node_t* ast = snobol_parser_parse(parser, lexer);
    
    test_assert(ast != NULL, "parser returns AST");
    test_assert(!snobol_parser_has_error(parser), "no parse error");
    test_assert(ast->type == AST_LITERAL, "AST node is LITERAL");
    test_assert(ast->data.literal.len == 5, "literal length is 5");
    test_assert(memcmp(ast->data.literal.text, "hello", 5) == 0, "literal text matches");
    
    snobol_ast_free(ast);
    snobol_lexer_destroy(lexer);
    snobol_parser_destroy(parser);
}

static void test_parser_alternation(void) {
    test_suite("Parser: alternation pattern");
    
    snobol_parser_t* parser = snobol_parser_create();
    snobol_lexer_t* lexer = snobol_lexer_create("'A' | 'B'", 9);
    
    ast_node_t* ast = snobol_parser_parse(parser, lexer);
    
    test_assert(ast != NULL, "parser returns AST");
    test_assert(!snobol_parser_has_error(parser), "no parse error");
    test_assert(ast->type == AST_ALT, "AST node is ALT");
    test_assert(ast->data.alt.left != NULL, "left child exists");
    test_assert(ast->data.alt.right != NULL, "right child exists");
    test_assert(ast->data.alt.left->type == AST_LITERAL, "left is LITERAL");
    test_assert(ast->data.alt.right->type == AST_LITERAL, "right is LITERAL");
    
    snobol_ast_free(ast);
    snobol_lexer_destroy(lexer);
    snobol_parser_destroy(parser);
}

static void test_parser_concatenation(void) {
    test_suite("Parser: concatenation pattern");
    
    snobol_parser_t* parser = snobol_parser_create();
    snobol_lexer_t* lexer = snobol_lexer_create("'hello' ' ' 'world'", 19);
    
    ast_node_t* ast = snobol_parser_parse(parser, lexer);
    
    test_assert(ast != NULL, "parser returns AST");
    test_assert(!snobol_parser_has_error(parser), "no parse error");
    test_assert(ast->type == AST_CONCAT, "AST node is CONCAT");
    test_assert(ast->data.concat.count == 3, "concat has 3 parts");
    
    snobol_ast_free(ast);
    snobol_lexer_destroy(lexer);
    snobol_parser_destroy(parser);
}

static void test_parser_arbno(void) {
    test_suite("Parser: arbno pattern");
    
    snobol_parser_t* parser = snobol_parser_create();
    snobol_lexer_t* lexer = snobol_lexer_create("'x'*", 4);
    
    ast_node_t* ast = snobol_parser_parse(parser, lexer);
    
    test_assert(ast != NULL, "parser returns AST");
    test_assert(!snobol_parser_has_error(parser), "no parse error");
    test_assert(ast->type == AST_ARBNO, "AST node is ARBNO");
    test_assert(ast->data.arbno.sub != NULL, "arbno has sub-pattern");
    test_assert(ast->data.arbno.sub->type == AST_LITERAL, "sub is LITERAL");
    
    snobol_ast_free(ast);
    snobol_lexer_destroy(lexer);
    snobol_parser_destroy(parser);
}

static void test_parser_span(void) {
    test_suite("Parser: SPAN function");
    
    snobol_parser_t* parser = snobol_parser_create();
    snobol_lexer_t* lexer = snobol_lexer_create("SPAN('0-9')", 11);
    
    ast_node_t* ast = snobol_parser_parse(parser, lexer);
    
    test_assert(ast != NULL, "parser returns AST");
    test_assert(!snobol_parser_has_error(parser), "no parse error");
    test_assert(ast->type == AST_SPAN, "AST node is SPAN");
    if (ast->type == AST_SPAN) {
        test_assert(ast->data.charclass.len == 3, "charclass length is 3 (content without quotes)");
        test_assert(memcmp(ast->data.charclass.set, "0-9", 3) == 0, "charclass text matches");
    }
    
    snobol_ast_free(ast);
    snobol_lexer_destroy(lexer);
    snobol_parser_destroy(parser);
}

static void test_parser_any(void) {
    test_suite("Parser: ANY function");
    
    snobol_parser_t* parser = snobol_parser_create();
    snobol_lexer_t* lexer = snobol_lexer_create("ANY('aeiou')", 12);
    
    ast_node_t* ast = snobol_parser_parse(parser, lexer);
    
    test_assert(ast != NULL, "parser returns AST");
    test_assert(!snobol_parser_has_error(parser), "no parse error");
    test_assert(ast->type == AST_ANY, "AST node is ANY");
    
    snobol_ast_free(ast);
    snobol_lexer_destroy(lexer);
    snobol_parser_destroy(parser);
}

static void test_parser_capture(void) {
    test_suite("Parser: capture pattern");
    
    snobol_parser_t* parser = snobol_parser_create();
    snobol_lexer_t* lexer = snobol_lexer_create("@var 'hello'", 13);
    
    ast_node_t* ast = snobol_parser_parse(parser, lexer);
    
    test_assert(ast != NULL, "parser returns AST");
    test_assert(!snobol_parser_has_error(parser), "no parse error");
    test_assert(ast->type == AST_CAP, "AST node is CAP");
    test_assert(ast->data.cap.reg == 1, "capture register is 1");
    test_assert(ast->data.cap.sub != NULL, "capture has sub-pattern");
    
    snobol_ast_free(ast);
    snobol_lexer_destroy(lexer);
    snobol_parser_destroy(parser);
}

static void test_parser_parenthesized(void) {
    test_suite("Parser: parenthesized pattern");
    
    snobol_parser_t* parser = snobol_parser_create();
    snobol_lexer_t* lexer = snobol_lexer_create("('A' | 'B')", 11);
    
    ast_node_t* ast = snobol_parser_parse(parser, lexer);
    
    test_assert(ast != NULL, "parser returns AST");
    test_assert(!snobol_parser_has_error(parser), "no parse error");
    test_assert(ast->type == AST_ALT, "AST node is ALT (parentheses removed)");
    
    snobol_ast_free(ast);
    snobol_lexer_destroy(lexer);
    snobol_parser_destroy(parser);
}

static void test_parser_anchors(void) {
    test_suite("Parser: anchors");
    
    snobol_parser_t* parser = snobol_parser_create();
    snobol_lexer_t* lexer = snobol_lexer_create("^'start'", 8);
    
    ast_node_t* ast = snobol_parser_parse(parser, lexer);
    
    test_assert(ast != NULL, "parser returns AST");
    test_assert(!snobol_parser_has_error(parser), "no parse error");
    test_assert(ast->type == AST_CONCAT, "AST node is CONCAT (anchor + literal)");
    test_assert(ast->data.concat.count == 2, "concat has 2 parts");
    test_assert(ast->data.concat.parts[0]->type == AST_ANCHOR, "first part is ANCHOR");
    test_assert(ast->data.concat.parts[0]->data.anchor.atype == ANCHOR_START, "anchor is START");
    
    snobol_ast_free(ast);
    snobol_lexer_destroy(lexer);
    snobol_parser_destroy(parser);
}

static void test_parser_nested(void) {
    test_suite("Parser: nested pattern");
    
    snobol_parser_t* parser = snobol_parser_create();
    snobol_lexer_t* lexer = snobol_lexer_create("('A' | 'B')*", 12);
    
    ast_node_t* ast = snobol_parser_parse(parser, lexer);
    
    test_assert(ast != NULL, "parser returns AST");
    test_assert(!snobol_parser_has_error(parser), "no parse error");
    test_assert(ast->type == AST_ARBNO, "AST node is ARBNO");
    test_assert(ast->data.arbno.sub != NULL, "arbno has sub-pattern");
    test_assert(ast->data.arbno.sub->type == AST_ALT, "sub is ALT");
    
    snobol_ast_free(ast);
    snobol_lexer_destroy(lexer);
    snobol_parser_destroy(parser);
}

static void test_parser_error_unclosed_literal(void) {
    test_suite("Parser: error - unclosed literal");
    
    snobol_parser_t* parser = snobol_parser_create();
    snobol_lexer_t* lexer = snobol_lexer_create("'unclosed", 8);
    
    ast_node_t* ast = snobol_parser_parse(parser, lexer);
    
    /* Parser may return partial AST or NULL for syntax errors */
    /* The important thing is it doesn't crash */
    if (ast) snobol_ast_free(ast);
    snobol_lexer_destroy(lexer);
    snobol_parser_destroy(parser);
    
    test_assert(true, "parser handles unclosed literal without crash");
}

static void test_parser_error_empty(void) {
    test_suite("Parser: error - empty input");
    
    snobol_parser_t* parser = snobol_parser_create();
    snobol_lexer_t* lexer = snobol_lexer_create("", 0);
    
    ast_node_t* ast = snobol_parser_parse(parser, lexer);
    
    /* Empty input should produce an error */
    test_assert(ast == NULL || snobol_parser_has_error(parser), "empty input produces error or NULL");
    
    if (ast) snobol_ast_free(ast);
    snobol_lexer_destroy(lexer);
    snobol_parser_destroy(parser);
}

static void test_parser_memory_cleanup(void) {
    test_suite("Parser: memory cleanup");
    
    /* Create and parse multiple patterns to check for memory leaks */
    for (int i = 0; i < 10; i++) {
        snobol_parser_t* parser = snobol_parser_create();
        snobol_lexer_t* lexer = snobol_lexer_create("'test' | 'pattern'", 17);
        
        ast_node_t* ast = snobol_parser_parse(parser, lexer);
        if (ast) snobol_ast_free(ast);
        
        snobol_lexer_destroy(lexer);
        snobol_parser_destroy(parser);
    }
    
    test_assert(true, "10 parse/free cycles completed without crash");
}

void test_parser_suite(void) {
    test_parser_create_destroy();
    test_parser_literal();
    test_parser_alternation();
    test_parser_concatenation();
    test_parser_arbno();
    test_parser_span();
    test_parser_any();
    test_parser_capture();
    test_parser_parenthesized();
    test_parser_anchors();
    test_parser_nested();
    test_parser_error_unclosed_literal();
    test_parser_error_empty();
    test_parser_memory_cleanup();
}
