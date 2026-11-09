/**
 * @file test_pattern_table_binding.c
 * @brief Tests for binding host tables to compiled patterns.
 *
 * Covers the binding API (`snobol_pattern_bind_tables`,
 * `snobol_pattern_bind_bytecode_tables`), the execution-VM registration in
 * the match/search paths, re-binding and clearing, the bytecode-only
 * search-state path (`snobol_pattern_search_state_set_tables`), and the
 * classification guarantees (table-bearing patterns stay on the general
 * tier).
 *
 * Semantics: a bound table read matches the STORED VALUE against the subject
 * at the current position (classic SNOBOL4, verified against CSNOBOL4 2.3.1:
 * `'hello world' T<'k'>` matches when `T<'k'> = 'hello'`, while
 * `'k world' T<'k'>` does not).  The key is data: a literal key is never
 * matched against or captured from the subject, and a register key reads an
 * existing capture.  The value is consumed on success; an absent key fails.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/include/snobol/snobol.h"
#include "snobol/table.h"
#include "snobol/vm.h"

/* External test framework functions */
extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

/* Bind `pattern` to a single table named "T". */
static void bind_T(snobol_pattern_t *pattern, snobol_table_t *table) {
  const char *names[1] = {"T"};
  snobol_table_t *tables[1] = {table};
  test_assert(snobol_pattern_bind_tables(pattern, names, tables, 1) == 0,
              "bind T succeeds");
}

/* --- Bound read matches the stored value --- */

static void test_bound_read_matches_value(void) {
  test_suite("Pattern Table Binding: bound read matches the stored value");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;
  snobol_pattern_t *p = snobol_pattern_compile_ex(ctx, "T['k']", 6, 0, &err);
  test_assert(p != nullptr, "T['k'] compiles");

  snobol_table_t *table = table_create("T");
  (void)table_set(table, "k", "value");

  /* Unbound: the table op fails and the match backtracks. */
  snobol_match_t *m = snobol_pattern_match(p, "value", 5);
  test_assert(!snobol_match_success(m), "unbound read fails");
  snobol_match_free(m);

  bind_T(p, table);

  /* Bound: the stored value is matched at the current position. */
  m = snobol_pattern_match(p, "value", 5);
  test_assert(snobol_match_success(m), "bound read matches the stored value");
  test_assert(m->position == 0 && m->length == 5,
              "read reports the value's span");
  snobol_match_free(m);

  /* The value is a prefix match: trailing subject bytes are left alone. */
  m = snobol_pattern_match(p, "value tail", 10);
  test_assert(snobol_match_success(m) && m->length == 5,
              "read consumes exactly the value");
  snobol_match_free(m);

  /* The key text is data, not the match: it must not match the subject. */
  m = snobol_pattern_match(p, "k", 1);
  test_assert(!snobol_match_success(m),
              "key text alone does not match (value is what matches)");
  snobol_match_free(m);

  /* Replacing the stored value changes what matches. */
  (void)table_set(table, "k", "other");
  m = snobol_pattern_match(p, "value", 5);
  test_assert(!snobol_match_success(m), "stale value no longer matches");
  snobol_match_free(m);
  m = snobol_pattern_match(p, "other", 5);
  test_assert(snobol_match_success(m), "new stored value matches");
  snobol_match_free(m);

  /* A key absent from the bound table fails. */
  snobol_pattern_t *missing =
      snobol_pattern_compile_ex(ctx, "T['q']", 6, 0, &err);
  test_assert(missing != nullptr, "T['q'] compiles");
  bind_T(missing, table);
  m = snobol_pattern_match(missing, "other", 5);
  test_assert(!snobol_match_success(m), "read of a missing key fails");
  snobol_match_free(m);
  snobol_pattern_free(missing);

  snobol_pattern_free(p);
  table_release(table);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- The value matches at the read's position, not at the start --- */

static void test_bound_read_position(void) {
  test_suite("Pattern Table Binding: read matches at its position");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;
  snobol_pattern_t *p =
      snobol_pattern_compile_ex(ctx, "'k' T['k']", 10, 0, &err);
  test_assert(p != nullptr, "'k' T['k'] compiles");

  snobol_table_t *table = table_create("T");
  (void)table_set(table, "k", "value");
  bind_T(p, table);

  snobol_match_t *m = snobol_pattern_match(p, "kvalue", 6);
  test_assert(snobol_match_success(m) && m->length == 6,
              "read matches at the position after the prefix");
  snobol_match_free(m);

  m = snobol_pattern_match(p, "value", 5);
  test_assert(!snobol_match_success(m),
              "value at the wrong position does not match");
  snobol_match_free(m);

  snobol_pattern_free(p);
  table_release(table);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- Bound write lands in the table --- */

static void test_bound_write_lands(void) {
  test_suite("Pattern Table Binding: bound write lands");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;
  snobol_pattern_t *p =
      snobol_pattern_compile_ex(ctx, "T['k'] = 'v'", 11, 0, &err);
  test_assert(p != nullptr, "T['k'] = 'v' compiles");

  snobol_table_t *table = table_create("T");

  /* Unbound write must not touch any table (and must not crash). */
  snobol_match_t *m = snobol_pattern_match(p, "v", 1);
  test_assert(!snobol_match_success(m), "unbound write fails");
  snobol_match_free(m);
  test_assert(!table_has(table, "k"), "unbound write leaves the table empty");

  bind_T(p, table);

  m = snobol_pattern_match(p, "v", 1);
  test_assert(snobol_match_success(m), "bound write succeeds");
  snobol_match_free(m);

  const char *got = table_get(table, "k");
  test_assert(got != nullptr && strcmp(got, "v") == 0,
              "table holds the written value");

  /* The literal key is data: a prefix before the value still stores under
   * "k". */
  snobol_pattern_t *prefixed = snobol_pattern_compile_ex(
      ctx, "'a' T['k'] = 'v'", strlen("'a' T['k'] = 'v'"), 0, &err);
  test_assert(prefixed != nullptr, "prefixed write compiles");
  bind_T(prefixed, table);
  m = snobol_pattern_match(prefixed, "av", 2);
  test_assert(snobol_match_success(m), "prefixed write succeeds");
  snobol_match_free(m);
  test_assert(strcmp(table_get(table, "k"), "v") == 0,
              "prefixed write stores under the literal key");
  snobol_pattern_free(prefixed);

  snobol_pattern_free(p);
  table_release(table);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- Multiple tables, order, and re-binding --- */

static void test_multiple_tables_and_rebind(void) {
  test_suite("Pattern Table Binding: multiple tables and re-binding");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;
  snobol_pattern_t *p = snobol_pattern_compile_ex(
      ctx, "T['a'] A['b']", strlen("T['a'] A['b']"), 0, &err);
  test_assert(p != nullptr, "T['a'] A['b'] compiles");

  snobol_table_t *t = table_create("T");
  snobol_table_t *a = table_create("A");
  (void)table_set(t, "a", "1");
  (void)table_set(a, "b", "2");

  const char *names[2] = {"T", "A"};
  snobol_table_t *tables[2] = {t, a};
  test_assert(snobol_pattern_bind_tables(p, names, tables, 2) == 0,
              "binding two tables succeeds");

  snobol_match_t *m = snobol_pattern_match(p, "12", 2);
  test_assert(snobol_match_success(m) && m->length == 2,
              "both tables resolve in one match");
  snobol_match_free(m);

  /* Re-bind with the names reordered: ids are re-patched to the new order. */
  const char *names_rev[2] = {"A", "T"};
  snobol_table_t *tables_rev[2] = {a, t};
  test_assert(snobol_pattern_bind_tables(p, names_rev, tables_rev, 2) == 0,
              "re-binding with reordered names succeeds");
  m = snobol_pattern_match(p, "12", 2);
  test_assert(snobol_match_success(m),
              "reordered re-bind still resolves both tables");
  snobol_match_free(m);

  /* Re-bind with only one of the two names: the other fails again. */
  const char *names_one[1] = {"T"};
  snobol_table_t *tables_one[1] = {t};
  test_assert(snobol_pattern_bind_tables(p, names_one, tables_one, 1) == 0,
              "re-binding a subset succeeds");
  m = snobol_pattern_match(p, "12", 2);
  test_assert(!snobol_match_success(m),
              "the name absent from the re-bind fails again");
  snobol_match_free(m);

  snobol_pattern_free(p);
  table_release(t);
  table_release(a);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- Clearing the binding --- */

static void test_clear_binding(void) {
  test_suite("Pattern Table Binding: clearing");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;
  snobol_pattern_t *p = snobol_pattern_compile_ex(ctx, "T['k']", 6, 0, &err);
  test_assert(p != nullptr, "T['k'] compiles");

  snobol_table_t *table = table_create("T");
  (void)table_set(table, "k", "value");
  bind_T(p, table);

  snobol_match_t *m = snobol_pattern_match(p, "value", 5);
  test_assert(snobol_match_success(m), "bound read succeeds before clearing");
  snobol_match_free(m);

  test_assert(snobol_pattern_bind_tables(p, nullptr, nullptr, 0) == 0,
              "clearing the binding succeeds");
  m = snobol_pattern_match(p, "value", 5);
  test_assert(!snobol_match_success(m), "cleared binding fails again");
  snobol_match_free(m);

  /* The table itself stays usable; only the pattern's binding was dropped. */
  test_assert(table_get(table, "k") != nullptr,
              "clearing does not disturb the table");

  snobol_pattern_free(p);
  table_release(table);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- Source and Builder patterns bind identically --- */

static void test_source_and_builder_twins(void) {
  test_suite("Pattern Table Binding: source and Builder twins");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;

  /* Source form. */
  snobol_pattern_t *src = snobol_pattern_compile_ex(ctx, "T['k']", 6, 0, &err);
  test_assert(src != nullptr, "source T['k'] compiles");

  /* Builder twin: emits the same unbound table instruction. */
  snobol_pattern_build_t *b = snobol_pattern_build_create();
  ast_node_t *root = snobol_pattern_build_emit(
      b, snobol_ast_create_table_access("T", snobol_ast_create_lit("k", 1)));
  snobol_pattern_t *built = snobol_pattern_build_compile(ctx, root, 0, &err);
  snobol_pattern_build_destroy(b);
  test_assert(built != nullptr, "builder tableAccess compiles");

  snobol_table_t *table = table_create("T");
  (void)table_set(table, "k", "value");

  /* Both unbound: both fail. */
  snobol_match_t *msrc = snobol_pattern_match(src, "value", 5);
  snobol_match_t *mbuilt = snobol_pattern_match(built, "value", 5);
  test_assert(!snobol_match_success(msrc) && !snobol_match_success(mbuilt),
              "both twins fail while unbound");
  snobol_match_free(msrc);
  snobol_match_free(mbuilt);

  bind_T(src, table);
  bind_T(built, table);

  msrc = snobol_pattern_match(src, "value", 5);
  mbuilt = snobol_pattern_match(built, "value", 5);
  test_assert(snobol_match_success(msrc) && snobol_match_success(mbuilt),
              "both twins resolve the bound read");

  /* And the write twin. */
  snobol_match_free(msrc);
  snobol_match_free(mbuilt);

  snobol_pattern_t *src_w =
      snobol_pattern_compile_ex(ctx, "T['k'] = 'v'", 11, 0, &err);
  snobol_pattern_build_t *bw = snobol_pattern_build_create();
  ast_node_t *root_w = snobol_pattern_build_emit(
      bw, snobol_ast_create_table_update("T", snobol_ast_create_lit("k", 1),
                                         snobol_ast_create_lit("v", 1)));
  snobol_pattern_t *built_w =
      snobol_pattern_build_compile(ctx, root_w, 0, &err);
  snobol_pattern_build_destroy(bw);
  test_assert((src_w != nullptr && built_w != nullptr) != 0,
              "write twins compile");

  snobol_table_t *tw = table_create("T");
  bind_T(src_w, tw);
  bind_T(built_w, tw);
  msrc = snobol_pattern_match(src_w, "v", 1);
  mbuilt = snobol_pattern_match(built_w, "v", 1);
  test_assert(snobol_match_success(msrc) && snobol_match_success(mbuilt),
              "both write twins succeed");
  snobol_match_free(msrc);
  snobol_match_free(mbuilt);
  test_assert(strcmp(table_get(tw, "k"), "v") == 0,
              "the shared table holds the written value");

  snobol_pattern_free(src);
  snobol_pattern_free(built);
  snobol_pattern_free(src_w);
  snobol_pattern_free(built_w);
  table_release(table);
  table_release(tw);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- Register-reference keys (`T[$vN]`) --- */

static void test_register_reference_key(void) {
  test_suite("Pattern Table Binding: register-reference key");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;
  snobol_pattern_t *p =
      snobol_pattern_compile_ex(ctx, "@w 'z' T[$v0]", 13, 0, &err);
  test_assert(p != nullptr, "@w 'z' T[$v0] compiles");

  snobol_table_t *table = table_create("T");
  (void)table_set(table, "z", "1");
  bind_T(p, table);

  /* The capture supplies the key; the stored value is what matches. */
  snobol_match_t *m = snobol_pattern_match(p, "z1", 2);
  test_assert(snobol_match_success(m) && m->length == 2,
              "register-reference key matches the stored value");
  snobol_match_free(m);

  /* Key exists but the value differs: fail (not an existence check). */
  m = snobol_pattern_match(p, "z2", 2);
  test_assert(!snobol_match_success(m),
              "register-reference read rejects a non-matching value");
  snobol_match_free(m);

  /* Capture a different key: the table entry under it decides. */
  (void)table_set(table, "y", "2");
  snobol_pattern_t *py =
      snobol_pattern_compile_ex(ctx, "@w 'y' T[$v0]", 13, 0, &err);
  test_assert(py != nullptr, "@w 'y' T[$v0] compiles");
  bind_T(py, table);
  m = snobol_pattern_match(py, "y2", 2);
  test_assert(snobol_match_success(m), "value under the captured key matches");
  snobol_match_free(m);
  snobol_pattern_free(py);

  /* Missing key: fail. */
  snobol_pattern_t *pm =
      snobol_pattern_compile_ex(ctx, "@w 'q' T[$v0]", 13, 0, &err);
  test_assert(pm != nullptr, "@w 'q' T[$v0] compiles");
  bind_T(pm, table);
  m = snobol_pattern_match(pm, "q1", 2);
  test_assert(!snobol_match_success(m), "missing captured key fails");
  snobol_match_free(m);
  snobol_pattern_free(pm);

  snobol_pattern_free(p);
  table_release(table);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- Register-reference key on the write side --- */

static void test_register_key_write(void) {
  test_suite("Pattern Table Binding: register-reference key write");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;
  snobol_pattern_t *p = snobol_pattern_compile_ex(
      ctx, "@w 'z' T[$v0] = 'x'", strlen("@w 'z' T[$v0] = 'x'"), 0, &err);
  test_assert(p != nullptr, "register-key write compiles");

  snobol_table_t *table = table_create("T");
  bind_T(p, table);

  snobol_match_t *m = snobol_pattern_match(p, "zx", 2);
  test_assert(snobol_match_success(m), "register-key write succeeds");
  snobol_match_free(m);
  test_assert(strcmp(table_get(table, "z"), "x") == 0,
              "value stored under the captured key");

  snobol_pattern_free(p);
  table_release(table);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- Binary-safe keys: literal and capture-derived --- */

static void test_binary_safe_keys(void) {
  test_suite("Pattern Table Binding: binary-safe keys");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;

  snobol_table_t *table = table_create("T");
  test_assert(table_set_ex(table, "k\0v", 3, "hit", 3),
              "NUL-bearing key stored");

  /* Literal key with a NUL byte. */
  const char src[] = {'T', '[', '\'', 'k', '\0', 'v', '\'', ']'};
  snobol_pattern_t *p =
      snobol_pattern_compile_ex(ctx, src, sizeof(src), 0, &err);
  test_assert(p != nullptr, "NUL-bearing literal key compiles");
  bind_T(p, table);
  snobol_match_t *m = snobol_pattern_match(p, "hit", 3);
  test_assert(snobol_match_success(m), "NUL-bearing literal key matches value");
  snobol_match_free(m);
  snobol_pattern_free(p);

  /* Capture-derived key with a NUL byte: LEN(1) captures the NUL. */
  test_assert(table_set_ex(table, "\0", 1, "hit", 3),
              "NUL key stored under the capture-derived form");
  const char subject[] = {'\0', 'h', 'i', 't'};
  snobol_pattern_t *pc = snobol_pattern_compile_ex(
      ctx, "@w LEN(1) T[$v0]", strlen("@w LEN(1) T[$v0]"), 0, &err);
  test_assert(pc != nullptr, "capture-key pattern compiles");
  bind_T(pc, table);
  m = snobol_pattern_match(pc, subject, sizeof(subject));
  test_assert(snobol_match_success(m),
              "NUL-bearing capture-derived key matches value");
  snobol_match_free(m);
  snobol_pattern_free(pc);

  table_release(table);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- Empty stored value matches zero-width --- */

static void test_empty_value(void) {
  test_suite("Pattern Table Binding: empty stored value");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;

  snobol_table_t *table = table_create("T");
  test_assert(table_set(table, "k", ""), "empty value stored");

  snobol_pattern_t *p = snobol_pattern_compile_ex(ctx, "T['k']", 6, 0, &err);
  test_assert(p != nullptr, "T['k'] compiles");
  bind_T(p, table);

  snobol_match_t *m = snobol_pattern_match(p, "", 0);
  test_assert(snobol_match_success(m) && m->length == 0,
              "empty value matches an empty subject zero-width");
  snobol_match_free(m);

  m = snobol_pattern_match(p, "abc", 3);
  test_assert(snobol_match_success(m) && m->length == 0,
              "empty value matches zero-width at a non-empty subject");
  snobol_match_free(m);

  snobol_pattern_t *mid =
      snobol_pattern_compile_ex(ctx, "'a' T['k'] 'b'", 13, 0, &err);
  test_assert(mid != nullptr, "'a' T['k'] 'b' compiles");
  bind_T(mid, table);
  m = snobol_pattern_match(mid, "ab", 2);
  test_assert(snobol_match_success(m) && m->length == 2,
              "empty value matches zero-width between literals");
  snobol_match_free(m);
  snobol_pattern_free(mid);

  snobol_pattern_free(p);
  table_release(table);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- A missing key or a value mismatch backtracks to the alternative --- */

static void test_absent_key_backtracks(void) {
  test_suite("Pattern Table Binding: missing key backtracks");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;

  snobol_table_t *table = table_create("T");
  (void)table_set(table, "k", "value");

  /* Missing key: the first branch fails, the alternative matches. */
  snobol_pattern_t *p = snobol_pattern_compile_ex(
      ctx, "T['q'] | 'x'", strlen("T['q'] | 'x'"), 0, &err);
  test_assert(p != nullptr, "T['q'] | 'x' compiles");
  bind_T(p, table);
  snobol_match_t *m = snobol_pattern_match(p, "x", 1);
  test_assert(snobol_match_success(m), "missing key backtracks to 'x'");
  snobol_match_free(m);
  snobol_pattern_free(p);

  /* Key present, value mismatch: also backtracks. */
  p = snobol_pattern_compile_ex(ctx, "T['k'] | 'x'", strlen("T['k'] | 'x'"), 0,
                                &err);
  test_assert(p != nullptr, "T['k'] | 'x' compiles");
  bind_T(p, table);
  m = snobol_pattern_match(p, "x", 1);
  test_assert(snobol_match_success(m), "value mismatch backtracks to 'x'");
  snobol_match_free(m);
  snobol_pattern_free(p);

  table_release(table);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- Case-insensitive value matching (ASCII and BMP folding) --- */

static void test_case_insensitive_value(void) {
  test_suite("Pattern Table Binding: case-insensitive value matching");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;

  snobol_table_t *table = table_create("T");
  (void)table_set(table, "k", "Hello");
  (void)table_set_ex(table, "u", 1, "\xC3\x84", 2); /* Ä */

  snobol_pattern_t *ci = snobol_pattern_compile_ex(
      ctx, "T['k']", 6, SNOBOL_FLAG_CASE_INSENSITIVE, &err);
  test_assert(ci != nullptr, "case-insensitive T['k'] compiles");
  bind_T(ci, table);

  snobol_match_t *m = snobol_pattern_match(ci, "hello", 5);
  test_assert(snobol_match_success(m), "ASCII case-insensitive value matches");
  snobol_match_free(m);

  m = snobol_pattern_match(ci, "HELLO", 5);
  test_assert(snobol_match_success(m), "opposite ASCII case matches");
  snobol_match_free(m);

  m = snobol_pattern_match(ci, "hullo", 5);
  test_assert(!snobol_match_success(m), "non-matching CI value still fails");
  snobol_match_free(m);

  /* BMP folding (Latin-1 Supplement): Ä stored, ä in the subject. */
  snobol_pattern_t *ci_u = snobol_pattern_compile_ex(
      ctx, "T['u']", 6, SNOBOL_FLAG_CASE_INSENSITIVE, &err);
  test_assert(ci_u != nullptr, "case-insensitive T['u'] compiles");
  bind_T(ci_u, table);
  m = snobol_pattern_match(ci_u, "\xC3\xA4", 2); /* ä */
  test_assert(snobol_match_success(m), "BMP case-insensitive value matches");
  snobol_match_free(m);

  /* Without the flag the same subject does not match. */
  snobol_pattern_t *cs = snobol_pattern_compile_ex(ctx, "T['k']", 6, 0, &err);
  test_assert(cs != nullptr, "case-sensitive T['k'] compiles");
  bind_T(cs, table);
  m = snobol_pattern_match(cs, "hello", 5);
  test_assert(!snobol_match_success(m),
              "case-sensitive read rejects a folded match");
  snobol_match_free(m);

  snobol_pattern_free(ci);
  snobol_pattern_free(ci_u);
  snobol_pattern_free(cs);
  table_release(table);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- The matched value is usable through naming captures --- */

static void test_value_naming_capture(void) {
  test_suite("Pattern Table Binding: value naming capture");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;

  snobol_table_t *table = table_create("T");
  (void)table_set(table, "k", "size");

  /* $vN wraps the read: the captured span is the matched value. */
  snobol_pattern_t *p =
      snobol_pattern_compile_ex(ctx, "T['k'] $v1", 11, 0, &err);
  test_assert(p != nullptr, "T['k'] $v1 compiles");
  bind_T(p, table);
  snobol_match_t *m = snobol_pattern_match(p, "size", 4);
  test_assert(snobol_match_success(m), "naming read matches");
  size_t vlen = 0;
  const char *v = snobol_match_get_variable(m, "v1", &vlen);
  test_assert(v != nullptr && vlen == 4 && memcmp(v, "size", 4) == 0,
              "v1 holds the matched value");
  snobol_match_free(m);
  snobol_pattern_free(p);

  /* . @name binds a fresh capture register; the engine exposes captures by
   * register (vN), so the value is read back through the register the parser
   * allocated (v0 here — the pattern has no earlier capture). */
  p = snobol_pattern_compile_ex(ctx, "T['k'] . @val", 14, 0, &err);
  test_assert(p != nullptr, "T['k'] . @val compiles");
  bind_T(p, table);
  m = snobol_pattern_match(p, "size", 4);
  test_assert(snobol_match_success(m), "named read matches");
  v = snobol_match_get_variable(m, "v0", &vlen);
  test_assert(v != nullptr && vlen == 4 && memcmp(v, "size", 4) == 0,
              "the @val register holds the matched value");
  snobol_match_free(m);
  snobol_pattern_free(p);

  table_release(table);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- Tier classification is unchanged by binding --- */

static void test_tier_unchanged(void) {
  test_suite("Pattern Table Binding: tier classification unchanged");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;
  const char *pats[4] = {"T['k']", "T['k'] = 'v'", "('a' | T['k'])",
                         "('a' FAIL()) | T['k']"};
  for (int i = 0; i < 4; i++) {
    snobol_pattern_t *p =
        snobol_pattern_compile_ex(ctx, pats[i], strlen(pats[i]), 0, &err);
    test_assert(p != nullptr, "table pattern compiles");
    uint8_t before = snobol_pattern_get_meta(p)->tier;

    snobol_table_t *table = table_create("T");
    (void)table_set(table, "k", "value");
    bind_T(p, table);

    uint8_t after = snobol_pattern_get_meta(p)->tier;
    test_assert(before == after && before == TIER_GENERAL,
                "table pattern stays on the general tier across binding");
    snobol_pattern_free(p);
    table_release(table);
  }
  snobol_context_destroy(ctx);
  free(err);
}

/* --- Regression: table op after an early branch's terminal op --- */

static void test_table_op_after_terminal_op(void) {
  test_suite("Pattern Table Binding: table op after a terminal op");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;

  /* The second branch is laid out after the first branch's terminal op.
   * Matching must not route to the search-VM (no table opcode) and the
   * binding must still reach the table op. */
  snobol_pattern_t *p = snobol_pattern_compile_ex(
      ctx, "('a' FAIL()) | T['k']", strlen("('a' FAIL()) | T['k']"), 0, &err);
  test_assert(p != nullptr, "('a' FAIL()) | T['k'] compiles");
  test_assert(snobol_pattern_get_meta(p)->tier == TIER_GENERAL,
              "stays on the general tier");

  snobol_table_t *table = table_create("T");
  (void)table_set(table, "k", "value");

  /* Unbound: clean failure, no crash (this shape crashed before the
   * eligibility walks were bounded). */
  snobol_match_t *m = snobol_pattern_match(p, "value", 5);
  test_assert(!snobol_match_success(m), "unbound fails cleanly");
  snobol_match_free(m);

  bind_T(p, table);
  m = snobol_pattern_match(p, "value", 5);
  test_assert(snobol_match_success(m), "the binding reaches the late table op");
  snobol_match_free(m);

  snobol_pattern_free(p);

  /* Same shape with an ABORT() branch. */
  p = snobol_pattern_compile_ex(ctx, "('a' ABORT()) | T['k']",
                                strlen("('a' ABORT()) | T['k']"), 0, &err);
  test_assert(p != nullptr, "('a' ABORT()) | T['k'] compiles");
  bind_T(p, table);
  m = snobol_pattern_match(p, "value", 5);
  test_assert(snobol_match_success(m),
              "ABORT shape resolves the late table op");
  snobol_match_free(m);
  snobol_pattern_free(p);

  table_release(table);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- Bytecode-only search state path --- */

static void test_search_state_tables(void) {
  test_suite("Pattern Table Binding: search-state tables");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;
  snobol_pattern_t *p = snobol_pattern_compile_ex(ctx, "T['k']", 6, 0, &err);
  test_assert(p != nullptr, "T['k'] compiles");

  const char *names[1] = {"T"};
  test_assert(snobol_pattern_bind_bytecode_tables(
                  (uint8_t *)snobol_pattern_get_bc(p),
                  snobol_pattern_get_bc_len(p), names, 1) == 0,
              "bytecode binding succeeds");

  snobol_table_t *table = table_create("T");
  (void)table_set(table, "k", "value");
  snobol_table_t *tables[1] = {table};

  snobol_pattern_search_state_t *state = snobol_pattern_search_state_create(
      snobol_pattern_get_bc(p), snobol_pattern_get_bc_len(p));
  test_assert(state != nullptr, "search state created");
  test_assert(snobol_pattern_search_state_set_tables(state, tables, 1) == 0,
              "state-level tables set");

  /* snobol_pattern_search_ex returns a match owned by the state: it must not
   * be freed by the caller (the next call or state destruction recycles it). */
  snobol_match_t *m = snobol_pattern_search_ex(state, "xxvalue", 7, 0);
  test_assert(m != nullptr && snobol_match_success(m) && m->position == 2,
              "stateful search finds the stored value");
  m = snobol_pattern_search_ex(state, "xxvalue", 7, 0);
  test_assert(m != nullptr && snobol_match_success(m) && m->position == 2,
              "second call still resolves (registry reused)");

  /* Clearing the state-level tables makes the op fail again. */
  test_assert(snobol_pattern_search_state_set_tables(state, nullptr, 0) == 0,
              "state-level clear succeeds");
  m = snobol_pattern_search_ex(state, "xxvalue", 7, 0);
  test_assert(m == nullptr || !snobol_match_success(m),
              "cleared state tables fail again");

  /* A pattern associated with the state supplies the binding instead. */
  bind_T(p, table);
  snobol_pattern_search_state_set_pattern(state, p);
  m = snobol_pattern_search_ex(state, "xxvalue", 7, 0);
  test_assert(m != nullptr && snobol_match_success(m),
              "pattern-sourced binding resolves in the stateful path");

  /* Clearing the pattern's binding drops the registry again. */
  test_assert(snobol_pattern_bind_tables(p, nullptr, nullptr, 0) == 0,
              "pattern clear succeeds");
  m = snobol_pattern_search_ex(state, "xxvalue", 7, 0);
  test_assert(m == nullptr || !snobol_match_success(m),
              "pattern clear drops the state registry");

  snobol_pattern_search_state_destroy(state);
  snobol_pattern_free(p);
  table_release(table);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- Stateless search paths resolve bound tables --- */

static void test_stateless_search_paths(void) {
  test_suite("Pattern Table Binding: stateless search paths");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;
  snobol_pattern_t *p = snobol_pattern_compile_ex(ctx, "T['k']", 6, 0, &err);
  test_assert(p != nullptr, "T['k'] compiles");

  snobol_table_t *table = table_create("T");
  (void)table_set(table, "k", "value");
  bind_T(p, table);

  snobol_match_t *m = snobol_pattern_search(p, "xxvalue", 7);
  test_assert(snobol_match_success(m) && m->position == 2,
              "snobol_pattern_search finds the stored value");
  snobol_match_free(m);

  snobol_match_t *reuse = snobol_match_create();
  test_assert(snobol_pattern_search_reuse(p, "xxvalue", 7, reuse) != 0,
              "snobol_pattern_search_reuse resolves the bound read");
  test_assert(reuse->position == 2, "reuse search position correct");
  snobol_match_free(reuse);

  snobol_pattern_free(p);
  table_release(table);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- Bind/unbind cycles keep refcounts balanced --- */

static void test_bind_cycles(void) {
  test_suite("Pattern Table Binding: bind/unbind cycles");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;
  snobol_pattern_t *p = snobol_pattern_compile_ex(ctx, "T['k']", 6, 0, &err);
  test_assert(p != nullptr, "T['k'] compiles");

  snobol_table_t *table = table_create("T");
  (void)table_set(table, "k", "value");

  for (int i = 0; i < 64; i++) {
    bind_T(p, table);
    snobol_match_t *m = snobol_pattern_match(p, "value", 5);
    test_assert(snobol_match_success(m), "cycle read succeeds");
    snobol_match_free(m);
    test_assert(snobol_pattern_bind_tables(p, nullptr, nullptr, 0) == 0,
                "cycle clear succeeds");
  }

  /* The table is still owned solely by this test: releasing it here must not
   * double-free, which would trip ASan/valgrind in the sanitizer runs. */
  table_release(table);
  snobol_pattern_free(p);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- Register-key writes keep the key when it shares the capture counter --- */

static void test_register_key_write_preserves_key(void) {
  test_suite("Pattern Table Binding: register-key write keeps the key");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;

  /* Register 1 holds the key 'z'; the value capture must not reuse it. */
  const char *src = "('z' $v1) 'q' T[$v1] = 'x'";
  snobol_pattern_t *p =
      snobol_pattern_compile_ex(ctx, src, strlen(src), 0, &err);
  test_assert(p != nullptr, "register-key write with a prior capture compiles");

  snobol_table_t *table = table_create("T");
  bind_T(p, table);

  snobol_match_t *m = snobol_pattern_match(p, "zqx", 3);
  test_assert(snobol_match_success(m), "register-key write matches");
  snobol_match_free(m);
  test_assert(strcmp(table_get(table, "z"), "x") == 0,
              "the captured key still addresses the write");
  test_assert(!table_has(table, "x"), "the value did not become the key");

  /* A register that was never captured must fail the write, not borrow the
   * value register. */
  const char *unset = "@a 'z' T[$v1] = 'x'";
  snobol_pattern_t *pu =
      snobol_pattern_compile_ex(ctx, unset, strlen(unset), 0, &err);
  test_assert(pu != nullptr, "unset register-key write compiles");
  snobol_table_t *t2 = table_create("T");
  bind_T(pu, t2);
  m = snobol_pattern_match(pu, "zx", 2);
  test_assert(!snobol_match_success(m),
              "an uncaptured key register fails the write");
  snobol_match_free(m);
  test_assert(table_size(t2) == 0, "no entry was written");

  snobol_pattern_free(pu);
  table_release(t2);
  snobol_pattern_free(p);
  table_release(table);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- A zero-width value pattern stores an empty value --- */

static void test_empty_value_write(void) {
  test_suite("Pattern Table Binding: empty value write");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;
  snobol_pattern_t *p = snobol_pattern_compile_ex(
      ctx, "T['k'] = ''", strlen("T['k'] = ''"), 0, &err);
  test_assert(p != nullptr, "empty-value write compiles");

  snobol_table_t *table = table_create("T");
  bind_T(p, table);

  snobol_match_t *m = snobol_pattern_match(p, "", 0);
  test_assert(snobol_match_success(m), "empty-value write matches zero-width");
  snobol_match_free(m);

  size_t len = 99;
  const char *got = table_get_ex(table, "k", 1, &len);
  test_assert(got != nullptr && len == 0, "the table holds an empty value");

  /* Round-trip: the read form matches an empty value zero-width. */
  snobol_pattern_t *read = snobol_pattern_compile_ex(ctx, "T['k']", 6, 0, &err);
  test_assert(read != nullptr, "read form compiles");
  bind_T(read, table);
  m = snobol_pattern_match(read, "", 0);
  test_assert(snobol_match_success(m) && m->length == 0,
              "the empty value reads back zero-width");
  snobol_match_free(m);

  snobol_pattern_free(read);
  snobol_pattern_free(p);
  table_release(table);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- Literal keys are not limited to 255 bytes --- */

static void test_long_literal_key(void) {
  test_suite("Pattern Table Binding: long literal key");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;

  char *key = (char *)malloc(301);
  test_assert(key != nullptr, "key buffer allocated");
  memset(key, 'k', 300);
  key[300] = '\0';

  char *src = (char *)malloc(320);
  int n = snprintf(src, 320, "T['%s']", key);
  test_assert(n > 0 && n < 320, "long-key source built");

  snobol_pattern_t *p = snobol_pattern_compile_ex(ctx, src, (size_t)n, 0, &err);
  test_assert(p != nullptr, "300-byte literal key compiles");

  snobol_table_t *table = table_create("T");
  test_assert(table_set_ex(table, key, 300, "v", 1), "long key stored");
  bind_T(p, table);

  snobol_match_t *m = snobol_pattern_match(p, "v", 1);
  test_assert(snobol_match_success(m), "the long key matches its value");
  snobol_match_free(m);

  snobol_pattern_free(p);
  table_release(table);
  snobol_context_destroy(ctx);
  free(err);
  free(src);
  free(key);
}

/* --- The binder stays inside the compiler-derived body --- */

static void test_binder_respects_body_bound(void) {
  test_suite("Pattern Table Binding: binder respects the body bound");

  /* One table instruction whose name bytes run past a trailer-derived body
   * of 8 bytes: [class_count u32 = 0][label_count u32 = 0][SNBL]. */
  uint8_t bc[20];
  memset(bc, 0, sizeof(bc));
  bc[0] = OP_TABLE_GET;
  bc[1] = 0xFF;
  bc[2] = 0xFF;
  bc[3] = 1; /* key kind: literal */
  bc[4] = 0; /* key length high */
  bc[5] = 1; /* key length low */
  bc[6] = 'k';
  bc[7] = 0; /* flags */
  bc[8] = 0; /* name length; also the class-count high byte */
  bc[16] = 'S';
  bc[17] = 'N';
  bc[18] = 'B';
  bc[19] = 'L';

  const char *names[1] = {"T"};
  int rc = snobol_pattern_bind_bytecode_tables(bc, sizeof(bc), names, 1);
  test_assert(rc != 0, "a table instruction straddling the body is rejected");
}

/* --- Unsupported key forms report why --- */

static void test_unsupported_key_reports_reason(void) {
  test_suite("Pattern Table Binding: unsupported key message");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;

  /* Builder::tableAccess with a concatenation key: no compile-time value. */
  snobol_pattern_build_t *b = snobol_pattern_build_create();
  ast_node_t **parts = (ast_node_t **)malloc(2 * sizeof(ast_node_t *));
  parts[0] = snobol_pattern_build_lit(b, "a", 1);
  parts[1] = snobol_pattern_build_lit(b, "b", 1);
  ast_node_t *key = snobol_pattern_build_concat(b, parts, 2);
  ast_node_t *root =
      snobol_pattern_build_emit(b, snobol_ast_create_table_access("T", key));
  snobol_pattern_t *p = snobol_pattern_build_compile(ctx, root, 0, &err);
  test_assert(p == nullptr, "a non-literal key does not compile");
  test_assert(err != nullptr && strstr(err, "table key") != nullptr,
              "the error names the table key requirement");
  snobol_pattern_build_destroy(b);
  free(err);
  err = nullptr;

  /* A literal above the u16 operand bound. */
  size_t big_len = 65536;
  char *big = (char *)malloc(big_len);
  test_assert(big != nullptr, "oversized key buffer allocated");
  memset(big, 'k', big_len);
  b = snobol_pattern_build_create();
  ast_node_t *big_root = snobol_pattern_build_emit(
      b, snobol_ast_create_table_access(
             "T", snobol_pattern_build_lit(b, big, big_len)));
  p = snobol_pattern_build_compile(ctx, big_root, 0, &err);
  test_assert(p == nullptr, "a key above 65535 bytes does not compile");
  test_assert(err != nullptr && strstr(err, "65535") != nullptr,
              "the error states the key length bound");
  snobol_pattern_build_destroy(b);
  free(err);
  free(big);

  snobol_context_destroy(ctx);
}

void test_pattern_table_binding_suite(void) {
  test_bound_read_matches_value();
  test_bound_read_position();
  test_bound_write_lands();
  test_multiple_tables_and_rebind();
  test_clear_binding();
  test_source_and_builder_twins();
  test_register_reference_key();
  test_register_key_write();
  test_register_key_write_preserves_key();
  test_empty_value_write();
  test_long_literal_key();
  test_binary_safe_keys();
  test_empty_value();
  test_absent_key_backtracks();
  test_case_insensitive_value();
  test_value_naming_capture();
  test_tier_unchanged();
  test_table_op_after_terminal_op();
  test_search_state_tables();
  test_stateless_search_paths();
  test_bind_cycles();
  test_binder_respects_body_bound();
  test_unsupported_key_reports_reason();
}
