/**
 * @file vm_choice.c
 * @brief Backtracking choice-stack management for the full SNOBOL4 VM.
 *
 * Split out of vm.c (see oss-readiness T4). Implements the compact choice
 * stack: push/pop of choice records, record-size accounting, stack depth /
 * memory statistics, and snobol_vm_reset(). The capture write-log lives in
 * vm_capture.c; the main executor lives in vm_exec.c.
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

size_t vm_compact_choice_record_size(const CompactChoiceHeader *hdr) {
  size_t sz = sizeof(CompactChoiceHeader);
  if (hdr->max_counter_used > 0) {
    sz += hdr->max_counter_used * (sizeof(uint32_t) + sizeof(size_t));
  }
  sz += hdr->write_log_count * sizeof(WriteLogEntry);
  return sz;
}

/* Reset VM between match attempts while preserving choice allocation */
void snobol_vm_reset(VM *vm) {
  memset(vm->cap_start, 0, sizeof(vm->cap_start));
  memset(vm->cap_end, 0, sizeof(vm->cap_end));
  vm->var_count = 0;
  memset(vm->var_start, 0, sizeof(vm->var_start));
  memset(vm->var_end, 0, sizeof(vm->var_end));
  memset(vm->counters, 0, sizeof(vm->counters));
  memset(vm->loop_min, 0, sizeof(vm->loop_min));
  memset(vm->loop_max, 0, sizeof(vm->loop_max));
  memset(vm->loop_last_pos, 0, sizeof(vm->loop_last_pos));
  vm->max_cap_used = 0;
  vm->max_counter_used = 0;
  vm->ip = 0;
  vm->pos = 0;
  vm->abort_flag = false;
  vm->in_goto_fail = false;
  vm->current_label = 0;
  vm->choices_top = 0;
  vm->choice_allocated = 0;
  vm->choice_push_count = 0;
  vm->choice_peak_depth = 0;
  vm->choice_peak_memory = 0;
  if (vm->write_log) {
    vm_write_log_clear(vm);
  }
}

/* Choice stack statistics */
size_t vm_choice_stack_memory_usage(VM *vm) { return vm->choices_top; }

size_t vm_choice_stack_depth(VM *vm) {
  /* Depth = number of choice records on stack.
   * We can estimate by dividing total bytes by average record size,
   * or we can walk the stack counting records. Walking is O(n) but accurate.
   * For simplicity, we walk the stack.
   */
  size_t depth = 0;
  size_t pos = 0;
  while (pos < vm->choices_top) {
    CompactChoiceHeader *h =
        (CompactChoiceHeader *)((uint8_t *)vm->choices + pos);
    size_t rec_size = h->total_size;
    pos += rec_size;
    depth++;
  }
  return depth;
}

size_t vm_choice_record_average_size(VM *vm) {
  if (vm->choice_push_count > 0) {
    return vm->choice_allocated / vm->choice_push_count;
  }
  return 0;
}

void vm_push_choice(VM *vm, size_t ip, size_t pos) {
  if (!vm->choices)
    return;
#ifdef SNOBOL_PROFILE
  vm->profile.push_count++;
#endif
  if (vm->use_compact_choice) {
    uint8_t num_counters = vm->max_counter_used;
    size_t wl_entries = vm_write_log_count_entries(vm);
    size_t data_size = sizeof(CompactChoiceHeader);
    if (num_counters > 0) {
      data_size += num_counters * (sizeof(uint32_t) + sizeof(size_t));
    }
    data_size += wl_entries * sizeof(WriteLogEntry);
    uint32_t record_size = (uint32_t)(data_size + sizeof(uint32_t));
    record_size = (record_size + 7) & ~7;
    if (vm->choices_top + record_size >= vm->choices_cap) {
      size_t new_cap = vm->choices_cap ? vm->choices_cap * 2 : 4096;
      while (vm->choices_top + record_size >= new_cap)
        new_cap *= 2;
      void *new_choices = snobol_realloc(vm->choices, new_cap);
      if (!new_choices)
        return;
      vm->choices = new_choices;
      vm->choices_cap = new_cap;
    }
    CompactChoiceHeader *h =
        (CompactChoiceHeader *)((uint8_t *)vm->choices + vm->choices_top);
    h->total_size = record_size;
    h->ip = ip;
    h->pos = pos;
    h->var_count = vm->var_count;
    h->max_cap_used = vm->max_cap_used;
    h->max_counter_used = num_counters;
    h->write_log_count = (uint8_t)wl_entries;
    h->pad = 0;
    uint8_t *p = (uint8_t *)(h + 1);
    if (num_counters > 0) {
      memcpy(p, vm->counters, num_counters * sizeof(uint32_t));
      p += num_counters * sizeof(uint32_t);
      memcpy(p, vm->loop_last_pos, num_counters * sizeof(size_t));
      p += num_counters * sizeof(size_t);
    }
    vm_write_log_copy_entries(vm, (WriteLogEntry *)p, wl_entries);
    uint32_t *size_ptr =
        (uint32_t *)((uint8_t *)h + record_size - sizeof(uint32_t));
    *size_ptr = record_size;
    vm->choices_top += record_size;
    vm->choice_allocated += record_size;
    vm->choice_push_count++;
    vm->choice_live_depth++;
    /* Track peak values */
    if (vm->choices_top > vm->choice_peak_memory)
      vm->choice_peak_memory = vm->choices_top;
    if (vm->choice_live_depth > vm->choice_peak_depth)
      vm->choice_peak_depth = vm->choice_live_depth;
    vm_write_log_clear(vm);
  } else {
    size_t record_size = sizeof(struct choice);
    if (vm->choices_top + record_size >= vm->choices_cap) {
      size_t new_cap =
          vm->choices_cap ? vm->choices_cap * 2 : (128 * sizeof(struct choice));
      void *new_choices = snobol_realloc(vm->choices, new_cap);
      if (!new_choices)
        return;
      vm->choices = new_choices;
      vm->choices_cap = new_cap;
    }
    struct choice *c =
        (struct choice *)((uint8_t *)vm->choices + vm->choices_top);
    c->ip = ip;
    c->pos = pos;
    c->var_count_snapshot = vm->var_count;
    c->max_cap_used_snapshot = vm->max_cap_used;
    c->max_counter_used_snapshot = vm->max_counter_used;
    if (vm->max_cap_used > 0) {
      size_t cap_size = vm->max_cap_used * sizeof(size_t);
      memcpy(c->cap_start_snapshot, vm->cap_start, cap_size);
      memcpy(c->cap_end_snapshot, vm->cap_end, cap_size);
    }
    if (vm->max_counter_used > 0) {
      memcpy(c->counters_snapshot, vm->counters,
             vm->max_counter_used * sizeof(uint32_t));
      memcpy(c->loop_last_pos_snapshot, vm->loop_last_pos,
             vm->max_counter_used * sizeof(size_t));
    }
    vm->choices_top += record_size;
    vm->choice_allocated += record_size;
    vm->choice_push_count++;
    vm->choice_live_depth++;
    /* Track peak values */
    if (vm->choices_top > vm->choice_peak_memory)
      vm->choice_peak_memory = vm->choices_top;
    if (vm->choice_live_depth > vm->choice_peak_depth)
      vm->choice_peak_depth = vm->choice_live_depth;
  }
}

bool vm_pop_choice(VM *vm) {
  if (!vm->choices || vm->choices_top == 0)
    return false;
#ifdef SNOBOL_PROFILE
  vm->profile.pop_count++;
#endif
  if (vm->use_compact_choice) {
    uint32_t last_size;
    memcpy(&last_size,
           (uint8_t *)vm->choices + vm->choices_top - sizeof(uint32_t),
           sizeof(uint32_t));
    vm->choices_top -= last_size;
    CompactChoiceHeader *h =
        (CompactChoiceHeader *)((uint8_t *)vm->choices + vm->choices_top);
    vm->ip = h->ip;
    vm->pos = h->pos;
    vm->var_count = h->var_count;
    vm->max_cap_used = h->max_cap_used;
    vm->max_counter_used = h->max_counter_used;
    uint8_t *p = (uint8_t *)(h + 1);
    if (h->max_counter_used > 0) {
      memcpy(vm->counters, p, h->max_counter_used * sizeof(uint32_t));
      p += h->max_counter_used * sizeof(uint32_t);
      memcpy(vm->loop_last_pos, p, h->max_counter_used * sizeof(size_t));
      p += h->max_counter_used * sizeof(size_t);
    }
    /* Restore captures from write-log entries (delta) */
    for (uint8_t i = 0; i < h->write_log_count; i++) {
      const WriteLogEntry *e = (const WriteLogEntry *)p;
      if (e->cap_index < MAX_CAPS) {
        vm->cap_start[e->cap_index] = e->old_start;
        vm->cap_end[e->cap_index] = e->old_end;
      }
      p += sizeof(WriteLogEntry);
    }
  } else {
    vm->choices_top -= sizeof(struct choice);
    struct choice *c =
        (struct choice *)((uint8_t *)vm->choices + vm->choices_top);
    vm->ip = c->ip;
    vm->pos = c->pos;
    vm->var_count = c->var_count_snapshot;
    vm->max_cap_used = c->max_cap_used_snapshot;
    vm->max_counter_used = c->max_counter_used_snapshot;
    if (vm->max_cap_used > 0) {
      size_t cap_bytes = vm->max_cap_used * sizeof(size_t);
      memcpy(vm->cap_start, c->cap_start_snapshot, cap_bytes);
      memcpy(vm->cap_end, c->cap_end_snapshot, cap_bytes);
    }
    if (vm->max_counter_used > 0) {
      memcpy(vm->counters, c->counters_snapshot,
             vm->max_counter_used * sizeof(uint32_t));
      memcpy(vm->loop_last_pos, c->loop_last_pos_snapshot,
             vm->max_counter_used * sizeof(size_t));
    }
  }
  if (vm->choice_live_depth > 0)
    vm->choice_live_depth--;
  return true;
}
