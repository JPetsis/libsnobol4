#pragma once

/**
 * @file search.h
 * @brief Core search runtime API for libsnobol4
 *
 * Provides search-from-offset, find-next-match, split-over-subject, and
 * replace-over-subject execution paths that operate within one native C
 * control loop, avoiding PHP-level offset iteration.
 *
 * The search runtime is language-agnostic: it depends only on the compiled
 * bytecode, the VM structure, and standard C library primitives.
 */

#include "snobol/snobol_attrs.h"
#include "snobol/vm.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Search candidate metadata
 *
 * Derived once from compiled bytecode; stored alongside patterns for fast
 * retrieval on each search invocation.  Allows the search loop to skip
 * unpromising candidate positions before invoking the general VM matcher.
 * ---------------------------------------------------------------------------
 */

/** Maximum literal prefix bytes retained for candidate skipping. */
#define SNOBOL_SEARCH_MAX_PREFIX 16

/** Maximum single-char alternation bytes tracked. */
#define SNOBOL_SEARCH_MAX_ALT 32

typedef struct {
  /* Literal prefix acceleration ----------------------------------------- */
  bool has_literal_prefix;
  uint8_t literal_prefix[SNOBOL_SEARCH_MAX_PREFIX];
  size_t literal_prefix_len;

  /* First-byte candidate ------------------------------------------------- */
  /* When has_first_byte is true, every match MUST start with first_byte.
   * Enables memchr-based position skipping when prefix_len >= 1. */
  bool has_first_byte;
  uint8_t first_byte;

  /* Candidate-set bitmap ------------------------------------------------- */
  /* Each set bit i means a match MAY start with byte i (0-127).
   * Used for alternation-of-single-chars and similar patterns. */
  bool has_candidate_bitmap;
  uint64_t candidate_bitmap[2]; /* 128-bit ASCII bitmap */

  /* Empty-match flag ----------------------------------------------------- */
  /* When true, the pattern can succeed while consuming zero bytes;
   * the search loop must advance unconditionally after an empty match. */
  bool may_match_empty;

  /* Always-consuming flag ------------------------------------------------ */
  /* When true, the pattern consumes at least one byte on success.
   * The search loop may advance by (match_end - match_start) safely. */
  bool always_consumes;

  /* Single-char alternation ---------------------------------------------- */
  /* True if the root pattern is a pure alternation of single-character
   * literals; these can be collapsed to a candidate-set for one-pass scan. */
  bool is_single_char_alt;
  uint8_t alt_bytes[SNOBOL_SEARCH_MAX_ALT];
  size_t alt_count;

  /* BREAK / SPAN / BREAKX classification --------------------------------- */
  /* True if the first effective opcode is BREAK, BREAKX, or SPAN. */
  bool is_break_family;     /**< OP_BREAK or OP_BREAKX as root op */
  bool is_span_family;      /**< OP_SPAN as root op */
  bool is_breakx;           /**< Specifically OP_BREAKX (extended retry) */
  bool ascii_class_only;    /**< Character class is entirely ASCII (<= 127) */
  uint64_t class_bitmap[2]; /**< ASCII bitmap for BREAK/SPAN/BREAKX root op */

  /* Start-byte bitmap ---------------------------------------------------- */
  /* 256-bit bitmap: bit i is set when the pattern can match starting with
   * byte value i.  Computed by derive_meta for ALL patterns.
   * Allows the search loop to reject candidate positions whose subject byte
   * is not in the bitmap (inspired by PCRE2's start_bits). */
  bool has_start_bitmap;
  uint8_t start_bitmap[32];

  /* Minimum match length (in bytes) -------------------------------------- */
  /* Lower bound on the number of subject bytes consumed by a successful
   * match.  Zero when unknown.  Inspired by PCRE2's find_minlength. */
  uint32_t minlength;

  /* Automaton eligibility ------------------------------------------------ */
  /* True if the pattern is eligible for the lightweight automaton path:
   * ASCII-only, side-effect free (no EVAL/ASSIGN/EMIT ops), and structurally
   * local (no DYNAMIC). */
  bool automaton_eligible;

  /* Alternation-of-literals detection ----------------------------------- */
  /* True when the pattern is a flat alternation of literal strings
   * (e.g. "abc" | "def" | "ghi").  Enables trie-based multi-string matching
   * (Tier 3a) which replaces the general VM loop for these patterns. */
  bool is_alt_literals;

  /* Boyer-Moore-Horspool skip table -------------------------------------- */
  /* When has_bmh_skip is true, bmh_skip[byte] gives the distance to advance
   * the search position after a VM failure at the current candidate.  The
   * table is computed from the literal prefix (bmh_skip_len) and allows the
   * search loop to skip more than one byte at a time for literal-leading
   * patterns that fall through to the general VM or automaton paths.
   *
   * The skip value is computed for the byte at subject[pos + bmh_skip_len - 1]
   * and is valid as long as pos + bmh_skip_len <= subject_len.
   * bmh_skip_len is identical to literal_prefix_len when has_bmh_skip is set. */
  bool has_bmh_skip;
  uint8_t bmh_skip[256];
  size_t bmh_skip_len;

  /* Literal-only detection ------------------------------------------------ */
  /* True when the pattern is a single literal followed by ACCEPT (optionally
   * with zero-width assertions like POS/RPOS/ANCHOR between).  Enables a
   * fast memmem/memcmp path that bypasses the VM entirely. */
  bool is_literal_only;

  /* Search-VM eligibility ------------------------------------------------- */
  /* True when the pattern contains only opcodes from the safe set supported
   * by the lightweight search-VM (search_vm_exec).  The safe set includes
   * LIT, LEN, ANY, NOTANY, SPAN, BREAK, JMP, SPLIT, POS, RPOS, TAB, RTAB,
   * ANCHOR, FENCE, NOP, REPEAT_INIT, REPEAT_STEP, ACCEPT, FAIL, ABORT,
   * SUCCEED.  Patterns with EVAL, ASSIGN, CAP_START, CAP_END, EMIT_*,
   * DYNAMIC, TABLE_*, ARRAY_*, GOTO, GOTO_F, LABEL, BAL, REM, BREAKX are
   * NOT eligible. */
  bool search_vm_eligible;
} snobol_search_meta_t;

/* ---------------------------------------------------------------------------
 * Search execution result
 * ---------------------------------------------------------------------------
 */
typedef struct {
  bool success;
  size_t match_start; /**< Byte offset of match start within the original
                         subject */
  size_t match_end;   /**< Byte offset just past the match end in the original
                         subject */
} snobol_search_result_t;

/* ---------------------------------------------------------------------------
 * Search-mode skip/bailout reason codes
 *
 * Used in diagnostics to distinguish expected candidate rejection from
 * unsupported control flow or safety fallbacks.
 * ---------------------------------------------------------------------------
 */
typedef enum {
  SNOBOL_SEARCH_SKIP_NONE = 0, /**< Position not skipped */
  SNOBOL_SEARCH_SKIP_LITERAL =
      1, /**< Skipped by literal prefix scan (memmem) */
  SNOBOL_SEARCH_SKIP_FIRST_BYTE = 2, /**< Skipped by first-byte memchr */
  SNOBOL_SEARCH_SKIP_BITMAP = 3,     /**< Skipped by candidate bitmap scan */
  SNOBOL_SEARCH_SKIP_BREAK_SCAN = 4, /**< Skipped by BREAK/BREAKX bitmap scan */
  SNOBOL_SEARCH_SKIP_SPAN_SCAN = 5,  /**< Skipped by SPAN bitmap scan */
  SNOBOL_SEARCH_SKIP_JIT_COLD =
      6, /**< JIT skip: below search-mode hotness threshold */
  SNOBOL_SEARCH_SKIP_JIT_BUDGET = 7, /**< JIT skip: compile budget exhausted */
  SNOBOL_SEARCH_SKIP_JIT_EXIT_RATE = 8, /**< JIT skip: exit rate exceeded */
  SNOBOL_SEARCH_BAILOUT_UNSUPPORTED =
      9, /**< JIT bailout: unsupported control flow */
  SNOBOL_SEARCH_BAILOUT_SAFETY =
      10, /**< JIT bailout: safety fallback triggered */
  SNOBOL_SEARCH_BAILOUT_CANDIDATE =
      11, /**< JIT bailout: expected candidate rejection */
} snobol_search_skip_reason_t;

/* ---------------------------------------------------------------------------
 * Per-search-invocation diagnostics
 *
 * Populated by snobol_search_exec() when a non-NULL pointer is passed.
 * ---------------------------------------------------------------------------
 */
typedef struct {
  uint64_t candidates_tested;  /**< Positions where the VM was invoked */
  uint64_t candidates_skipped; /**< Positions skipped by candidate metadata */
  uint64_t automaton_tests;    /**< Positions tested by the automaton path */
  uint64_t search_vm_tests;    /**< Positions tested by the search-VM path */
  snobol_search_skip_reason_t last_skip_reason;
} snobol_search_diag_t;

/* ---------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------------
 */

/**
 * Derive search candidate metadata from compiled pattern bytecode.
 *
 * Call once after compilation; store alongside the compiled pattern for fast
 * retrieval on each search invocation.  The function always writes to *out
 * (all fields default to false/0 when the pattern has no analyzable structure).
 *
 * @param bc      Compiled bytecode buffer (read-only)
 * @param bc_len  Bytecode length in bytes
 * @param out     Output metadata struct (caller-allocated)
 */
void snobol_search_derive_meta(const uint8_t *bc, size_t bc_len,
                               snobol_search_meta_t *out);

/**
 * Execute a single search over [subject, subject+subject_len) from
 * start_offset, returning the first non-overlapping match using one native C
 * control loop.
 *
 * The caller must populate vm->bc and vm->bc_len with the match pattern
 * bytecode and may set vm->eval_fn, vm->emit_fn, and related callback fields
 * before calling.  The function manages vm->s, vm->len, vm->pos, and vm->ip
 * internally for each candidate position.
 *
 * After a successful return, vm->var_start, vm->var_end, and vm->var_count
 * reflect the captures from the winning match.
 *
 * @param vm            Partially-initialised VM (bc/bc_len + callbacks set)
 * @param subject       Full subject string pointer
 * @param subject_len   Subject length in bytes
 * @param start_offset  Byte offset to start searching from (0 = beginning)
 * @param meta          Optional pre-derived metadata for acceleration (NULL =
 * derive inline)
 * @param out_result    Output: match position (always written; success=false on
 * no match)
 * @param diag          Optional diagnostics output (may be NULL)
 * @return true if a match was found; false if no match in [start_offset,
 * subject_len]
 */
SNOBOL_NODISCARD bool snobol_search_exec(VM *vm, const char *subject,
                                         size_t subject_len,
                                         size_t start_offset,
                                         const snobol_search_meta_t *meta,
                                         snobol_search_result_t *out_result,
                                         snobol_search_diag_t *diag);
