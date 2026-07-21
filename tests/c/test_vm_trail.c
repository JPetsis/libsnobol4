/*
 * test_vm_trail.c - Trail-based choice save (W2a) correctness tests
 *
 * The trail-based choice save (replacing full-state memcpy snapshots with an
 * undo trail) must produce bit-identical matches, captures, and EMIT output to
 * the legacy snapshot path. These tests run REPEAT / EMIT / CAP heavy patterns
 * in BOTH modes and assert the results are identical.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <stdlib.h> /* _putenv_s */
static int setenv(const char *name, const char *value, int overwrite) {
  (void)overwrite;
  return _putenv_s(name, value);
}
static int unsetenv(const char *name) {
  return _putenv_s(name, "");
}
#else
#include <unistd.h> /* setenv/unsetenv */
#endif

#include "snobol/vm.h"

/* External test framework functions */
extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

/* ── Bytecode builder ────────────────────────────────────────────────────── */

typedef struct {
  uint8_t bc[2048];
  size_t ip;
  size_t lit_pool;
} Bc;

static void bc_init(Bc *b) {
  memset(b, 0, sizeof(*b));
  b->ip = 0;
  b->lit_pool = 1024;
}

static uint32_t bc_add_lit(Bc *b, const char *bytes, size_t len) {
  uint32_t off = (uint32_t)b->lit_pool;
  memcpy(b->bc + b->lit_pool, bytes, len);
  b->lit_pool += len;
  return off;
}

static void bc_u8(Bc *b, uint8_t v) {
  b->bc[b->ip++] = v;
}
static void bc_u16(Bc *b, uint16_t v) {
  b->bc[b->ip++] = (uint8_t)((v >> 8) & 0xFF);
  b->bc[b->ip++] = (uint8_t)(v & 0xFF);
}
static void bc_u32(Bc *b, uint32_t v) {
  b->bc[b->ip++] = (uint8_t)((v >> 24) & 0xFF);
  b->bc[b->ip++] = (uint8_t)((v >> 16) & 0xFF);
  b->bc[b->ip++] = (uint8_t)((v >> 8) & 0xFF);
  b->bc[b->ip++] = (uint8_t)(v & 0xFF);
}
static void bc_lit1(Bc *b, char c) {
  uint32_t off = bc_add_lit(b, &c, 1);
  bc_u8(b, OP_LIT);
  bc_u32(b, off);
  bc_u32(b, 1);
}

/* Build `arbno( 'a' CAPTURE(r) EMIT_CAPTURE(r) )` style loop.
 * loop_id: which REPEAT loop to use. Returns filled Bc ready to ACCEPT. */
static void bc_build_repeat_emit(Bc *b, uint8_t loop_id, uint8_t cap_reg) {
  bc_init(b);

  size_t init_pos = b->ip;
  bc_u8(b, OP_REPEAT_INIT);
  bc_u8(b, loop_id);
  bc_u32(b, 0);            /* min */
  bc_u32(b, (uint32_t)-1); /* max = unbounded */
  size_t skip_off = b->ip;
  bc_u32(b, 0); /* skip_target placeholder */

  size_t body_start = b->ip;
  bc_u8(b, OP_CAP_START);
  bc_u8(b, cap_reg);
  bc_lit1(b, 'a');
  bc_u8(b, OP_CAP_END);
  bc_u8(b, cap_reg);
  bc_u8(b, OP_EMIT_CAPTURE);
  bc_u8(b, cap_reg);

  bc_u8(b, OP_REPEAT_STEP);
  bc_u8(b, loop_id);
  bc_u32(b, (uint32_t)body_start);

  size_t done_pos = b->ip;
  uint32_t v_done = (uint32_t)done_pos;
  b->bc[skip_off + 0] = (v_done >> 24) & 0xFF;
  b->bc[skip_off + 1] = (v_done >> 16) & 0xFF;
  b->bc[skip_off + 2] = (v_done >> 8) & 0xFF;
  b->bc[skip_off + 3] = v_done & 0xFF;

  /* NB: no trailing OP_ACCEPT — the caller appends what follows the loop. */
}

static VM make_vm(const uint8_t *bc, size_t bc_len, const char *subject,
                  snobol_buf *out) {
  VM vm;
  memset(&vm, 0, sizeof(vm));
  vm.bc = bc;
  vm.bc_len = bc_len;
  vm.s = subject;
  vm.len = strlen(subject);
  vm.ip = 0;
  vm.pos = 0;
  vm.var_count = 0;
  vm.out = out;
  return vm;
}

/* Run the same bytecode in legacy (snapshot) and compact (trail) mode and
 * assert identical results. */
typedef struct {
  bool ok;
  bool cap_set[64];
  size_t cap_start[64];
  size_t cap_end[64];
  char out[256];
  size_t out_len;
} TrailResult;

static TrailResult run_mode(const uint8_t *bc, size_t bc_len,
                            const char *subject, bool legacy) {
  TrailResult r;
  memset(&r, 0, sizeof(r));
  snobol_buf out;
  snobol_buf_init(&out);

  if (legacy)
    setenv("SNOBOL_LEGACY_CHOICE", "1", 1);
  else
    unsetenv("SNOBOL_LEGACY_CHOICE");

  VM vm = make_vm(bc, bc_len, subject, &out);
  r.ok = vm_run(&vm);

  for (int i = 0; i < 64; i++) {
    if (vm.cap_end[i] > vm.cap_start[i]) {
      r.cap_set[i] = true;
      r.cap_start[i] = vm.cap_start[i];
      r.cap_end[i] = vm.cap_end[i];
    } else if (i < (int)vm.max_cap_used && (vm.cap_start[i] || vm.cap_end[i])) {
      r.cap_set[i] = true;
      r.cap_start[i] = vm.cap_start[i];
      r.cap_end[i] = vm.cap_end[i];
    }
  }
  if (out.len > 0 && out.len < sizeof(r.out)) {
    memcpy(r.out, out.data, out.len);
    r.out_len = out.len;
  }
  snobol_buf_free(&out);

  /* Leave env clean for later suites. */
  unsetenv("SNOBOL_LEGACY_CHOICE");
  return r;
}

static bool results_equal(const TrailResult *a, const TrailResult *b) {
  if (a->ok != b->ok)
    return false;
  for (int i = 0; i < 64; i++) {
    if (a->cap_set[i] != b->cap_set[i])
      return false;
    if (a->cap_set[i]) {
      if (a->cap_start[i] != b->cap_start[i])
        return false;
      if (a->cap_end[i] != b->cap_end[i])
        return false;
    }
  }
  if (a->out_len != b->out_len)
    return false;
  if (memcmp(a->out, b->out, a->out_len) != 0)
    return false;
  return true;
}

/* ── REPEAT/EMIT heavy ───────────────────────────────────────────────────── */

static void test_repeat_emit_matches_legacy(void) {
  test_suite("Trail: REPEAT+EMIT ≡ legacy snapshot");

  Bc b;
  bc_build_repeat_emit(&b, 0, 0);
  bc_u8(&b, OP_ACCEPT);

  const char *subject = "aaa";

  TrailResult legacy = run_mode(b.bc, b.lit_pool, subject, true);
  TrailResult compact = run_mode(b.bc, b.lit_pool, subject, false);

  test_assert(legacy.ok, "legacy mode matches 'aaa'");
  test_assert(compact.ok, "compact (trail) mode matches 'aaa'");
  test_assert(results_equal(&legacy, &compact),
              "trail output equals legacy snapshot (match + captures + emit)");

  /* The EMIT output should be 'a' repeated for each 'a' consumed. */
  test_assert(compact.out_len == 3 && memcmp(compact.out, "aaa", 3) == 0,
              "EMIT output is 'aaa'");

  printf("  [info] legacy ok=%d emit='%.*s'  compact ok=%d emit='%.*s'\n",
         legacy.ok, (int)legacy.out_len, legacy.out, compact.ok,
         (int)compact.out_len, compact.out);
}

/* ── CAP heavy: nested captures inside a loop ────────────────────────────── */

static void test_cap_heavy_matches_legacy(void) {
  test_suite("Trail: CAP heavy ≡ legacy snapshot");

  /* Run the repeat/emit pattern over a longer subject (more backtracking
   * points and more captures). */
  Bc b;
  bc_build_repeat_emit(&b, 1, 2);
  bc_u8(&b, OP_ACCEPT);

  const char *subject = "aaaaaa";

  TrailResult legacy = run_mode(b.bc, b.lit_pool, subject, true);
  TrailResult compact = run_mode(b.bc, b.lit_pool, subject, false);

  test_assert(legacy.ok && compact.ok, "both modes match 'aaaaaa'");
  test_assert(results_equal(&legacy, &compact),
              "trail captures/output equal legacy snapshot");

  char expect[16];
  memset(expect, 'a', 6);
  test_assert(compact.out_len == 6 && memcmp(compact.out, expect, 6) == 0,
              "EMIT output is six 'a's");
}

/* ── Backtracking with intermediate failure (must replay trail) ───────────── */

static void test_repeat_backtrack_replays_trail(void) {
  test_suite("Trail: backtracking replays undo on failure");

  /* arbno( 'a' CAP(0) EMIT_CAP(0) ) followed by a literal 'Z' that must
   * follow the loop. With subject "aaa" (no trailing 'Z') the match must
   * fully backtrack — exercising trail replay on the abandoned loop thread —
   * and then fail. Both modes must agree on the failure. */
  Bc b;
  bc_build_repeat_emit(&b, 0, 0);
  /* append a literal 'Z' after the loop's done position */
  bc_lit1(&b, 'Z');
  bc_u8(&b, OP_ACCEPT);

  const char *subject = "aaa";

  TrailResult legacy = run_mode(b.bc, b.lit_pool, subject, true);
  TrailResult compact = run_mode(b.bc, b.lit_pool, subject, false);

  test_assert(!legacy.ok && !compact.ok, "both modes fail on 'aaa' (no 'Z')");
  test_assert(results_equal(&legacy, &compact),
              "trail failure state equals legacy");
}

/* ── Multiple loops + captures interleaved ───────────────────────────────── */

static void test_multi_loop_matches_legacy(void) {
  test_suite("Trail: multiple REPEAT loops ≡ legacy snapshot");

  /* Two independent arbno loops over different chars with distinct capture
   * registers, exercising several counters on the trail. */
  Bc b;
  bc_init(&b);
  /* loop 0 over 'a' capturing reg 0, emit reg 0 */
  size_t init0 = b.ip;
  bc_u8(&b, OP_REPEAT_INIT);
  bc_u8(&b, 0);
  bc_u32(&b, 0);
  bc_u32(&b, (uint32_t)-1);
  size_t skip0 = b.ip;
  bc_u32(&b, 0);
  size_t body0 = b.ip;
  bc_u8(&b, OP_CAP_START);
  bc_u8(&b, 0);
  bc_lit1(&b, 'a');
  bc_u8(&b, OP_CAP_END);
  bc_u8(&b, 0);
  bc_u8(&b, OP_EMIT_CAPTURE);
  bc_u8(&b, 0);
  bc_u8(&b, OP_REPEAT_STEP);
  bc_u8(&b, 0);
  bc_u32(&b, (uint32_t)body0);
  size_t done0 = b.ip;
  uint32_t v0 = (uint32_t)done0;
  b.bc[skip0] = (v0 >> 24) & 0xFF;
  b.bc[skip0 + 1] = (v0 >> 16) & 0xFF;
  b.bc[skip0 + 2] = (v0 >> 8) & 0xFF;
  b.bc[skip0 + 3] = v0 & 0xFF;

  /* loop 1 over 'b' capturing reg 1, emit reg 1 */
  size_t init1 = b.ip;
  bc_u8(&b, OP_REPEAT_INIT);
  bc_u8(&b, 1);
  bc_u32(&b, 0);
  bc_u32(&b, (uint32_t)-1);
  size_t skip1 = b.ip;
  bc_u32(&b, 0);
  size_t body1 = b.ip;
  bc_u8(&b, OP_CAP_START);
  bc_u8(&b, 1);
  bc_lit1(&b, 'b');
  bc_u8(&b, OP_CAP_END);
  bc_u8(&b, 1);
  bc_u8(&b, OP_EMIT_CAPTURE);
  bc_u8(&b, 1);
  bc_u8(&b, OP_REPEAT_STEP);
  bc_u8(&b, 1);
  bc_u32(&b, (uint32_t)body1);
  size_t done1 = b.ip;
  uint32_t v1 = (uint32_t)done1;
  b.bc[skip1] = (v1 >> 24) & 0xFF;
  b.bc[skip1 + 1] = (v1 >> 16) & 0xFF;
  b.bc[skip1 + 2] = (v1 >> 8) & 0xFF;
  b.bc[skip1 + 3] = v1 & 0xFF;

  bc_u8(&b, OP_ACCEPT);

  const char *subject = "aabbb";

  TrailResult legacy = run_mode(b.bc, b.lit_pool, subject, true);
  TrailResult compact = run_mode(b.bc, b.lit_pool, subject, false);

  test_assert(legacy.ok && compact.ok, "both modes match 'aabbb'");
  test_assert(results_equal(&legacy, &compact),
              "trail multi-loop state equals legacy snapshot");

  char expect[16];
  size_t ei = 0;
  for (int i = 0; i < 2; i++)
    expect[ei++] = 'a';
  for (int i = 0; i < 3; i++)
    expect[ei++] = 'b';
  test_assert(compact.out_len == 5 && memcmp(compact.out, expect, 5) == 0,
              "EMIT output interleaves 'aa' + 'bbb'");
}

/* ── Suite entry point ────────────────────────────────────────────────────── */

void test_vm_trail_suite(void) {
  test_suite("VM Trail Choice Save");

  test_repeat_emit_matches_legacy();
  test_cap_heavy_matches_legacy();
  test_repeat_backtrack_replays_trail();
  test_multi_loop_matches_legacy();
}
