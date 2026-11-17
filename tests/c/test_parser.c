/**
 * @file test_parser.c
 * @brief Tests for the SNOBOL C parser
 */

#include "snobol/ast.h"
#include "snobol/lexer.h"
#include "snobol/parser.h"
#include "snobol/snobol.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <pthread.h>
#endif

/* Test framework functions (from test_runner.c) */
extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

static void test_parser_create_destroy(void) {
  test_suite("Parser: create and destroy");

  snobol_parser_t *parser = snobol_parser_create();
  test_assert(parser != NULL, "parser_create returns non-NULL");

  snobol_parser_destroy(parser);
  test_assert(true, "parser_destroy completes without crash");
}

static void test_parser_literal(void) {
  test_suite("Parser: literal pattern");

  snobol_parser_t *parser = snobol_parser_create();
  snobol_lexer_t *lexer = snobol_lexer_create("'hello'", 7);

  ast_node_t *ast = snobol_parser_parse(parser, lexer);

  test_assert(ast != NULL, "parser returns AST");
  test_assert((!snobol_parser_has_error(parser)) != 0, "no parse error");
  test_assert(ast->type == AST_LITERAL, "AST node is LITERAL");
  test_assert(ast->data.literal.len == 5, "literal length is 5");
  test_assert(memcmp(ast->data.literal.text, "hello", 5) == 0,
              "literal text matches");

  snobol_ast_free(ast);
  snobol_lexer_destroy(lexer);
  snobol_parser_destroy(parser);
}

static void test_parser_alternation(void) {
  test_suite("Parser: alternation pattern");

  snobol_parser_t *parser = snobol_parser_create();
  snobol_lexer_t *lexer = snobol_lexer_create("'A' | 'B'", 9);

  ast_node_t *ast = snobol_parser_parse(parser, lexer);

  test_assert(ast != NULL, "parser returns AST");
  test_assert((!snobol_parser_has_error(parser)) != 0, "no parse error");
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

  snobol_parser_t *parser = snobol_parser_create();
  snobol_lexer_t *lexer = snobol_lexer_create("'hello' ' ' 'world'", 19);

  ast_node_t *ast = snobol_parser_parse(parser, lexer);

  test_assert(ast != NULL, "parser returns AST");
  test_assert((!snobol_parser_has_error(parser)) != 0, "no parse error");
  test_assert(ast->type == AST_CONCAT, "AST node is CONCAT");
  test_assert(ast->data.concat.count == 3, "concat has 3 parts");

  snobol_ast_free(ast);
  snobol_lexer_destroy(lexer);
  snobol_parser_destroy(parser);
}

static void test_parser_arbno(void) {
  test_suite("Parser: arbno pattern");

  snobol_parser_t *parser = snobol_parser_create();
  snobol_lexer_t *lexer = snobol_lexer_create("'x'*", 4);

  ast_node_t *ast = snobol_parser_parse(parser, lexer);

  test_assert(ast != NULL, "parser returns AST");
  test_assert((!snobol_parser_has_error(parser)) != 0, "no parse error");
  test_assert(ast->type == AST_ARBNO, "AST node is ARBNO");
  test_assert(ast->data.arbno.sub != NULL, "arbno has sub-pattern");
  test_assert(ast->data.arbno.sub->type == AST_LITERAL, "sub is LITERAL");

  snobol_ast_free(ast);
  snobol_lexer_destroy(lexer);
  snobol_parser_destroy(parser);
}

static void test_parser_span(void) {
  test_suite("Parser: SPAN function");

  snobol_parser_t *parser = snobol_parser_create();
  snobol_lexer_t *lexer = snobol_lexer_create("SPAN('0-9')", 11);

  ast_node_t *ast = snobol_parser_parse(parser, lexer);

  test_assert(ast != NULL, "parser returns AST");
  test_assert((!snobol_parser_has_error(parser)) != 0, "no parse error");
  test_assert(ast->type == AST_SPAN, "AST node is SPAN");
  if (ast->type == AST_SPAN) {
    test_assert(ast->data.charclass.len == 3,
                "charclass length is 3 (content without quotes)");
    test_assert(memcmp(ast->data.charclass.set, "0-9", 3) == 0,
                "charclass text matches");
  }

  snobol_ast_free(ast);
  snobol_lexer_destroy(lexer);
  snobol_parser_destroy(parser);
}

static void test_parser_any(void) {
  test_suite("Parser: ANY function");

  snobol_parser_t *parser = snobol_parser_create();
  snobol_lexer_t *lexer = snobol_lexer_create("ANY('aeiou')", 12);

  ast_node_t *ast = snobol_parser_parse(parser, lexer);

  test_assert(ast != NULL, "parser returns AST");
  test_assert((!snobol_parser_has_error(parser)) != 0, "no parse error");
  test_assert(ast->type == AST_ANY, "AST node is ANY");

  snobol_ast_free(ast);
  snobol_lexer_destroy(lexer);
  snobol_parser_destroy(parser);
}

static void test_parser_capture(void) {
  test_suite("Parser: capture pattern");

  snobol_parser_t *parser = snobol_parser_create();
  snobol_lexer_t *lexer = snobol_lexer_create("@var 'hello'", 13);

  ast_node_t *ast = snobol_parser_parse(parser, lexer);

  test_assert(ast != NULL, "parser returns AST");
  test_assert((!snobol_parser_has_error(parser)) != 0, "no parse error");
  test_assert(ast->type == AST_CAP, "AST node is CAP");
  test_assert(ast->data.cap.reg == 0, "first named capture gets register 0");
  test_assert(ast->data.cap.sub != NULL, "capture has sub-pattern");

  snobol_ast_free(ast);
  snobol_lexer_destroy(lexer);
  snobol_parser_destroy(parser);
}

static void test_parser_parenthesized(void) {
  test_suite("Parser: parenthesized pattern");

  snobol_parser_t *parser = snobol_parser_create();
  snobol_lexer_t *lexer = snobol_lexer_create("('A' | 'B')", 11);

  ast_node_t *ast = snobol_parser_parse(parser, lexer);

  test_assert(ast != NULL, "parser returns AST");
  test_assert((!snobol_parser_has_error(parser)) != 0, "no parse error");
  test_assert(ast->type == AST_ALT, "AST node is ALT (parentheses removed)");

  snobol_ast_free(ast);
  snobol_lexer_destroy(lexer);
  snobol_parser_destroy(parser);
}

static void test_parser_anchors(void) {
  test_suite("Parser: anchors");

  snobol_parser_t *parser = snobol_parser_create();
  snobol_lexer_t *lexer = snobol_lexer_create("^'start'", 8);

  ast_node_t *ast = snobol_parser_parse(parser, lexer);

  test_assert(ast != NULL, "parser returns AST");
  test_assert((!snobol_parser_has_error(parser)) != 0, "no parse error");
  test_assert(ast->type == AST_CONCAT, "AST node is CONCAT (anchor + literal)");
  test_assert(ast->data.concat.count == 2, "concat has 2 parts");
  test_assert(ast->data.concat.parts[0]->type == AST_ANCHOR,
              "first part is ANCHOR");
  test_assert(ast->data.concat.parts[0]->data.anchor.atype == ANCHOR_START,
              "anchor is START");

  snobol_ast_free(ast);
  snobol_lexer_destroy(lexer);
  snobol_parser_destroy(parser);
}

static void test_parser_nested(void) {
  test_suite("Parser: nested pattern");

  snobol_parser_t *parser = snobol_parser_create();
  snobol_lexer_t *lexer = snobol_lexer_create("('A' | 'B')*", 12);

  ast_node_t *ast = snobol_parser_parse(parser, lexer);

  test_assert(ast != NULL, "parser returns AST");
  test_assert((!snobol_parser_has_error(parser)) != 0, "no parse error");
  test_assert(ast->type == AST_ARBNO, "AST node is ARBNO");
  test_assert(ast->data.arbno.sub != NULL, "arbno has sub-pattern");
  test_assert(ast->data.arbno.sub->type == AST_ALT, "sub is ALT");

  snobol_ast_free(ast);
  snobol_lexer_destroy(lexer);
  snobol_parser_destroy(parser);
}

static void test_parser_error_unclosed_literal(void) {
  test_suite("Parser: error - unclosed literal");

  snobol_parser_t *parser = snobol_parser_create();
  snobol_lexer_t *lexer = snobol_lexer_create("'unclosed", 8);

  ast_node_t *ast = snobol_parser_parse(parser, lexer);

  /* Parser may return partial AST or NULL for syntax errors */
  /* The important thing is it doesn't crash */
  if (ast) {
    snobol_ast_free(ast);
  }
  snobol_lexer_destroy(lexer);
  snobol_parser_destroy(parser);

  test_assert(true, "parser handles unclosed literal without crash");
}

static void test_parser_error_empty(void) {
  test_suite("Parser: error - empty input");

  snobol_parser_t *parser = snobol_parser_create();
  snobol_lexer_t *lexer = snobol_lexer_create("", 0);

  ast_node_t *ast = snobol_parser_parse(parser, lexer);

  /* Empty input should produce an error */
  test_assert((ast == NULL || snobol_parser_has_error(parser)) != 0,
              "empty input produces error or NULL");

  if (ast) {
    snobol_ast_free(ast);
  }
  snobol_lexer_destroy(lexer);
  snobol_parser_destroy(parser);
}

static void test_parser_memory_cleanup(void) {
  test_suite("Parser: memory cleanup");

  /* Create and parse multiple patterns to check for memory leaks */
  for (int i = 0; i < 10; i++) {
    snobol_parser_t *parser = snobol_parser_create();
    snobol_lexer_t *lexer = snobol_lexer_create("'test' | 'pattern'", 17);

    ast_node_t *ast = snobol_parser_parse(parser, lexer);
    if (ast) {
      snobol_ast_free(ast);
    }

    snobol_lexer_destroy(lexer);
    snobol_parser_destroy(parser);
  }

  test_assert(true, "10 parse/free cycles completed without crash");
}


/* ===== test_coverage_parser: coverage-driven tests merged into test_parser.c ===== */
#include <stdint.h>
#include "../../core/include/snobol/ast.h"
#include "../../core/include/snobol/lexer.h"
#include "../../core/include/snobol/parser.h"


/* Parse a source string; returns the AST (caller frees) or NULL. */
static ast_node_t *covp_parse(snobol_parser_t *parser, const char *src,
                              bool *has_error) {
  snobol_lexer_t *lexer = snobol_lexer_create(src, strlen(src));
  ast_node_t *ast = snobol_parser_parse(parser, lexer);
  if (has_error) {
    *has_error = snobol_parser_has_error(parser);
  }
  snobol_lexer_destroy(lexer);
  return ast;
}

void test_cov_parser_labels_and_gotos(void) {
  test_suite("Coverage: parser labels + gotos");

  /* Label: `foo: 'a'` parses to AST_LABEL wrapping a literal. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "foo: 'a'", &err);
    test_assert((ast && !err && ast->type == AST_LABEL) != 0, "label parses");
    if (ast) {
      test_assert((ast->data.label.name &&
                   strcmp(ast->data.label.name, "foo") == 0) != 0,
                  "label name captured");
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* Nested labels grow the seen-label table past its initial capacity. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "a: b: c: d: e: 'x'", &err);
    test_assert((ast && !err) != 0, "five nested labels parse");
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* Duplicate label is rejected. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "dup: dup: 'a'", &err);
    test_assert((ast == NULL && err) != 0, "duplicate label rejected");
    snobol_parser_destroy(parser);
  }

  /* Goto syntax: `'a' : (L)` produces concat(pattern, goto). */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "'a' : (L)", &err);
    test_assert((ast && !err && ast->type == AST_CONCAT) != 0, "goto parses");
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* Goto failure modes. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "'a' : (", &err);
    test_assert((ast == NULL && err) != 0, "goto without paren body rejected");
    snobol_parser_destroy(parser);
  }
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "'a' : (1)", &err);
    test_assert((ast == NULL && err) != 0,
                "goto with non-ident label rejected");
    snobol_parser_destroy(parser);
  }
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "'a' : (L", &err);
    test_assert((ast == NULL && err) != 0,
                "goto without closing paren rejected");
    snobol_parser_destroy(parser);
  }

  /* Trailing garbage after the pattern. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "'a' 'b'", &err);
    test_assert((ast && !err) != 0, "concatenation parses");
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }
}


void test_cov_parser_repetition_and_primary(void) {
  test_suite("Coverage: parser repetition + primary errors");

  /* '?' and '+' postfix operators. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "'a'?", &err);
    test_assert((ast && !err && ast->type == AST_REPETITION) != 0,
                "question-mark parses to repetition");
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "'a'+", &err);
    test_assert((ast && !err && ast->type == AST_CONCAT) != 0,
                "plus parses to concat(clone, arbno)");
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* Alternation with a missing right side. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "'a' |", &err);
    test_assert((ast == NULL && err) != 0, "dangling pipe rejected");
    snobol_parser_destroy(parser);
  }

  /* Unclosed paren. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "('a'", &err);
    test_assert((ast == NULL && err) != 0, "unclosed paren rejected");
    snobol_parser_destroy(parser);
  }

  /* Anchors as primaries. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "^'a'", &err);
    test_assert((ast && !err && ast->type == AST_CONCAT) != 0,
                "start anchor parses");
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "$ 'a'", &err);
    test_assert((ast && !err) != 0, "end anchor parses as primary");
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* Bad capture targets. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "@5 'a'", &err);
    test_assert((ast == NULL && err) != 0, "non-ident capture target rejected");
    snobol_parser_destroy(parser);
  }
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "@r1", &err);
    test_assert((ast == NULL && err) != 0,
                "capture without sub-pattern rejected");
    snobol_parser_destroy(parser);
  }

  /* Bare identifier and unknown token. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "bareident", &err);
    test_assert((ast == NULL && err) != 0, "bare identifier rejected");
    snobol_parser_destroy(parser);
  }
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "5", &err);
    test_assert((ast == NULL && err) != 0, "numeric token rejected");
    snobol_parser_destroy(parser);
  }
}


void test_cov_parser_functions(void) {
  test_suite("Coverage: parser function-call validation");

  /* ANY() with no argument is accepted. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "ANY()", &err);
    test_assert((ast && !err && ast->type == AST_ANY) != 0, "ANY() parses");
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* Argument-type errors for each builtin. */
  {
    /* Integer builtins reject identifiers; string builtins reject them too. */
    const char *bad[] = {"SPAN(foo)", "BREAK(foo)",  "BREAKX(foo)",
                         "ANY(foo)",  "NOTANY(foo)", "POS(foo)",
                         "TAB(foo)",  "FOO('x')"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
      snobol_parser_t *parser = snobol_parser_create();
      bool err = false;
      ast_node_t *ast = covp_parse(parser, bad[i], &err);
      test_assert((ast == NULL && err) != 0, "bad builtin argument rejected");
      snobol_parser_destroy(parser);
    }
  }

  /* Missing closing paren after the argument. */
  {
    const char *bad[] = {"SPAN('a'",   "BREAK('a'", "BREAKX('a'", "ANY('a'",
                         "NOTANY('a'", "LEN('5'",   "POS('2'",    "TAB('3'",
                         "ABORT(x)",   "ABORT(",    "FAIL(",      "SUCCEED("};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
      snobol_parser_t *parser = snobol_parser_create();
      bool err = false;
      ast_node_t *ast = covp_parse(parser, bad[i], &err);
      test_assert((ast == NULL && err) != 0, "unclosed builtin call rejected");
      snobol_parser_destroy(parser);
    }
  }

  /* Valid zero-arg and integer-arg builtins. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "ABORT()", &err);
    test_assert((ast && !err && ast->type == AST_ABORT) != 0, "ABORT() parses");
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "FAIL()", &err);
    test_assert((ast && !err && ast->type == AST_FAIL) != 0, "FAIL() parses");
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "SUCCEED()", &err);
    test_assert((ast && !err && ast->type == AST_SUCCEED) != 0,
                "SUCCEED() parses");
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "POS(2)", &err);
    test_assert((ast && !err && ast->type == AST_POS) != 0, "POS(2) parses");
    if (ast && ast->type == AST_POS) {
      test_assert(ast->data.rpos_rtab.n == 2, "POS(2) carries n=2");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "TAB(3)", &err);
    test_assert((ast && !err && ast->type == AST_TAB) != 0, "TAB(3) parses");
    if (ast && ast->type == AST_TAB) {
      test_assert(ast->data.rpos_rtab.n == 3, "TAB(3) carries n=3");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "LEN(5)", &err);
    test_assert((ast && !err && ast->type == AST_LEN) != 0, "LEN(5) parses");
    if (ast && ast->type == AST_LEN) {
      test_assert(ast->data.len.n == 5, "LEN(5) carries n=5 (not placeholder)");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* Numeric argument validation: missing, quoted, mis-typed, out of range. */
  {
    const char *bad[] = {
        "LEN()",    "POS()",    "TAB()",   "LEN('5')",
        "POS('2')", "TAB('3')", "POS(x3)", "LEN(99999999999999999999)"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
      snobol_parser_t *parser = snobol_parser_create();
      bool err = false;
      ast_node_t *ast = covp_parse(parser, bad[i], &err);
      test_assert((ast == NULL && err) != 0,
                  "invalid numeric argument rejected");
      if (!ast && err) {
        const char *msg = snobol_parser_get_error(parser);
        test_assert(
            (msg && (strstr(msg, "LEN") != NULL || strstr(msg, "POS") != NULL ||
                     strstr(msg, "TAB") != NULL ||
                     strstr(msg, "too large") != NULL)) != 0,
            "numeric-argument error names the builtin or the overflow");
      }
      snobol_parser_destroy(parser);
    }
  }
  {
    /* Negative integers are accepted by the lexer (repeat bounds validate
     * them later); POS(-5) never succeeds at match time. */
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "POS(-5)", &err);
    test_assert((ast && !err && ast->type == AST_POS) != 0,
                "POS(-5) parses (negative integer token)");
    if (ast && ast->type == AST_POS) {
      test_assert(ast->data.rpos_rtab.n == -5, "POS(-5) carries n=-5");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* NULL guards. */
  test_assert(snobol_parser_parse(nullptr, nullptr) == NULL,
              "parse(NULL,NULL)");
  {
    snobol_parser_t *parser = snobol_parser_create();
    test_assert(snobol_parser_parse(parser, nullptr) == NULL,
                "parse(parser,NULL)");
    test_assert((!snobol_parser_has_error(nullptr)) != 0, "has_error(NULL)");
    test_assert(snobol_parser_get_error(nullptr) == NULL, "get_error(NULL)");
    snobol_parser_destroy(parser);
  }
}


void test_cov_parser_round3(void) {
  test_suite("Coverage: parser trailing tokens + misc errors");

  /* Trailing token after the pattern. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "'a' =", &err);
    test_assert((ast == NULL && err) != 0, "trailing token rejected");
    snobol_parser_destroy(parser);
  }

  /* ':' followed by EOF (no '(') — goto expect failure. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "'a' :", &err);
    test_assert((ast == NULL && err) != 0, "dangling colon rejected");
    snobol_parser_destroy(parser);
  }

  /* '@*' capture target (star fallback branch). */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    /* '@*' has no valid capture target: the parser must reject it with a
     * parse error (no star fallback branch). */
    ast_node_t *ast = covp_parse(parser, "@* 'a'", &err);
    test_assert((ast == NULL && err) != 0, "star capture target rejected");
    snobol_parser_destroy(parser);
  }

  /* EVAL with an empty expression. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "EVAL()", &err);
    test_assert((ast == NULL && err) != 0, "empty EVAL expression rejected");
    snobol_parser_destroy(parser);
  }

  /* EVAL without closing paren. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "EVAL('x'", &err);
    test_assert((ast == NULL && err) != 0, "unclosed EVAL rejected");
    snobol_parser_destroy(parser);
  }

  /* Re-parse frees the previous parse's labels. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "x: 'a'", &err);
    test_assert((ast && !err) != 0, "first labelled parse");
    if (ast) {
      snobol_ast_free(ast);
    }
    ast = covp_parse(parser, "y: 'b'", &err);
    test_assert((ast && !err) != 0, "second labelled parse reuses parser");
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* Error-location and clear-error accessors. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    size_t line = 0;
    size_t col = 0;
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "5", &err);
    test_assert((ast == NULL && err) != 0, "numeric token errors");
    snobol_parser_get_error_location(parser, &line, &col);
    test_assert(line >= 1, "error location line");
    snobol_parser_clear_error(parser);
    test_assert((!snobol_parser_has_error(parser)) != 0, "clear error resets");
    snobol_parser_get_error_location(parser, nullptr, nullptr);
    snobol_parser_get_error_location(nullptr, &line, &col);
    snobol_parser_clear_error(nullptr);
    snobol_parser_destroy(parser);
  }
}

void test_cov_parser_source_primitives(void) {
  test_suite("Coverage: parser source primitives");

  /* ARB (bare and ARB()) → arbno(len(1)) */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "'abc' ARB 'def'", &err);
    test_assert((ast && !err && ast->type == AST_CONCAT) != 0,
                "'abc' ARB 'def' concatenates");
    if (ast && ast->type == AST_CONCAT && ast->data.concat.count == 3) {
      ast_node_t *arb = ast->data.concat.parts[1];
      test_assert(arb->type == AST_ARBNO, "ARB maps to ARBNO");
      if (arb->type == AST_ARBNO && arb->data.arbno.sub) {
        test_assert(arb->data.arbno.sub->type == AST_LEN, "ARB sub is LEN");
        test_assert(arb->data.arbno.sub->data.len.n == 1, "ARB is LEN(1)");
      }
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* FENCE and REM as bare identifiers and as zero-arg calls. */
  {
    const char *forms[] = {"FENCE", "FENCE()", "REM", "REM()"};
    for (size_t i = 0; i < sizeof(forms) / sizeof(forms[0]); i++) {
      snobol_parser_t *parser = snobol_parser_create();
      bool err = false;
      ast_node_t *ast = covp_parse(parser, forms[i], &err);
      ast_type_t want = (forms[i][0] == 'F') ? AST_FENCE : AST_REM;
      test_assert((ast != NULL && !err && ast->type == want) != 0,
                  "bare/zero-arg primitive parses");
      if (ast) {
        snobol_ast_free(ast);
      }
      snobol_parser_destroy(parser);
    }
  }

  /* ARBNO(pattern) → arbno(node). */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "ARBNO('a')", &err);
    test_assert((ast && !err && ast->type == AST_ARBNO) != 0,
                "ARBNO('a') parses");
    if (ast && ast->type == AST_ARBNO && ast->data.arbno.sub) {
      test_assert(ast->data.arbno.sub->type == AST_LITERAL,
                  "ARBNO sub is LITERAL");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* BAL() / BAL('(') / BAL('(', ')') / BAL('<', '>'). */
  {
    struct {
      const char *src;
      uint32_t open;
      uint32_t close;
    } cases[] = {{"BAL()", '(', ')'},
                 {"BAL('(')", '(', ')'},
                 {"BAL('(', ')')", '(', ')'},
                 {"BAL('<', '>')", '<', '>'}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      snobol_parser_t *parser = snobol_parser_create();
      bool err = false;
      ast_node_t *ast = covp_parse(parser, cases[i].src, &err);
      test_assert((ast && !err && ast->type == AST_BAL) != 0,
                  "BAL form parses");
      if (ast && ast->type == AST_BAL) {
        test_assert(ast->data.bal.open_cp == cases[i].open,
                    "BAL open delimiter");
        test_assert(ast->data.bal.close_cp == cases[i].close,
                    "BAL close delimiter");
      }
      if (ast) {
        snobol_ast_free(ast);
      }
      snobol_parser_destroy(parser);
    }
  }

  /* RPOS(n) / RTAB(n). */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "RPOS(0)", &err);
    test_assert((ast && !err && ast->type == AST_RPOS) != 0, "RPOS(0) parses");
    if (ast && ast->type == AST_RPOS) {
      test_assert(ast->data.rpos_rtab.n == 0, "RPOS(0) carries n=0");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "RTAB(3)", &err);
    test_assert((ast && !err && ast->type == AST_RTAB) != 0, "RTAB(3) parses");
    if (ast && ast->type == AST_RTAB) {
      test_assert(ast->data.rpos_rtab.n == 3, "RTAB(3) carries n=3");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* repeat(pattern, min, max) with bound validation. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "repeat('a', 2, 3)", &err);
    test_assert((ast && !err && ast->type == AST_REPETITION) != 0,
                "repeat('a', 2, 3) parses");
    if (ast && ast->type == AST_REPETITION) {
      test_assert(ast->data.repetition.min == 2, "repeat min=2");
      test_assert(ast->data.repetition.max == 3, "repeat max=3");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* Wrong argument count/type and invalid bounds are descriptive errors. */
  {
    const char *bad[] = {"ARBNO()",
                         "ARBNO('a', 'b')",
                         "repeat('a', 5, 2)",
                         "repeat('a', -1, 2)",
                         "repeat('a', 2)",
                         "repeat()",
                         "repeat('a', '2', 3)",
                         "BAL(5)",
                         "BAL('(',)",
                         "FENCE('x')",
                         "REM('x')",
                         "RPOS('1')",
                         "RTAB()"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
      snobol_parser_t *parser = snobol_parser_create();
      bool err = false;
      ast_node_t *ast = covp_parse(parser, bad[i], &err);
      test_assert((ast == NULL && err) != 0, "invalid primitive rejected");
      if (!ast && err) {
        const char *msg = snobol_parser_get_error(parser);
        test_assert((msg && (strstr(msg, "ARBNO") || strstr(msg, "repeat") ||
                             strstr(msg, "BAL") || strstr(msg, "FENCE") ||
                             strstr(msg, "REM") || strstr(msg, "RPOS") ||
                             strstr(msg, "RTAB")) != NULL) != 0,
                    "primitive error names the primitive");
      }
      snobol_parser_destroy(parser);
    }
  }
}

void test_cov_parser_emit_table_assign(void) {
  test_suite("Coverage: parser EMIT / TABLE / assignment");

  /* EMIT('text') → AST_EMIT with text; EMIT(@vN) / EMIT(@name) → reg form. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "EMIT('X')", &err);
    test_assert((ast && !err && ast->type == AST_EMIT) != 0,
                "EMIT('X') parses");
    if (ast && ast->type == AST_EMIT) {
      test_assert(ast->data.emit.reg == -1, "literal EMIT has no register");
      test_assert(ast->data.emit.len == 1 && ast->data.emit.text &&
                      ast->data.emit.text[0] == 'X',
                  "literal EMIT carries the text");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "@x 'ab' EMIT(@v1)", &err);
    test_assert((ast && !err) != 0, "EMIT(@v1) parses");
    if (ast) {
      ast_node_t *emit = ast;
      if (emit->type == AST_CONCAT && emit->data.concat.count == 2) {
        emit = emit->data.concat.parts[1];
      }
      test_assert(emit->type == AST_EMIT && emit->data.emit.reg == 1,
                  "EMIT(@v1) carries register 1");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }
  {
    /* EMIT(@name) resolves the capture allocated for @name (register 0). */
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "@name 'ab' EMIT(@name)", &err);
    test_assert((ast && !err) != 0, "EMIT(@name) parses with prior capture");
    if (ast) {
      ast_node_t *emit = ast;
      if (emit->type == AST_CONCAT && emit->data.concat.count == 2) {
        emit = emit->data.concat.parts[1];
      }
      test_assert(emit->type == AST_EMIT && emit->data.emit.reg == 0,
                  "EMIT(@name) resolves to the name's register 0");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* TABLE['k'] → table_access with literal key. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "T['k']", &err);
    test_assert((ast && !err && ast->type == AST_TABLE_ACCESS) != 0,
                "T['k'] parses as table access");
    if (ast && ast->type == AST_TABLE_ACCESS) {
      test_assert(strcmp(ast->data.table_access.table, "T") == 0,
                  "table name is T");
      test_assert(ast->data.table_access.key &&
                      ast->data.table_access.key->type == AST_LITERAL,
                  "literal key node");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* TABLE[$vN] → table access with a register-reference key. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "@w 'z' T[$v0]", &err);
    test_assert((ast && !err) != 0, "T[$v0] parses");
    if (ast) {
      ast_node_t *acc = ast;
      if (acc->type == AST_CONCAT && acc->data.concat.count == 2) {
        acc = acc->data.concat.parts[1];
      }
      test_assert(acc->type == AST_TABLE_ACCESS, "T[$v0] is a table access");
      if (acc->type == AST_TABLE_ACCESS && acc->data.table_access.key) {
        test_assert(acc->data.table_access.key->type == AST_REG_REF &&
                        acc->data.table_access.key->data.reg_ref.reg == 0,
                    "T[$v0] key is a register reference to v0");
      }
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* TABLE[key] = value → table update. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "T['k'] = 'v'", &err);
    test_assert((ast && !err && ast->type == AST_TABLE_UPDATE) != 0,
                "T['k'] = 'v' parses as table update");
    if (ast && ast->type == AST_TABLE_UPDATE) {
      test_assert(ast->data.table_update.value &&
                      ast->data.table_update.value->type == AST_LITERAL,
                  "update value is the literal 'v'");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* Register assignment: vN = <reg> and capture-name targets. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "@x 'ab' v1 = 0", &err);
    test_assert((ast && !err) != 0, "v1 = 0 parses");
    if (ast) {
      ast_node_t *assign = ast;
      if (assign->type == AST_CONCAT && assign->data.concat.count == 2) {
        assign = assign->data.concat.parts[1];
      }
      test_assert(assign->type == AST_ASSIGN, "v1 = 0 is an ASSIGN node");
      if (assign->type == AST_ASSIGN) {
        test_assert(assign->data.assign.var == 1 &&
                        assign->data.assign.reg == 0,
                    "assign(var=1, reg=0)");
      }
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }
  {
    /* name = <reg> resolves a registered capture name. */
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "@hold 'z' hold = 0", &err);
    test_assert((ast && !err) != 0, "name = 0 parses for a capture name");
    if (ast) {
      ast_node_t *assign = ast;
      if (assign->type == AST_CONCAT && assign->data.concat.count == 2) {
        assign = assign->data.concat.parts[1];
      }
      test_assert(assign->type == AST_ASSIGN && assign->data.assign.var == 0,
                  "name assignment targets the capture's register");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* Descriptive errors for malformed EMIT / TABLE / assignment forms. */
  {
    const char *bad[] = {"EMIT()",  "EMIT(5)", "EMIT(@nope)", "T[ab]",
                         "T[]",     "T[$va]",  "unknown = 0", "v1 = 'x'",
                         "v1 = 99", "v1 = -1"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
      snobol_parser_t *parser = snobol_parser_create();
      bool err = false;
      ast_node_t *ast = covp_parse(parser, bad[i], &err);
      test_assert((ast == NULL && err) != 0, "malformed form rejected");
      if (!ast && err) {
        const char *msg = snobol_parser_get_error(parser);
        test_assert((msg && (strstr(msg, "EMIT") || strstr(msg, "table") ||
                             strstr(msg, "TABLE") || strstr(msg, "assign") ||
                             strstr(msg, "value"))) != NULL,
                    "form error is descriptive");
      }
      snobol_parser_destroy(parser);
    }
  }
}

void test_cov_parser_naming_ops(void) {
  test_suite("Coverage: parser match-naming operators");

  /* SPAN('a-z') . @name → cap(0, span): sequential register allocation. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "SPAN('a-z') . @name", &err);
    test_assert((ast && !err && ast->type == AST_CAP) != 0,
                "SPAN('a-z') . @name wraps in a capture");
    if (ast && ast->type == AST_CAP) {
      test_assert(ast->data.cap.reg == 0, "naming target gets register 0");
      test_assert(ast->data.cap.sub && ast->data.cap.sub->type == AST_SPAN,
                  "captured sub-pattern is the SPAN");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* 'id:' SPAN('0-9') $v1 → concat(lit, cap(1, span)): explicit register. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "'id:' SPAN('0-9') $v1", &err);
    test_assert((ast && !err && ast->type == AST_CONCAT) != 0,
                "'id:' SPAN('0-9') $v1 parses");
    if (ast && ast->type == AST_CONCAT && ast->data.concat.count == 2) {
      ast_node_t *cap = ast->data.concat.parts[1];
      test_assert(cap->type == AST_CAP && cap->data.cap.reg == 1,
                  "$v1 naming targets explicit register 1");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* Naming binds tighter than concatenation: 'a' . @x 'b'. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "'a' . @x 'b'", &err);
    test_assert((ast && !err && ast->type == AST_CONCAT &&
                 ast->data.concat.count == 2) != 0,
                "'a' . @x 'b' is a two-part concat");
    if (ast && ast->type == AST_CONCAT && ast->data.concat.count == 2) {
      test_assert(ast->data.concat.parts[0]->type == AST_CAP,
                  "naming wraps only the immediately preceding part");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* Alternate target spellings: '. $vN', '. vN', '$ @name'. */
  {
    const char *forms[] = {"'a' . $v1", "'a' . v1", "'a' $ @x", "'a' $v1"};
    for (size_t i = 0; i < sizeof(forms) / sizeof(forms[0]); i++) {
      snobol_parser_t *parser = snobol_parser_create();
      bool err = false;
      ast_node_t *ast = covp_parse(parser, forms[i], &err);
      test_assert((ast && !err) != 0, "alternate naming spelling parses");
      if (ast) {
        ast_node_t *cap = ast;
        if (cap->type == AST_CONCAT && cap->data.concat.count == 1) {
          cap = cap->data.concat.parts[0];
        }
        test_assert(cap->type == AST_CAP, "naming spelling wraps in a capture");
      }
      if (ast) {
        snobol_ast_free(ast);
      }
      snobol_parser_destroy(parser);
    }
  }

  /* '$' stays the end anchor when no naming target follows. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "'a'$", &err);
    test_assert((ast && !err && ast->type == AST_CONCAT &&
                 ast->data.concat.count == 2) != 0,
                "'a'$ still parses as literal + anchor");
    if (ast && ast->type == AST_CONCAT && ast->data.concat.count == 2) {
      test_assert(ast->data.concat.parts[1]->type == AST_ANCHOR &&
                      ast->data.concat.parts[1]->data.anchor.atype ==
                          ANCHOR_END,
                  "trailing $ is the end anchor");
    }
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "'a' $ 'b'", &err);
    test_assert((ast && !err && ast->type == AST_CONCAT &&
                 ast->data.concat.count == 3) != 0,
                "'a' $ 'b' keeps the anchor between literals");
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_parser_destroy(parser);
  }

  /* Unary '$' indirect references and bad targets fail descriptively. */
  {
    const char *bad[] = {"'a' . 'b'", "'a' . x",   "SPAN('a-z') . $name",
                         "'a' . @",   "'a' . v99", "'a' $x"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
      snobol_parser_t *parser = snobol_parser_create();
      bool err = false;
      ast_node_t *ast = covp_parse(parser, bad[i], &err);
      test_assert((ast == NULL && err) != 0, "invalid naming form rejected");
      if (!ast && err) {
        const char *msg = snobol_parser_get_error(parser);
        test_assert((msg && (strstr(msg, "naming") || strstr(msg, "indirect") ||
                             strstr(msg, "register") ||
                             strstr(msg, "capture"))) != NULL,
                    "naming failure is descriptive");
      }
      snobol_parser_destroy(parser);
    }
  }
  {
    /* Unary '$x' at pattern start → indirect-reference error. */
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "$x 'a'", &err);
    test_assert((ast == NULL && err) != 0, "leading $x rejected");
    if (!ast && err) {
      const char *msg = snobol_parser_get_error(parser);
      test_assert((msg && strstr(msg, "indirect") != NULL) != 0,
                  "leading $x names indirect reference");
    }
    snobol_parser_destroy(parser);
  }
}

static void test_parser_integer_argument_boundaries(void) {
  test_suite("Parser: integer argument boundaries");

  /* INT64_MAX and INT64_MIN scan as values, then fail the int32 range check. */
  const char *boundary[] = {"LEN(9223372036854775807)",
                            "LEN(-9223372036854775808)"};
  for (size_t i = 0; i < sizeof(boundary) / sizeof(boundary[0]); i++) {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, boundary[i], &err);
    test_assert((ast == NULL && err) != 0, "boundary literal is rejected");
    const char *msg = snobol_parser_get_error(parser);
    test_assert(msg != NULL && strstr(msg, "LEN") != NULL,
                "boundary literal reports a range error naming LEN");
    test_assert(msg != NULL && strstr(msg, "too large") == NULL,
                "boundary literal is not reported as a 64-bit overflow");
    snobol_parser_destroy(parser);
  }

  /* Beyond the 64-bit range is the overflow error. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    bool err = false;
    ast_node_t *ast = covp_parse(parser, "LEN(999999999999999999999)", &err);
    test_assert((ast == NULL && err) != 0, "oversized literal is rejected");
    const char *msg = snobol_parser_get_error(parser);
    test_assert(msg != NULL && strstr(msg, "too large") != NULL,
                "oversized literal reports the 64-bit overflow");
    snobol_parser_destroy(parser);
  }
}

/* Compile a source that must fail and assert the reported message names the
 * failure.  Covers the parser's diagnostic branches for malformed input. */
static void expect_compile_error_cov(const char *src, size_t len,
                                     const char *needle) {
  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;
  snobol_pattern_t *p = snobol_pattern_compile_ex(ctx, src, len, 0, &err);
  test_assert(p == nullptr, "invalid source is rejected");
  test_assert(err != nullptr && strstr(err, needle) != nullptr,
              "error message names the failure");
  if (p) {
    snobol_pattern_free(p);
  }
  free(err);
  snobol_context_destroy(ctx);
}

static void test_cov_parser_error_paths(void) {
  test_suite("Coverage: parser error paths");

  expect_compile_error_cov("'x' . v99", strlen("'x' . v99"),
                           "naming register out of range");
  expect_compile_error_cov("T['k'] =", strlen("T['k'] ="),
                           "expected a value pattern");
  expect_compile_error_cov("T['k'", strlen("T['k'"), "Expected RBRACKET");
  expect_compile_error_cov("T[12]", strlen("T[12]"), "invalid table key");
  expect_compile_error_cov("('a' | 12)", strlen("('a' | 12)"),
                           "Expected pattern element");
  expect_compile_error_cov("EMIT('x'", strlen("EMIT('x'"), "Expected RPAREN");
  expect_compile_error_cov("EMIT(@12)", strlen("EMIT(@12)"), "EMIT(@...)");
  expect_compile_error_cov("EMIT(12)", strlen("EMIT(12)"),
                           "EMIT expects an argument");
  expect_compile_error_cov("@", 1, "Expected capture target");
  expect_compile_error_cov("SPAN(", strlen("SPAN("),
                           "SPAN expects string argument");

  /* A truncated UTF-8 sequence inside a BAL delimiter is rejected by the
   * parser's delimiter validation, not by the VM. */
  static const unsigned char bal_bad_utf8[] = {
      'B', 'A', 'L', '(', '\'', 0xC3, '\'', ',', ' ', '\'', ')', '\'', ')'};
  expect_compile_error_cov((const char *)bal_bad_utf8, sizeof(bal_bad_utf8),
                           "BAL delimiter is not valid UTF-8");
  expect_compile_error_cov("BAL('(', )", strlen("BAL('(', )"),
                           "BAL expects a closing delimiter");
  expect_compile_error_cov("BAL('(', ')'", strlen("BAL('(', ')'"),
                           "Expected RPAREN");
  static const unsigned char bal_bad_close[] = {
      'B', 'A', 'L', '(', '\'', '(', '\'', ',', ' ', '\'', 0xC3, '\'', ')'};
  expect_compile_error_cov((const char *)bal_bad_close, sizeof(bal_bad_close),
                           "BAL delimiter is not valid UTF-8");

  /* Exhausting the capture register file through a naming-operator chain. */
  char big[16384];
  size_t off = 0;
  for (int i = 0; i < 65; i++) {
    off += (size_t)snprintf(big + off, sizeof(big) - off, "'x' . @c%d ", i);
  }
  expect_compile_error_cov(big, off, "Too many captures");

  /* Exhausting it through the table-write value scratch allocation. */
  off = 0;
  for (int i = 0; i < 64; i++) {
    off += (size_t)snprintf(big + off, sizeof(big) - off, "@c%d 'x' ", i);
  }
  off += (size_t)snprintf(big + off, sizeof(big) - off, "T['k'] = 'y'");
  expect_compile_error_cov(big, off, "Too many captures");
}

/* ---------------------------------------------------------------------------
 * Structural depth limit (CWE-674: unbounded recursion fed by pattern text).
 *
 * The payloads below are the original attack shapes: nested parentheses,
 * label prefixes, a flat alternation, and naming wrappers, plus a nested
 * alternation that would multiply a per-level branch cap.  Every one of them
 * must produce a parse error instead of exhausting the C stack.
 */

static char *build_depth_parens(size_t n, size_t *out_len) {
  char *src = (char *)malloc(n * 2 + 4);
  if (!src) {
    *out_len = 0;
    return nullptr;
  }
  size_t k = 0;
  for (size_t i = 0; i < n; i++)
    src[k++] = '(';
  src[k++] = '\'';
  src[k++] = 'x';
  src[k++] = '\'';
  for (size_t i = 0; i < n; i++)
    src[k++] = ')';
  src[k] = '\0';
  *out_len = k;
  return src;
}

static char *build_depth_labels(size_t n, size_t *out_len) {
  char *src = (char *)malloc(n * 32 + 8);
  if (!src) {
    *out_len = 0;
    return nullptr;
  }
  size_t k = 0;
  for (size_t i = 0; i < n; i++) {
    k += (size_t)snprintf(src + k, 32, "l%zu: ", i);
  }
  src[k++] = '\'';
  src[k++] = 'x';
  src[k++] = '\'';
  src[k] = '\0';
  *out_len = k;
  return src;
}

static char *build_depth_alts(size_t n, size_t *out_len) {
  char *src = (char *)malloc(n * 4 + 4);
  if (!src) {
    *out_len = 0;
    return nullptr;
  }
  size_t k = 0;
  for (size_t i = 0; i < n; i++) {
    if (i)
      src[k++] = '|';
    src[k++] = '\'';
    src[k++] = 'x';
    src[k++] = '\'';
  }
  src[k] = '\0';
  *out_len = k;
  return src;
}

static char *build_depth_names(size_t n, size_t *out_len) {
  char *src = (char *)malloc(n * 3 + 8);
  if (!src) {
    *out_len = 0;
    return nullptr;
  }
  size_t k = 0;
  src[k++] = '\'';
  src[k++] = 'x';
  src[k++] = '\'';
  for (size_t i = 0; i < n; i++) {
    src[k++] = '.';
    src[k++] = 'v';
    src[k++] = '0';
  }
  src[k] = '\0';
  *out_len = k;
  return src;
}

/* n nested calls of @p fn, e.g. ARBNO(ARBNO('x')). */
static char *build_depth_call(const char *fn, size_t n, size_t *out_len) {
  size_t fn_len = strlen(fn);
  char *src = (char *)malloc(n * fn_len + n + 8);
  if (!src) {
    *out_len = 0;
    return nullptr;
  }
  size_t k = 0;
  for (size_t i = 0; i < n; i++) {
    memcpy(src + k, fn, fn_len);
    k += fn_len;
  }
  src[k++] = '\'';
  src[k++] = 'x';
  src[k++] = '\'';
  for (size_t i = 0; i < n; i++)
    src[k++] = ')';
  src[k] = '\0';
  *out_len = k;
  return src;
}

/* levels nested alternations, each with `branches` additional branches and
 * the nested tree on the left spine. */
static char *build_depth_alt_nest(size_t levels, size_t branches,
                                  size_t *out_len) {
  size_t cap = levels * (branches * 4 + 2) + 16;
  char *cur = (char *)malloc(cap);
  if (!cur) {
    *out_len = 0;
    return nullptr;
  }
  memcpy(cur, "'x'", 3);
  size_t clen = 3;
  for (size_t i = 0; i < levels; i++) {
    char *next = (char *)malloc(cap);
    if (!next) {
      free(cur);
      *out_len = 0;
      return nullptr;
    }
    size_t k = 0;
    next[k++] = '(';
    memcpy(next + k, cur, clen);
    k += clen;
    for (size_t j = 0; j < branches; j++) {
      memcpy(next + k, "|'x'", 4);
      k += 4;
    }
    next[k++] = ')';
    free(cur);
    cur = next;
    clen = k;
  }
  cur[clen] = '\0';
  *out_len = clen;
  return cur;
}

static void expect_depth_error(const char *src, size_t len, const char *msg) {
  snobol_parser_t *parser = snobol_parser_create();
  snobol_lexer_t *lexer = snobol_lexer_create(src, len);
  ast_node_t *ast = snobol_parser_parse(parser, lexer);

  test_assert(ast == nullptr, msg);
  test_assert(snobol_parser_has_error(parser) != 0,
              "depth limit records a parse error");
  const char *err = snobol_parser_get_error(parser);
  test_assert((err != nullptr && strstr(err, "nesting too deep") != nullptr) !=
                  0,
              "error message names the nesting limit");

  if (ast) {
    snobol_ast_free(ast);
  }
  snobol_lexer_destroy(lexer);
  snobol_parser_destroy(parser);
}

static void expect_depth_ok(const char *src, size_t len, const char *msg) {
  snobol_parser_t *parser = snobol_parser_create();
  snobol_lexer_t *lexer = snobol_lexer_create(src, len);
  ast_node_t *ast = snobol_parser_parse(parser, lexer);

  test_assert((ast != nullptr && !snobol_parser_has_error(parser)) != 0, msg);

  if (ast) {
    snobol_ast_free(ast);
  }
  snobol_lexer_destroy(lexer);
  snobol_parser_destroy(parser);
}

#if !defined(_WIN32)
typedef struct {
  const char *src;
  size_t len;
  bool compiled;
  bool saw_depth_error;
  bool saw_stack_error;
} depth_stack_job_t;

static void *depth_stack_worker(void *arg) {
  depth_stack_job_t *job = (depth_stack_job_t *)arg;
  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;

  snobol_pattern_t *pattern =
      snobol_pattern_compile(ctx, job->src, job->len, &err);
  job->compiled = pattern != nullptr;
  job->saw_depth_error =
      err != nullptr && strstr(err, "nesting too deep") != nullptr;
  job->saw_stack_error =
      err != nullptr && strstr(err, "parser stack limit") != nullptr;

  if (pattern) {
    snobol_pattern_free(pattern);
  }
  free(err);
  snobol_context_destroy(ctx);
  return nullptr;
}

/* Compile @p src on a thread with a deliberately small stack.  Returns false
 * when the host refuses to create the thread. */
static bool compile_on_stack(const char *src, size_t len, size_t stack_bytes,
                             depth_stack_job_t *out) {
  out->src = src;
  out->len = len;
  out->compiled = false;
  out->saw_depth_error = false;
  out->saw_stack_error = false;

  pthread_attr_t attr;
  if (pthread_attr_init(&attr) != 0) {
    return false;
  }
  int set = pthread_attr_setstacksize(&attr, stack_bytes);
  pthread_t thread;
  bool started =
      set == 0 && pthread_create(&thread, &attr, depth_stack_worker, out) == 0;
  pthread_attr_destroy(&attr);
  if (started) {
    pthread_join(thread, nullptr);
  }
  return started;
}
#endif

static void test_parser_depth_limit(void) {
  test_suite("Parser: structural depth limit");

  /* The budget is sized to fit the smallest default secondary-thread stack,
   * so it must not grow past what that stack can carry. */
  test_assert(SNOBOL_PARSER_MAX_DEPTH <= 256,
              "depth budget fits a default secondary-thread stack");
  test_assert(SNOBOL_PARSER_MAX_STACK_BYTES <= 256 * 1024,
              "stack budget fits a default secondary-thread stack");

  const size_t attack_n = 20000;
#if defined(SNOBOL_SANITIZER)
  const size_t ok_n = 16;
#else
  const size_t ok_n = 64;
#endif
  size_t len = 0;
  char *src = nullptr;

  /* Attack shapes: each must be rejected, not crash. */
  src = build_depth_parens(attack_n, &len);
  expect_depth_error(src, len, "20000 nested '(' rejected");
  free(src);

  src = build_depth_labels(attack_n, &len);
  expect_depth_error(src, len, "20000 label prefixes rejected");
  free(src);

  src = build_depth_alts(attack_n, &len);
  expect_depth_error(src, len, "20000 alternation branches rejected");
  free(src);

  src = build_depth_names(attack_n, &len);
  expect_depth_error(src, len, "20000 naming wrappers rejected");
  free(src);

  src = build_depth_alt_nest(100, 32, &len);
  expect_depth_error(src, len, "nested alternation depth does not multiply");
  free(src);

  /* Legitimate patterns under the limit still parse. */
  src = build_depth_parens(ok_n, &len);
  expect_depth_ok(src, len, "nesting under the limit still parses");
  free(src);

  src = build_depth_labels(ok_n, &len);
  expect_depth_ok(src, len, "label nesting under the limit still parses");
  free(src);

  src = build_depth_alts(ok_n, &len);
  expect_depth_ok(src, len, "alternation under the limit still parses");
  free(src);

  src = build_depth_names(ok_n, &len);
  expect_depth_ok(src, len, "naming chain under the limit still parses");
  free(src);

  src = build_depth_alt_nest(20, 32, &len);
  expect_depth_ok(src, len, "modest alternation nesting still parses");
  free(src);

  /* The original attack path through the public compile API. */
  {
    src = build_depth_parens(attack_n, &len);
    snobol_context_t *ctx = snobol_context_create();
    char *err = nullptr;
    snobol_pattern_t *pat = snobol_pattern_compile(ctx, src, len, &err);
    test_assert(pat == nullptr, "snobol_pattern_compile rejects deep nesting");
    test_assert(
        (err != nullptr && strstr(err, "nesting too deep") != nullptr) != 0,
        "compile API surfaces the nesting error");
    if (pat) {
      snobol_pattern_free(pat);
    }
    free(err);
    snobol_context_destroy(ctx);
    free(src);
  }

#if !defined(_WIN32)
  /* The guard must fire before the recursion overruns the stack, not after.
   * 512 KiB is the macOS default for a secondary thread, the smallest stack
   * a supported host hands out by default.  ASan inflates every frame, so it
   * needs 2 MiB to reach the same depth. */
#if defined(SNOBOL_SANITIZER)
  const size_t depth_stack_bytes = 2 * 1024 * 1024;
#else
  const size_t depth_stack_bytes = 512 * 1024;
#endif
  {
    depth_stack_job_t job;

    src = build_depth_parens(attack_n, &len);
    if (compile_on_stack(src, len, depth_stack_bytes, &job)) {
      test_assert(job.compiled == false,
                  "deep nesting rejected on a small thread stack");
      test_assert(job.saw_depth_error != 0,
                  "small-stack rejection is the depth error");
    }
    free(src);

    src = build_depth_parens(ok_n, &len);
    if (compile_on_stack(src, len, depth_stack_bytes, &job)) {
      test_assert(job.compiled != 0,
                  "under-limit nesting compiles on a small thread stack");
    }
    free(src);

    /* Function-call and dynamic-eval productions re-enter the descent with
     * much larger frames than a group; the byte budget must stop them.  The
     * EVAL nest stays below the level cap in both build types, so it is
     * rejected only when the byte budget is doing the work. */
    src = build_depth_call("ARBNO(", SNOBOL_PARSER_MAX_DEPTH - 1, &len);
    if (compile_on_stack(src, len, depth_stack_bytes, &job)) {
      test_assert(job.compiled == false,
                  "nested ARBNO rejected on a small thread stack");
      test_assert(job.saw_depth_error != 0,
                  "ARBNO rejection is a nesting error");
    }
    free(src);

    src = build_depth_call("EVAL(", SNOBOL_PARSER_MAX_DEPTH - 1, &len);
    if (compile_on_stack(src, len, depth_stack_bytes, &job)) {
      test_assert(job.compiled == false,
                  "nested EVAL rejected on a small thread stack");
      test_assert(job.saw_stack_error != 0,
                  "EVAL rejection comes from the parser stack budget");
    }
    free(src);
  }
#endif

  /* A depth-limit error leaves the parser reusable. */
  {
    snobol_parser_t *parser = snobol_parser_create();
    src = build_depth_parens(attack_n, &len);
    snobol_lexer_t *lexer = snobol_lexer_create(src, len);
    ast_node_t *ast = snobol_parser_parse(parser, lexer);
    test_assert(ast == nullptr, "first parse hits the depth limit");
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_lexer_destroy(lexer);
    free(src);

    lexer = snobol_lexer_create("'ok'", 4);
    ast = snobol_parser_parse(parser, lexer);
    test_assert((ast != nullptr && !snobol_parser_has_error(parser)) != 0,
                "parser reusable after a depth-limit error");
    if (ast) {
      snobol_ast_free(ast);
    }
    snobol_lexer_destroy(lexer);
    snobol_parser_destroy(parser);
  }
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
  test_parser_depth_limit();
  test_parser_integer_argument_boundaries();
  test_cov_parser_labels_and_gotos();
  test_cov_parser_repetition_and_primary();
  test_cov_parser_functions();
  test_cov_parser_round3();
  test_cov_parser_source_primitives();
  test_cov_parser_emit_table_assign();
  test_cov_parser_naming_ops();
  test_cov_parser_error_paths();
}
