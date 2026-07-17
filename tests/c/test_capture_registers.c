/**
 * test_capture_registers.c
 *
 * Tests for register-style captures (P3): captures are stored as
 * (subject, offset, length) registers in snobol_match_t and materialized
 * lazily into owned byte copies only when snobol_match_get_variable() is
 * called.  These tests exercise the lazy-materialization contract directly
 * (the engine populates the registers; the accessor does the on-demand copy)
 * and verify:
 *   - an unmaterialized register reads back the correct source span
 *   - unread registers stay NULL (zero per-match allocation)
 *   - reading materializes exactly one owned copy
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snobol/snobol.h"

extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

/* Populate a match's register `i` with (subject, off, len) and leave the
 * materialized copy NULL, mirroring what the search API does per match. */
static void set_register(snobol_match_t *m, int i, const char *subject,
                        size_t off, size_t len) {
  m->var_subject = subject;
  m->var_off[i] = off;
  m->var_len[i] = len;
  m->var_values[i] = NULL;
  m->var_lens[i] = 0;
}

/* ── Lazy materialization produces correct bytes ────────────────────── */
static void test_lazy_correct(void) {
  test_suite("Captures: lazy materialization returns correct span");

  snobol_match_t *m = snobol_match_create();
  test_assert(m != NULL, "match allocated");
  if (!m)
    return;
  m->success = true;
  m->var_count = 2;

  const char *subject = "hello world";
  set_register(m, 0, subject, 0, 5);  /* "hello" */
  set_register(m, 1, subject, 6, 5);  /* "world" */

  /* Both unmaterialized before any read. */
  test_assert(m->var_values[0] == NULL && m->var_values[1] == NULL,
              "registers unmaterialized before read");

  size_t len = 0;
  const char *v0 = snobol_match_get_variable(m, "1", &len);
  test_assert(v0 != NULL && len == 5 && memcmp(v0, "hello", 5) == 0,
              "variable '1' materializes to 'hello'");
  test_assert(m->var_values[0] != NULL, "register 0 now materialized");

  const char *v1 = snobol_match_get_variable(m, "2", &len);
  test_assert(v1 != NULL && len == 5 && memcmp(v1, "world", 5) == 0,
              "variable '2' materializes to 'world'");

  /* Re-read returns the same cached owned copy. */
  const char *v0b = snobol_match_get_variable(m, "1", &len);
  test_assert(v0b == v0, "re-read returns cached copy (no re-alloc)");

  snobol_match_free(m);
}

/* ── Unread register stays NULL (zero per-match alloc) ───────────── */
static void test_unread_untouched(void) {
  test_suite("Captures: unread register stays unmaterialized");

  snobol_match_t *m = snobol_match_create();
  test_assert(m != NULL, "match allocated");
  if (!m)
    return;
  m->success = true;
  m->var_count = 2;

  const char *subject = "abc123";
  set_register(m, 0, subject, 0, 3); /* "abc" */
  set_register(m, 1, subject, 3, 3); /* "123" */

  /* Read only register 0. */
  size_t len = 0;
  const char *v0 = snobol_match_get_variable(m, "1", &len);
  test_assert(v0 != NULL && len == 3 && memcmp(v0, "abc", 3) == 0,
              "register 0 reads correctly");

  test_assert(m->var_values[0] != NULL, "register 0 materialized");
  test_assert(m->var_values[1] == NULL,
              "register 1 still unmaterialized after sibling read");

  snobol_match_free(m);
}

/* ── Empty (zero-width) register materializes to empty string ─────── */
static void test_empty(void) {
  test_suite("Captures: zero-width register materializes to empty");

  snobol_match_t *m = snobol_match_create();
  test_assert(m != NULL, "match allocated");
  if (!m)
    return;
  m->success = true;
  m->var_count = 1;

  const char *subject = "xyz";
  set_register(m, 0, subject, 1, 0); /* empty span at offset 1 */

  size_t len = 0;
  const char *v = snobol_match_get_variable(m, "1", &len);
  test_assert(v != NULL && len == 0 && v[0] == '\0',
              "zero-width register reads as empty string");

  snobol_match_free(m);
}

/* ── Absent variable returns NULL without allocating ──────────────── */
static void test_absent(void) {
  test_suite("Captures: absent variable returns NULL");

  snobol_match_t *m = snobol_match_create();
  test_assert(m != NULL, "match allocated");
  if (!m)
    return;
  m->success = true;
  m->var_count = 0;

  size_t len = 0;
  const char *v = snobol_match_get_variable(m, "1", &len);
  test_assert(v == NULL && len == 0, "absent variable is NULL");

  snobol_match_free(m);
}

void test_capture_registers_suite(void) {
  test_suite("Captures: register-style lazy materialization");
  test_lazy_correct();
  test_unread_untouched();
  test_empty();
  test_absent();
}
