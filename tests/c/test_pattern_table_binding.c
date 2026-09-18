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
 * Semantics note: a bound table read resolves when the captured key exists
 * in the table and the read succeeds zero-width.  Classic SNOBOL4 instead
 * matches the stored VALUE against the subject (verified against CSNOBOL4
 * 2.3.1: `'hello world' T<'k'>` matches when `T<'k'> = 'hello'`, while
 * `'k world' T<'k'>` does not); the value-matching form is not implemented.
 * The tests below pin the implemented behaviour.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/include/snobol/snobol.h"
#include "snobol/table.h"

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

/* --- Bound read resolves, unbound read fails --- */

static void test_bound_read_resolves(void) {
  test_suite("Pattern Table Binding: bound read resolves");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;
  snobol_pattern_t *p = snobol_pattern_compile_ex(ctx, "T['k']", 6, 0, &err);
  test_assert(p != nullptr, "T['k'] compiles");

  snobol_table_t *table = table_create("T");
  (void)table_set(table, "k", "value");

  /* Unbound: the table op fails and the match backtracks. */
  snobol_match_t *m = snobol_pattern_match(p, "k", 1);
  test_assert(!snobol_match_success(m), "unbound read fails");
  snobol_match_free(m);

  bind_T(p, table);

  /* Bound: the captured key resolves through the table. */
  m = snobol_pattern_match(p, "k", 1);
  test_assert(snobol_match_success(m), "bound read succeeds");
  snobol_match_free(m);

  /* Key missing from the bound table: fails like unbound. */
  m = snobol_pattern_match(p, "z", 1);
  test_assert(!snobol_match_success(m),
              "read of a missing key fails (no match)");
  snobol_match_free(m);

  /* Current semantics: the KEY text is what matches the subject; the stored
   * value is not matched against it (see the file header). */
  m = snobol_pattern_match(p, "value", 5);
  test_assert(!snobol_match_success(m),
              "stored value is not matched against the subject");
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
  snobol_match_t *m = snobol_pattern_match(p, "kv", 2);
  test_assert(!snobol_match_success(m), "unbound write fails");
  snobol_match_free(m);
  test_assert(!table_has(table, "k"), "unbound write leaves the table empty");

  bind_T(p, table);

  m = snobol_pattern_match(p, "kv", 2);
  test_assert(snobol_match_success(m), "bound write succeeds");
  snobol_match_free(m);

  const char *got = table_get(table, "k");
  test_assert(got != nullptr && strcmp(got, "v") == 0,
              "table holds the written value");

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

  snobol_match_t *m = snobol_pattern_match(p, "ab", 2);
  test_assert(snobol_match_success(m), "both tables resolve in one match");
  snobol_match_free(m);

  /* Re-bind with the names reordered: ids are re-patched to the new order. */
  const char *names_rev[2] = {"A", "T"};
  snobol_table_t *tables_rev[2] = {a, t};
  test_assert(snobol_pattern_bind_tables(p, names_rev, tables_rev, 2) == 0,
              "re-binding with reordered names succeeds");
  m = snobol_pattern_match(p, "ab", 2);
  test_assert(snobol_match_success(m),
              "reordered re-bind still resolves both tables");
  snobol_match_free(m);

  /* Re-bind with only one of the two names: the other fails again. */
  const char *names_one[1] = {"T"};
  snobol_table_t *tables_one[1] = {t};
  test_assert(snobol_pattern_bind_tables(p, names_one, tables_one, 1) == 0,
              "re-binding a subset succeeds");
  m = snobol_pattern_match(p, "ab", 2);
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

  snobol_match_t *m = snobol_pattern_match(p, "k", 1);
  test_assert(snobol_match_success(m), "bound read succeeds before clearing");
  snobol_match_free(m);

  test_assert(snobol_pattern_bind_tables(p, nullptr, nullptr, 0) == 0,
              "clearing the binding succeeds");
  m = snobol_pattern_match(p, "k", 1);
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
  snobol_match_t *msrc = snobol_pattern_match(src, "k", 1);
  snobol_match_t *mbuilt = snobol_pattern_match(built, "k", 1);
  test_assert(!snobol_match_success(msrc) && !snobol_match_success(mbuilt),
              "both twins fail while unbound");
  snobol_match_free(msrc);
  snobol_match_free(mbuilt);

  bind_T(src, table);
  bind_T(built, table);

  msrc = snobol_pattern_match(src, "k", 1);
  mbuilt = snobol_pattern_match(built, "k", 1);
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
  msrc = snobol_pattern_match(src_w, "kv", 2);
  mbuilt = snobol_pattern_match(built_w, "kv", 2);
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

  snobol_match_t *m = snobol_pattern_match(p, "z", 1);
  test_assert(snobol_match_success(m),
              "register-reference key resolves through the table");
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
  snobol_match_t *m = snobol_pattern_match(p, "k", 1);
  test_assert(!snobol_match_success(m), "unbound fails cleanly");
  snobol_match_free(m);

  bind_T(p, table);
  m = snobol_pattern_match(p, "k", 1);
  test_assert(snobol_match_success(m), "the binding reaches the late table op");
  snobol_match_free(m);

  snobol_pattern_free(p);

  /* Same shape with an ABORT() branch. */
  p = snobol_pattern_compile_ex(ctx, "('a' ABORT()) | T['k']",
                                strlen("('a' ABORT()) | T['k']"), 0, &err);
  test_assert(p != nullptr, "('a' ABORT()) | T['k'] compiles");
  bind_T(p, table);
  m = snobol_pattern_match(p, "k", 1);
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

  snobol_match_t *m = snobol_pattern_search_ex(state, "xxk", 3, 0);
  test_assert(m != nullptr && snobol_match_success(m),
              "stateful search resolves the bound read");
  m = snobol_pattern_search_ex(state, "xxk", 3, 0);
  test_assert(m != nullptr && snobol_match_success(m),
              "second call still resolves (registry reused)");

  /* Clearing the state-level tables makes the op fail again. */
  test_assert(snobol_pattern_search_state_set_tables(state, nullptr, 0) == 0,
              "state-level clear succeeds");
  m = snobol_pattern_search_ex(state, "xxk", 3, 0);
  test_assert(m == nullptr || !snobol_match_success(m),
              "cleared state tables fail again");

  /* A pattern associated with the state supplies the binding instead. */
  bind_T(p, table);
  snobol_pattern_search_state_set_pattern(state, p);
  m = snobol_pattern_search_ex(state, "xxk", 3, 0);
  test_assert(m != nullptr && snobol_match_success(m),
              "pattern-sourced binding resolves in the stateful path");

  /* Clearing the pattern's binding drops the registry again. */
  test_assert(snobol_pattern_bind_tables(p, nullptr, nullptr, 0) == 0,
              "pattern clear succeeds");
  m = snobol_pattern_search_ex(state, "xxk", 3, 0);
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

  snobol_match_t *m = snobol_pattern_search(p, "xxk", 3);
  test_assert(snobol_match_success(m) && m->position == 2,
              "snobol_pattern_search resolves the bound read");
  snobol_match_free(m);

  snobol_match_t *reuse = snobol_match_create();
  test_assert(snobol_pattern_search_reuse(p, "xxk", 3, reuse) != 0,
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
    snobol_match_t *m = snobol_pattern_match(p, "k", 1);
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

void test_pattern_table_binding_suite(void) {
  test_bound_read_resolves();
  test_bound_write_lands();
  test_multiple_tables_and_rebind();
  test_clear_binding();
  test_source_and_builder_twins();
  test_register_reference_key();
  test_tier_unchanged();
  test_table_op_after_terminal_op();
  test_search_state_tables();
  test_stateless_search_paths();
  test_bind_cycles();
}
