/**
 * @file test_pattern_from_bytecode.c
 * @brief Tests for snobol_pattern_from_bytecode().
 *
 * Covers the bytecode-to-pattern constructor: compiler-produced bytecode
 * round-trips with the same metadata and behavior as the compiled pattern,
 * hand-built bytecode drives the VM without any struct replication, ownership
 * transfers to the pattern (checked under ASan), and invalid arguments are
 * rejected with a message.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/include/snobol/snobol.h"
#include "snobol/vm.h"

extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

static void put_u32_be(uint8_t *bc, size_t *at, uint32_t value) {
  bc[(*at)++] = (uint8_t)((value >> 24) & 0xFF);
  bc[(*at)++] = (uint8_t)((value >> 16) & 0xFF);
  bc[(*at)++] = (uint8_t)((value >> 8) & 0xFF);
  bc[(*at)++] = (uint8_t)(value & 0xFF);
}

/* --- Compiler-produced bytecode round-trip --- */

static void test_compiler_bytecode_roundtrip(void) {
  test_suite("Pattern from bytecode: compiler-produced round-trip");

  snobol_context_t *ctx = snobol_context_create();
  char *err = nullptr;
  const char *src = "('ab' | 'cd') 'ef'";
  snobol_pattern_t *compiled =
      snobol_pattern_compile_ex(ctx, src, strlen(src), 0, &err);
  test_assert(compiled != nullptr, "source pattern compiles");

  size_t bc_len = snobol_pattern_get_bc_len(compiled);
  uint8_t *copy = (uint8_t *)malloc(bc_len);
  test_assert(copy != nullptr, "bytecode copy allocated");
  memcpy(copy, snobol_pattern_get_bc(compiled), bc_len);

  snobol_pattern_t *from_bc =
      snobol_pattern_from_bytecode(copy, bc_len, 0, &err);
  test_assert(from_bc != nullptr, "pattern constructed from bytecode");
  test_assert(err == nullptr, "no error on success");

  test_assert(snobol_pattern_get_bc_len(from_bc) == bc_len &&
                  memcmp(snobol_pattern_get_bc(from_bc),
                         snobol_pattern_get_bc(compiled), bc_len) == 0,
              "the constructed pattern keeps the same bytecode");

  const snobol_search_meta_t *meta_a = snobol_pattern_get_meta(compiled);
  const snobol_search_meta_t *meta_b = snobol_pattern_get_meta(from_bc);
  test_assert(meta_a->tier == meta_b->tier,
              "tier matches the compiled pattern");

  size_t ra = 0;
  size_t rb = 0;
  (void)snobol_pattern_get_range_meta(compiled, &ra);
  (void)snobol_pattern_get_range_meta(from_bc, &rb);
  test_assert(ra == rb, "range metadata matches the compiled pattern");

  const char *subjects[] = {"abef", "cdef", "abe", "zz"};
  for (int i = 0; i < 4; i++) {
    const char *s = subjects[i];
    snobol_match_t *ma = snobol_pattern_match(compiled, s, strlen(s));
    snobol_match_t *mb = snobol_pattern_match(from_bc, s, strlen(s));
    test_assert(snobol_match_success(ma) == snobol_match_success(mb),
                "match success agrees");
    test_assert(ma->position == mb->position, "match position agrees");
    test_assert(ma->length == mb->length, "match length agrees");
    snobol_match_free(ma);
    snobol_match_free(mb);
  }

  snobol_match_t *sa = snobol_pattern_search(compiled, "xxcdefyy", 8);
  snobol_match_t *sb = snobol_pattern_search(from_bc, "xxcdefyy", 8);
  test_assert(snobol_match_success(sa) && snobol_match_success(sb),
              "search finds the pattern in both");
  test_assert(sa->position == sb->position && sa->length == sb->length,
              "search position and length agree");
  snobol_match_free(sa);
  snobol_match_free(sb);

  snobol_pattern_free(from_bc);
  snobol_pattern_free(compiled);
  snobol_context_destroy(ctx);
  free(err);
}

/* --- Hand-built bytecode --- */

static void test_handbuilt_bytecode(void) {
  test_suite("Pattern from bytecode: hand-built stream");

  /* CAP_START(0) LIT("ab") CAP_END(0) ACCEPT */
  uint8_t *bc = (uint8_t *)malloc(16);
  test_assert(bc != nullptr, "hand-built buffer allocated");
  size_t at = 0;
  bc[at++] = OP_CAP_START;
  bc[at++] = 0;
  bc[at++] = OP_LIT;
  put_u32_be(bc, &at, 11); /* literal text offset */
  put_u32_be(bc, &at, 2);
  bc[at++] = 'a';
  bc[at++] = 'b';
  bc[at++] = OP_CAP_END;
  bc[at++] = 0;
  bc[at++] = OP_ACCEPT;
  test_assert(at == 16, "hand-built stream sized as expected");

  char *err = nullptr;
  snobol_pattern_t *p = snobol_pattern_from_bytecode(bc, at, 0, &err);
  test_assert(p != nullptr, "hand-built pattern constructed");

  snobol_match_t *m = snobol_pattern_match(p, "ab", 2);
  test_assert(snobol_match_success(m) && m->length == 2,
              "hand-built pattern matches");
  size_t vlen = 0;
  const char *v = snobol_match_get_variable(m, "v0", &vlen);
  test_assert(v != nullptr && vlen == 2 && memcmp(v, "ab", 2) == 0,
              "hand-built capture is exposed");
  snobol_match_free(m);

  m = snobol_pattern_match(p, "x", 1);
  test_assert(!snobol_match_success(m), "hand-built pattern rejects a miss");
  snobol_match_free(m);

  /* Source-compiled equivalent: @w 'ab' captures the same span. */
  snobol_context_t *ctx = snobol_context_create();
  snobol_pattern_t *compiled =
      snobol_pattern_compile_ex(ctx, "@w 'ab'", strlen("@w 'ab'"), 0, &err);
  test_assert(compiled != nullptr, "source equivalent compiles");
  snobol_match_t *mc = snobol_pattern_match(compiled, "ab", 2);
  test_assert(snobol_match_success(mc) && mc->length == 2,
              "source equivalent reports the same length");
  snobol_match_free(mc);

  snobol_pattern_free(compiled);
  snobol_context_destroy(ctx);
  snobol_pattern_free(p);
  free(err);
}

/* --- Ownership and invalid arguments --- */

static void test_ownership_and_invalid_args(void) {
  test_suite("Pattern from bytecode: ownership and invalid arguments");

  char *err = nullptr;
  snobol_pattern_t *p = snobol_pattern_from_bytecode(nullptr, 0, 0, &err);
  test_assert(p == nullptr, "null buffer is rejected");
  test_assert(err != nullptr && err[0] != '\0', "null buffer reports an error");
  free(err);
  err = nullptr;

  /* Zero length with a compiler-owned buffer: the constructor consumes it. */
  snobol_context_t *ctx = snobol_context_create();
  snobol_pattern_t *tmp = snobol_pattern_compile_ex(ctx, "'x'", 3, 0, &err);
  test_assert(tmp != nullptr, "buffer source compiles");
  size_t bc_len = snobol_pattern_get_bc_len(tmp);
  uint8_t *copy = (uint8_t *)malloc(bc_len);
  memcpy(copy, snobol_pattern_get_bc(tmp), bc_len);
  snobol_pattern_free(tmp);
  p = snobol_pattern_from_bytecode(copy, 0, 0, &err);
  test_assert(p == nullptr, "zero length is rejected");
  test_assert(err != nullptr && err[0] != '\0', "zero length reports an error");
  free(err);
  err = nullptr;

  /* Unknown flag bits are ignored, like snobol_pattern_compile_ex(). */
  tmp = snobol_pattern_compile_ex(ctx, "'x'", 3, 0, &err);
  bc_len = snobol_pattern_get_bc_len(tmp);
  copy = (uint8_t *)malloc(bc_len);
  memcpy(copy, snobol_pattern_get_bc(tmp), bc_len);
  snobol_pattern_free(tmp);
  p = snobol_pattern_from_bytecode(copy, bc_len, 0x80000000u, &err);
  test_assert(p != nullptr, "unknown flags are ignored");
  test_assert(err == nullptr, "unknown flags do not set an error");
  snobol_pattern_free(p);

  snobol_context_destroy(ctx);
  free(err);
}

/* The table binder walks the instruction stream to patch table ids, so it must
 * size every opcode layout exactly like the VM.  A crafted stream containing
 * one instruction of each layout pins that contract without executing it. */
static void test_bytecode_walker_opcode_sizes(void) {
  test_suite("Pattern from bytecode: walker sizes every opcode");

  uint8_t bc[256];
  memset(bc, 0, sizeof(bc));
  size_t at = 0;

  bc[at++] = OP_EMIT_EXPR;
  bc[at++] = 0;
  bc[at++] = 0;

  bc[at++] = OP_EVAL;
  bc[at++] = 0;
  bc[at++] = 0;
  bc[at++] = 0;

  bc[at++] = OP_REPEAT_STEP;
  for (int i = 0; i < 5; i++) {
    bc[at++] = 0;
  }

  bc[at++] = OP_REPEAT_INIT;
  for (int i = 0; i < 13; i++) {
    bc[at++] = 0;
  }

  bc[at++] = OP_EMIT_FORMAT;
  bc[at++] = 0;
  bc[at++] = 0;

  bc[at++] = OP_EMIT_FORMAT;
  bc[at++] = 0;
  bc[at++] = SNBL_FMT_LPAD;
  bc[at++] = 0;
  bc[at++] = 0;
  bc[at++] = 0;

  bc[at++] = OP_LIT;
  put_u32_be(bc, &at, (uint32_t)(at + 8)); /* inline payload offset */
  put_u32_be(bc, &at, 2);
  bc[at++] = 'a';
  bc[at++] = 'b';

  bc[at++] = OP_ARRAY_GET;
  bc[at++] = 0;
  bc[at++] = 0;
  bc[at++] = 0;
  bc[at++] = 0;
  bc[at++] = 1;
  bc[at++] = 'a';

  bc[at++] = OP_ARRAY_SET;
  bc[at++] = 0;
  bc[at++] = 0;
  bc[at++] = 0;
  bc[at++] = 0;
  bc[at++] = 1;
  bc[at++] = 's';

  bc[at++] = OP_EMIT_TABLE;
  bc[at++] = 0;
  bc[at++] = 0;
  bc[at++] = 1; /* capture key */
  bc[at++] = 1; /* name length */
  bc[at++] = 't';
  bc[at++] = 0; /* key register */

  bc[at++] = OP_EMIT_TABLE;
  bc[at++] = 0;
  bc[at++] = 0;
  bc[at++] = 0; /* literal key */
  bc[at++] = 1; /* name length */
  bc[at++] = 'u';
  bc[at++] = 0; /* key length high byte */
  bc[at++] = 1; /* key length low byte */
  bc[at++] = 'k';

  bc[at++] = OP_DYNAMIC_DEF;
  put_u32_be(bc, &at, 0);
  put_u32_be(bc, &at, 0);

  bc[at++] = OP_ACCEPT;

  test_assert(snobol_pattern_bind_bytecode_tables(bc, at, nullptr, 0) == 0,
              "the binder walks every opcode layout");
  test_assert(snobol_pattern_bind_bytecode_tables(nullptr, 0, nullptr, 0) == -1,
              "null bytecode is rejected");

  const char *names_one[] = {"x"};
  test_assert(snobol_pattern_bind_bytecode_tables(bc, at, names_one,
                                                  SNBL_TABLE_ID_UNBOUND) == -1,
              "an exhausted table id space is rejected");

  uint8_t trunc[16];
  memset(trunc, 0, sizeof(trunc));
  trunc[0] = OP_LIT;
  test_assert(snobol_pattern_bind_bytecode_tables(trunc, 8, nullptr, 0) == -1,
              "truncated operands are rejected");
  trunc[0] = 0xFF;
  test_assert(snobol_pattern_bind_bytecode_tables(trunc, 2, nullptr, 0) == -1,
              "unknown opcodes are rejected");

  /* Malformed table instructions: each guard must reject, not read past the
   * buffer or silently patch a straddling instruction. */
  uint8_t bad_table[8];
  memset(bad_table, 0, sizeof(bad_table));
  bad_table[0] = OP_TABLE_GET;
  test_assert(snobol_pattern_bind_bytecode_tables(bad_table, 3, nullptr, 0) ==
                  -1,
              "truncated table operands are rejected");
  bad_table[3] = 2; /* invalid key kind */
  test_assert(snobol_pattern_bind_bytecode_tables(bad_table, 6, nullptr, 0) ==
                  -1,
              "an invalid table key kind is rejected");
  memset(bad_table, 0, sizeof(bad_table));
  bad_table[0] = OP_TABLE_GET;
  bad_table[3] = 1; /* capture key with no key byte */
  test_assert(snobol_pattern_bind_bytecode_tables(bad_table, 5, nullptr, 0) ==
                  -1,
              "a truncated capture key is rejected");
  memset(bad_table, 0, sizeof(bad_table));
  bad_table[0] = OP_TABLE_GET; /* key kind 0, no room for the name length */
  test_assert(snobol_pattern_bind_bytecode_tables(bad_table, 6, nullptr, 0) ==
                  -1,
              "a missing table name length is rejected");
  memset(bad_table, 0, sizeof(bad_table));
  bad_table[0] = OP_TABLE_GET;
  bad_table[5] = 1; /* name length at 6 */
  bad_table[6] = 5; /* name length 5, but no name bytes */
  test_assert(snobol_pattern_bind_bytecode_tables(bad_table, 7, nullptr, 0) ==
                  -1,
              "a truncated table name is rejected");
}

void test_pattern_from_bytecode_suite(void) {
  test_compiler_bytecode_roundtrip();
  test_handbuilt_bytecode();
  test_ownership_and_invalid_args();
  test_bytecode_walker_opcode_sizes();
}
