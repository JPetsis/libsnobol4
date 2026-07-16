/**
 * @file vm_capture.c
 * @brief Capture-register write-log for the full SNOBOL4 VM.
 *
 * Split out of vm.c (see oss-readiness T4). The write-log records prior
 * capture-register values so captures can be restored precisely on
 * backtrack. The choice stack lives in vm_choice.c; the main executor
 * lives in vm_exec.c.
 */

#include "snobol/vm.h"
#include "snobol/array.h"
#include "snobol/dynamic_pattern.h"
#include "snobol/snobol_attrs.h"
#include "snobol/snobol_internal.h"
#include "snobol/string_fn.h"
#include "snobol/table.h"
#include "snobol/type_fn.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

/* ========== Write-log management for compact choice stack ========== */

void vm_write_log_init(VM *vm) {
  vm->write_log = nullptr;
  vm->write_log_cap = 0;
  vm->write_log_next = 0;
  vm->write_log_bitmap = 0;
  vm->write_log_compressed_count = 0;
  vm->write_log_dirty = false;
}

void vm_write_log_free(VM *vm) {
  if (vm->write_log) {
    snobol_free(vm->write_log);
    vm->write_log = nullptr;
  }
  vm->write_log_cap = 0;
  vm->write_log_next = 0;
  vm->write_log_bitmap = 0;
  vm->write_log_compressed_count = 0;
  vm->write_log_dirty = false;
}

void vm_write_log_clear(VM *vm) {
  vm->write_log_next = 0;
  vm->write_log_bitmap = 0;
  vm->write_log_dirty = false;
}

void vm_write_log_track_cap_start(VM *vm, uint8_t cap, size_t old_start) {
  if (!vm->use_compact_choice || !vm->write_log)
    return;
  /* Look for existing entry for this cap (most recent first) */
  size_t slot =
      (vm->write_log_next > 0) ? vm->write_log_next - 1 : vm->write_log_cap - 1;
  for (size_t i = 0; i < vm->write_log_cap; i++) {
    if (vm->write_log_bitmap & (1ULL << slot)) {
      if (vm->write_log[slot].cap_index == cap) {
        vm->write_log[slot].old_start = old_start;
        return;
      }
    }
    if (slot == 0)
      slot = vm->write_log_cap - 1;
    else
      slot--;
  }
  /* No existing entry: create new */
  slot = vm->write_log_next++;
  if (slot >= vm->write_log_cap) {
    vm->write_log_next = 0;
    slot = 0;
  }
  vm->write_log[slot].cap_index = cap;
  vm->write_log[slot].old_start = old_start;
  vm->write_log[slot].old_end = (size_t)-1; /* sentinel: end not changed */
  vm->write_log_bitmap |= (1ULL << slot);
  vm->write_log_dirty = true;
}

void vm_write_log_track_cap_end(VM *vm, uint8_t cap, size_t old_end) {
  if (!vm->use_compact_choice || !vm->write_log)
    return;
  /* Look for existing entry for this cap (most recent first) */
  size_t slot =
      (vm->write_log_next > 0) ? vm->write_log_next - 1 : vm->write_log_cap - 1;
  for (size_t i = 0; i < vm->write_log_cap; i++) {
    if (vm->write_log_bitmap & (1ULL << slot)) {
      if (vm->write_log[slot].cap_index == cap) {
        vm->write_log[slot].old_end = old_end;
        return;
      }
    }
    if (slot == 0)
      slot = vm->write_log_cap - 1;
    else
      slot--;
  }
  /* No existing entry: create new */
  slot = vm->write_log_next++;
  if (slot >= vm->write_log_cap) {
    vm->write_log_next = 0;
    slot = 0;
  }
  vm->write_log[slot].cap_index = cap;
  vm->write_log[slot].old_start = (size_t)-1; /* sentinel: start not changed */
  vm->write_log[slot].old_end = old_end;
  vm->write_log_bitmap |= (1ULL << slot);
  vm->write_log_dirty = true;
}

size_t vm_write_log_count_entries(const VM *vm) {
  size_t count = 0;
  for (size_t i = 0; i < vm->write_log_cap; i++) {
    if (vm->write_log_bitmap & (1ULL << i)) {
      const WriteLogEntry *e = &vm->write_log[i];
      if (e->old_start != (size_t)-1 || e->old_end != (size_t)-1) {
        count++;
      }
    }
  }
  return count;
}

void vm_write_log_copy_entries(const VM *vm, WriteLogEntry *dst,
                               size_t dst_cap) {
  size_t copied = 0;
  for (size_t i = 0; i < vm->write_log_cap; i++) {
    if (vm->write_log_bitmap & (1ULL << i)) {
      const WriteLogEntry *e = &vm->write_log[i];
      if (e->old_start != (size_t)-1 || e->old_end != (size_t)-1) {
        if (copied < dst_cap) {
          dst[copied] = *e;
          copied++;
        }
      }
    }
  }
}

void vm_write_log_restore(VM *vm, const CompactChoiceHeader *hdr) {
  const uint8_t *p = (const uint8_t *)(hdr + 1);
  if (hdr->max_counter_used > 0) {
    p += hdr->max_counter_used * (sizeof(uint32_t) + sizeof(size_t));
  }
  for (uint8_t i = 0; i < hdr->write_log_count; i++) {
    const WriteLogEntry *e = (const WriteLogEntry *)p;
    if (e->cap_index < MAX_CAPS) {
      vm->cap_start[e->cap_index] = e->old_start;
      vm->cap_end[e->cap_index] = e->old_end;
    }
    p += sizeof(WriteLogEntry);
  }
}
