#include "snobol/vm.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

static void emit_u8(uint8_t *bc, size_t *ip, uint8_t v) {
  bc[(*ip)++] = v;
}
static void emit_u32(uint8_t *bc, size_t *ip, uint32_t v) {
  bc[(*ip)++] = (v >> 24) & 0xFF;
  bc[(*ip)++] = (v >> 16) & 0xFF;
  bc[(*ip)++] = (v >> 8) & 0xFF;
  bc[(*ip)++] = v & 0xFF;
}

/* W2c: page-linked choice-stack arena unit tests. These exercise the arena
 * mechanics directly (page chaining across many records, LIFO pop order,
 * reset, and full teardown) so that behaviour is verified independently of the
 * VM, and so ASan can catch any use-after-free / leak in the arena. */

static void test_arena_unit(void) {
  ChoiceArena *a = vm_arena_create();
  test_assert(a != NULL, "arena create succeeds");
  if (!a)
    return;

  /* Push many records whose payload is a sequence number; force multiple
   * pages (each page is 4096 bytes; records are ~24 bytes -> >150 records
   * spans pages). */
  const int N = 4000;
  /* The arena 8-aligns each record's payload and pads both the leading
   * (footprint + 4-byte pad) and trailing (pad + footprint) words, giving
   * a per-record footprint of aligned_payload + 2*sizeof(uint32_t) + 8.
   * For a 4-byte payload, aligned_payload = 8, so footprint = 24. */
  size_t aligned_payload = (sizeof(uint32_t) + 7) & ~(size_t)7;
  size_t per_rec = aligned_payload + 2 * sizeof(uint32_t) + 8;
  for (int i = 0; i < N; i++) {
    uint32_t *p = (uint32_t *)vm_arena_alloc(a, sizeof(uint32_t));
    test_assert(p != NULL, "arena alloc succeeds");
    if (!p)
      break;
    *p = (uint32_t)i;
  }
  test_assert(a->total_used == (size_t)N * per_rec,
              "arena total_used accounts for payload + size words");
  test_assert(a->peak_used == a->total_used, "peak tracks total during growth");

  /* Pop in LIFO order and verify the sequence number matches. */
  for (int i = N - 1; i >= 0; i--) {
    /* Trailing size word is at the page tail; payload starts at offset 8
     * from rec_base (after the leading footprint + 4-byte alignment pad). */
    uint32_t footprint =
        *(uint32_t *)(a->cur->data + a->cur->used - sizeof(uint32_t));
    uint8_t *rec_base = a->cur->data + a->cur->used - footprint;
    uint32_t *payload = (uint32_t *)(rec_base + 8);
    test_assert(*payload == (uint32_t)i, "LIFO pop yields expected record");
    vm_arena_pop_last(a);
  }
  test_assert(a->total_used == 0, "arena drained after popping all records");
  test_assert(a->cur == a->head, "arena returns to head page after drain");

  vm_arena_destroy(a);
}

static void test_arena_reset(void) {
  ChoiceArena *a = vm_arena_create();
  test_assert(a != NULL, "arena create succeeds");
  if (!a)
    return;

  for (int i = 0; i < 3000; i++) {
    uint8_t *p = vm_arena_alloc(a, 8);
    test_assert(p != NULL, "arena alloc succeeds");
    if (!p)
      break;
  }
  test_assert(a->total_used > 0, "arena has live records");
  vm_arena_reset(a);
  test_assert(a->total_used == 0, "reset clears all records");
  test_assert(a->cur == a->head, "reset returns to head page");

  /* Arena must be reusable after reset. */
  uint8_t *p = vm_arena_alloc(a, 8);
  test_assert(p != NULL, "arena reusable after reset");
  (void)p;
  vm_arena_destroy(a);
}

/* Deep backtracking through the real VM choice stack (now arena-backed):
 * ARBNO(ARBNO('a')) creates a deep, multi-page live choice stack (one choice
 * per 'a' consumed). We assert the match succeeds and that the arena handled
 * many pages of live records. */
static void test_arena_deep_vm_backtrack(void) {
  const int N = 1500;
  /* Pattern: ARBNO(ARBNO('a')) 'a'  over subject "aaaa...a" (N+1 'a's).
   * Should succeed (the trailing 'a' matches the final literal). */
  uint8_t bc[1024];
  memset(bc, 0, sizeof(bc));
  size_t ip = 0;

  emit_u8(bc, &ip, OP_REPEAT_INIT);
  emit_u8(bc, &ip, 0);
  emit_u32(bc, &ip, 0);
  emit_u32(bc, &ip, (uint32_t)-1);
  size_t l1_skip_ref = ip;
  emit_u32(bc, &ip, 0);

  size_t l1_body = ip;
  emit_u8(bc, &ip, OP_REPEAT_INIT);
  emit_u8(bc, &ip, 1);
  emit_u32(bc, &ip, 0);
  emit_u32(bc, &ip, (uint32_t)-1);
  size_t l2_skip_ref = ip;
  emit_u32(bc, &ip, 0);

  size_t l2_body = ip;
  uint32_t lit_a_off = 600;
  bc[lit_a_off] = 'a';
  emit_u8(bc, &ip, OP_LIT);
  emit_u32(bc, &ip, lit_a_off);
  emit_u32(bc, &ip, 1);

  emit_u8(bc, &ip, OP_REPEAT_STEP);
  emit_u8(bc, &ip, 1);
  emit_u32(bc, &ip, (uint32_t)l2_body);

  size_t l2_done = ip;
  uint32_t v = (uint32_t)l2_done;
  bc[l2_skip_ref + 0] = v >> 24;
  bc[l2_skip_ref + 1] = v >> 16;
  bc[l2_skip_ref + 2] = v >> 8;
  bc[l2_skip_ref + 3] = v & 0xFF;

  emit_u8(bc, &ip, OP_REPEAT_STEP);
  emit_u8(bc, &ip, 0);
  emit_u32(bc, &ip, (uint32_t)l1_body);

  size_t l1_done = ip;
  v = (uint32_t)l1_done;
  bc[l1_skip_ref + 0] = v >> 24;
  bc[l1_skip_ref + 1] = v >> 16;
  bc[l1_skip_ref + 2] = v >> 8;
  bc[l1_skip_ref + 3] = v & 0xFF;

  /* Trailing literal 'a' (present in subject). */
  emit_u8(bc, &ip, OP_LIT);
  emit_u32(bc, &ip, lit_a_off);
  emit_u32(bc, &ip, 1);

  emit_u8(bc, &ip, OP_ACCEPT);

  char *subject = (char *)malloc((size_t)N + 2);
  memset(subject, 'a', (size_t)N + 1);
  subject[N + 1] = '\0';

  VM vm;
  memset(&vm, 0, sizeof(vm));
  vm.bc = bc;
  vm.bc_len = 1024;
  vm.s = subject;
  vm.len = (size_t)N + 1;
  vm.keep_choices = true; /* keep the arena so we can inspect peak usage */

  bool result = vm_exec(&vm);
  test_assert(result == true, "deep nested-arbno matches subject");
  test_assert(vm.choices_arena != NULL, "arena was allocated");
  test_assert(vm.choices_arena->peak_used >= (size_t)(N * 16),
              "arena handled deep multi-page choice stack");

  vm_arena_destroy(vm.choices_arena);
  /* keep_choices skipped trail/write_log teardown inside vm_run; free here. */
  if (vm.trail)
    vm_trail_free(&vm);
  if (vm.write_log)
    vm_write_log_free(&vm);
  free(subject);
}

void test_choice_arena_suite(void) {
  test_suite("Choice-Stack Arena (W2c)");
  test_arena_unit();
  test_arena_reset();
  test_arena_deep_vm_backtrack();
}
