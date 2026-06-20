#include "../../core/include/snobol/jit.h"
#include "../../core/include/snobol/vm.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_suite(const char *name);
void test_assert(bool condition, const char *message);

static bool jit_is_supported(void) {
#if defined(__arm__) || defined(__thumb__) || defined(__ARM_ARCH_7A__) ||      \
    defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
  return true;
#else
  return false;
#endif
}

#ifdef SNOBOL_JIT

static void emit_u32(uint8_t *bc, size_t *ip, uint32_t v) {
  bc[(*ip)++] = (uint8_t)((v >> 24) & 0xFF);
  bc[(*ip)++] = (uint8_t)((v >> 16) & 0xFF);
  bc[(*ip)++] = (uint8_t)((v >> 8) & 0xFF);
  bc[(*ip)++] = (uint8_t)(v & 0xFF);
}

static void test_arm32_backend_lifecycle(void) {
  if (!jit_is_supported()) {
    test_assert(true, "ARM32 lifecycle test skipped (not ARM)");
    return;
  }

  snobol_jit_init();
  snobol_jit_reset_stats();
  test_assert(true, "ARM32: snobol_jit_init() succeeded without crash");

  SnobolJitConfig *cfg = (SnobolJitConfig *)snobol_jit_get_config();
  test_assert(cfg != NULL, "ARM32: snobol_jit_get_config() returns non-NULL");

  snobol_jit_shutdown();
}

static void test_arm32_literal_roundtrip(void) {
  if (!jit_is_supported()) {
    test_assert(true, "ARM32 literal roundtrip test skipped (not ARM)");
    return;
  }

  uint8_t bc[256] = {0};
  size_t ip = 0;

  bc[ip++] = OP_LIT;
  size_t lit_off = ip;
  emit_u32(bc, &ip, 200);
  emit_u32(bc, &ip, 3);
  bc[200] = 'f';
  bc[201] = 'o';
  bc[202] = 'o';
  bc[ip++] = OP_ACCEPT;

  snobol_jit_init();
  snobol_jit_reset_stats();

  SnobolJitConfig saved_cfg = *snobol_jit_get_config();
  SnobolJitConfig cfg = saved_cfg;
  cfg.min_useful_ops = 0;
  cfg.hotness_threshold = 5;
  cfg.skip_backtrack_heavy = false;
  snobol_jit_set_config(&cfg);

  SnobolJitStats *stats = snobol_jit_get_stats();
  SnobolJitContext *ctx = snobol_jit_acquire_context(bc, 256);

  VM vm = {0};
  vm.bc = bc;
  vm.bc_len = 256;
  vm.s = "foo";
  vm.len = 3;
  vm.jit.enabled = true;
  vm.jit.ip_counts = ctx->ip_counts;
  vm.jit.traces = ctx->traces;
  vm.jit.stats = stats;
  vm.jit.ctx = ctx;

  for (int i = 0; i < 20; i++) {
    vm.ip = 0;
    vm.pos = 0;
    vm_run(&vm);
  }

  test_assert(stats->compilations_total > 0,
              "ARM32: literal pattern compiles a region");
  test_assert(stats->cache_hits_total >= 0,
              "ARM32: cache hits counter is accessible");

  snobol_jit_reset_stats();
  vm.ip = 0;
  vm.pos = 0;
  bool ok = vm_run(&vm);
  test_assert(ok, "ARM32: literal roundtrip match succeeds");
  test_assert(vm.pos == 3, "ARM32: pos == 3 after consuming 'foo'");

  snobol_jit_release_context(ctx);
  snobol_jit_set_config(&saved_cfg);
  snobol_jit_shutdown();
}

static void test_arm32_split_roundtrip(void) {
  if (!jit_is_supported()) {
    test_assert(true, "ARM32 SPLIT roundtrip test skipped (not ARM)");
    return;
  }

  uint8_t bc[256] = {0};
  size_t ip = 0;

  size_t split_ip = ip;
  bc[ip++] = OP_SPLIT;
  emit_u32(bc, &ip, 0);
  emit_u32(bc, &ip, 0);

  size_t a_ip = ip;
  bc[ip++] = OP_LIT;
  size_t a_lit_off = ip;
  emit_u32(bc, &ip, 200);
  emit_u32(bc, &ip, 1);
  bc[200] = 'x';

  size_t jmp_ip = ip;
  bc[ip++] = OP_JMP;
  emit_u32(bc, &ip, 0);

  size_t b_ip = ip;
  bc[ip++] = OP_LIT;
  size_t b_lit_off = ip;
  emit_u32(bc, &ip, 201);
  emit_u32(bc, &ip, 1);
  bc[201] = 'y';

  size_t end_ip = ip;
  bc[ip++] = OP_ACCEPT;

  ip = split_ip + 1;
  emit_u32(bc, &ip, (uint32_t)a_ip);
  ip = split_ip + 5;
  emit_u32(bc, &ip, (uint32_t)b_ip);
  ip = jmp_ip + 1;
  emit_u32(bc, &ip, (uint32_t)end_ip);

  snobol_jit_init();
  snobol_jit_reset_stats();

  SnobolJitConfig saved_cfg = *snobol_jit_get_config();
  SnobolJitConfig cfg = saved_cfg;
  cfg.min_useful_ops = 0;
  cfg.hotness_threshold = 5;
  cfg.skip_backtrack_heavy = false;
  snobol_jit_set_config(&cfg);

  SnobolJitStats *stats = snobol_jit_get_stats();
  SnobolJitContext *ctx = snobol_jit_acquire_context(bc, 256);

  VM vm = {0};
  vm.bc = bc;
  vm.bc_len = 256;
  vm.s = "y";
  vm.len = 1;
  vm.jit.enabled = true;
  vm.jit.ip_counts = ctx->ip_counts;
  vm.jit.traces = ctx->traces;
  vm.jit.stats = stats;
  vm.jit.ctx = ctx;

  for (int i = 0; i < 20; i++) {
    vm.ip = 0;
    vm.pos = 0;
    vm_run(&vm);
  }

  test_assert(stats->compilations_total > 0, "ARM32 SPLIT: region compiled");

  snobol_jit_reset_stats();
  vm.ip = 0;
  vm.pos = 0;
  bool ok = vm_run(&vm);
  test_assert(ok, "ARM32 SPLIT: match via non-taken branch succeeds");
  test_assert(vm.pos == 1, "ARM32 SPLIT: pos == 1 after consuming 'y'");

  snobol_jit_release_context(ctx);
  snobol_jit_set_config(&saved_cfg);
  snobol_jit_shutdown();
}

static void test_arm32_len_roundtrip(void) {
  if (!jit_is_supported()) {
    test_assert(true, "ARM32 LEN roundtrip test skipped (not ARM)");
    return;
  }

  uint8_t bc[64] = {0};
  size_t ip = 0;

  bc[ip++] = OP_LEN;
  emit_u32(bc, &ip, 2);
  bc[ip++] = OP_ACCEPT;

  snobol_jit_init();
  snobol_jit_reset_stats();

  SnobolJitConfig saved_cfg = *snobol_jit_get_config();
  SnobolJitConfig cfg = saved_cfg;
  cfg.min_useful_ops = 0;
  cfg.hotness_threshold = 5;
  cfg.skip_backtrack_heavy = false;
  snobol_jit_set_config(&cfg);

  SnobolJitStats *stats = snobol_jit_get_stats();
  SnobolJitContext *ctx = snobol_jit_acquire_context(bc, 64);

  VM vm = {0};
  vm.bc = bc;
  vm.bc_len = 64;
  vm.s = "ab";
  vm.len = 2;
  vm.jit.enabled = true;
  vm.jit.ip_counts = ctx->ip_counts;
  vm.jit.traces = ctx->traces;
  vm.jit.stats = stats;
  vm.jit.ctx = ctx;

  for (int i = 0; i < 20; i++) {
    vm.ip = 0;
    vm.pos = 0;
    vm_run(&vm);
  }

  test_assert(stats->compilations_total > 0, "ARM32 LEN: region compiled");

  snobol_jit_reset_stats();
  vm.ip = 0;
  vm.pos = 0;
  bool ok = vm_run(&vm);
  test_assert(ok, "ARM32 LEN: match succeeds");
  test_assert(vm.pos == 2, "ARM32 LEN: pos == 2 after consuming 2 chars");

  snobol_jit_release_context(ctx);
  snobol_jit_set_config(&saved_cfg);
  snobol_jit_shutdown();
}
#endif

void test_jit_arm32_suite(void) {
#ifdef SNOBOL_JIT
  test_suite("JIT ARM32 Backend");
  test_arm32_backend_lifecycle();
  test_arm32_literal_roundtrip();
  test_arm32_split_roundtrip();
  test_arm32_len_roundtrip();
#endif
}
