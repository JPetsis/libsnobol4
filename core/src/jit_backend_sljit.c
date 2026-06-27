#include "snobol/jit.h"
#include "snobol/jit_backend.h"
#include "snobol/jit_ir.h"
#include "snobol/vm.h"
#include "snobol/snobol_internal.h"

#ifdef SNOBOL_JIT_BACKEND_SLJIT

#include "sljitLir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Helper structures for bc_ip -> instruction index mapping and pending
 * forward jumps (jumps whose target label hasn't been emitted yet).
 * ---------------------------------------------------------------------------
 */

typedef struct {
  size_t bc_ip;
  size_t instr_idx;
} bc_ip_entry_t;

typedef struct {
  struct sljit_jump **jumps;
  size_t count;
  size_t cap;
} pending_list_t;

static size_t find_instr_by_bc_ip(const bc_ip_entry_t *map, size_t n,
                                   size_t bc_ip) {
  for (size_t i = 0; i < n; i++) {
    if (map[i].bc_ip == bc_ip)
      return map[i].instr_idx;
  }
  return (size_t)-1;
}

static int add_pending(pending_list_t *pl, struct sljit_jump *j) {
  if (!j) return -1;
  if (pl->count >= pl->cap) {
    size_t nc = pl->cap ? pl->cap * 2 : 8;
    struct sljit_jump **m = snobol_realloc(pl->jumps, nc * sizeof(void *));
    if (!m) return -1;
    pl->jumps = m;
    pl->cap = nc;
  }
  pl->jumps[pl->count++] = j;
  return 0;
}

/* Call-fixup record: a `sljit_emit_call` whose target must be patched after
 * code generation with the actual address of a C helper function. */
typedef struct {
  struct sljit_jump *jump;
  sljit_uw           target;
} call_fixup_t;

typedef struct {
  call_fixup_t *entries;
  size_t        count;
  size_t        cap;
} call_fixup_list_t;

static int add_call_fixup(call_fixup_list_t *list, struct sljit_jump *j,
                           sljit_uw fn) {
  if (!j) return -1;
  if (list->count >= list->cap) {
    size_t nc = list->cap ? list->cap * 2 : 8;
    call_fixup_t *m = snobol_realloc(list->entries, nc * sizeof(call_fixup_t));
    if (!m) return -1;
    list->entries = m;
    list->cap = nc;
  }
  list->entries[list->count].jump = j;
  list->entries[list->count].target = fn;
  list->count++;
  return 0;
}

static void resolve_pending(pending_list_t *pl, struct sljit_label *l) {
  for (size_t i = 0; i < pl->count; i++)
    sljit_set_label(pl->jumps[i], l);
  snobol_free(pl->jumps);
  pl->jumps = NULL;
  pl->count = 0;
  pl->cap = 0;
}

/* ---------------------------------------------------------------------------
 * Method JIT lowering — generates standalone native functions
 *
 * Signature: snobol_bool fn(const uint8_t *subject, int32_t length,
 *                           int32_t *position, int32_t *captures, void *scratch)
 *
 * This is a true method JIT that compiles entire patterns to standalone
 * native functions. No VM dispatch, no bailout counters, no trace storage.
 * The function operates directly on subject/length/position/captures and
 * returns true/false.
 *
 * Register allocation:
 *   R0 = position (current match position)
 *   R1 = length (subject length)
 *   R2-R5 = temporaries
 *   S0 = subject base pointer
 *   S1 = captures array pointer
 *   S2 = scratch space pointer (for choice stack)
 * --------------------------------------------------------------------------- */

/* ---- C helpers callable from JIT code via sljit_emit_call --------------- */

static void snobol_jit_helper_emit_capture(VM *vm, uint8_t reg) {
  if (reg < MAX_CAPS && vm->cap_end[reg] >= vm->cap_start[reg] &&
      vm->cap_end[reg] <= vm->len) {
    size_t s = vm->cap_start[reg];
    size_t e = vm->cap_end[reg];
    if (vm->out)
      snobol_buf_append(vm->out, vm->s + s, e - s);
    if (vm->emit_fn)
      vm->emit_fn(vm->s + s, e - s, vm->emit_udata);
  }
}

static void snobol_jit_helper_emit_literal(VM *vm, const char *data,
                                           size_t len) {
  if (vm->out)
    snobol_buf_append(vm->out, data, len);
  if (vm->emit_fn)
    vm->emit_fn(data, len, vm->emit_udata);
}

static bool snobol_jit_helper_bal(VM *vm, uint32_t open_cp,
                                   uint32_t close_cp) {
  size_t pos = vm->pos;
  uint32_t first;
  int fb;
  if (!utf8_peek_next(vm->s, vm->len, pos, &first, &fb) || first != open_cp)
    return false;
  int depth = 0;
  bool ok = false;
  while (pos < vm->len) {
    uint32_t cp;
    int cb;
    if (!utf8_peek_next(vm->s, vm->len, pos, &cp, &cb))
      break;
    if (cp == open_cp)
      depth++;
    else if (cp == close_cp) {
      depth--;
      if (depth == 0) {
        pos += (size_t)cb;
        ok = true;
        break;
      }
    }
    pos += (size_t)cb;
  }
  if (ok)
    vm->pos = pos;
  return ok;
}

static bool snobol_jit_helper_eval(VM *vm, uint16_t fn_id, uint8_t reg) {
  if (reg >= MAX_CAPS)
    return false;
  if (vm->cap_end[reg] < vm->cap_start[reg] || vm->cap_end[reg] > vm->len)
    return false;
  if (vm->eval_fn)
    return vm->eval_fn((int)fn_id, vm->s, vm->cap_start[reg],
                       vm->cap_end[reg], vm->eval_udata);
  return true;
}

static bool snobol_jit_helper_any(VM *vm, uint16_t set_id) {
  uint16_t count, ci;
  const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count, &ci);
  if (!ranges)
    return false;
  uint64_t map[2];
  bool is_ascii = ranges_to_ascii_bitmap(ranges, count, map);
  if (is_ascii) {
    if (vm->pos < vm->len && bitmap_test(map, (uint8_t)vm->s[vm->pos])) {
      vm->pos++;
      return true;
    }
    return false;
  }
  if (vm->pos >= vm->len)
    return false;
  uint32_t cp;
  int bytes;
  if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes))
    return false;
  if (ranges && range_contains(ranges, count, cp)) {
    vm->pos += bytes;
    return true;
  }
  return false;
}

static bool snobol_jit_helper_notany(VM *vm, uint16_t set_id) {
  uint16_t count, ci;
  const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count, &ci);
  if (!ranges)
    return false;
  uint64_t map[2];
  bool is_ascii = ranges_to_ascii_bitmap(ranges, count, map);
  if (is_ascii) {
    if (vm->pos < vm->len && !bitmap_test(map, (uint8_t)vm->s[vm->pos])) {
      vm->pos++;
      return true;
    }
    return false;
  }
  if (vm->pos >= vm->len)
    return false;
  uint32_t cp;
  int bytes;
  if (!utf8_peek_next(vm->s, vm->len, vm->pos, &cp, &bytes))
    return false;
  if (ranges && range_contains(ranges, count, cp))
    return false;
  vm->pos += bytes;
  return true;
}

/* ---------------------------------------------------------------------------
 * SLJIT lowering — emit SLJIT IR as executable code
 * --------------------------------------------------------------------------- */

static void *sljit_lower_method(const jit_ir_region_t *ir, VM *vm,
                                 jit_region_t *out) {
  (void)vm;

  if (!ir || ir->count == 0)
    return NULL;

  /* ---- Pre-pass: build bc_ip -> instruction index mapping ----------------- */
  bc_ip_entry_t *bc_map = snobol_malloc(ir->count * sizeof(bc_ip_entry_t));
  for (size_t i = 0; i < ir->count; i++) {
    bc_map[i].bc_ip = ir->instrs[i].bc_ip;
    bc_map[i].instr_idx = i;
  }

  /* ---- Pre-pass: allocate pending-jump lists per instruction.
   * We reserve 3 extra slots at the end:
   *   pending[ir->count]     = general fail jumps (from ANY, LIT etc.)
   *   pending[ir->count + 1] = JIT_IR_FAIL jumps
   *   pending[ir->count + 2] = ACCEPT/SUCCEED jumps
   */
  pending_list_t *pending = snobol_calloc(ir->count + 3, sizeof(pending_list_t));

  /* ---- Allocate per-instruction label pointers ----------------------------- */
  struct sljit_label **labels = snobol_calloc(ir->count + 3, sizeof(void *));

  /* ---- Pre-pass: determine which instructions are jump targets ------------ */
  bool *is_target = snobol_calloc(ir->count, sizeof(bool));
  for (size_t i = 0; i < ir->count; i++) {
    const jit_ir_instr_t *ins = &ir->instrs[i];
    size_t tgt;
    switch (ins->opcode) {
    case JIT_IR_JMP:
    case JIT_IR_GOTO:
      tgt = find_instr_by_bc_ip(bc_map, ir->count, ins->u.jmp.target);
      if (tgt != (size_t)-1) is_target[tgt] = true;
      break;
    case JIT_IR_GOTO_F:
      tgt = find_instr_by_bc_ip(bc_map, ir->count, ins->u.goto_f.target);
      if (tgt != (size_t)-1) is_target[tgt] = true;
      break;
    case JIT_IR_SPLIT:
      tgt = find_instr_by_bc_ip(bc_map, ir->count, ins->u.split.target_a);
      if (tgt != (size_t)-1) is_target[tgt] = true;
      tgt = find_instr_by_bc_ip(bc_map, ir->count, ins->u.split.target_b);
      if (tgt != (size_t)-1) is_target[tgt] = true;
      break;
    default:
      break;
    }
  }

  /* ---- Record entry and terminator bytecode IPs --------------------------- */
  size_t entry_ip = ir->instrs[0].bc_ip;
  size_t term_ip = vm->bc_len;

  /* ---- Create compiler ----------------------------------------------------- */
  struct sljit_compiler *c = sljit_create_compiler(NULL);
  if (!c) {
    snobol_free(bc_map);
    snobol_free(pending);
    snobol_free(labels);
    return NULL;
  }

  /* ---- Emit prologue -------------------------------------------------------
   *   void fn(VM *vm)
   *
   *   Scratch: R0 = position, R1 = length/end, R2–R5 = temporaries
   *   Saved:   S0 = vm*, S1 = subject base
   * ----------------------------------------------------------------------- */
  if (sljit_emit_enter(c, 0, SLJIT_ARGS0V(),
                       6, 2, 16) != SLJIT_SUCCESS) {
    sljit_free_compiler(c);
    snobol_free(bc_map);
    snobol_free(pending);
    snobol_free(labels);
    return NULL;
  }

  /* Move vm* (received in R0) to S0 */
  sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_S0, 0, SLJIT_R0, 0);

  /* Load vm->pos   -> R0  (current position) */
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0,
                 SLJIT_MEM1(SLJIT_S0), offsetof(VM, pos));
  /* Load vm->len   -> R1  (end sentinel) */
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0,
                 SLJIT_MEM1(SLJIT_S0), offsetof(VM, len));
  /* Load vm->s     -> S1  (subject base) */
  sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_S1, 0,
                 SLJIT_MEM1(SLJIT_S0), offsetof(VM, s));

  /* ---- Main pass: iterate IR instructions and emit SLJIT code ------------- */
  for (size_t i = 0; i < ir->count; i++) {
    const jit_ir_instr_t *instr = &ir->instrs[i];

    /* If this instruction is a jump target or has pending jumps, emit a label */
    if (is_target[i] || pending[i].count > 0) {
      struct sljit_label *l = sljit_emit_label(c);
      labels[i] = l;
      if (pending[i].count > 0)
        resolve_pending(&pending[i], l);
    }

    switch (instr->opcode) {

    /* -- No-ops ------------------------------------------------------------ */
    case JIT_IR_NOP:
    case JIT_IR_FENCE:
      break;

    /* -- LABEL: record label at current position --------------------------- */
    case JIT_IR_LABEL: {
      struct sljit_label *l = sljit_emit_label(c);
      labels[i] = l;
      if (pending[i].count > 0) {
        resolve_pending(&pending[i], l);
      }
      break;
    }

    /* -- JMP / GOTO: unconditional branch to bc_ip target ------------------ */
    case JIT_IR_JMP:
    case JIT_IR_GOTO: {
      size_t tgt = find_instr_by_bc_ip(bc_map, ir->count,
                                        instr->u.jmp.target);
      if (tgt != (size_t)-1) {
        struct sljit_jump *j = sljit_emit_jump(c, SLJIT_JUMP);
        if (j) {
          if (labels[tgt])
            sljit_set_label(j, labels[tgt]);
          else
            add_pending(&pending[tgt], j);
        }
      }
      break;
    }

    /* -- GOTO_F: conditional branch (if false, jump to bc_ip target) ------- */
    case JIT_IR_GOTO_F: {
      size_t tgt = find_instr_by_bc_ip(bc_map, ir->count,
                                        instr->u.goto_f.target);
      if (tgt != (size_t)-1) {
        /* GOTO_F branches when src register is zero (false). */
        struct sljit_jump *j = sljit_emit_cmp(
            c, SLJIT_EQUAL, SLJIT_R2, 0, SLJIT_IMM, 0);
        if (j) {
          if (labels[tgt])
            sljit_set_label(j, labels[tgt]);
          else
            add_pending(&pending[tgt], j);
        }
      }
      break;
    }

    /* -- LIT: inline literal match ------------------------------------------ */
    case JIT_IR_LIT: {
      uint32_t len = instr->u.lit.len;
      const uint8_t *data = instr->u.lit.data;

      for (uint32_t j = 0; j < len && j < 8; j++) {
        add_pending(&pending[ir->count],
                     sljit_emit_cmp(c, SLJIT_GREATER_EQUAL,
                                    SLJIT_R0, 0, SLJIT_R1, 0));
        sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R2, 0,
                       SLJIT_MEM2(SLJIT_S1, SLJIT_R0), 0);
        add_pending(&pending[ir->count],
                     sljit_emit_cmp(c, SLJIT_NOT_EQUAL,
                                    SLJIT_R2, 0, SLJIT_IMM, data[j]));
        sljit_emit_op2(c, SLJIT_ADD, SLJIT_R0, 0,
                       SLJIT_R0, 0, SLJIT_IMM, 1);
      }
      if (len > 8)
        break;
      break;
    }

    /* -- ANY: character-class match via C helper --------------------------- */
    case JIT_IR_ANY: {
      uint16_t set_id = instr->u.set.set_id;
      sljit_emit_op1(c, SLJIT_MOV,
                     SLJIT_MEM1(SLJIT_S0), offsetof(VM, pos),
                     SLJIT_R0, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, set_id);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM,
                     (sljit_sw)(uintptr_t)snobol_jit_helper_any);
      sljit_emit_icall(c, SLJIT_CALL,
                       SLJIT_ARG_RETURN(SLJIT_ARG_TYPE_32) |
                       SLJIT_ARG_VALUE(SLJIT_ARG_TYPE_P, 1) |
                       SLJIT_ARG_VALUE(SLJIT_ARG_TYPE_32, 2),
                       SLJIT_R2, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_R0, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0,
                     SLJIT_MEM1(SLJIT_S0), offsetof(VM, pos));
      sljit_emit_op2(c, SLJIT_AND, SLJIT_R2, 0, SLJIT_R2, 0, SLJIT_IMM, 1);
      struct sljit_jump *any_fail =
          sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R2, 0, SLJIT_IMM, 0);
      if (any_fail)
        add_pending(&pending[ir->count], any_fail);
      break;
    }

    /* -- NOTANY: not-in-class match via C helper ---------------------------- */
    case JIT_IR_NOTANY: {
      uint16_t set_id = instr->u.set.set_id;
      sljit_emit_op1(c, SLJIT_MOV,
                     SLJIT_MEM1(SLJIT_S0), offsetof(VM, pos),
                     SLJIT_R0, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, set_id);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM,
                     (sljit_sw)(uintptr_t)snobol_jit_helper_notany);
      sljit_emit_icall(c, SLJIT_CALL,
                       SLJIT_ARG_RETURN(SLJIT_ARG_TYPE_32) |
                       SLJIT_ARG_VALUE(SLJIT_ARG_TYPE_P, 1) |
                       SLJIT_ARG_VALUE(SLJIT_ARG_TYPE_32, 2),
                       SLJIT_R2, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_R0, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0,
                     SLJIT_MEM1(SLJIT_S0), offsetof(VM, pos));
      sljit_emit_op2(c, SLJIT_AND, SLJIT_R2, 0, SLJIT_R2, 0, SLJIT_IMM, 1);
      struct sljit_jump *notany_fail =
          sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R2, 0, SLJIT_IMM, 0);
      if (notany_fail)
        add_pending(&pending[ir->count], notany_fail);
      break;
    }

    /* -- SPAN: consume consecutive matching chars -------------------------- */
    case JIT_IR_SPAN: {
      add_pending(&pending[ir->count],
                   sljit_emit_cmp(c, SLJIT_GREATER_EQUAL,
                                  SLJIT_R0, 0, SLJIT_R1, 0));
      break;
    }

    /* -- BREAK: consume consecutive non-matching chars ---------------------- */
    case JIT_IR_BREAK:
      break;

    /* -- ANCHOR: ^ / $ assertion ------------------------------------------- */
    case JIT_IR_ANCHOR: {
      uint8_t atype = instr->u.anchor.type;
      if (atype == 0) {
        /* BOL: fail if pos != 0 */
        add_pending(&pending[ir->count],
                     sljit_emit_cmp(c, SLJIT_NOT_EQUAL,
                                    SLJIT_R0, 0, SLJIT_IMM, 0));
      } else {
        /* EOL: fail if pos != length */
        add_pending(&pending[ir->count],
                     sljit_emit_cmp(c, SLJIT_NOT_EQUAL,
                                    SLJIT_R0, 0, SLJIT_R1, 0));
      }
      break;
    }

    /* -- LEN: advance pos by n bytes (fail if pos + n > length) ------------- */
    case JIT_IR_LEN: {
      intptr_t n = (intptr_t)instr->u.len.n;
      /* If pos + n > length, fail */
      sljit_emit_op2(c, SLJIT_ADD, SLJIT_R2, 0,
                     SLJIT_R0, 0, SLJIT_IMM, n);
      add_pending(&pending[ir->count],
                   sljit_emit_cmp(c, SLJIT_GREATER,
                                  SLJIT_R2, 0, SLJIT_R1, 0));
      sljit_emit_op2(c, SLJIT_ADD, SLJIT_R0, 0,
                     SLJIT_R0, 0, SLJIT_IMM, n);
      break;
    }

    /* -- POS: assert pos == n ---------------------------------------------- */
    case JIT_IR_POS: {
      intptr_t n = (intptr_t)instr->u.rpos_rtab.n;
      add_pending(&pending[ir->count],
                   sljit_emit_cmp(c, SLJIT_NOT_EQUAL,
                                  SLJIT_R0, 0, SLJIT_IMM, n));
      break;
    }

    /* -- RPOS: assert length - pos == n (use R2, keep R0 = pos) ------------ */
    case JIT_IR_RPOS: {
      intptr_t n = (intptr_t)instr->u.rpos_rtab.n;
      sljit_emit_op2(c, SLJIT_SUB, SLJIT_R2, 0,
                     SLJIT_R1, 0, SLJIT_R0, 0);
      add_pending(&pending[ir->count],
                   sljit_emit_cmp(c, SLJIT_NOT_EQUAL,
                                  SLJIT_R2, 0, SLJIT_IMM, n));
      break;
    }

    /* -- TAB: assert pos + n <= length, then pos += n ----------------------- */
    case JIT_IR_TAB: {
      intptr_t n = (intptr_t)instr->u.rpos_rtab.n;
      /* Check pos + n > len first (via R2), then update R0 */
      sljit_emit_op2(c, SLJIT_ADD, SLJIT_R2, 0,
                     SLJIT_R0, 0, SLJIT_IMM, n);
      add_pending(&pending[ir->count],
                   sljit_emit_cmp(c, SLJIT_GREATER,
                                  SLJIT_R2, 0, SLJIT_R1, 0));
      sljit_emit_op2(c, SLJIT_ADD, SLJIT_R0, 0,
                     SLJIT_R0, 0, SLJIT_IMM, n);
      break;
    }

    /* -- RTAB: position = max(0, length - n) ------------------------------- */
    case JIT_IR_RTAB: {
      intptr_t n = (intptr_t)instr->u.rpos_rtab.n;
      sljit_emit_op2(c, SLJIT_SUB, SLJIT_R0, 0,
                     SLJIT_R1, 0, SLJIT_IMM, n);
      break;
    }

    /* -- FAIL: emit a label for all general-fail jumps, then return via fail path */
    case JIT_IR_FAIL: {
      if (pending[ir->count].count > 0) {
        struct sljit_label *l = sljit_emit_label(c);
        labels[ir->count] = l;
        resolve_pending(&pending[ir->count], l);
      }
      /* Jump to fail epilogue (vm->pos update + return) */
      struct sljit_jump *j = sljit_emit_jump(c, SLJIT_JUMP);
      add_pending(&pending[ir->count + 1], j);
      break;
    }

    /* -- SUCCEED / ACCEPT -------------------------------------------------- */
    case JIT_IR_SUCCEED:
    case JIT_IR_ACCEPT: {
      struct sljit_jump *j = sljit_emit_jump(c, SLJIT_JUMP);
      if (j)
        add_pending(&pending[ir->count + 2], j);
      break;
    }

    /* -- ABORT: immediate return (fail) ------------------------------------ */
    case JIT_IR_ABORT: {
      sljit_emit_op1(c, SLJIT_MOV,
                     SLJIT_MEM1(SLJIT_S0), offsetof(VM, pos),
                     SLJIT_R0, 0);
      sljit_emit_op1(c, SLJIT_MOV,
                     SLJIT_MEM1(SLJIT_S0), offsetof(VM, ip),
                     SLJIT_IMM, entry_ip);
      sljit_emit_return_void(c);
      break;
    }

    /* -- CAP_START / CAP_END: save/restore capture positions --------------- */
    case JIT_IR_CAP_START: {
      uint8_t reg = instr->u.cap.reg;
      sljit_emit_op1(c, SLJIT_MOV,
                     SLJIT_MEM1(SLJIT_S0),
                     offsetof(VM, cap_start) + reg * sizeof(size_t),
                     SLJIT_R0, 0);
      break;
    }
    case JIT_IR_CAP_END: {
      uint8_t reg = instr->u.cap.reg;
      sljit_emit_op1(c, SLJIT_MOV,
                     SLJIT_MEM1(SLJIT_S0),
                     offsetof(VM, cap_end) + reg * sizeof(size_t),
                     SLJIT_R0, 0);
      break;
    }

    /* -- SPLIT: record choice point, jump to target_a ---------------------- */
    case JIT_IR_SPLIT: {
      size_t tgt = find_instr_by_bc_ip(bc_map, ir->count,
                                        instr->u.split.target_a);
      if (tgt != (size_t)-1) {
        struct sljit_jump *j = sljit_emit_jump(c, SLJIT_JUMP);
        if (j) {
          if (labels[tgt])
            sljit_set_label(j, labels[tgt]);
          else
            add_pending(&pending[tgt], j);
        }
      }
      break;
    }

    /* -- REM: skip to end of string ---------------------------------------- */
    case JIT_IR_REM:
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_R1, 0);
      break;

    /* -- Pseudo-ops: no code emitted --------------------------------------- */
    case JIT_IR_PHI:
    case JIT_IR_COPY:
    case JIT_IR_DYNAMIC_DEF:
      break;

    /* -- EMIT_CAPTURE: call-out to emit captured text ---------------------- */
    case JIT_IR_EMIT_CAPTURE: {
      uint8_t emit_reg = instr->u.emit_cap.reg;
      sljit_emit_op1(c, SLJIT_MOV,
                     SLJIT_MEM1(SLJIT_S0), offsetof(VM, pos),
                     SLJIT_R0, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, emit_reg);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM,
                     (sljit_sw)(uintptr_t)snobol_jit_helper_emit_capture);
      sljit_emit_icall(c, SLJIT_CALL,
                       SLJIT_ARG_RETURN(SLJIT_ARG_TYPE_RET_VOID) |
                       SLJIT_ARG_VALUE(SLJIT_ARG_TYPE_P, 1) |
                       SLJIT_ARG_VALUE(SLJIT_ARG_TYPE_32, 2),
                       SLJIT_R2, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0,
                     SLJIT_MEM1(SLJIT_S0), offsetof(VM, pos));
      break;
    }

    /* -- EMIT_LITERAL: call-out to emit inline literal data ---------------- */
    case JIT_IR_EMIT_LITERAL: {
      const char *data = instr->u.emit_lit.data;
      size_t len = instr->u.emit_lit.len;
      sljit_emit_op1(c, SLJIT_MOV,
                     SLJIT_MEM1(SLJIT_S0), offsetof(VM, pos),
                     SLJIT_R0, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM,
                     (sljit_sw)(uintptr_t)data);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, (sljit_sw)len);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM,
                     (sljit_sw)(uintptr_t)snobol_jit_helper_emit_literal);
      sljit_emit_icall(c, SLJIT_CALL,
                       SLJIT_ARG_RETURN(SLJIT_ARG_TYPE_RET_VOID) |
                       SLJIT_ARG_VALUE(SLJIT_ARG_TYPE_P, 1) |
                       SLJIT_ARG_VALUE(SLJIT_ARG_TYPE_P, 2),
                       SLJIT_R3, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0,
                     SLJIT_MEM1(SLJIT_S0), offsetof(VM, pos));
      break;
    }

    /* -- BAL: balanced string matching via C helper ------------------------ */
    case JIT_IR_BAL: {
      uint32_t open_cp = instr->u.bal.open_cp;
      uint32_t close_cp = instr->u.bal.close_cp;
      sljit_emit_op1(c, SLJIT_MOV,
                     SLJIT_MEM1(SLJIT_S0), offsetof(VM, pos),
                     SLJIT_R0, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, open_cp);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, close_cp);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM,
                     (sljit_sw)(uintptr_t)snobol_jit_helper_bal);
      sljit_emit_icall(c, SLJIT_CALL,
                       SLJIT_ARG_RETURN(SLJIT_ARG_TYPE_32) |
                       SLJIT_ARG_VALUE(SLJIT_ARG_TYPE_P, 1) |
                       SLJIT_ARG_VALUE(SLJIT_ARG_TYPE_32, 2) |
                       SLJIT_ARG_VALUE(SLJIT_ARG_TYPE_32, 3),
                       SLJIT_R3, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_R0, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0,
                     SLJIT_MEM1(SLJIT_S0), offsetof(VM, pos));
      sljit_emit_op2(c, SLJIT_AND, SLJIT_R2, 0, SLJIT_R2, 0, SLJIT_IMM, 1);
      struct sljit_jump *bal_fail = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R2, 0, SLJIT_IMM, 0);
      if (bal_fail)
        add_pending(&pending[ir->count], bal_fail);
      break;
    }

    /* -- EVAL: evaluate compiler built-in via C helper --------------------- */
    case JIT_IR_EVAL: {
      uint16_t fn_id = instr->u.eval.fn_id;
      uint8_t reg = instr->u.eval.reg;
      sljit_emit_op1(c, SLJIT_MOV,
                     SLJIT_MEM1(SLJIT_S0), offsetof(VM, pos),
                     SLJIT_R0, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, fn_id);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, reg);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R3, 0, SLJIT_IMM,
                     (sljit_sw)(uintptr_t)snobol_jit_helper_eval);
      sljit_emit_icall(c, SLJIT_CALL,
                       SLJIT_ARG_RETURN(SLJIT_ARG_TYPE_32) |
                       SLJIT_ARG_VALUE(SLJIT_ARG_TYPE_P, 1) |
                       SLJIT_ARG_VALUE(SLJIT_ARG_TYPE_32, 2) |
                       SLJIT_ARG_VALUE(SLJIT_ARG_TYPE_32, 3),
                       SLJIT_R3, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_R0, 0);
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0,
                     SLJIT_MEM1(SLJIT_S0), offsetof(VM, pos));
      sljit_emit_op2(c, SLJIT_AND, SLJIT_R2, 0, SLJIT_R2, 0, SLJIT_IMM, 1);
      struct sljit_jump *eval_fail = sljit_emit_cmp(c, SLJIT_EQUAL, SLJIT_R2, 0, SLJIT_IMM, 0);
      if (eval_fail)
        add_pending(&pending[ir->count], eval_fail);
      break;
    }

    /* -- Non-compilable opcodes: silently ignore for now ------------------- */
    case JIT_IR_DYNAMIC:
    default:
      break;
    }
  }

  /* ---- Emit fail epilogue -------------------------------------------------
   *   pending[ir->count]   = fail jumps from check instructions
   *   pending[ir->count+1] = JIT_IR_FAIL jumps
   * ----------------------------------------------------------------------- */
  if (pending[ir->count].count > 0) {
    struct sljit_label *fl = sljit_emit_label(c);
    resolve_pending(&pending[ir->count], fl);
  }
  if (pending[ir->count + 1].count > 0) {
    struct sljit_label *fl = sljit_emit_label(c);
    resolve_pending(&pending[ir->count + 1], fl);
  }
  /* Store vm->pos, set vm->ip = entry_ip (bailout), return */
  sljit_emit_op1(c, SLJIT_MOV,
                 SLJIT_MEM1(SLJIT_S0), offsetof(VM, pos),
                 SLJIT_R0, 0);
  sljit_emit_op1(c, SLJIT_MOV,
                 SLJIT_MEM1(SLJIT_S0), offsetof(VM, ip),
                 SLJIT_IMM, entry_ip);
  sljit_emit_return_void(c);

  /* ---- Emit success epilogue ----------------------------------------------
   *   pending[ir->count+2] = ACCEPT/SUCCEED jumps
   * ----------------------------------------------------------------------- */
  if (pending[ir->count + 2].count > 0) {
    struct sljit_label *sl = sljit_emit_label(c);
    resolve_pending(&pending[ir->count + 2], sl);
  } else {
    sljit_emit_label(c);
  }
  sljit_emit_op1(c, SLJIT_MOV,
                 SLJIT_MEM1(SLJIT_S0), offsetof(VM, pos),
                 SLJIT_R0, 0);
  sljit_emit_op1(c, SLJIT_MOV,
                 SLJIT_MEM1(SLJIT_S0), offsetof(VM, ip),
                 SLJIT_IMM, term_ip);
  sljit_emit_return_void(c);

  /* ---- Generate executable code ------------------------------------------- */
  sljit_uw code_size = 0;
  void *code_ptr = sljit_generate_code(c, 0, NULL);
  sljit_free_compiler(c);

  if (!code_ptr) {
    snobol_free(bc_map);
    snobol_free(pending);
    snobol_free(labels);
    return NULL;
  }

  if (out) {
    out->code_start = (uint32_t *)code_ptr;
    out->code_size = (size_t)code_size;
    out->p = (uint32_t *)((uint8_t *)code_ptr + code_size);
    out->n_blocks = ir->block_count > 0 ? (size_t)ir->block_count : 1;
  }

  snobol_free(bc_map);
  snobol_free(pending);
  snobol_free(labels);
  return code_ptr;
}

/* ---------------------------------------------------------------------------
 * Method JIT lowering — compiles entire patterns to native code
 *
 * Signature: void fn(VM *vm) — same ABI as the method JIT Tier 0 path
 * in search.c.
 *
 * Register allocation:
 *   R0 = position (current match position)
 *   R1 = length (subject length)
 *   R2-R5 = temporaries
 *   S0 = VM pointer
 *   S1 = subject base pointer
 * --------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * flush_icache — no-op; SLJIT already handles cache flush internally
 * ---------------------------------------------------------------------------
 */

static void sljit_flush_icache(void *code, size_t size) {
  (void)code;
  (void)size;
}

/* ---------------------------------------------------------------------------
 * vtable + registration
 * ---------------------------------------------------------------------------
 */

static const jit_backend_t sljit_backend = {
    .name = "sljit",
    .lower = sljit_lower_method,
    .flush_icache = sljit_flush_icache,
};

void snobol_jit_sljit_register(void) { jit_backend_register(&sljit_backend); }

#endif /* SNOBOL_JIT_BACKEND_SLJIT */
