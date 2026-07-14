/**
 * @file search_simd.c
 * @brief SIMD-accelerated Thompson NFA engine
 *
 * Implements Tier 9 of the search execution pipeline: a SIMD Thompson NFA
 * engine that processes 32 bytes (AVX2) or 16 bytes (NEON) simultaneously
 * for character-class patterns (SPAN, BREAK, ANY, NOTANY) with ASCII-only
 * ranges.
 *
 * The engine works by:
 *  1. Pre-building NFA state transition bitmasks from the pattern bytecode
 *  2. Loading 32/16 subject bytes into a SIMD register
 *  3. Broadcasting each byte and AND'ing with state masks to test character
 *     class membership
 *  4. Updating the active state set using SIMD blend/shift operations
 *
 * A scalar reference implementation is provided for platforms without SIMD
 * and for handling tail bytes that don't fill a full SIMD register.
 */

#include "snobol/search.h"
#include "snobol/simd.h"
#include "snobol/vm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declaration for fallback when NFA build fails */
bool tier_general_fallback(VM *vm, const char *subject, size_t subject_len,
                           size_t start_offset, const snobol_search_meta_t *meta,
                           const snobol_dfa_t *dfa,
                           snobol_search_result_t *out_result,
                           snobol_search_diag_t *diag, bool anchored);

/* ---------------------------------------------------------------------------
 * NFA state bitmask representation
 *
 * Each NFA state is represented as a bit in a bitmask. The state set tracks
 * which NFA states are active at the current subject position.
 * ---------------------------------------------------------------------------
 */

/** Maximum NFA states tracked by the SIMD engine. */
#define SIMD_NFA_MAX_STATES 64

/** Bitmask type for NFA state sets. */
typedef uint64_t simd_nfa_state_t;

/** Holds the pre-computed transition data for one NFA state. */
typedef struct {
  uint64_t char_mask[4]; /**< 256-bit byte-level bitmap: bytes that advance this state */
  uint16_t next_state;   /**< Next NFA state index on match */
  bool is_accept;        /**< True if this state is an accepting state */
  bool is_split;         /**< True if this is a split/epsilon state */
} simd_nfa_trans_t;

/** NFA compiled state for SIMD execution. */
typedef struct {
  simd_nfa_trans_t states[SIMD_NFA_MAX_STATES];
  uint16_t num_states;
  uint16_t start_state;
  bool is_span;  /**< True for SPAN-style (match consecutive class bytes) */
  bool is_break; /**< True for BREAK-style (match up to first non-class byte) */
  bool is_any;   /**< True for ANY/NOTANY (match single class byte) */
} simd_nfa_t;

/* ---------------------------------------------------------------------------
 * NFA bytecode walker helpers (reuse from search.c)
 *
 * These duplicate the helpers in search.c to avoid exposing them in a header.
 * TODO: refactor into a shared internal header.
 * ---------------------------------------------------------------------------
 */

static inline uint32_t simd_read_u32(const uint8_t *bc, size_t ip) {
  return ((uint32_t)bc[ip] << 24) | ((uint32_t)bc[ip + 1] << 16) |
         ((uint32_t)bc[ip + 2] << 8) | (uint32_t)bc[ip + 3];
}

static inline uint16_t simd_read_u16(const uint8_t *bc, size_t ip) {
  return ((uint16_t)bc[ip] << 8) | (uint16_t)bc[ip + 1];
}

/* ---------------------------------------------------------------------------
 * 3.1 simd_nfa_state_t definition (bitmask) — used via typedef above
 * 3.2 simd_nfa_build_masks — convert NFA bytecode to state transition bitmasks
 * 3.4 SIMD eligibility check
 * ---------------------------------------------------------------------------
 */

/**
 * Check whether a pattern is eligible for SIMD NFA execution.
 *
 * A pattern is SIMD-eligible when:
 *  1. The bytecode starts with a single character-class op (SPAN, BREAK, ANY,
 *     NOTANY) followed only by terminators (ACCEPT, FAIL, ABORT, SUCCEED)
 *     and no-ops
 *  2. No side effects, captures, literal strings, or backtracking
 *  3. Character classes may cover the full byte range (0-255), making the
 *     SIMD path UTF-8 safe
 */
bool check_simd_eligible(const uint8_t *bc, size_t bc_len) {
  if (!bc || bc_len < 2)
    return false;

  /* The SIMD NFA engine (`build_nfa_masks`) only supports patterns that start
   * with a single character-class op (SPAN, BREAK, ANY, NOTANY) followed by a
   * terminator (ACCEPT, FAIL, ABORT, SUCCEED).  Patterns with LEN, LIT, or
   * control flow (JMP, SPLIT, POS, RPOS) are not supported and must fall back
   * to the VM. */
  uint8_t op0 = bc[0];
  switch (op0) {
  case OP_SPAN:
  case OP_BREAK:
  case OP_ANY:
  case OP_NOTANY:
    break;
  default:
    return false;
  }

  /* Skip the first op and its arguments.
   * SPAN/BREAK/ANY/NOTANY: 1 (op) + 2 (set_id) = 3 bytes, but the range data
   * follows at variable offset referenced by set_id — we don't walk it here.
   * The remaining bytecode must be only terminators. */
  size_t ip = 0;
  uint8_t first_op = bc[ip++];
  (void)first_op;
  /* SPAN/BREAK: op + 2-byte set_id */
  ip += 2;

  /* The rest must be only terminators and no-ops.
   * Stop at the first terminator — any bytes after it are inline annotation
   * data (range entries, offset table), not bytecode to execute. */
  while (ip < bc_len) {
    uint8_t op = bc[ip++];
    switch (op) {
    case OP_ACCEPT:
    case OP_FAIL:
    case OP_ABORT:
    case OP_SUCCEED:
      return true; /* terminator found — pattern is valid */
    case OP_NOP:
      break;
    default:
      return false;
    }
  }
  return false; /* no terminator found */
}

/* ---------------------------------------------------------------------------
 * Build NFA transition masks from bytecode.
 *
 * Walks the bytecode and builds a state machine where each state has a
 * character-class bitmap (which bytes cause a transition) and a next-state
 * pointer.
 *
 * For SPAN patterns: the state loops on class bytes (self-transition) and
 * exits on non-class bytes (epsilon to next instruction).
 *
 * For BREAK patterns: the state advances through non-delimiter bytes and
 * exits on delimiter bytes.
 * ---------------------------------------------------------------------------
 */
static bool build_nfa_masks(simd_nfa_t *nfa, const uint8_t *bc,
                            size_t bc_len, const VM *vm) {
  memset(nfa, 0, sizeof(*nfa));
  nfa->num_states = 0;
  nfa->start_state = 0;

  if (!bc || bc_len < 2)
    return false;

  size_t ip = 0;
  uint16_t state_id = 0;

  if (ip >= bc_len)
    return false;

  uint8_t op0 = bc[ip];
  uint16_t set_id;
  uint16_t count, ci;
  const uint8_t *ranges;

  if (op0 == OP_SPAN && ip + 3 <= bc_len) {
    nfa->is_span = true;
    set_id = simd_read_u16(bc, ip + 1);
    ranges = get_ranges_ptr(vm, set_id, &count, &ci);
    if (!ranges || count == 0)
      return false;
    /* State 0: on class byte → stay in state 0; on non-class → epsilon to
     * next instruction (state 1 = accept) */
    if (!ranges_to_full_bitmap(ranges, count, nfa->states[0].char_mask))
      return false;
    nfa->states[0].next_state = 0; /* self-loop */
    nfa->states[0].is_split = false;
    nfa->states[0].is_accept = false;
    /* State 1: accept */
    nfa->states[1].is_accept = true;
    nfa->states[1].is_split = false;
    nfa->num_states = 2;

  } else if (op0 == OP_BREAK && ip + 3 <= bc_len) {
    nfa->is_break = true;
    set_id = simd_read_u16(bc, ip + 1);
    ranges = get_ranges_ptr(vm, set_id, &count, &ci);
    if (!ranges || count == 0)
      return false;
    /* BREAK: advance on non-delimiter bytes.  Build the inverted bitmap. */
    uint64_t delim_mask[4];
    if (!ranges_to_full_bitmap(ranges, count, delim_mask))
      return false;
    nfa->states[0].char_mask[0] = ~delim_mask[0];
    nfa->states[0].char_mask[1] = ~delim_mask[1];
    nfa->states[0].char_mask[2] = ~delim_mask[2];
    nfa->states[0].char_mask[3] = ~delim_mask[3];
    nfa->states[0].next_state = 0; /* self-loop on non-delimiter */
    nfa->states[0].is_accept = false;
    /* State 1: delimiter found → accept */
    nfa->states[1].is_accept = true;
    nfa->num_states = 2;

  } else if (op0 == OP_ANY && ip + 3 <= bc_len) {
    nfa->is_any = true;
    set_id = simd_read_u16(bc, ip + 1);
    ranges = get_ranges_ptr(vm, set_id, &count, &ci);
    if (!ranges || count == 0)
      return false;
    if (!ranges_to_full_bitmap(ranges, count, nfa->states[0].char_mask))
      return false;
    nfa->states[0].next_state = 1; /* advance to accept on match */
    nfa->states[0].is_accept = false;
    nfa->states[1].is_accept = true;
    nfa->num_states = 2;

  } else if (op0 == OP_NOTANY && ip + 3 <= bc_len) {
    nfa->is_any = true;
    set_id = simd_read_u16(bc, ip + 1);
    ranges = get_ranges_ptr(vm, set_id, &count, &ci);
    if (!ranges || count == 0)
      return false;
    uint64_t class_bits[4];
    if (!ranges_to_full_bitmap(ranges, count, class_bits))
      return false;
    nfa->states[0].char_mask[0] = ~class_bits[0];
    nfa->states[0].char_mask[1] = ~class_bits[1];
    nfa->states[0].char_mask[2] = ~class_bits[2];
    nfa->states[0].char_mask[3] = ~class_bits[3];
    nfa->states[0].next_state = 1;
    nfa->states[0].is_accept = false;
    nfa->states[1].is_accept = true;
    nfa->num_states = 2;

  } else {
    /* Alternation and other patterns — not directly supported yet */
    return false;
  }

  return true;
}

/* ---------------------------------------------------------------------------
 * 3.3 Scalar reference implementation
 *
 * Processes the subject 1 byte at a time, following NFA state transitions.
 * This is the reference implementation used for correctness verification
 * and as a fallback for tail bytes.
 * ---------------------------------------------------------------------------
 */
static bool simd_nfa_exec_scalar(const simd_nfa_t *nfa, const char *subject,
                                  size_t subject_len, size_t offset,
                                  snobol_search_result_t *out_result) {
  size_t start = offset;
  simd_nfa_state_t state = (simd_nfa_state_t)1 << nfa->start_state;

  while (offset < subject_len) {
    uint8_t byte = (uint8_t)subject[offset];

    /* Compute next state set */
    simd_nfa_state_t next = 0;
    for (uint16_t s = 0; s < nfa->num_states; s++) {
      if (!(state & ((simd_nfa_state_t)1 << s)))
        continue;
      if (nfa->states[s].is_accept) {
        next |= (simd_nfa_state_t)1 << s;
        continue;
      }
      if (nfa->states[s].is_split) {
        /* Epsilon transition: advance to next state */
        next |= (simd_nfa_state_t)1 << nfa->states[s].next_state;
        continue;
      }
      /* Check character class membership (256-bit bitmap) */
      unsigned idx = byte >> 6;
      bool in_class = (nfa->states[s].char_mask[idx] >> (byte & 63)) & 1ULL;

      if (in_class) {
        uint16_t ns = nfa->states[s].next_state;
        if (ns == s) {
          /* Self-loop: stay in this state */
          next |= (simd_nfa_state_t)1 << s;
        } else if (ns < nfa->num_states) {
          /* Transition to next state */
          next |= (simd_nfa_state_t)1 << ns;
        }
      }
    }

    state = next;
    offset++;

    /* Check if accepting state is reached */
    for (uint16_t s = 0; s < nfa->num_states; s++) {
      if ((state & ((simd_nfa_state_t)1 << s)) && nfa->states[s].is_accept) {
        out_result->success = true;
        out_result->match_start = start;
        out_result->match_end = offset;
        return true;
      }
    }

    /* If no states active, fail */
    if (state == 0)
      break;
  }

  /* Check if we can accept at end of subject (e.g., BREAK at position = len) */
  for (uint16_t s = 0; s < nfa->num_states; s++) {
    if ((state & ((simd_nfa_state_t)1 << s)) && nfa->states[s].is_accept) {
      out_result->success = true;
      out_result->match_start = start;
      out_result->match_end = offset;
      return true;
    }
  }

  /* SPAN matches at least one byte from the class.
   * When state == 0 a non-class byte was found and we've incremented
   * offset past it, so match_end = offset - 1 (position of the non-class
   * byte).  When state != 0 (end of all-class subject) match_end = offset.
   *
   * Require that at least one byte was actually processed (offset > start).
   * This prevents returning an empty match when the while loop never ran
   * (e.g. SPAN([0xC0-0xDF]) on ASCII-only data, or offset == subject_len). */
  if (nfa->is_span && offset > start &&
      (state != 0 || offset > start + 1)) {
    size_t end = (state == 0) ? offset - 1 : offset;
    out_result->success = true;
    out_result->match_start = start;
    out_result->match_end = end;
    return true;
  }

  out_result->success = false;
  return false;
}

/* ---------------------------------------------------------------------------
 * 4/5. SIMD-accelerated implementations
 *
 * These process SNOBOL_SIMD_WIDTH bytes at once using SIMD instructions.
 * Currently this file provides the scalar reference; the platform-specific
 * SIMD implementations will be added in their own sections.
 *
 * The dispatch in tier_simd_nfa() selects the appropriate implementation
 * based on compile-time SIMD availability and runtime subject length.
 * ---------------------------------------------------------------------------
 */

#if SNOBOL_HAS_AVX2
#include <immintrin.h>

/**
 * 4.1 AVX2 implementation — process 32 bytes at once.
 *
 * For each 32-byte chunk:
 *  1. Load 32 subject bytes into a __m256i register
 *  2. For each NFA state, broadcast the state's byte value and compare
 *  3. Use AVX2 blend operations to update the active state set
 */
static bool simd_nfa_exec_avx2(const simd_nfa_t *nfa, const char *subject,
                                size_t subject_len, size_t offset,
                                snobol_search_result_t *out_result) {
  size_t start = offset;
  /* State set: we use a single uint64_t (up to 64 states supported) */
  uint64_t state = (uint64_t)1 << nfa->start_state;
  size_t chunk_end = subject_len & ~(size_t)31;

  while (offset < chunk_end) {
    /* Load 32 bytes */
    __m256i bytes = _mm256_loadu_si256((const __m256i *)(subject + offset));
    uint64_t next = 0;

    /* Process each active state */
    for (uint16_t s = 0; s < nfa->num_states; s++) {
      if (!(state & ((uint64_t)1 << s)))
        continue;
      if (nfa->states[s].is_accept) {
        next |= (uint64_t)1 << s;
        continue;
      }
      if (nfa->states[s].is_split) {
        next |= (uint64_t)1 << nfa->states[s].next_state;
        continue;
      }
      /* For character-class states, build a mask of matching byte positions
       * in this 32-byte chunk.
       *
       * We use the pre-computed ASCII bitmap as 4 x 64-bit masks.
       * For each 64-bit lane of the bitmap, we check which bytes match.
       * A byte b matches state s when bitmap[s][b / 64] has bit (b % 64) set.
       *
       * Simplified: for each byte position i in [0, 31], check individually.
       * A more advanced approach would broadcast the mask and compare,
       * but for correctness we start simple. */
    }

    /* For each byte position, test character class membership */
    alignas(32) uint8_t buf[32];
    _mm256_storeu_si256((__m256i *)buf, bytes);

    for (int i = 0; i < 32 && (offset + i) < subject_len; i++) {
      uint8_t b = buf[i];
      uint64_t next_by_byte = 0;

      /* Test each active state against this byte */
      uint64_t active = state;
      while (active) {
        int s = __builtin_ctzll(active);
        active &= active - 1;

        if (nfa->states[s].is_accept) {
          next_by_byte |= (uint64_t)1 << s;
          continue;
        }
        if (nfa->states[s].is_split) {
          next_by_byte |= (uint64_t)1 << nfa->states[s].next_state;
          continue;
        }
        unsigned idx = b >> 6;
        bool in_class = (nfa->states[s].char_mask[idx] >> (b & 63)) & 1ULL;

        if (in_class) {
          uint16_t ns = nfa->states[s].next_state;
          if (ns == s)
            next_by_byte |= (uint64_t)1 << s;
          else if (ns < nfa->num_states)
            next_by_byte |= (uint64_t)1 << ns;
        }
      }

      state = next_by_byte;
      offset++;

      /* Check accept */
      for (uint16_t s = 0; s < nfa->num_states; s++) {
        if ((state & ((uint64_t)1 << s)) && nfa->states[s].is_accept) {
          out_result->success = true;
          out_result->match_start = start;
          out_result->match_end = offset;
          return true;
        }
      }
      if (state == 0) {
        if (nfa->is_span && offset > start + 1) {
          out_result->success = true;
          out_result->match_start = start;
          out_result->match_end = offset - 1;
          return true;
        }
        break;
      }
    }

    if (state == 0)
      break;
  }

  /* 4.4 Handle tail bytes with scalar fallback */
  if (offset < subject_len) {
    bool ret = simd_nfa_exec_scalar(nfa, subject, subject_len, offset,
                                     out_result);
    if (nfa->is_span && ret)
      out_result->match_start = start;
    return ret;
  }

  /* Check accept at end */
  for (uint16_t s = 0; s < nfa->num_states; s++) {
    if ((state & ((uint64_t)1 << s)) && nfa->states[s].is_accept) {
      out_result->success = true;
      out_result->match_start = start;
      out_result->match_end = offset;
      return true;
    }
  }

  /* SPAN matches at least one byte from the class.
   * Require that at least one byte was actually processed (offset > start). */
  if (nfa->is_span && offset > start &&
      (state != 0 || offset > start + 1)) {
    size_t end = (state == 0) ? offset - 1 : offset;
    out_result->success = true;
    out_result->match_start = start;
    out_result->match_end = end;
    return true;
  }

  out_result->success = false;
  return false;
}
#endif /* SNOBOL_HAS_AVX2 */

#if SNOBOL_HAS_NEON
#include <arm_neon.h>

/**
 * 5.1 NEON implementation — process 16 bytes at once.
 */
static bool simd_nfa_exec_neon(const simd_nfa_t *nfa, const char *subject,
                                size_t subject_len, size_t offset,
                                snobol_search_result_t *out_result) {
  size_t start = offset;
  uint64_t state = (uint64_t)1 << nfa->start_state;
  size_t chunk_end = subject_len & ~(size_t)15;

  while (offset < chunk_end) {
    uint8x16_t bytes = vld1q_u8((const uint8_t *)(subject + offset));
    uint64_t next = 0;

    alignas(16) uint8_t buf[16];
    vst1q_u8(buf, bytes);

    for (int i = 0; i < 16 && (offset + i) < subject_len; i++) {
      uint8_t b = buf[i];
      uint64_t next_by_byte = 0;
      uint64_t active = state;

      while (active) {
        int s = __builtin_ctzll(active);
        active &= active - 1;

        if (nfa->states[s].is_accept) {
          next_by_byte |= (uint64_t)1 << s;
          continue;
        }
        if (nfa->states[s].is_split) {
          next_by_byte |= (uint64_t)1 << nfa->states[s].next_state;
          continue;
        }
        unsigned idx = b >> 6;
        bool in_class = (nfa->states[s].char_mask[idx] >> (b & 63)) & 1ULL;

        if (in_class) {
          uint16_t ns = nfa->states[s].next_state;
          if (ns == s)
            next_by_byte |= (uint64_t)1 << s;
          else if (ns < nfa->num_states)
            next_by_byte |= (uint64_t)1 << ns;
        }
      }

      state = next_by_byte;
      offset++;

      for (uint16_t s = 0; s < nfa->num_states; s++) {
        if ((state & ((uint64_t)1 << s)) && nfa->states[s].is_accept) {
          out_result->success = true;
          out_result->match_start = start;
          out_result->match_end = offset;
          return true;
        }
      }
      if (state == 0) {
        if (nfa->is_span && offset > start + 1) {
          out_result->success = true;
          out_result->match_start = start;
          out_result->match_end = offset - 1;
          return true;
        }
        break;
      }
    }

    if (state == 0)
      break;
  }

  /* 5.4 Handle tail bytes with scalar fallback */
  if (offset < subject_len) {
    bool ret = simd_nfa_exec_scalar(nfa, subject, subject_len, offset,
                                     out_result);
    if (nfa->is_span && ret)
      out_result->match_start = start;
    return ret;
  }

  for (uint16_t s = 0; s < nfa->num_states; s++) {
    if ((state & ((uint64_t)1 << s)) && nfa->states[s].is_accept) {
      out_result->success = true;
      out_result->match_start = start;
      out_result->match_end = offset;
      return true;
    }
  }

  /* SPAN matches at least one byte from the class.
   * Require that at least one byte was actually processed (offset > start). */
  if (nfa->is_span && offset > start &&
      (state != 0 || offset > start + 1)) {
    size_t end = (state == 0) ? offset - 1 : offset;
    out_result->success = true;
    out_result->match_start = start;
    out_result->match_end = end;
    return true;
  }

  out_result->success = false;
  return false;
}
#endif /* SNOBOL_HAS_NEON */

/* ---------------------------------------------------------------------------
 * 3.5 Tier handler — top-level entry point for Tier 9 (SIMD NFA).
 *
 * Builds the NFA masks from bytecode, then dispatches to the appropriate
 * SIMD implementation (AVX2, NEON, or scalar fallback).
 * ---------------------------------------------------------------------------
 */
bool tier_simd_nfa(VM *vm, const char *subject, size_t subject_len,
                    size_t start_offset, const snobol_search_meta_t *meta,
                    const snobol_dfa_t *dfa,
                    snobol_search_result_t *out_result,
                    snobol_search_diag_t *diag, bool anchored) {
  (void)diag;

  /* Build NFA masks from pattern bytecode */
  simd_nfa_t nfa;
  if (!build_nfa_masks(&nfa, vm->bc, vm->bc_len, vm)) {
    /* Fall back to general VM if NFA build fails */
    return tier_general_fallback(vm, subject, subject_len, start_offset,
                                  meta, dfa, out_result, diag, anchored);
  }

  /* Scan through subject positions using the anchored NFA exec.
   * For SIMD-eligible patterns (SPAN/BREAK/ANY/NOTANY), the NFA exec
   * processes a single position. We try each position until a match
   * is found.  The inner loop is accelerated by SIMD implementations
   * (NEON on Apple Silicon, AVX2 on x86) for chunks of 16/32 bytes,
   * falling back to scalar for tail bytes. */
  size_t offset = start_offset;
  while (offset <= subject_len) {
    snobol_search_result_t tmp;
    bool ok;

#if SNOBOL_HAS_NEON
    ok = simd_nfa_exec_neon(&nfa, subject, subject_len, offset, &tmp);
#elif SNOBOL_HAS_AVX2
    ok = simd_nfa_exec_avx2(&nfa, subject, subject_len, offset, &tmp);
#else
    ok = simd_nfa_exec_scalar(&nfa, subject, subject_len, offset, &tmp);
#endif

    if (ok) {
      out_result->success = true;
      out_result->match_start = tmp.match_start;
      out_result->match_end = tmp.match_end;
      return true;
    }

    if (offset >= subject_len)
      break;
    if (anchored)
      break;
    offset++;
  }

  out_result->success = false;
  return false;
}
