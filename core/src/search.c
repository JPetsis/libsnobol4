/**
 * @file search.c
 * @brief Core search runtime implementation for libsnobol4
 *
 * Implements:
 *  - snobol_search_derive_meta(): compile-time metadata extraction for search
 *    acceleration (literal prefix, first-byte, candidate bitmap, BREAK/SPAN
 *    classification, automaton eligibility).
 *  - snobol_search_exec(): universal search-from-offset loop that routes
 *    workloads to the appropriate acceleration tier:
 *      Tier 1 – libc-style literal/byte scan (memmem, memchr, bitmap scan)
 *      Tier 2 – BREAK/BREAKX/SPAN accelerated scan using ASCII bitmaps
 *      Tier 3 – lightweight automaton for eligible patterns (optional)
 *      Tier 4 – fallback general VM (via vm_exec)
 */

#include "snobol/search.h"
#include "snobol/snobol_internal.h"
#include "snobol/vm.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Portable memmem — not available in MSVC or some other non-GNU environments.
 * ---------------------------------------------------------------------------
 */
#if defined(_WIN32) || !defined(__GLIBC__)
static const void *snobol_memmem(const void *hay, size_t hlen,
                                 const void *needle, size_t nlen) {
  if (nlen == 0)
    return hay;
  if (hlen < nlen)
    return nullptr;
  const char *h = (const char *)hay;
  const char *n = (const char *)needle;
  size_t limit = hlen - nlen;
  for (size_t i = 0; i <= limit; i++) {
    if (memcmp(h + i, n, nlen) == 0)
      return h + i;
  }
  return nullptr;
}
#define memmem snobol_memmem
#endif

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------------
 */

/** Read a big-endian u32 from bc[ip..ip+3]. */
static inline uint32_t search_read_u32(const uint8_t *bc, size_t ip) {
  return ((uint32_t)bc[ip] << 24) | ((uint32_t)bc[ip + 1] << 16) |
         ((uint32_t)bc[ip + 2] << 8) | (uint32_t)bc[ip + 3];
}

/** Read a big-endian u16 from bc[ip..ip+1]. */
static inline uint16_t search_read_u16(const uint8_t *bc, size_t ip) {
  return ((uint16_t)bc[ip] << 8) | (uint16_t)bc[ip + 1];
}

/* ---------------------------------------------------------------------------
 * Trie data structures for alternation-of-literals matching
 *
 * Uses a compact edge-list representation: each trie node references its
 * first outgoing edge via `first_edge`; edges for the same parent form a
 * singly-linked list via `sibling`.  Memory is pre-allocated on the stack
 * as a fixed pool (no malloc/free in the hot path).
 * ---------------------------------------------------------------------------
 */

/** Sentinel: no edge / no node. */
#define SNOBOL_AUTO_NULL UINT16_MAX

/** Maximum nodes in the automaton trie (stack-allocated pool). */
#define SNOBOL_AUTO_MAX_NODES 256

/** Maximum edges in the automaton trie (stack-allocated pool). */
#define SNOBOL_AUTO_MAX_EDGES 1024

/** A single outgoing edge from a trie node. */
typedef struct {
  uint8_t byte;          /**< Transition byte. */
  uint16_t next;         /**< Target node index (SNOBOL_AUTO_NULL = none). */
  uint16_t sibling;      /**< Next sibling edge index (SNOBOL_AUTO_NULL = end). */
} snobol_auto_edge_t;

/** A single trie node. */
typedef struct {
  uint16_t first_edge;   /**< Index of first outgoing edge (SNOBOL_AUTO_NULL = none). */
  bool is_end;           /**< True when this node terminates a valid pattern. */
} snobol_auto_node_t;

/** Pre-allocated trie storage (stack-friendly, ~7 KB). */
typedef struct {
  snobol_auto_node_t nodes[SNOBOL_AUTO_MAX_NODES];
  snobol_auto_edge_t edges[SNOBOL_AUTO_MAX_EDGES];
  uint16_t node_count;
  uint16_t edge_count;
} snobol_auto_trie_t;

/* ---------------------------------------------------------------------------
 * Trie builder — inserts a single literal string into the trie.
 * Returns false when the node/edge pool is exhausted.
 * ---------------------------------------------------------------------------
 */
static bool trie_insert(snobol_auto_trie_t *t, const uint8_t *s, size_t len) {
  uint16_t n = 0; /* start at root */
  for (size_t i = 0; i < len; i++) {
    uint8_t b = s[i];
    /* Search for existing edge */
    uint16_t e = t->nodes[n].first_edge;
    uint16_t prev = SNOBOL_AUTO_NULL;
    while (e != SNOBOL_AUTO_NULL) {
      if (t->edges[e].byte == b)
        break;
      prev = e;
      e = t->edges[e].sibling;
    }
    if (e != SNOBOL_AUTO_NULL) {
      n = t->edges[e].next;
    } else {
      /* Create new edge + node */
      if (t->edge_count >= SNOBOL_AUTO_MAX_EDGES ||
          t->node_count >= SNOBOL_AUTO_MAX_NODES)
        return false;
      uint16_t ne = t->edge_count++;
      uint16_t nn = t->node_count++;
      t->edges[ne].byte = b;
      t->edges[ne].next = nn;
      t->edges[ne].sibling = SNOBOL_AUTO_NULL;
      t->nodes[nn].first_edge = SNOBOL_AUTO_NULL;
      t->nodes[nn].is_end = false;
      if (prev == SNOBOL_AUTO_NULL) {
        t->nodes[n].first_edge = ne;
      } else {
        t->edges[prev].sibling = ne;
      }
      n = nn;
    }
  }
  t->nodes[n].is_end = true;
  return true;
}

/* ---------------------------------------------------------------------------
 * Trie matcher — scans the subject from `offset`, returning true when the
 * trie matches at that position.  On match, `*match_len` receives the
 * number of bytes consumed.
 * ---------------------------------------------------------------------------
 */
static bool trie_match(const snobol_auto_trie_t *t, const char *subject,
                       size_t subject_len, size_t offset, size_t *match_len) {
  size_t pos = offset;
  uint16_t n = 0; /* start at root */
  while (pos < subject_len) {
    uint8_t b = (uint8_t)subject[pos];
    uint16_t e = t->nodes[n].first_edge;
    while (e != SNOBOL_AUTO_NULL && t->edges[e].byte != b)
      e = t->edges[e].sibling;
    if (e == SNOBOL_AUTO_NULL)
      return false;
    n = t->edges[e].next;
    pos++;
    if (t->nodes[n].is_end) {
      *match_len = pos - offset;
      return true;
    }
  }
  return false;
}

/* ---------------------------------------------------------------------------
 * Trie-based search for alternation-of-literals patterns.
 *
 * Walks the bytecode SPLIT tree to collect literals, builds a trie, then
 * scans the subject for matches.  Returns true and fills out_result on
 * the first match at or after start_offset.
 * ---------------------------------------------------------------------------
 */
static bool search_alt_literals_try(VM *vm, const char *subject,
                                    size_t subject_len, size_t start_offset,
                                    snobol_search_result_t *out_result) {
  /* ---- Build trie from bytecode SPLIT tree ---- */
  snobol_auto_trie_t trie;
  trie.node_count = 1; /* root node (index 0) */
  trie.edge_count = 0;
  trie.nodes[0].first_edge = SNOBOL_AUTO_NULL;
  trie.nodes[0].is_end = false;

  /* Walk SPLIT tree and insert each LIT … ACCEPT branch */
  /* We use a recursive helper on the C stack — depth is bounded by the
   * bytecode length, typically < 10 for alternation patterns. */
  bool build_ok = true;
  size_t bc_len = vm->bc_len;
  const uint8_t *bc = vm->bc;

  /* Inline lambda via nested helper (C23 / GCC extension).  For portability
   * we use a local function pointer trampoline. */
  bool all_ok = true;
  size_t stack[64]; /* explicit stack to avoid recursion */
  int sp = 0;
  stack[sp++] = 0; /* start at ip=0 */

  while (sp > 0 && all_ok) {
    size_t ip = stack[--sp];
    if (ip + 2 > bc_len) { all_ok = false; break; }
    uint8_t op = bc[ip];
    if (op == OP_LIT) {
      /* Leaf: insert literal into trie */
      if (ip + 10 > bc_len) { all_ok = false; break; }
      uint32_t off = search_read_u32(bc, ip + 1);
      uint32_t len = search_read_u32(bc, ip + 5);
      if (off >= bc_len || off + len > bc_len) { all_ok = false; break; }
      if (!trie_insert(&trie, bc + off, len)) { all_ok = false; break; }
    } else if (op == OP_SPLIT) {
      if (ip + 9 > bc_len) { all_ok = false; break; }
      uint32_t a = search_read_u32(bc, ip + 1);
      uint32_t b = search_read_u32(bc, ip + 5);
      if (a >= bc_len || b >= bc_len) { all_ok = false; break; }
      if (sp + 2 > 64) { all_ok = false; break; }
      stack[sp++] = b;
      stack[sp++] = a;
    } else {
      all_ok = false;
      break;
    }
  }

  if (!all_ok)
    return false;

  /* ---- Scan subject from start_offset ---- */
  size_t offset = start_offset;
  while (offset <= subject_len) {
    size_t match_len = 0;
    if (trie_match(&trie, subject, subject_len, offset, &match_len)) {
      out_result->success = true;
      out_result->match_start = offset;
      out_result->match_end = offset + match_len;
      return true;
    }
    if (offset >= subject_len)
      break;
    offset++;
  }
  return false;
}

/* ---------------------------------------------------------------------------
 * Automaton eligibility checker
 *
 * A pattern is automaton-eligible if it:
 *   1. Contains no EVAL, ASSIGN, EMIT_*, DYNAMIC, TABLE_GET/SET, GOTO*, LABEL
 * ops (side-effect-free during matching).
 *   2. Contains no BREAKX, BAL, or DYNAMIC_DEF (structurally local).
 *   3. All character classes reference only ASCII ranges (verified by the
 *      caller using the class bitmap).
 *   4. Total bytecode is short (< 512 bytes excluding charclass table).
 * ---------------------------------------------------------------------------
 */
static bool check_automaton_eligible(const uint8_t *bc, size_t bc_len) {
  if (bc_len > 512)
    return false;
  size_t ip = 0;
  while (ip < bc_len) {
    uint8_t op = bc[ip++];
    switch (op) {
    /* Side-effect ops — disqualify */
    case OP_EVAL:
    case OP_ASSIGN:
    case OP_EMIT_LITERAL:
    case OP_EMIT_CAPTURE:
    case OP_EMIT_EXPR:
    case OP_EMIT_TABLE:
    case OP_EMIT_FORMAT:
    case OP_DYNAMIC:
    case OP_DYNAMIC_DEF:
    case OP_TABLE_GET:
    case OP_TABLE_SET:
    case OP_GOTO:
    case OP_GOTO_F:
    case OP_LABEL:
    case OP_BREAKX: /* extended retry complicates backtracking-free execution */
    case OP_BAL:    /* requires a stack */
      return false;
    /* Simple opcode families — safe */
    case OP_ACCEPT:
    case OP_FAIL:
      return true; /* Short pattern: eligible if we hit terminal */
    case OP_JMP:
      if (ip + 4 > bc_len)
        return false;
      ip += 4;
      break;
    case OP_SPLIT:
      if (ip + 8 > bc_len)
        return false;
      ip += 8;
      break;
    case OP_LIT: {
      if (ip + 8 > bc_len)
        return false;
      /* off(u32) len(u32) */
      uint32_t lit_len = search_read_u32(bc, ip + 4);
      ip += 8 + lit_len;
      break;
    }
    case OP_ANY:
    case OP_NOTANY:
    case OP_SPAN:
    case OP_BREAK:
      if (ip + 2 > bc_len)
        return false;
      ip += 2;
      break;
    case OP_LEN:
    case OP_RPOS:
    case OP_RTAB:
    case OP_POS:
    case OP_TAB:
      if (ip + 4 > bc_len)
        return false;
      ip += 4;
      break;
    case OP_ANCHOR:
    case OP_CAP_START:
    case OP_CAP_END:
    case OP_FENCE:
    case OP_REM:
    case OP_NOP:
    case OP_ABORT:
    case OP_SUCCEED:
      if (ip > bc_len)
        return false;
      break;
    case OP_REPEAT_INIT:
      if (ip + 13 > bc_len)
        return false;
      ip += 13;
      break;
    case OP_REPEAT_STEP:
      if (ip + 5 > bc_len)
        return false;
      ip += 5;
      break;
    default:
      /* Unknown opcode: conservative — not eligible */
      return false;
    }
  }
  return true;
}

/* ---------------------------------------------------------------------------
 * Alternation-of-literals checker
 *
 * Walks the SPLIT tree starting at `ip` and returns true when every leaf
 * branch is OP_LIT(…) followed by OP_ACCEPT — i.e. the pattern is a flat
 * alternation of literal strings (e.g. "abc" | "def" | "ghi").
 *
 * When true, the search runtime can use a trie-based multi-string matcher
 * (Tier 3a) instead of calling vm_exec at each candidate position.
 * ---------------------------------------------------------------------------
 */
static bool check_alt_literals(const uint8_t *bc, size_t bc_len, size_t ip) {
  if (ip + 2 > bc_len)
    return false;
  uint8_t op = bc[ip];

  if (op == OP_LIT) {
    /* Leaf: must be LIT(off, len) followed by ACCEPT.
     * Compiler always emits data inline after the 9-byte header,
     * so ACCEPT lives at ip + 9 + len. */
    if (ip + 10 > bc_len)
      return false;
    uint32_t lit_len = search_read_u32(bc, ip + 5);
    if (ip + 9 + lit_len + 1 > bc_len)
      return false;
    if (bc[ip + 9 + lit_len] != OP_ACCEPT)
      return false;
    return true;
  }

  if (op == OP_SPLIT) {
    /* Branch node: recurse into both arms */
    if (ip + 9 > bc_len)
      return false;
    uint32_t a = search_read_u32(bc, ip + 1);
    uint32_t b = search_read_u32(bc, ip + 5);
    if (a >= bc_len || b >= bc_len)
      return false;
    return check_alt_literals(bc, bc_len, a) &&
           check_alt_literals(bc, bc_len, b);
  }

  return false;
}

/* ---------------------------------------------------------------------------
 * snobol_search_derive_meta
 *
 * Scans the compiled bytecode to extract search-acceleration hints:
 *  - Literal-prefix bytes for memmem/memchr candidate skipping
 *  - First-byte for memchr when prefix length is 1
 *  - ASCII candidate bitmap for alternation-of-single-chars
 *  - BREAK/SPAN/BREAKX root-op classification
 *  - Alternation-of-literals flag for trie matching
 *  - Automaton eligibility for the lightweight path
 *
 * The function is pure (read-only), fast, and language-agnostic.
 * ---------------------------------------------------------------------------
 */
void snobol_search_derive_meta(const uint8_t *bc, size_t bc_len,
                               snobol_search_meta_t *out) {
  /* Zero everything */
  memset(out, 0, sizeof(*out));

  if (!bc || bc_len < 2)
    return;

  /* ---- Skip any leading cap/anchor ops that don't consume input ---- */
  size_t ip = 0;
  /* We peek at the first "real" consuming opcode to classify root behavior. */

  /* -----------------------------------------------------------------------
   * Root-op classification: look for literal prefix, BREAK/SPAN/BREAKX,
   * or single-char alternation at the start of the instruction sequence.
   * ----------------------------------------------------------------------- */
  uint8_t op0 = bc[ip];

  /* OP_LIT: extract literal prefix bytes */
  if (op0 == OP_LIT && ip + 9 <= bc_len) {
    uint32_t lit_off = search_read_u32(bc, ip + 1);
    uint32_t lit_len = search_read_u32(bc, ip + 5);
    if (lit_off < bc_len && lit_off + lit_len <= bc_len && lit_len > 0) {
      size_t prefix_len = lit_len < SNOBOL_SEARCH_MAX_PREFIX
                              ? lit_len
                              : SNOBOL_SEARCH_MAX_PREFIX;
      memcpy(out->literal_prefix, bc + lit_off, prefix_len);
      out->literal_prefix_len = prefix_len;
      out->has_literal_prefix = true;
      out->has_first_byte = true;
      out->first_byte = bc[lit_off];
      out->always_consumes = (lit_len > 0);
      /* Not an empty-match pattern if there's a non-empty literal */
      out->may_match_empty = false;

      /* Compute BMH skip table for the literal prefix */
      if (prefix_len >= 2) {
        memset(out->bmh_skip, (int)prefix_len, sizeof(out->bmh_skip));
        for (size_t i = 0; i < prefix_len - 1; i++) {
          uint8_t b = bc[lit_off + i];
          out->bmh_skip[b] = (uint8_t)(prefix_len - 1 - i);
        }
        out->has_bmh_skip = true;
        out->bmh_skip_len = prefix_len;
      }
    }
  }

  /* OP_BREAK / OP_BREAKX: scan past delimiter set */
  else if ((op0 == OP_BREAK || op0 == OP_BREAKX) && ip + 3 <= bc_len) {
    uint16_t set_id = search_read_u16(bc, ip + 1);
    out->is_break_family = true;
    out->is_breakx = (op0 == OP_BREAKX);
    out->always_consumes = false; /* BREAK can match at end / consume 0 */
    out->may_match_empty = true;

    /* Extract ASCII bitmap for the character class */
    /* We need a minimal VM-like structure to call get_ranges_ptr */
    VM tmp_vm;
    memset(&tmp_vm, 0, sizeof(tmp_vm));
    tmp_vm.bc = bc;
    tmp_vm.bc_len = bc_len;
    uint16_t count = 0, ci = 0;
    const uint8_t *ranges = get_ranges_ptr(&tmp_vm, set_id, &count, &ci);
    if (ranges) {
      out->ascii_class_only =
          ranges_to_ascii_bitmap(ranges, count, out->class_bitmap);
    }
  }

  /* OP_SPAN: consume characters in set */
  else if (op0 == OP_SPAN && ip + 3 <= bc_len) {
    uint16_t set_id = search_read_u16(bc, ip + 1);
    out->is_span_family = true;
    out->always_consumes = true; /* SPAN requires at least 1 char */
    out->may_match_empty = false;

    VM tmp_vm;
    memset(&tmp_vm, 0, sizeof(tmp_vm));
    tmp_vm.bc = bc;
    tmp_vm.bc_len = bc_len;
    uint16_t count = 0, ci = 0;
    const uint8_t *ranges = get_ranges_ptr(&tmp_vm, set_id, &count, &ci);
    if (ranges) {
      out->ascii_class_only =
          ranges_to_ascii_bitmap(ranges, count, out->class_bitmap);
    }
  }

  /* OP_ANY: fused single-char charclass (e.g. from SPLIT→ANY fusion pass).
   *
   * Build the candidate bitmap directly from the charclass ranges so that
   * snobol_search_exec() can route to the fast bitmap-accelerated Tier 3
   * path instead of calling vm_exec at every position (Tier 4/5).
   */
  else if (op0 == OP_ANY && ip + 3 <= bc_len) {
    uint16_t set_id = search_read_u16(bc, ip + 1);
    VM tmp_vm;
    memset(&tmp_vm, 0, sizeof(tmp_vm));
    tmp_vm.bc = bc;
    tmp_vm.bc_len = bc_len;
    uint16_t count = 0, ci = 0;
    const uint8_t *ranges = get_ranges_ptr(&tmp_vm, set_id, &count, &ci);
    if (ranges && count > 0) {
      /* Build 4×u64 ASCII bitmap from the charclass ranges */
      bool ascii_only =
          ranges_to_ascii_bitmap(ranges, count, out->candidate_bitmap);
      if (ascii_only) {
        out->has_candidate_bitmap = true;
        out->is_single_char_alt = true; /* route to bitmap_accelerated */
        out->always_consumes = true;
        out->may_match_empty = false;
      }
    }
  }

  /* OP_SPLIT: try to detect single-char alternation
   *
   * Pattern structure for a two-alternative single-char alternation:
   *   SPLIT a b
   *   LIT off1 1 <byte1>    ← branch a
   *   ACCEPT
   *   LIT off2 1 <byte2>    ← branch b
   *   ACCEPT
   *
   * We walk the SPLIT tree up to SNOBOL_SEARCH_MAX_ALT branches, collecting
   * single-char literals.  If every branch is a 1-byte literal followed by
   * ACCEPT, we classify this as a single_char_alt and build a bitmap.
   */
  else if (op0 == OP_SPLIT && ip + 9 <= bc_len) {
    /* Simple two-branch case first */
    uint32_t branch_a = search_read_u32(bc, ip + 1);
    uint32_t branch_b = search_read_u32(bc, ip + 5);

    bool all_single = true;
    uint8_t alt_bytes[SNOBOL_SEARCH_MAX_ALT];
    size_t alt_count = 0;

/* Helper: check if bc[off..] is LIT(1-byte) ACCEPT */
#define CHECK_SINGLE_LIT(off)                                                  \
  do {                                                                         \
    if ((off) + 10 > bc_len) {                                                 \
      all_single = false;                                                      \
      break;                                                                   \
    }                                                                          \
    if (bc[(off)] != OP_LIT) {                                                 \
      all_single = false;                                                      \
      break;                                                                   \
    }                                                                          \
    uint32_t _off2 = search_read_u32(bc, (off) + 1);                           \
    uint32_t _len = search_read_u32(bc, (off) + 5);                            \
    if (_len != 1) {                                                           \
      all_single = false;                                                      \
      break;                                                                   \
    }                                                                          \
    if (_off2 >= bc_len) {                                                     \
      all_single = false;                                                      \
      break;                                                                   \
    }                                                                          \
    uint8_t _byte = bc[_off2];                                                 \
    if (bc[(off) + 9 + _len] != OP_ACCEPT) {                                   \
      all_single = false;                                                      \
      break;                                                                   \
    }                                                                          \
    if (alt_count < SNOBOL_SEARCH_MAX_ALT) {                                   \
      alt_bytes[alt_count++] = _byte;                                          \
    }                                                                          \
  } while (0)

    CHECK_SINGLE_LIT(branch_a);
    if (all_single)
      CHECK_SINGLE_LIT(branch_b);

#undef CHECK_SINGLE_LIT

    if (all_single && alt_count > 0) {
      out->is_single_char_alt = true;
      out->alt_count = alt_count;
      memcpy(out->alt_bytes, alt_bytes, alt_count);
      /* Build candidate bitmap */
      out->has_candidate_bitmap = true;
      memset(out->candidate_bitmap, 0, sizeof(out->candidate_bitmap));
      for (size_t i = 0; i < alt_count; i++) {
        uint8_t b = alt_bytes[i];
        if (b < 64)
          out->candidate_bitmap[0] |= (1ULL << b);
        else if (b < 128)
          out->candidate_bitmap[1] |= (1ULL << (b - 64));
      }
      out->has_first_byte = (alt_count == 1);
      if (out->has_first_byte)
        out->first_byte = alt_bytes[0];
      out->always_consumes = true;
      out->may_match_empty = false;
    }
  }

  /* ---- Alternation-of-literals detection ---- */
  /* Walk the SPLIT tree: every branch must be LIT(…) ACCEPT. */
  if (op0 == OP_SPLIT && bc_len >= 10) {
    size_t bc_remain = bc_len > 512 ? 512 : bc_len;
    out->is_alt_literals =
        check_alt_literals(bc, bc_remain, 0);
  }

  /* ---- Automaton eligibility ---- */
  out->automaton_eligible = check_automaton_eligible(bc, bc_len);
  /* Further restrict: must also be ASCII-only for classes */
  if (out->automaton_eligible &&
      (out->is_break_family || out->is_span_family) && !out->ascii_class_only) {
    out->automaton_eligible = false;
  }
}

/* ---------------------------------------------------------------------------
 * Internal: reset a VM for a fresh match attempt at the given position
 * within [subject, subject+subject_len).
 * ---------------------------------------------------------------------------
 */
static inline void search_reset_vm(VM *vm, const char *subject,
                                   size_t subject_len, size_t offset) {
  vm->s = subject + offset;
  vm->len = subject_len - offset;
  vm->ip = 0;
  vm->pos = 0;
  /* Clear capture state */
  vm->var_count = 0;
  vm->max_cap_used = 0;
  vm->max_counter_used = 0;
  /* Reset choice stack */
  vm->choices_top = 0;
}


/* ---------------------------------------------------------------------------
 * Tier 2: BREAK/BREAKX accelerated search
 *
 * For a BREAK-family pattern with a pure-ASCII character class bitmap, we
 * scan forward with the bitmap until we find the first delimiter byte, then
 * confirm with the VM.  This avoids invoking the VM at every non-delimiter
 * byte.
 * ---------------------------------------------------------------------------
 */
static bool search_break_accelerated(VM *vm, const char *subject,
                                     size_t subject_len, size_t start_offset,
                                     const snobol_search_meta_t *meta,
                                     snobol_search_result_t *out_result,
                                     snobol_search_diag_t *diag) {
  const uint64_t *bmap = meta->class_bitmap;
  size_t offset = start_offset;

  while (offset <= subject_len) {
    /* Advance until we find the first delimiter byte − or end of subject */
    size_t scan = offset;
    while (scan < subject_len) {
      uint8_t c = (uint8_t)subject[scan];
      if (c > 127 || bitmap_test(bmap, c))
        break;
      scan++;
    }

    if (diag) {
      diag->candidates_skipped += (scan - offset);
    }

    /* Try the VM from `scan` (where BREAK would produce a 0-length match
     * consuming 0 bytes up to the delimiter).  For BREAKX the VM needs to
     * confirm the retry semantics. */
    search_reset_vm(vm, subject, subject_len, scan);
    if (diag)
      diag->candidates_tested++;

    bool ok = vm_exec(vm);
    if (ok) {
      out_result->success = true;
      out_result->match_start = scan;
      out_result->match_end = scan + vm->pos;
      return true;
    }

    /* Advance past current position */
    if (scan >= subject_len)
      break;
    offset = scan + 1;
  }

  out_result->success = false;
  return false;
}

/* ---------------------------------------------------------------------------
 * Tier 2: SPAN accelerated search
 *
 * For a SPAN pattern with a pure-ASCII class, we look for the next position
 * where the SPAN character set begins.  We use a simple forward scan with
 * the bitmap.
 * ---------------------------------------------------------------------------
 */
static bool search_span_accelerated(VM *vm, const char *subject,
                                    size_t subject_len, size_t start_offset,
                                    const snobol_search_meta_t *meta,
                                    snobol_search_result_t *out_result,
                                    snobol_search_diag_t *diag) {
  const uint64_t *bmap = meta->class_bitmap;
  size_t offset = start_offset;

  while (offset < subject_len) {
    /* Skip positions where subject[offset] is not in the SPAN set */
    uint8_t c = (uint8_t)subject[offset];
    if (c > 127 || !bitmap_test(bmap, c)) {
      if (diag)
        diag->candidates_skipped++;
      offset++;
      continue;
    }

    /* Likely start of a SPAN match — verify with VM */
    search_reset_vm(vm, subject, subject_len, offset);
    if (diag)
      diag->candidates_tested++;

    bool ok = vm_exec(vm);
    if (ok) {
      out_result->success = true;
      out_result->match_start = offset;
      out_result->match_end = offset + vm->pos;
      return true;
    }
    offset++;
  }

  out_result->success = false;
  return false;
}

/* ---------------------------------------------------------------------------
 * Tier 1: Literal prefix accelerated search
 *
 * Uses memmem (or memchr for single-byte) to jump directly to the next
 * candidate position.  Falls back to the VM for confirmation (in case the
 * literal is only a prefix and the pattern has additional constraints).
 * ---------------------------------------------------------------------------
 */
static bool search_literal_accelerated(VM *vm, const char *subject,
                                       size_t subject_len, size_t start_offset,
                                       const snobol_search_meta_t *meta,
                                       snobol_search_result_t *out_result,
                                       snobol_search_diag_t *diag) {
  size_t offset = start_offset;

  while (offset <= subject_len) {
    const char *hay = subject + offset;
    size_t haylen = subject_len - offset;

    /* Find next candidate position */
    const char *found = nullptr;
    size_t skipped = 0;

    if (meta->literal_prefix_len == 1) {
      /* Single-byte prefix: use memchr */
      found = (const char *)memchr(hay, meta->literal_prefix[0], haylen);
    } else if (meta->literal_prefix_len > 1) {
      /* Multi-byte prefix: use memmem */
      found = (const char *)memmem(hay, haylen, meta->literal_prefix,
                                   meta->literal_prefix_len);
    } else {
      found = hay; /* No prefix hint — try every position */
    }

    if (!found) {
      /* No candidate in remaining subject */
      if (diag)
        diag->candidates_skipped += haylen;
      break;
    }

    size_t cand = (size_t)(found - subject);
    skipped = cand - offset;
    if (diag) {
      diag->candidates_skipped += skipped;
      diag->candidates_tested++;
    }

    /* Confirm with VM at candidate position */
    search_reset_vm(vm, subject, subject_len, cand);
    bool ok = vm_exec(vm);
    if (ok) {
      out_result->success = true;
      out_result->match_start = cand;
      out_result->match_end = cand + vm->pos;
      return true;
    }

    /* Not a real match — advance one byte past this candidate */
    offset = cand + 1;
  }

  out_result->success = false;
  return false;
}

/* ---------------------------------------------------------------------------
 * Tier 1: Candidate-bitmap accelerated search
 *
 * Scans forward skipping bytes whose bit is not set in the candidate bitmap.
 * Invokes the VM only at positions where subject[pos] IS in the candidate set.
 * ---------------------------------------------------------------------------
 */
static bool search_bitmap_accelerated(VM *vm, const char *subject,
                                      size_t subject_len, size_t start_offset,
                                      const snobol_search_meta_t *meta,
                                      snobol_search_result_t *out_result,
                                      snobol_search_diag_t *diag) {
  const uint64_t *bmap = meta->candidate_bitmap;
  size_t offset = start_offset;

  while (offset < subject_len) {
    uint8_t c = (uint8_t)subject[offset];
    if (c > 127 || !bitmap_test(bmap, c)) {
      if (diag)
        diag->candidates_skipped++;
      offset++;
      continue;
    }

    if (diag)
      diag->candidates_tested++;
    search_reset_vm(vm, subject, subject_len, offset);
    bool ok = vm_exec(vm);
    if (ok) {
      out_result->success = true;
      out_result->match_start = offset;
      out_result->match_end = offset + vm->pos;
      return true;
    }
    offset++;
  }

  out_result->success = false;
  return false;
}

/* ---------------------------------------------------------------------------
 * Tier 3: Lightweight automaton path
 *
 * For automaton-eligible patterns (no side effects, no complex control flow,
 * ASCII-only), we use a simplified execution that avoids choice-stack overhead
 * for the common case.  Falls back to the general VM on any branching op
 * (SPLIT).
 *
 * The automaton runs from `offset` and returns true when the pattern matches.
 * Captures are populated in the VM state on success.
 * ---------------------------------------------------------------------------
 */
static bool search_automaton_try(VM *vm, const char *subject,
                                 size_t subject_len, size_t offset,
                                 snobol_search_result_t *out_result) {
  /* Reuse the general vm_exec for now; the automaton_eligible flag acts
   * as a routing hint for future optimisation.  The VM is correct for
   * all eligible patterns.  The eligibility classification ensures we
   * never route EVAL/DYNAMIC/table ops through this path.
   *
   * Future: replace with a dedicated NFA or backtracking-free interpreter. */
  search_reset_vm(vm, subject, subject_len, offset);
  bool ok = vm_exec(vm);
  if (ok) {
    out_result->success = true;
    out_result->match_start = offset;
    out_result->match_end = offset + vm->pos;
  } else {
    out_result->success = false;
  }
  return ok;
}

/* ---------------------------------------------------------------------------
 * snobol_search_exec
 *
 * Unified search entrypoint.  Dispatches to the most specific tier available
 * using the pre-derived (or inline-derived) metadata, then falls back to the
 * general VM.
 *
 * Tier routing (highest priority first):
 *   1. BREAK/BREAKX with ASCII bitmap  → break_accelerated
 *   2. SPAN with ASCII bitmap           → span_accelerated
 *   3. Literal prefix (>=1 byte)        → literal_accelerated
 *   4. Single-char alt (candidate set)  → bitmap_accelerated
 *   5. Automaton-eligible               → automaton path (general VM + eligible
 * flag)
 *   6. General VM fallback
 *
 * Semantics are identical to repeated anchored vm_exec calls in the caller:
 * the first non-overlapping match at or after start_offset is returned.
 *
 * Per-candidate VM reset policy (FIELDS-ONLY):
 * This function does NOT call memset(VM, 0, sizeof(VM)) per candidate. The
 * per-candidate reset is done by search_reset_vm() (above) which updates
 * only the fields that change between candidates:
 *   vm->s, vm->len, vm->ip, vm->pos, vm->var_count, vm->max_cap_used,
 *   vm->max_counter_used, vm->choices_top
 *
 * Callers that loop snobol_search_exec() (e.g. snobol_pattern_search_ex,
 * PHP Pattern::searchSplit) MUST initialise the VM struct once (setting
 * vm->bc, vm->bc_len, vm->out) and then call this function
 * repeatedly without re-memset'ing the VM. snobol_pattern_search_ex()
 * in core/src/api.c is the reference implementation.
 * ---------------------------------------------------------------------------
 */
bool snobol_search_exec(VM *vm, const char *subject, size_t subject_len,
                        size_t start_offset, const snobol_search_meta_t *meta,
                        snobol_search_result_t *out_result,
                        snobol_search_diag_t *diag) {
  if (diag)
    memset(diag, 0, sizeof(*diag));
  if (out_result)
    out_result->success = false;
  if (!vm || !subject || !out_result)
    return false;
  if (start_offset > subject_len)
    return false;

  /* Derive metadata inline if not provided */
  snobol_search_meta_t local_meta;
  if (!meta) {
    snobol_search_derive_meta(vm->bc, vm->bc_len, &local_meta);
    meta = &local_meta;
  }

/* ---- Tier 1a: BREAK / BREAKX with ASCII bitmap ---- */
  if (meta->is_break_family && meta->ascii_class_only) {
    if (diag)
      diag->last_skip_reason = SNOBOL_SEARCH_SKIP_BREAK_SCAN;
    return search_break_accelerated(vm, subject, subject_len, start_offset,
                                    meta, out_result, diag);
  }

  /* ---- Tier 1b: SPAN with ASCII bitmap ---- */
  if (meta->is_span_family && meta->ascii_class_only) {
    if (diag)
      diag->last_skip_reason = SNOBOL_SEARCH_SKIP_SPAN_SCAN;
    return search_span_accelerated(vm, subject, subject_len, start_offset, meta,
                                   out_result, diag);
  }

  /* ---- Tier 2: Literal prefix (memmem / memchr) ---- */
  if (meta->has_literal_prefix && meta->literal_prefix_len > 0) {
    if (diag) {
      diag->last_skip_reason = (meta->literal_prefix_len == 1)
                                   ? SNOBOL_SEARCH_SKIP_FIRST_BYTE
                                   : SNOBOL_SEARCH_SKIP_LITERAL;
    }
    return search_literal_accelerated(vm, subject, subject_len, start_offset,
                                      meta, out_result, diag);
  }

  /* ---- Tier 3: Candidate-set bitmap for single-char alternation ---- */
  if (meta->has_candidate_bitmap && meta->is_single_char_alt) {
    if (diag)
      diag->last_skip_reason = SNOBOL_SEARCH_SKIP_BITMAP;
    return search_bitmap_accelerated(vm, subject, subject_len, start_offset,
                                     meta, out_result, diag);
  }

  /* ---- Tier 3a: Alternation-of-literals (trie-based) ---- */
  if (meta->is_alt_literals) {
    /* The trie matcher builds a trie from the bytecode, then scans the
     * subject linearly without invoking vm_exec.  Faster than Tier 4 for
     * alternation-of-literals patterns (e.g. "abc"|"def"|"ghi"). */
    if (diag)
      diag->last_skip_reason = SNOBOL_SEARCH_SKIP_NONE;
    return search_alt_literals_try(vm, subject, subject_len, start_offset,
                                   out_result);
  }

  /* ---- Tier 4: Automaton path for eligible patterns ---- */
  if (meta->automaton_eligible) {
    size_t offset = start_offset;
    while (offset <= subject_len) {
      if (diag)
        diag->candidates_tested++;
      if (diag)
        diag->automaton_tests++;
      bool ok =
          search_automaton_try(vm, subject, subject_len, offset, out_result);
      if (ok)
        return true;
      /* Advance: use BMH skip when available, else +1 */
      if (offset >= subject_len)
        break;
      if (meta->has_bmh_skip &&
          offset + meta->bmh_skip_len <= subject_len) {
        size_t adv =
            meta->bmh_skip[(unsigned char)subject[offset + meta->bmh_skip_len - 1]];
        offset += adv > 0 ? adv : 1;
      } else {
        offset++;
      }
    }
    out_result->success = false;
    return false;
  }

  /* ---- Tier 5: General VM fallback ---- */
  size_t offset = start_offset;
  while (offset <= subject_len) {
    if (diag)
      diag->candidates_tested++;
    search_reset_vm(vm, subject, subject_len, offset);
    bool ok = vm_exec(vm);
    if (ok) {
      out_result->success = true;
      out_result->match_start = offset;
      out_result->match_end = offset + vm->pos;
      return true;
    }
    if (offset >= subject_len)
      break;
    if (meta->has_bmh_skip &&
        offset + meta->bmh_skip_len <= subject_len) {
      size_t adv =
          meta->bmh_skip[(unsigned char)subject[offset + meta->bmh_skip_len - 1]];
      offset += adv > 0 ? adv : 1;
    } else {
      offset++;
    }
  }

  out_result->success = false;
  return false;
}
