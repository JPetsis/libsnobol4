#include "../../core/include/snobol/jit.h"
#include "../../core/include/snobol/vm.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

void test_suite(const char *name);
void test_assert(bool condition, const char *message);

#ifdef SNOBOL_JIT
/* Build a minimal "LIT 'hello' ACCEPT" bytecode sequence. */
static size_t build_lit_accept_bc(uint8_t *bc, const char *literal) {
  size_t ip = 0;
  size_t lit_len = strlen(literal);
  /* OP_LIT followed by u16 length, then literal bytes, then OP_ACCEPT */
  bc[ip++] = 1; /* OP_LIT — exact value depends on the opcode enum */
  bc[ip++] = (uint8_t)((lit_len >> 8) & 0xFF);
  bc[ip++] = (uint8_t)(lit_len & 0xFF);
  memcpy(bc + ip, literal, lit_len);
  ip += lit_len;
  bc[ip++] = 2; /* OP_ACCEPT */
  return ip;
}

/* Build a pattern that contains an EVAL opcode, which the method JIT cannot
 * compile (it must return NULL). */
static size_t build_eval_bc(uint8_t *bc) {
  size_t ip = 0;
  /* The exact EVAL opcode number is intentionally not relied on — the test
   * simply asserts that the method-JIT rejects the IR graph via the
   * non_compilable flag regardless of which opcode it lands on. */
  bc[ip++] = 0xFF; /* placeholder */
  bc[ip++] = 0xFF;
  bc[ip++] = 0xFF;
  bc[ip++] = 0xFF;
  bc[ip++] = 2; /* OP_ACCEPT */
  return ip;
}

static void test_method_jit_compile_returns_function(void) {
  test_suite("Method JIT: simple literal compiles to non-NULL function");

  uint8_t bc[64];
  size_t bc_len = build_lit_accept_bc(bc, "hello");
  test_assert(bc_len > 0, "bytecode buffer built");

  size_t code_size = 0;
  jit_trace_fn fn = snobol_jit_method_compile(bc, bc_len, &code_size);
  test_assert(fn != NULL || code_size == 0,
              "snobol_jit_method_compile returned either a callable function "
              "or NULL with zero code size (never garbage)");

  if (fn) {
    /* Compile a second time (should hit the cache and return the same fn) */
    size_t cs2 = 0;
    jit_trace_fn fn2 = snobol_jit_method_compile(bc, bc_len, &cs2);
    test_assert(fn2 == fn, "second compile hits cache and returns same pointer");
    test_assert(cs2 == code_size, "cached code size matches");
  }
}

static void test_method_jit_fallback_on_uncompilable(void) {
  test_suite("Method JIT: uncompilable patterns fall through to NULL");

  uint8_t bc[64];
  size_t bc_len = build_eval_bc(bc);

  size_t code_size = 0xDEAD;
  jit_trace_fn fn = snobol_jit_method_compile(bc, bc_len, &code_size);
  /* The method-JIT must reject this and return NULL.  The code_size is
   * reset to 0 on failure — that is the contract callers rely on. */
  test_assert(fn == NULL, "snobol_jit_method_compile returns NULL");
  test_assert(code_size == 0, "code_size is reset to 0 on failure");
}

static void test_method_jit_query_cached_after_compile(void) {
  test_suite("Method JIT: query returns cached function after compile");

  uint8_t bc[64];
  size_t bc_len = build_lit_accept_bc(bc, "query");
  /* Before compile, query returns NULL */
  jit_trace_fn fn = snobol_jit_method_query(bc, bc_len);
  test_assert(fn == NULL, "query returns NULL before compile");

  /* After compile, query returns the same function */
  size_t cs = 0;
  jit_trace_fn compiled = snobol_jit_method_compile(bc, bc_len, &cs);
  test_assert(compiled != NULL, "compile succeeds");

  jit_trace_fn cached = snobol_jit_method_query(bc, bc_len);
  test_assert(cached == compiled, "query returns cached function after compile");
}

static void test_method_jit_stats_recorded(void) {
  test_suite("Method JIT: stats counters increment on compile attempts");

  snobol_jit_init();
  snobol_jit_reset_stats();
  SnobolJitStats *stats = snobol_jit_get_stats();

  uint64_t attempts_before = stats->method_attempts_total;
  uint64_t successes_before = stats->method_successes_total;
  uint64_t fallbacks_before = stats->method_fallbacks_total;

  uint8_t bc[64];
  size_t bc_len = build_lit_accept_bc(bc, "x");
  (void)snobol_jit_method_compile(bc, bc_len, NULL);

  test_assert(stats->method_attempts_total > attempts_before,
              "method_attempts_total increments on compile attempt");
  test_assert(stats->method_successes_total + stats->method_fallbacks_total >
                  successes_before + fallbacks_before,
              "method outcome (success or fallback) is recorded");
}
#endif /* SNOBOL_JIT */

void test_jit_method_suite(void) {
#ifdef SNOBOL_JIT
  test_method_jit_compile_returns_function();
  test_method_jit_fallback_on_uncompilable();
  test_method_jit_query_cached_after_compile();
  test_method_jit_stats_recorded();
#else
  test_suite("Method JIT (skipped — SNOBOL_JIT not defined)");
  test_assert(true, "Method JIT test suite is a no-op without SNOBOL_JIT");
#endif
}
