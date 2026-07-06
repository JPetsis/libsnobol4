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
#include "snobol/snobol.h"
#include "snobol/snobol_internal.h"
#include "snobol/vm.h"

/* ---- Internal pattern accessors (implemented in api.c) ---- */
extern snobol_dfa_t *snobol_pattern_get_automaton(
    const snobol_pattern_t *pattern);
extern void snobol_pattern_set_automaton(snobol_pattern_t *pattern,
                                          snobol_dfa_t *dfa);
extern const snobol_search_meta_t *snobol_pattern_get_meta(
    const snobol_pattern_t *pattern);

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
 * Start-byte bitmap helpers
 *
 * A 256-bit bitmap: bit i set → pattern CAN match starting with byte i.
 * Inspired by PCRE2's start_bits study data.
 * ---------------------------------------------------------------------------
 */

static inline void bitmap256_set(uint8_t bm[32], uint8_t b) {
  bm[b >> 3] |= (uint8_t)(1u << (b & 7));
}
static inline void bitmap256_set_all(uint8_t bm[32]) {
  memset(bm, 0xFF, 32);
}
static inline void bitmap256_or(uint8_t dst[32], const uint8_t src[32]) {
  for (int i = 0; i < 32; i++)
    dst[i] |= src[i];
}

/**
 * Recursively compute start-byte bitmap for bytecode rooted at `ip`.
 *
 * Walks forward through sequential ops; for SPLIT (alternation) recurses
 * into both branches and OR's their bitmaps.  Depth-limited to prevent
 * infinite loops from back-edges.
 *
 * @param bc       Bytecode buffer
 * @param bc_len   Bytecode length
 * @param ip       Current bytecode offset (pointing to an opcode)
 * @param bm       Output bitmap (caller must zero before first call)
 * @param depth    Recursion depth guard (pass 0)
 * @param tmp_vm   Temporary VM for charclass resolution (zeroed by caller)
 */
static void compute_start_bitmap(const uint8_t *bc, size_t bc_len, size_t ip,
                                  uint8_t bm[32], int depth, VM *tmp_vm) {
  if (depth > 12) { bitmap256_set_all(bm); return; }

  while (ip < bc_len) {
    uint8_t op = bc[ip]; /* ip points to opcode */
    switch (op) {

    /* ---- Terminal consuming ops: determine start bytes and stop ---- */
    case OP_LIT: {
      if (ip + 9 > bc_len) { bitmap256_set_all(bm); return; }
      uint32_t lit_off = search_read_u32(bc, ip + 1);
      if (lit_off >= bc_len) { bitmap256_set_all(bm); return; }
      bitmap256_set(bm, bc[lit_off]);
      return;
    }

    case OP_ANY: {
      if (ip + 3 > bc_len) { bitmap256_set_all(bm); return; }
      uint16_t set_id = search_read_u16(bc, ip + 1);
      uint16_t count = 0, ci = 0;
      const uint8_t *rng = get_ranges_ptr(tmp_vm, set_id, &count, &ci);
      if (rng) {
        uint64_t abm[2] = {0, 0};
        ranges_to_ascii_bitmap(rng, count, abm);
        for (int i = 0; i < 256; i++)
          if (bitmap_test(abm, (unsigned)i))
            bitmap256_set(bm, (uint8_t)i);
      } else {
        bitmap256_set_all(bm);
      }
      return;
    }

    case OP_NOTANY: {
      if (ip + 3 > bc_len) { bitmap256_set_all(bm); return; }
      uint16_t set_id = search_read_u16(bc, ip + 1);
      uint16_t count = 0, ci = 0;
      const uint8_t *rng = get_ranges_ptr(tmp_vm, set_id, &count, &ci);
      if (rng) {
        uint64_t abm[2] = {0, 0};
        ranges_to_ascii_bitmap(rng, count, abm);
        for (int i = 0; i < 256; i++)
          if (!bitmap_test(abm, (unsigned)i))
            bitmap256_set(bm, (uint8_t)i);
      } else {
        bitmap256_set_all(bm);
      }
      return;
    }

    case OP_SPAN: {
      if (ip + 3 > bc_len) { bitmap256_set_all(bm); return; }
      uint16_t set_id = search_read_u16(bc, ip + 1);
      uint16_t count = 0, ci = 0;
      const uint8_t *rng = get_ranges_ptr(tmp_vm, set_id, &count, &ci);
      if (rng) {
        uint64_t abm[2] = {0, 0};
        ranges_to_ascii_bitmap(rng, count, abm);
        for (int i = 0; i < 256; i++)
          if (bitmap_test(abm, (unsigned)i))
            bitmap256_set(bm, (uint8_t)i);
      } else {
        bitmap256_set_all(bm);
      }
      return;
    }

    case OP_BREAK:
    case OP_BREAKX:
      /* BREAK can succeed at ANY position (zero-width at a delimiter or
       * at end-of-subject).  Conservatively allow all bytes. */
      bitmap256_set_all(bm);
      return;

    case OP_LEN:
    case OP_REM:
    case OP_BAL:
      /* LEN: any byte (consumes n arbitrary codepoints).
       * REM: any byte (consumes remainder).
       * BAL: any byte (balanced pair can start with any char). */
      bitmap256_set_all(bm);
      return;

    /* ---- Alternation: union of branch start bytes ---- */
    case OP_SPLIT: {
      if (ip + 9 > bc_len) { bitmap256_set_all(bm); return; }
      uint32_t a = search_read_u32(bc, ip + 1);
      uint32_t b = search_read_u32(bc, ip + 5);
      if (a >= bc_len || b >= bc_len) { bitmap256_set_all(bm); return; }
      uint8_t ba[32], bb[32];
      memset(ba, 0, sizeof(ba));
      memset(bb, 0, sizeof(bb));
      compute_start_bitmap(bc, bc_len, (size_t)a, ba, depth + 1, tmp_vm);
      compute_start_bitmap(bc, bc_len, (size_t)b, bb, depth + 1, tmp_vm);
      bitmap256_or(bm, ba);
      bitmap256_or(bm, bb);
      return;
    }

    /* ---- Zero-width wrappers: skip to next instruction ---- */
    case OP_CAP_START:
    case OP_CAP_END:
      ip += 2; /* opcode + reg byte */
      continue;
    case OP_FENCE:
    case OP_NOP:
      ip += 1;
      continue;
    case OP_ANCHOR:
      ip += 2; /* opcode + type byte */
      continue;
    case OP_ASSIGN:
      if (ip + 4 > bc_len) { bitmap256_set_all(bm); return; }
      ip += 4;
      continue;

    case OP_POS:
    case OP_RPOS:
    case OP_TAB:
    case OP_RTAB:
      if (ip + 5 > bc_len) { bitmap256_set_all(bm); return; }
      ip += 5;
      continue;

    /* ---- Unconditional jump: follow target ---- */
    case OP_JMP: {
      if (ip + 5 > bc_len) { bitmap256_set_all(bm); return; }
      uint32_t target = search_read_u32(bc, ip + 1);
      if (target >= bc_len) { bitmap256_set_all(bm); return; }
      ip = (size_t)target;
      continue;
    }

    /* ---- Unpredictable ops: set all bytes ---- */
    case OP_REPEAT_INIT:
    case OP_REPEAT_STEP:
    case OP_EVAL:
    case OP_DYNAMIC:
    case OP_DYNAMIC_DEF:
    case OP_TABLE_GET:
    case OP_TABLE_SET:
    case OP_ARRAY_GET:
    case OP_ARRAY_SET:
    case OP_EMIT_LITERAL:
    case OP_EMIT_CAPTURE:
    case OP_EMIT_EXPR:
    case OP_EMIT_TABLE:
    case OP_EMIT_FORMAT:
    case OP_GOTO:
    case OP_GOTO_F:
    case OP_LABEL:
      bitmap256_set_all(bm);
      return;

    /* ---- Terminals: no start bytes contributed ---- */
    case OP_ACCEPT:
    case OP_FAIL:
    case OP_ABORT:
    case OP_SUCCEED:
      return;

    default:
      bitmap256_set_all(bm);
      return;
    }
  }
}

/* ---------------------------------------------------------------------------
 * Minimum match length computation (PCRE2-style find_minlength)
 *
 * Walk bytecode from `ip` and compute a lower bound on subject bytes consumed
 * by a successful match.  Returns 0 when the bound is unknown (the skip loop
 * simply skips the minlength check).
 *
 * For alternation (SPLIT) we take the MIN of branches; FAIL branches are
 * treated as infinite (UINT32_MAX) so they don't affect the min.
 * Depth-limited for safety.
 * ---------------------------------------------------------------------------
 */

static uint32_t compute_minlength(const uint8_t *bc, size_t bc_len,
                                   size_t ip, int depth) {
  if (depth > 12)
    return 0;

  uint32_t len = 0;

  while (ip < bc_len) {
    uint8_t op = bc[ip]; /* ip points to opcode */
    switch (op) {

    case OP_LIT: {
      if (ip + 9 > bc_len) return 0;
      uint32_t lit_len = search_read_u32(bc, ip + 5);
      len += lit_len;
      ip += 9 + lit_len;
      continue;
    }

    case OP_ANY:
    case OP_NOTANY:
      len += 1;
      if (ip + 3 > bc_len) return len;
      ip += 3;
      continue;

    case OP_SPAN:
      len += 1;
      if (ip + 3 > bc_len) return len;
      ip += 3;
      continue;

    case OP_BREAK:
    case OP_BREAKX:
      /* BREAK can consume 0 bytes (match at delimiter or end) */
      ip += 3;
      continue;

    case OP_LEN: {
      if (ip + 5 > bc_len) return len;
      uint32_t n = search_read_u32(bc, ip + 1);
      len += n;
      ip += 5;
      continue;
    }

    case OP_POS:
    case OP_RPOS:
    case OP_TAB:
    case OP_RTAB:
      if (ip + 5 > bc_len) return len;
      ip += 5;
      continue;

    case OP_ANCHOR:
      ip += 2;
      continue;

    case OP_CAP_START:
    case OP_CAP_END:
      ip += 2;
      continue;

    case OP_FENCE:
    case OP_NOP:
      ip += 1;
      continue;

    case OP_ASSIGN:
      if (ip + 4 > bc_len) return len;
      ip += 4;
      continue;

    case OP_SPLIT: {
      if (ip + 9 > bc_len) return len;
      uint32_t a = search_read_u32(bc, ip + 1);
      uint32_t b = search_read_u32(bc, ip + 5);
      if (a >= bc_len || b >= bc_len) return len;
      uint32_t ma = compute_minlength(bc, bc_len, (size_t)a, depth + 1);
      uint32_t mb = compute_minlength(bc, bc_len, (size_t)b, depth + 1);
      /* Treat FAIL (UINT32_MAX) branches as infinite */
      uint32_t branch_min;
      if (ma != UINT32_MAX && mb != UINT32_MAX)
        branch_min = ma < mb ? ma : mb;
      else if (ma != UINT32_MAX)
        branch_min = ma;
      else if (mb != UINT32_MAX)
        branch_min = mb;
      else
        branch_min = 0;
      len += branch_min;
      return len;
    }

    case OP_JMP: {
      if (ip + 5 > bc_len) return len;
      uint32_t target = search_read_u32(bc, ip + 1);
      if (target >= bc_len) return len;
      ip = (size_t)target;
      continue;
    }

    case OP_REPEAT_INIT:
      /* Lower bound = inner_min * min_rep.  We can't determine inner min
       * easily, so return 0 (conservative). */
      return 0;

    case OP_REPEAT_STEP:
      return len;

    case OP_BAL:
      return len + 2;


    case OP_REM:
      return len;

    case OP_ACCEPT:
    case OP_ABORT:
    case OP_SUCCEED:
      return len;

    case OP_FAIL:
      /* Dead path — return sentinel so SPLIT ignores this branch */
      return UINT32_MAX;

    case OP_EVAL:
    case OP_DYNAMIC:
    case OP_DYNAMIC_DEF:
    case OP_TABLE_GET:
    case OP_TABLE_SET:
    case OP_ARRAY_GET:
    case OP_ARRAY_SET:
    case OP_EMIT_LITERAL:
    case OP_EMIT_CAPTURE:
    case OP_EMIT_EXPR:
    case OP_EMIT_TABLE:
    case OP_EMIT_FORMAT:
    case OP_GOTO:
    case OP_GOTO_F:
    case OP_LABEL:
      return 0;

    default:
      return 0;
    }
  }

  return len;
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
      if (ip + 4 > bc_len)
        return false;
      ip += 4;
      break;
    /* Position-dependent zero-width ops: excluded from DFA because they
     * require runtime position constraint checking.  The search-VM
     * handles them correctly. */
    case OP_POS:
    case OP_RPOS:
    case OP_TAB:
    case OP_RTAB:
      return false;
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
 * Literal-only checker
 *
 * Returns true when the bytecode represents a single literal match with no
 * side effects, captures, or output — i.e. the pattern is equivalent to
 * a plain substring search.  Zero-width assertions (NOP, FENCE, ANCHOR,
 * POS, RPOS) are allowed between the LIT and ACCEPT.
 *
 * When true, the search runtime can skip the VM entirely and use memmem()
 * (unanchored) or memcmp() (anchored) to find matches.
 * ---------------------------------------------------------------------------
 */
static bool check_literal_only(const uint8_t *bc, size_t bc_len) {
  if (!bc || bc_len < 2)
    return false;
  size_t ip = 0;

  /* Skip leading zero-width ops */
  while (ip < bc_len) {
    uint8_t op = bc[ip];
    if (op == OP_NOP) {
      ip++;
      continue;
    }
    if (op == OP_FENCE || op == OP_ANCHOR) {
      ip++;
      continue;
    }
    if (op == OP_POS || op == OP_RPOS) {
      if (ip + 5 > bc_len)
        return false;
      ip += 5;
      continue;
    }
    break;
  }

  /* Must start with LIT */
  if (ip + 9 > bc_len)
    return false;
  if (bc[ip] != OP_LIT)
    return false;
  uint32_t lit_off = search_read_u32(bc, ip + 1);
  uint32_t lit_len = search_read_u32(bc, ip + 5);
  ip += 9;
  if (lit_off >= bc_len || lit_off + lit_len > bc_len || lit_len == 0)
    return false;
  ip += lit_len;

  /* Skip trailing zero-width ops */
  while (ip < bc_len) {
    uint8_t op = bc[ip];
    if (op == OP_NOP) {
      ip++;
      continue;
    }
    if (op == OP_FENCE || op == OP_ANCHOR) {
      ip++;
      continue;
    }
    if (op == OP_POS || op == OP_RPOS) {
      if (ip + 5 > bc_len)
        return false;
      ip += 5;
      continue;
    }
    break;
  }

  /* Must end with ACCEPT (possibly OP_ACCEPT = 0) */
  if (ip >= bc_len)
    return false;
  return bc[ip] == OP_ACCEPT;
}

/* ---------------------------------------------------------------------------
 * Search-VM eligibility checker
 *
 * Returns true when the pattern contains only opcodes from the safe set
 * supported by the lightweight search-VM.  This includes all pattern-match
 * primitives (LIT, LEN, ANY, NOTANY, SPAN, BREAK, POS, RPOS, TAB, RTAB,
 * ANCHOR, FENCE, NOP), control flow (JMP, SPLIT, REPEAT_INIT, REPEAT_STEP,
 * ACCEPT, FAIL, ABORT, SUCCEED), but EXCLUDES side-effect ops (EVAL, ASSIGN,
 * CAP_START, CAP_END, EMIT_*), dynamic ops (DYNAMIC, DYNAMIC_DEF), table/array
 * ops, GOTO/GOTO_F/LABEL, BAL, REM, and BREAKX.
 *
 * Patterns that pass are eligible for the lightweight search-VM path
 * (Tier 6) which drops output buffer, capture tracking, and var tracking.
 * ---------------------------------------------------------------------------
 */
static bool check_search_vm_eligible(const uint8_t *bc, size_t bc_len) {
  if (!bc || bc_len < 2)
    return false;
  size_t ip = 0;
  while (ip < bc_len) {
    uint8_t op = bc[ip++];
    switch (op) {
    /* Side-effect / complex ops — disqualify */
    case OP_EVAL:
    case OP_ASSIGN:
    case OP_CAP_START:
    case OP_CAP_END:
    case OP_EMIT_LITERAL:
    case OP_EMIT_CAPTURE:
    case OP_EMIT_EXPR:
    case OP_EMIT_TABLE:
    case OP_EMIT_FORMAT:
    case OP_DYNAMIC:
    case OP_DYNAMIC_DEF:
    case OP_TABLE_GET:
    case OP_TABLE_SET:
    case OP_ARRAY_GET:
    case OP_ARRAY_SET:
    case OP_GOTO:
    case OP_GOTO_F:
    case OP_LABEL:
    case OP_BREAKX:
    case OP_BAL:
    case OP_REM:
      return false;
    /* Terminal — immediately eligible for this sub-pattern */
    case OP_ACCEPT:
    case OP_FAIL:
    case OP_ABORT:
    case OP_SUCCEED:
      return true;
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
    case OP_FENCE:
    case OP_NOP:
      /* zero-width, no operands beyond opcode */
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
      return false;
    }
  }
  return true;
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
        out->bmh_skip = (uint8_t *)snobol_malloc(256);
        if (out->bmh_skip) {
          memset(out->bmh_skip, (int)prefix_len, 256);
          for (size_t i = 0; i < prefix_len - 1; i++) {
            uint8_t b = bc[lit_off + i];
            out->bmh_skip[b] = (uint8_t)(prefix_len - 1 - i);
          }
          out->has_bmh_skip = true;
          out->bmh_skip_len = prefix_len;
        }
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

  /* ---- Literal-only detection ---- */
  out->is_literal_only = check_literal_only(bc, bc_len);

  /* ---- Search-VM eligibility ---- */
  out->search_vm_eligible = check_search_vm_eligible(bc, bc_len);

  /* ---- Start-byte bitmap & minimum match length ---- */
  {
    VM bm_vm;
    memset(&bm_vm, 0, sizeof(bm_vm));
    bm_vm.bc = bc;
    bm_vm.bc_len = bc_len;
    memset(out->start_bitmap, 0, sizeof(out->start_bitmap));
    compute_start_bitmap(bc, bc_len, 0, out->start_bitmap, 0, &bm_vm);
    out->has_start_bitmap = true;
  }
  out->minlength = compute_minlength(bc, bc_len, 0, 0);

  /* ---- Pack boolean flags into flags bitfield ---- */
  out->flags = 0;
  if (out->has_literal_prefix)    out->flags |= META_HAS_LITERAL_PREFIX;
  if (out->has_first_byte)        out->flags |= META_HAS_FIRST_BYTE;
  if (out->has_candidate_bitmap)  out->flags |= META_HAS_CANDIDATE_BITMAP;
  if (out->may_match_empty)       out->flags |= META_MAY_MATCH_EMPTY;
  if (out->always_consumes)       out->flags |= META_ALWAYS_CONSUMES;
  if (out->is_single_char_alt)    out->flags |= META_IS_SINGLE_CHAR_ALT;
  if (out->is_break_family)       out->flags |= META_IS_BREAK_FAMILY;
  if (out->is_span_family)        out->flags |= META_IS_SPAN_FAMILY;
  if (out->is_breakx)             out->flags |= META_IS_BREAKX;
  if (out->ascii_class_only)      out->flags |= META_ASCII_CLASS_ONLY;
  if (out->has_start_bitmap)      out->flags |= META_HAS_START_BITMAP;
  if (out->automaton_eligible)    out->flags |= META_AUTOMATON_ELIGIBLE;
  if (out->is_alt_literals)       out->flags |= META_IS_ALT_LITERALS;
  if (out->has_bmh_skip)          out->flags |= META_HAS_BMH_SKIP;
  if (out->is_literal_only)       out->flags |= META_IS_LITERAL_ONLY;
  if (out->search_vm_eligible)    out->flags |= META_SEARCH_VM_ELIGIBLE;

  /* ---- Compute tier index from flags ---- */
  if (out->is_break_family && out->ascii_class_only)
    out->tier = TIER_BREAK_SCAN;
  else if (out->is_span_family && out->ascii_class_only)
    out->tier = TIER_SPAN_SCAN;
  else if (out->is_literal_only)
    out->tier = TIER_LITERAL;
  else if (out->has_literal_prefix && out->literal_prefix_len > 0)
    out->tier = TIER_PREFIX;
  else if (out->has_candidate_bitmap && out->is_single_char_alt)
    out->tier = TIER_BITMAP;
  else if (out->is_alt_literals)
    out->tier = TIER_ALT_LIT;
  else if (out->search_vm_eligible)
    out->tier = TIER_SEARCH_VM;
  else if (out->automaton_eligible)
    out->tier = TIER_AUTOMATON;
  else
    out->tier = TIER_GENERAL;

  /* ---- Allocate BMH table separately (heap) ---- */
  if (out->has_bmh_skip && out->bmh_skip_len > 0) {
    out->bmh_skip = (uint8_t *)snobol_malloc(256);
    if (out->bmh_skip) {
      memset(out->bmh_skip, (int)out->bmh_skip_len, 256);
      /* Repopulate from literal prefix if available */
      if (out->has_literal_prefix) {
        size_t plen = out->literal_prefix_len < SNOBOL_SEARCH_MAX_PREFIX
                          ? out->literal_prefix_len
                          : SNOBOL_SEARCH_MAX_PREFIX;
        for (size_t i = 0; i < plen - 1; i++) {
          uint8_t b = out->literal_prefix[i];
          out->bmh_skip[b] = (uint8_t)(plen - 1 - i);
        }
      }
    }
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
 * Tier 2: Literal-only fast path
 *
 * For patterns that are a single literal (optionally wrapped in zero-width
 * assertions) with no captures, output, or side effects, we bypass the VM
 * entirely and use memmem() (unanchored) to find the literal position.
 *
 * This is the fastest possible search path: a single libc call per candidate,
 * no bytecode dispatch, no choice stack, no capture tracking.
 * ---------------------------------------------------------------------------
 */
static bool search_literal_only(VM *vm, const char *subject,
                                size_t subject_len, size_t start_offset,
                                const snobol_search_meta_t *meta,
                                snobol_search_result_t *out_result,
                                snobol_search_diag_t *diag) {
  (void)vm;
  (void)meta;
  size_t offset = start_offset;

  /* Extract the literal from bytecode: skip any leading zero-width ops,
   * then read LIT(offset, len). */
  size_t ip = 0;
  const uint8_t *bc = vm->bc;
  size_t bc_len = vm->bc_len;

  while (ip < bc_len) {
    uint8_t op = bc[ip];
    if (op == OP_NOP || op == OP_FENCE || op == OP_ANCHOR) {
      ip++;
      continue;
    }
    if ((op == OP_POS || op == OP_RPOS) && ip + 5 <= bc_len) {
      ip += 5;
      continue;
    }
    break;
  }
  if (ip + 9 > bc_len || bc[ip] != OP_LIT)
    goto fallback;
  uint32_t lit_off = search_read_u32(bc, ip + 1);
  uint32_t lit_len = search_read_u32(bc, ip + 5);
  if (lit_off >= bc_len || lit_off + lit_len > bc_len || lit_len == 0)
    goto fallback;
  const uint8_t *lit = bc + lit_off;

  if (subject_len < lit_len)
    goto nomatch;

  while (offset + lit_len <= subject_len) {
    const char *hay = subject + offset;
    size_t haylen = subject_len - offset;

    const char *found = (const char *)memmem(hay, haylen, lit, lit_len);
    if (!found)
      break;

    size_t cand = (size_t)(found - subject);
    if (diag) {
      diag->candidates_skipped += (cand - offset);
      diag->candidates_tested++;
    }

    /* No VM confirmation needed — the ENTIRE pattern is this literal. */
    out_result->success = true;
    out_result->match_start = cand;
    out_result->match_end = cand + lit_len;
    return true;
  }

nomatch:
  out_result->success = false;
  return false;

fallback:
  /* Should never happen if derive_meta correctly set is_literal_only */
  out_result->success = false;
  return false;
}

/* ---------------------------------------------------------------------------
 * Tier 6: Search-VM dispatch
 *
 * Lightweight bytecode interpreter for search-mode-eligible patterns (no
 * side effects, no captures, no output).  Uses computed-goto dispatch with
 * a reduced opcode table supporting only the safe opcode set:
 *
 *   LIT, LEN, ANY, NOTANY, SPAN, BREAK, JMP, SPLIT, POS, RPOS, TAB, RTAB,
 *   ANCHOR, FENCE, NOP, REPEAT_INIT, REPEAT_STEP, ACCEPT, FAIL, ABORT,
 *   SUCCEED
 *
 * Key differences from vm_exec:
 *  - No output buffer (vm->out is unused)
 *  - No capture tracking (cap_start/cap_end arrays skipped)
 *  - No var tracking (var_start/var_end/var_count skipped)
 *  - Range pointers pre-resolved at entry from vm->range_meta
 *  - Backtracking through shared vm_push_choice/vm_pop_choice
 *
 * The caller must reset vm->ip and vm->pos before each call.
 * On success, vm->pos holds the match length (match_end - match_start).
 * ---------------------------------------------------------------------------
 */

/* ---------------------------------------------------------------------------
 * Simplified choice stack for search_vm_t (Tier 6 search-VM)
 *
 * The search-VM never tracks captures, variables, or loop counters.
 * These simplified push/pop functions only save/restore ip and pos,
 * avoiding the overhead of the full vm_push_choice/vm_pop_choice.
 * ---------------------------------------------------------------------------
 */
typedef struct {
  size_t ip;
  size_t pos;
} search_choice_t;

static inline bool search_vm_push_choice(search_vm_t *vm, size_t ip, size_t pos) {
  if (!vm->choices)
    return false;
  if (vm->choices_top + sizeof(search_choice_t) >= vm->choices_cap) {
    size_t new_cap = vm->choices_cap ? vm->choices_cap * 2 : 256;
    void *new_choices = snobol_realloc(vm->choices, new_cap);
    if (!new_choices)
      return false;
    vm->choices = new_choices;
    vm->choices_cap = new_cap;
  }
  search_choice_t *c = (search_choice_t *)((uint8_t *)vm->choices + vm->choices_top);
  c->ip = ip;
  c->pos = pos;
  vm->choices_top += sizeof(search_choice_t);
  return true;
}

static inline bool search_vm_pop_choice(search_vm_t *vm) {
  if (!vm->choices || vm->choices_top == 0)
    return false;
  vm->choices_top -= sizeof(search_choice_t);
  if (vm->choices_top > 0) {
    search_choice_t *c = (search_choice_t *)((uint8_t *)vm->choices + vm->choices_top - sizeof(search_choice_t));
    vm->ip = c->ip;
    vm->pos = c->pos;
  } else {
    vm->ip = 0;
    vm->pos = 0;
  }
  return true;
}

/* ---------------------------------------------------------------------------
 * Initialize a search_vm_t from a full VM struct.
 * Copies only the fields needed by the search-VM (Tier 6).
 * ---------------------------------------------------------------------------
 */
static inline void search_vm_init_from_vm(search_vm_t *svm, const VM *vm) {
  svm->bc = vm->bc;
  svm->bc_len = vm->bc_len;
  svm->s = vm->s;
  svm->len = vm->len;
  svm->ip = vm->ip;
  svm->pos = vm->pos;
  svm->range_meta = vm->range_meta;
  svm->range_meta_count = vm->range_meta_count;
  svm->choices = vm->choices;
  svm->choices_cap = vm->choices_cap;
  svm->choices_top = vm->choices_top;
  svm->use_compact_choice = vm->use_compact_choice;
  memcpy(svm->counters, vm->counters, sizeof(svm->counters));
  memcpy(svm->loop_min, vm->loop_min, sizeof(svm->loop_min));
  memcpy(svm->loop_max, vm->loop_max, sizeof(svm->loop_max));
  memcpy(svm->loop_last_pos, vm->loop_last_pos, sizeof(svm->loop_last_pos));
  svm->max_counter_used = vm->max_counter_used;
}

/* ---------------------------------------------------------------------------
 * Write back search_vm_t state to full VM struct.
 * Copies only the fields that may have changed during search-VM execution.
 * ---------------------------------------------------------------------------
 */
static inline void search_vm_writeback_to_vm(const search_vm_t *svm, VM *vm) {
  vm->ip = svm->ip;
  vm->pos = svm->pos;
  vm->choices = svm->choices;
  vm->choices_cap = svm->choices_cap;
  vm->choices_top = svm->choices_top;
  vm->use_compact_choice = svm->use_compact_choice;
  memcpy(vm->counters, svm->counters, sizeof(svm->counters));
  memcpy(vm->loop_last_pos, svm->loop_last_pos, sizeof(vm->loop_last_pos));
  vm->max_counter_used = svm->max_counter_used;
}

static bool search_vm_exec(search_vm_t *vm, const char *subject, size_t subject_len,
                           size_t offset, snobol_search_result_t *out_result) {
  (void)subject;
  (void)subject_len;
  const uint8_t *bc = vm->bc;
  size_t bc_len = vm->bc_len;
  size_t ip = vm->ip; /* set by search_reset_vm */
  size_t pos = vm->pos;
  const char *s = vm->s;
  size_t len = vm->len;

  /* ---- Pre-resolve range pointers ---- */
#define SEARCH_VM_MAX_RANGES 64
  struct {
    const uint8_t *ranges_ptr;
    uint16_t count;
    uint16_t case_flag;
  } srange[SEARCH_VM_MAX_RANGES];
  size_t srange_count = 0;

  if (vm->range_meta) {
    srange_count = vm->range_meta_count;
    if (srange_count > SEARCH_VM_MAX_RANGES)
      srange_count = SEARCH_VM_MAX_RANGES;
    for (size_t i = 0; i < srange_count; i++) {
      srange[i].ranges_ptr = vm->range_meta[i].ranges_ptr;
      srange[i].count = vm->range_meta[i].count;
      srange[i].case_flag = vm->range_meta[i].case_insensitive;
    }
  }

  /* ---- Prepare choice stack if not already initialised ---- */
  if (!vm->choices) {
    size_t initial_cap = 256;
    vm->choices = snobol_malloc(initial_cap);
    if (!vm->choices)
      return false;
    vm->choices_cap = initial_cap;
  }
  vm->use_compact_choice = true;
  vm->choices_top = 0;
  /* Don't touch compact-choice write log — we don't track captures */

  /* ---- Computed-goto dispatch table ---- */
#ifndef _MSC_VER
  static void *search_opcode_table[] = {
      [OP_ACCEPT] = &&svm_accept,
      [OP_FAIL] = &&svm_fail,
      [OP_JMP] = &&svm_jmp,
      [OP_SPLIT] = &&svm_split,
      [OP_LIT] = &&svm_lit,
      [OP_ANY] = &&svm_any,
      [OP_NOTANY] = &&svm_notany,
      [OP_SPAN] = &&svm_span,
      [OP_BREAK] = &&svm_break,
      [OP_LEN] = &&svm_len,
      [OP_ANCHOR] = &&svm_anchor,
      [OP_REPEAT_INIT] = &&svm_repeat_init,
      [OP_REPEAT_STEP] = &&svm_repeat_step,
      [OP_FENCE] = &&svm_fence,
      [OP_RPOS] = &&svm_rpos,
      [OP_RTAB] = &&svm_rtab,
      [OP_POS] = &&svm_pos,
      [OP_TAB] = &&svm_tab,
      [OP_ABORT] = &&svm_abort,
      [OP_SUCCEED] = &&svm_succeed,
      [OP_NOP] = &&svm_nop,
  };
#endif

  while (1) {
    if (ip >= bc_len) {
      if (!search_vm_pop_choice(vm))
        goto svm_fail_ret;
      ip = vm->ip;
      continue;
    }
    uint8_t op = bc[ip++];
#ifndef _MSC_VER
    goto *search_opcode_table[op];
#else
    switch (op) {
#endif

/* ---- Zero-width ops ---- */
#ifndef _MSC_VER
svm_nop:
#endif
#ifdef _MSC_VER
    case OP_NOP:
#endif
      continue;

#ifndef _MSC_VER
svm_fence:
#endif
#ifdef _MSC_VER
    case OP_FENCE:
#endif
      /* Cut: discard all prior choice points */
      vm->choices_top = 0;
      continue;

#ifndef _MSC_VER
svm_anchor:
#endif
#ifdef _MSC_VER
    case OP_ANCHOR:
#endif
    {
      uint8_t anchor_type = bc[ip++];
      bool anchor_ok = (anchor_type == 0) ? (pos == 0) : (pos == len);
      if (!anchor_ok) {
        if (!search_vm_pop_choice(vm))
          goto svm_fail_ret;
        ip = vm->ip;
        continue;
      }
      continue;
    }

/* ---- Terminal opcodes ---- */
#ifndef _MSC_VER
svm_accept:
#endif
#ifdef _MSC_VER
    case OP_ACCEPT:
#endif
      out_result->success = true;
      out_result->match_start = offset;
      out_result->match_end = offset + pos;
      return true;

#ifndef _MSC_VER
svm_abort:
#endif
#ifdef _MSC_VER
    case OP_ABORT:
#endif
      out_result->success = true;
      out_result->match_start = offset;
      out_result->match_end = offset + pos;
      return true;

#ifndef _MSC_VER
svm_succeed:
#endif
#ifdef _MSC_VER
    case OP_SUCCEED:
#endif
      /* Force success at current position, consuming nothing more */
      out_result->success = true;
      out_result->match_start = offset;
      out_result->match_end = offset + pos;
      return true;

#ifndef _MSC_VER
svm_fail:
#endif
#ifdef _MSC_VER
    case OP_FAIL:
#endif
    {
      bool had = search_vm_pop_choice(vm);
      if (!had)
        goto svm_fail_ret;
      ip = vm->ip;
      continue;
    }

/* ---- LIT: inline literal match ---- */
#ifndef _MSC_VER
svm_lit:
#endif
#ifdef _MSC_VER
    case OP_LIT:
#endif
    {
      uint32_t lit_off = read_u32(bc, bc_len, &ip);
      uint32_t lit_len = read_u32(bc, bc_len, &ip);
      if (lit_off == ip)
        ip += lit_len;
      if (lit_len <= len - pos &&
          memcmp(s + pos, bc + lit_off, lit_len) == 0) {
        pos += lit_len;
      } else {
        if (!search_vm_pop_choice(vm))
          goto svm_fail_ret;
        ip = vm->ip;
      }
      continue;
    }

/* ---- LEN: advance by N codepoints ---- */
#ifndef _MSC_VER
svm_len:
#endif
#ifdef _MSC_VER
    case OP_LEN:
#endif
    {
      uint32_t n = read_u32(bc, bc_len, &ip);
      size_t p = pos;
      uint32_t i;
      for (i = 0; i < n; i++) {
        uint32_t cp;
        int bytes;
        if (!utf8_peek_next(s, len, p, &cp, &bytes))
          break;
        p += bytes;
      }
      if (i < n) {
        if (!search_vm_pop_choice(vm))
          goto svm_fail_ret;
        ip = vm->ip;
        continue;
      }
      pos = p;
      continue;
    }

/* ---- ANY: match single char in class ---- */
#ifndef _MSC_VER
svm_any:
#endif
#ifdef _MSC_VER
    case OP_ANY:
#endif
    {
      uint16_t set_id = read_u16(bc, bc_len, &ip);
      bool ok = false;
      if (set_id > 0 && (size_t)(set_id - 1) < srange_count) {
        const uint8_t *rp = srange[set_id - 1].ranges_ptr;
        uint16_t cnt = srange[set_id - 1].count;
        if (rp) {
          if (pos < len) {
            uint8_t c = (uint8_t)s[pos];
            uint64_t map[2];
            if (c <= 127 && ranges_to_ascii_bitmap(rp, cnt, map) &&
                bitmap_test(map, c)) {
              ok = true;
            } else {
              uint32_t cp;
              int bytes;
              if (utf8_peek_next(s, len, pos, &cp, &bytes) &&
                  range_contains(rp, cnt, cp)) {
                ok = true;
              }
            }
          }
        }
      }
      if (ok) {
        /* Advance by 1 byte (ASCII) or multi-byte (UTF-8) */
        uint32_t cp;
        int bytes;
        if (!utf8_peek_next(s, len, pos, &cp, &bytes)) {
          if (!search_vm_pop_choice(vm))
            goto svm_fail_ret;
          ip = vm->ip;
          continue;
        }
        pos += bytes;
      } else {
        if (!search_vm_pop_choice(vm))
          goto svm_fail_ret;
        ip = vm->ip;
      }
      continue;
    }

/* ---- NOTANY: match single char NOT in class ---- */
#ifndef _MSC_VER
svm_notany:
#endif
#ifdef _MSC_VER
    case OP_NOTANY:
#endif
    {
      uint16_t set_id = read_u16(bc, bc_len, &ip);
      bool in_class = false;
      if (set_id > 0 && (size_t)(set_id - 1) < srange_count) {
        const uint8_t *rp = srange[set_id - 1].ranges_ptr;
        uint16_t cnt = srange[set_id - 1].count;
        if (rp && pos < len) {
          uint8_t c = (uint8_t)s[pos];
          uint64_t map[2];
          if (c <= 127 && ranges_to_ascii_bitmap(rp, cnt, map) &&
              bitmap_test(map, c)) {
            in_class = true;
          } else {
            uint32_t cp;
            int bytes;
            if (utf8_peek_next(s, len, pos, &cp, &bytes) &&
                range_contains(rp, cnt, cp)) {
              in_class = true;
            }
          }
        }
      }
      if (!in_class && pos < len) {
        uint32_t cp;
        int bytes;
        if (!utf8_peek_next(s, len, pos, &cp, &bytes)) {
          if (!search_vm_pop_choice(vm))
            goto svm_fail_ret;
          ip = vm->ip;
          continue;
        }
        pos += bytes;
      } else {
        if (!search_vm_pop_choice(vm))
          goto svm_fail_ret;
        ip = vm->ip;
      }
      continue;
    }

/* ---- SPAN: consume 1+ characters in class ---- */
#ifndef _MSC_VER
svm_span:
#endif
#ifdef _MSC_VER
    case OP_SPAN:
#endif
    {
      uint16_t set_id = read_u16(bc, bc_len, &ip);
      const uint8_t *rp = nullptr;
      uint16_t cnt = 0;
      if (set_id > 0 && (size_t)(set_id - 1) < srange_count) {
        rp = srange[set_id - 1].ranges_ptr;
        cnt = srange[set_id - 1].count;
      }
      size_t start = pos;
      while (pos < len) {
        bool in_class = false;
        if (rp) {
          uint8_t c = (uint8_t)s[pos];
          uint64_t map[2];
          if (c <= 127 && ranges_to_ascii_bitmap(rp, cnt, map) &&
              bitmap_test(map, c)) {
            in_class = true;
          } else {
            uint32_t cp;
            int bytes;
            if (utf8_peek_next(s, len, pos, &cp, &bytes) &&
                range_contains(rp, cnt, cp)) {
              in_class = true;
            }
          }
        }
        if (!in_class)
          break;
        uint32_t cp;
        int bytes;
        if (!utf8_peek_next(s, len, pos, &cp, &bytes))
          break;
        pos += bytes;
      }
      if (pos == start) {
        /* SPAN requires at least 1 char */
        if (!search_vm_pop_choice(vm))
          goto svm_fail_ret;
        ip = vm->ip;
      }
      continue;
    }

/* ---- BREAK: consume 0+ characters until one in class ---- */
#ifndef _MSC_VER
svm_break:
#endif
#ifdef _MSC_VER
    case OP_BREAK:
#endif
    {
      uint16_t set_id = read_u16(bc, bc_len, &ip);
      const uint8_t *rp = nullptr;
      uint16_t cnt = 0;
      if (set_id > 0 && (size_t)(set_id - 1) < srange_count) {
        rp = srange[set_id - 1].ranges_ptr;
        cnt = srange[set_id - 1].count;
      }
      while (pos < len) {
        bool in_class = false;
        if (rp) {
          uint8_t c = (uint8_t)s[pos];
          uint64_t map[2];
          if (c <= 127 && ranges_to_ascii_bitmap(rp, cnt, map) &&
              bitmap_test(map, c)) {
            in_class = true;
          } else {
            uint32_t cp;
            int bytes;
            if (utf8_peek_next(s, len, pos, &cp, &bytes) &&
                range_contains(rp, cnt, cp)) {
              in_class = true;
            }
          }
        }
        if (in_class)
          break;
        uint32_t cp;
        int bytes;
        if (!utf8_peek_next(s, len, pos, &cp, &bytes))
          break;
        pos += bytes;
      }
      continue;
    }

/* ---- POS: succeed at exact byte offset ---- */
#ifndef _MSC_VER
svm_pos:
#endif
#ifdef _MSC_VER
    case OP_POS:
#endif
    {
      uint32_t n = read_u32(bc, bc_len, &ip);
      if (pos != (size_t)n) {
        if (!search_vm_pop_choice(vm))
          goto svm_fail_ret;
        ip = vm->ip;
        continue;
      }
      continue;
    }

/* ---- RPOS: succeed at N codepoints from end ---- */
#ifndef _MSC_VER
svm_rpos:
#endif
#ifdef _MSC_VER
    case OP_RPOS:
#endif
    {
      uint32_t n = read_u32(bc, bc_len, &ip);
      /* Count remaining codepoints from pos to end */
      size_t p = pos;
      uint32_t remaining = 0;
      while (p < len) {
        uint32_t cp;
        int bytes;
        if (!utf8_peek_next(s, len, p, &cp, &bytes))
          break;
        p += bytes;
        remaining++;
      }
      if (remaining != n) {
        if (!search_vm_pop_choice(vm))
          goto svm_fail_ret;
        ip = vm->ip;
        continue;
      }
      continue;
    }

/* ---- TAB: advance to exact byte offset ---- */
#ifndef _MSC_VER
svm_tab:
#endif
#ifdef _MSC_VER
    case OP_TAB:
#endif
    {
      uint32_t n = read_u32(bc, bc_len, &ip);
      /* Count codepoints from start to n, advance pos to that byte */
      size_t p = 0;
      uint32_t i;
      for (i = 0; i < n; i++) {
        uint32_t cp;
        int bytes;
        if (!utf8_peek_next(s, len, p, &cp, &bytes))
          break;
        p += bytes;
      }
      if (i < n || p > len) {
        if (!search_vm_pop_choice(vm))
          goto svm_fail_ret;
        ip = vm->ip;
        continue;
      }
      pos = p;
      continue;
    }

/* ---- RTAB: advance to N codepoints from end ---- */
#ifndef _MSC_VER
svm_rtab:
#endif
#ifdef _MSC_VER
    case OP_RTAB:
#endif
    {
      uint32_t n = read_u32(bc, bc_len, &ip);
      /* Count total codepoints */
      size_t p = 0;
      uint32_t total_cp = 0;
      while (p < len) {
        uint32_t cp;
        int bytes;
        if (!utf8_peek_next(s, len, p, &cp, &bytes))
          break;
        p += bytes;
        total_cp++;
      }
      if (n > total_cp) {
        if (!search_vm_pop_choice(vm))
          goto svm_fail_ret;
        ip = vm->ip;
        continue;
      }
      /* Advance to (total_cp - n)th codepoint */
      uint32_t target = total_cp - n;
      p = 0;
      uint32_t i;
      for (i = 0; i < target; i++) {
        uint32_t cp;
        int bytes;
        if (!utf8_peek_next(s, len, p, &cp, &bytes))
          break;
        p += bytes;
      }
      if (i < target) {
        if (!search_vm_pop_choice(vm))
          goto svm_fail_ret;
        ip = vm->ip;
        continue;
      }
      pos = p;
      continue;
    }

/* ---- JMP: unconditional jump ---- */
#ifndef _MSC_VER
svm_jmp:
#endif
#ifdef _MSC_VER
    case OP_JMP:
#endif
    {
      uint32_t tgt = read_u32(bc, bc_len, &ip);
      ip = (size_t)tgt;
      continue;
    }

/* ---- SPLIT: non-deterministic branch ---- */
#ifndef _MSC_VER
svm_split:
#endif
#ifdef _MSC_VER
    case OP_SPLIT:
#endif
    {
      uint32_t a = read_u32(bc, bc_len, &ip);
      uint32_t b = read_u32(bc, bc_len, &ip);
      search_vm_push_choice(vm, (size_t)b, pos);
      ip = (size_t)a;
      continue;
    }

/* ---- REPEAT_INIT: begin bounded repetition ---- */
#ifndef _MSC_VER
svm_repeat_init:
#endif
#ifdef _MSC_VER
    case OP_REPEAT_INIT:
#endif
    {
      uint8_t loop_id = read_u8(bc, bc_len, &ip);
      uint32_t min = read_u32(bc, bc_len, &ip);
      uint32_t max = read_u32(bc, bc_len, &ip);
      uint32_t skip_target = read_u32(bc, bc_len, &ip);
      if (loop_id < MAX_LOOPS) {
        vm->counters[loop_id] = 0;
        vm->loop_min[loop_id] = min;
        vm->loop_max[loop_id] = max;
        vm->loop_last_pos[loop_id] = pos;
      }
      /* Push choice to skip the loop body entirely (for 0 iterations) */
      search_vm_push_choice(vm, (size_t)skip_target, pos);
      continue;
    }

/* ---- REPEAT_STEP: step bounded repetition ---- */
#ifndef _MSC_VER
svm_repeat_step:
#endif
#ifdef _MSC_VER
    case OP_REPEAT_STEP:
#endif
    {
      uint8_t loop_id = read_u8(bc, bc_len, &ip);
      uint32_t jmp_target = read_u32(bc, bc_len, &ip);
      if (loop_id < MAX_LOOPS) {
        vm->counters[loop_id]++;
        uint32_t c = vm->counters[loop_id];
        uint32_t max = vm->loop_max[loop_id];
        /* If last_pos didn't advance we're in an infinite loop */
        size_t lp = vm->loop_last_pos[loop_id];
        if (pos == lp) {
          if (!search_vm_pop_choice(vm))
            goto svm_fail_ret;
          ip = vm->ip;
          continue;
        }
        vm->loop_last_pos[loop_id] = pos;
        if (c >= max) {
          /* Done: fall through */
          continue;
        } else {
          /* Push choice to exit after min is met */
          if (c >= vm->loop_min[loop_id]) {
            /* After skip_target meaning: after the loop body,
             * the skip_target of REPEAT_INIT. We push a choice
             * to jump to jmp_target to repeat, with fallthrough
             * being the exit path (implicit choice already there). */
            search_vm_push_choice(vm, (size_t)jmp_target, pos);
          }
          /* Continue to loop body (already there via jmp_target
           * or fallthrough for first iteration) */
          ip = (size_t)jmp_target;
          continue;
        }
      }
      /* Invalid loop_id — treat as no-op */
      continue;
    }

#ifdef _MSC_VER
    }
#endif
  }

svm_fail_ret:
  out_result->success = false;
  return false;
}
#undef SEARCH_VM_MAX_RANGES

/* ---------------------------------------------------------------------------
 * Tier 8: Lightweight automaton path
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
 * Automaton (DFA) — NFA-to-DFA construction
 *
 * We treat the search-VM bytecode as an NFA and apply subset construction.
 * Each VM offset is an NFA state; epsilon transitions come from JMP, SPLIT,
 * and zero-width ops.  The DFA is a deterministic state machine with
 * 256-entry-per-state transition tables indexed by input byte.
 *
 * Because we only handle ascii_class_only patterns, every consuming op
 * operates on single bytes (no UTF-8 multi-byte sequences).
 * ---------------------------------------------------------------------------
 */

/* ---- NFA modelling helpers ---- */

/** Maximum NFA states (VM bytecode offsets) we track. */
#define DFA_NFA_MAX 512

/** A set of NFA states (VM offsets), stored as a sorted u16 array. */
typedef struct {
  uint16_t states[DFA_NFA_MAX];
  uint16_t count;
} nfa_set_t;

/** Reset a set to empty. */
static inline void nfa_set_clear(nfa_set_t *s) { s->count = 0; }

/** Insert a value into a sorted set (no-op if already present). */
static inline void nfa_set_add(nfa_set_t *s, uint16_t v) {
  uint16_t i = 0;
  while (i < s->count && s->states[i] < v) i++;
  if (i < s->count && s->states[i] == v) return;
  if (s->count >= DFA_NFA_MAX) return;
  memmove(&s->states[i + 1], &s->states[i],
          (size_t)(s->count - i) * sizeof(uint16_t));
  s->states[i] = v;
  s->count++;
}

/** Return true if the set contains v. */
static inline bool nfa_set_contains(const nfa_set_t *s, uint16_t v) {
  for (uint16_t i = 0; i < s->count; i++)
    if (s->states[i] == v) return true;
  return false;
}

/** Copy a set. */
static inline void nfa_set_copy(nfa_set_t *dst, const nfa_set_t *src) {
  dst->count = src->count;
  memcpy(dst->states, src->states, src->count * sizeof(uint16_t));
}

/** Compare two sets for equality. */
static inline bool nfa_set_eq(const nfa_set_t *a, const nfa_set_t *b) {
  if (a->count != b->count) return false;
  for (uint16_t i = 0; i < a->count; i++)
    if (a->states[i] != b->states[i]) return false;
  return true;
}

/*
 * Get the bytecode opcode at the given offset.  Returns OP_NOP for out-of-range.
 * The offset is treated as an absolute bytecode position.
 */
static inline uint8_t dfab_op(const uint8_t *bc, size_t bc_len, uint16_t off) {
  return (off < bc_len) ? bc[off] : OP_ACCEPT;
}

/**
 * Compute epsilon-closure: all NFA states reachable from `start` via
 * epsilon transitions (JMP, both SPLIT branches, zero-width ops).
 *
 * Iterates until a fixed point is reached.
 */
static void epsilon_closure(const uint8_t *bc, size_t bc_len, uint16_t start,
                            nfa_set_t *out) {
  nfa_set_clear(out);
  nfa_set_add(out, start);
  bool changed = true;
  while (changed) {
    changed = false;
    for (uint16_t si = 0; si < out->count; si++) {
      uint16_t off = out->states[si];
      if (off >= bc_len) continue;
      uint8_t op = bc[off];
      size_t ip = (size_t)off;

      switch (op) {

      case OP_JMP: {
        /* JMP target (u32) is an epsilon transition */
        if (ip + 5 <= bc_len) {
          uint32_t t = search_read_u32(bc, ip + 1);
          if (t < bc_len && !nfa_set_contains(out, (uint16_t)t)) {
            nfa_set_add(out, (uint16_t)t);
            changed = true;
          }
        }
        break;
      }

      case OP_SPLIT: {
        /* Both SPLIT targets are epsilon transitions */
        if (ip + 9 <= bc_len) {
          uint32_t a = search_read_u32(bc, ip + 1);
          uint32_t b = search_read_u32(bc, ip + 5);
          if (a < bc_len && !nfa_set_contains(out, (uint16_t)a)) {
            nfa_set_add(out, (uint16_t)a);
            changed = true;
          }
          if (b < bc_len && !nfa_set_contains(out, (uint16_t)b)) {
            nfa_set_add(out, (uint16_t)b);
            changed = true;
          }
        }
        break;
      }

      /* Zero-width ops — epsilon transition to next instruction */
      case OP_NOP:
        if (!nfa_set_contains(out, (uint16_t)(off + 1))) {
          nfa_set_add(out, (uint16_t)(off + 1));
          changed = true;
        }
        break;

      case OP_FENCE:
        if (!nfa_set_contains(out, (uint16_t)(off + 1))) {
          nfa_set_add(out, (uint16_t)(off + 1));
          changed = true;
        }
        break;

      case OP_ANCHOR:
        if (ip + 2 <= bc_len && !nfa_set_contains(out, (uint16_t)(off + 2))) {
          nfa_set_add(out, (uint16_t)(off + 2));
          changed = true;
        }
        break;

      case OP_POS:
      case OP_RPOS:
      case OP_TAB:
      case OP_RTAB:
        if (ip + 5 <= bc_len && !nfa_set_contains(out, (uint16_t)(off + 5))) {
          nfa_set_add(out, (uint16_t)(off + 5));
          changed = true;
        }
        break;

      case OP_REPEAT_INIT:
        /* REPEAT_INIT: the skip_target is an epsilon transition to exit
         * the loop; fall-through to the loop body is also epsilon. */
        if (ip + 14 <= bc_len) {
          uint32_t skip = search_read_u32(bc, ip + 9); /* after loop_id, min, max */
          if (skip < bc_len && !nfa_set_contains(out, (uint16_t)skip)) {
            nfa_set_add(out, (uint16_t)skip);
            changed = true;
          }
          /* Also fall-through to the next instruction (the loop body) */
          if (!nfa_set_contains(out, (uint16_t)(off + 14))) {
            nfa_set_add(out, (uint16_t)(off + 14));
            changed = true;
          }
        }
        break;

      case OP_BREAK:
        /* BREAK has an epsilon transition to the next instruction (exit).
         * This means the DFA can exit BREAK without consuming a delimiter byte. */
        if (ip + 3 <= bc_len && !nfa_set_contains(out, (uint16_t)(off + 3))) {
          nfa_set_add(out, (uint16_t)(off + 3));
          changed = true;
        }
        break;

      case OP_REPEAT_STEP: {
        /* REPEAT_STEP: epsilon to jmp_target (next iteration) */
        if (ip + 6 <= bc_len) {
          uint32_t jmp_tgt = search_read_u32(bc, ip + 1); /* after loop_id */
          if (jmp_tgt < bc_len && !nfa_set_contains(out, (uint16_t)jmp_tgt)) {
            nfa_set_add(out, (uint16_t)jmp_tgt);
            changed = true;
          }
        }
        break;
      }

      default:
        break;
      }
    }
  }
}

/**
 * Check if an NFA state (VM offset) contains ACCEPT.
 */
static inline bool nfa_is_accept(const uint8_t *bc, size_t bc_len,
                                  uint16_t off) {
  return off < bc_len && bc[off] == OP_ACCEPT;
}

/**
 * For a consuming op at VM offset `off`, mark the bytes that trigger a
 * transition in `byte_set` (256-bit bitmap, as 4 uint64_ts).
 *
 * Returns true if there is a consuming transition; false for non-consuming ops.
 * For LEN and other ops that match any byte, all 256 bits are set.
 */
static bool get_op_bytes(const uint8_t *bc, size_t bc_len, uint16_t off,
                         const VM *dfa_vm, uint64_t byte_set[4]) {
  memset(byte_set, 0, 4 * sizeof(uint64_t));

  if (off >= bc_len) return false;
  uint8_t op = bc[off];
  size_t ip = (size_t)off;

  switch (op) {

  case OP_LIT: {
    /* LIT matches a specific sequence of bytes.  The first byte of the
     * literal determines the consuming transition from this offset. */
    if (ip + 9 > bc_len) return false;
    uint32_t lit_off = search_read_u32(bc, ip + 1);
    uint32_t lit_len = search_read_u32(bc, ip + 5);
    if (lit_off >= bc_len || lit_len == 0) return false;
    uint8_t b = bc[lit_off];
    if (b < 64)       byte_set[0] |= (1ULL << b);
    else if (b < 128) byte_set[1] |= (1ULL << (b - 64));
    else if (b < 192) byte_set[2] |= (1ULL << (b - 128));
    else              byte_set[3] |= (1ULL << (b - 192));
    return true;
  }

  case OP_ANY: {
    if (ip + 3 > bc_len) return false;
    uint16_t set_id = search_read_u16(bc, ip + 1);
    if (set_id == 0 || (size_t)(set_id - 1) >= dfa_vm->range_meta_count)
      return false;
    const uint8_t *rp = dfa_vm->range_meta[set_id - 1].ranges_ptr;
    uint16_t cnt = dfa_vm->range_meta[set_id - 1].count;
    if (!rp || cnt == 0) {
      memset(byte_set, 0xFF, 4 * sizeof(uint64_t));
      return true;
    }
    uint64_t abm[2] = {0, 0};
    ranges_to_ascii_bitmap(rp, cnt, abm);
    /* Copy 128-bit ASCII bitmap into 256-bit byte_set */
    byte_set[0] = abm[0];
    byte_set[1] = abm[1];
    return true;
  }

  case OP_NOTANY: {
    if (ip + 3 > bc_len) return false;
    uint16_t set_id = search_read_u16(bc, ip + 1);
    if (set_id == 0 || (size_t)(set_id - 1) >= dfa_vm->range_meta_count)
      return false;
    const uint8_t *rp = dfa_vm->range_meta[set_id - 1].ranges_ptr;
    uint16_t cnt = dfa_vm->range_meta[set_id - 1].count;
    if (!rp || cnt == 0) {
      memset(byte_set, 0xFF, 4 * sizeof(uint64_t));
      return true;
    }
    uint64_t abm[2] = {0, 0};
    ranges_to_ascii_bitmap(rp, cnt, abm);
    /* Invert: NOTANY matches bytes NOT in the class */
    byte_set[0] = ~abm[0];
    byte_set[1] = ~abm[1];
    byte_set[2] = ~0ULL;
    byte_set[3] = ~0ULL;
    return true;
  }

  case OP_SPAN: {
    /* SPAN first-byte transition: same as ANY */
    if (ip + 3 > bc_len) return false;
    uint16_t set_id = search_read_u16(bc, ip + 1);
    if (set_id == 0 || (size_t)(set_id - 1) >= dfa_vm->range_meta_count)
      return false;
    const uint8_t *rp = dfa_vm->range_meta[set_id - 1].ranges_ptr;
    uint16_t cnt = dfa_vm->range_meta[set_id - 1].count;
    if (!rp || cnt == 0) {
      memset(byte_set, 0xFF, 4 * sizeof(uint64_t));
      return true;
    }
    uint64_t abm[2] = {0, 0};
    ranges_to_ascii_bitmap(rp, cnt, abm);
    byte_set[0] = abm[0];
    byte_set[1] = abm[1];
    return true;
  }

  case OP_BREAK: {
    /* BREAK first-byte transition: bytes NOT in the class (matches the
     * delimiter, so the transition would be triggered by a delimiter byte).
     * Actually, BREAK consumes 0+ bytes until a delimiter.  The first
     * consuming step is: if current byte IS in the class, BREAK matches
     * 0 bytes (consuming nothing) and control passes to the next op.
     * If current byte is NOT in the class, BREAK consumes 1 byte.
     * Wait — BREAK matches 0+ bytes until a delimiter, so it consumes
     * everything up to the delimiter.  The transition from the BREAK op
     * itself is: "if not delimiter, consume one byte and stay". */
    if (ip + 3 > bc_len) return false;
    uint16_t set_id = search_read_u16(bc, ip + 1);
    if (set_id == 0 || (size_t)(set_id - 1) >= dfa_vm->range_meta_count)
      return false;
    const uint8_t *rp = dfa_vm->range_meta[set_id - 1].ranges_ptr;
    uint16_t cnt = dfa_vm->range_meta[set_id - 1].count;
    if (!rp || cnt == 0) {
      memset(byte_set, 0xFF, 4 * sizeof(uint64_t));
      return true;
    }
    uint64_t abm[2] = {0, 0};
    ranges_to_ascii_bitmap(rp, cnt, abm);
    /* BREAK loop: stay in BREAK while byte is NOT in the delimiter set.
     * The consuming transition (stay-in-loop) eats bytes outside the set. */
    byte_set[0] = ~abm[0];
    byte_set[1] = ~abm[1];
    byte_set[2] = ~0ULL;
    byte_set[3] = ~0ULL;
    return true;
  }

  case OP_LEN: {
    /* LEN(n): matches any byte (first byte of N). */
    memset(byte_set, 0xFF, 4 * sizeof(uint64_t));
    return true;
  }

  default:
    return false; /* Not a consuming op */
  }
}

/* ---- Byte-class comparison: two byte sets are equivalent iff the set of
 * bytes that trigger a transition is identical.  We use a simple 256-bit
 * bitmap comparison. */
static inline bool byte_set_eq(const uint64_t a[4], const uint64_t b[4]) {
  return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

/*
 * Fast epsilon-closure merge: adds all states from the pre-computed
 * epsilon closure of `off` into `out`.  Uses the table built by
 * build_dfa so we avoid re-computing the fixed point each time.
 */
static inline void ec_merge(const nfa_set_t *eps_closures, uint16_t off,
                             nfa_set_t *out) {
  const nfa_set_t *src = &eps_closures[off];
  for (uint16_t i = 0; i < src->count; i++)
    nfa_set_add(out, src->states[i]);
}

/* ---- DFA state hash table for deduplication ---- */

/* Simple modulo hash for a DFA state: hash the nfa_set_t contents. */
static uint32_t dfa_state_hash(const nfa_set_t *s) {
  uint32_t h = 0;
  for (uint16_t i = 0; i < s->count; i++)
    h = h * 31 + s->states[i];
  return h;
}

/* A map from nfa_set_t -> DFA state index, using open addressing. */
#define DFA_HASH_CAP 8192

typedef struct {
  nfa_set_t key;
  uint16_t dfa_state;  /**< DFA state index */
  bool occupied;
} dfa_hash_entry_t;

static void dfa_hash_init(dfa_hash_entry_t *ht, size_t cap) {
  memset(ht, 0, cap * sizeof(dfa_hash_entry_t));
}

static uint16_t dfa_hash_find_or_insert(dfa_hash_entry_t *ht, size_t cap,
                                         const nfa_set_t *s,
                                         uint16_t *next_id) {
  if (s->count == 0) return SNOBOL_DFA_DEAD;
  uint32_t h = dfa_state_hash(s);
  for (size_t i = 0; i < cap; i++) {
    size_t idx = (size_t)((h + i) % cap);
    if (!ht[idx].occupied) {
      /* Insert */
      nfa_set_copy(&ht[idx].key, s);
      ht[idx].dfa_state = *next_id;
      ht[idx].occupied = true;
      uint16_t id = *next_id;
      (*next_id)++;
      return id;
    }
    if (nfa_set_eq(&ht[idx].key, s))
      return ht[idx].dfa_state;
  }
  return SNOBOL_DFA_DEAD; /* Table full */
}

/* ---- Main DFA builder ---- */

/**
 * Build a DFA from search-VM-eligible bytecode.
 *
 * Returns the constructed DFA on success, or NULL on failure (state
 * explosion, allocation failure, etc.).  The caller owns the DFA and
 * must free it via snobol_dfa_free().
 *
 * @param bc       Bytecode buffer (search-VM-eligible subset)
 * @param bc_len   Bytecode length
 * @param dfa_vm   VM with range_meta populated for charclass resolution
 * @return Allocated DFA, or NULL on failure
 */
snobol_dfa_t *build_dfa(const uint8_t *bc, size_t bc_len,
                        const VM *dfa_vm) {
  if (!bc || bc_len == 0) return NULL;

  /* ---- Pre-scan bytecode for multi-byte LIT data positions ---- */
  /* Build a table mapping each literal data byte offset to its continuation:
   *   lit_next[pos] = next position after matching byte at pos
   * For the last byte of a LIT, this is the instruction after the LIT.
   * For intermediate bytes, it's the next byte of the literal data.
   * Positions not within literal data stay SNOBOL_DFA_DEAD.
   */
  nfa_set_t *eps_closures = NULL;
  snobol_dfa_t *dfa = NULL;
  dfa_hash_entry_t *ht = NULL;
  uint16_t *queue = NULL;
  uint16_t *lit_next = (uint16_t *)snobol_calloc(bc_len, sizeof(uint16_t));
  if (!lit_next) goto fail;
  for (size_t i = 0; i < bc_len; i++) lit_next[i] = SNOBOL_DFA_DEAD;
  {
    size_t ip = 0;
    while (ip < bc_len) {
      uint8_t op = bc[ip];
      size_t next_ip = ip + 1;
      if (op == OP_LIT && ip + 9 <= bc_len) {
        uint32_t lo = search_read_u32(bc, ip + 1);
        uint32_t ll = search_read_u32(bc, ip + 5);
        next_ip = ip + 9 + ll;
        if (ll > 1 && lo < bc_len && lo + ll <= bc_len) {
          for (uint32_t k = 0; k < ll; k++) {
            uint16_t pos = (uint16_t)(lo + k);
            if (k == ll - 1) {
              /* Last byte → next instruction after LIT */
              lit_next[pos] = (uint16_t)next_ip;
            } else {
              lit_next[pos] = (uint16_t)(lo + k + 1);
            }
          }
        }
      } else if (op == OP_LEN && ip + 5 <= bc_len && search_read_u32(bc, ip + 1) > 1) {
        /* For LEN(n > 1): we chain through virtual offsets beyond bc_len.
         * Handled during transition computation using a tracking counter. */
        next_ip = ip + 5;
      } else if (op == OP_ACCEPT || op == OP_ABORT || op == OP_SUCCEED) {
        /* Don't break — later code may be reachable via epsilon edges (SPLIT).
         * Just advance by 1 (these are 1-byte instructions). */
      } else if (op == OP_JMP && ip + 5 <= bc_len) {
        next_ip = search_read_u32(bc, ip + 1);
      } else if (op == OP_SPLIT && ip + 9 <= bc_len) {
        /* Don't scan past SPLIT; epsilon closure handles branching */
        ip++; /* scan byte-by-byte */
        continue;
      } else if (op == OP_NOP || op == OP_FENCE) {
        /* no-op: advance 1 */
      } else if (op == OP_ANCHOR) {
        next_ip = ip + 2;
      } else if (op == OP_POS || op == OP_RPOS || op == OP_TAB || op == OP_RTAB) {
        next_ip = ip + 5;
      } else if (op == OP_ANY || op == OP_NOTANY || op == OP_SPAN || op == OP_BREAK) {
        next_ip = ip + 3;
      } else if (op == OP_REPEAT_INIT) {
        next_ip = ip + 14;
      } else if (op == OP_REPEAT_STEP) {
        next_ip = ip + 6;
      } else if (op == OP_CAP_START || op == OP_CAP_END) {
        next_ip = ip + 2;
      } else {
        next_ip = ip + 1;
      }
      ip = next_ip > ip ? next_ip : ip + 1;
    }
  }

  /* Pre-compute epsilon closure for every VM offset up to bc_len */
  eps_closures = (nfa_set_t *)snobol_calloc(bc_len, sizeof(nfa_set_t));
  if (!eps_closures) goto fail;

  for (uint16_t off = 0; off < bc_len; off++) {
    uint8_t op = bc[off];
    if (op == OP_ACCEPT || op == OP_ABORT || op == OP_SUCCEED) {
      nfa_set_clear(&eps_closures[off]);
      nfa_set_add(&eps_closures[off], off);
    } else {
      epsilon_closure(bc, bc_len, off, &eps_closures[off]);
    }
  }

  /* Allocate DFA */
  dfa = (snobol_dfa_t *)snobol_calloc(1, sizeof(snobol_dfa_t));
  if (!dfa) goto fail;

  dfa->state_cap = 64;
  dfa->trans = (uint16_t *)snobol_calloc(dfa->state_cap * 256, sizeof(uint16_t));
  if (!dfa->trans) goto fail;
  dfa->accepting = (uint8_t *)snobol_calloc((dfa->state_cap + 7) / 8, 1);
  if (!dfa->accepting) goto fail;
  dfa->num_states = 0;
  dfa->start_state = SNOBOL_DFA_DEAD;

  /* Hash table for mapping NFA sets -> DFA states.
   * Must be heap-allocated (≈4.2 MB) to avoid stack overflow. */
  ht = (dfa_hash_entry_t *)snobol_calloc(DFA_HASH_CAP, sizeof(dfa_hash_entry_t));
  if (!ht) goto fail;
  dfa_hash_init(ht, DFA_HASH_CAP);

  uint16_t next_dfa_id = 0;

  /* Start state = epsilon closure of offset 0 */
  nfa_set_t start_set;
  epsilon_closure(bc, bc_len, 0, &start_set);
  if (start_set.count == 0) goto fail;

  dfa->start_state = dfa_hash_find_or_insert(ht, DFA_HASH_CAP, &start_set,
                                              &next_dfa_id);

  /* BFS queue of DFA states to process */
  size_t q_head = 0, q_tail = 0;
  size_t q_cap = 512;
  queue = (uint16_t *)snobol_malloc(q_cap * sizeof(uint16_t));
  if (!queue) goto fail;
  queue[q_tail++] = dfa->start_state;
  dfa->num_states = next_dfa_id;

#define DFA_ENSURE(s) do { \
  if ((s) >= dfa->state_cap) { \
    uint32_t new_cap = dfa->state_cap * 2; \
    if (new_cap > SNOBOL_DFA_MAX_STATES) new_cap = SNOBOL_DFA_MAX_STATES; \
    if ((s) >= new_cap) goto fail; \
    uint16_t *nt = (uint16_t *)snobol_realloc(dfa->trans, new_cap * 256 * sizeof(uint16_t)); \
    if (!nt) goto fail; \
    memset(nt + dfa->state_cap * 256, 0, (new_cap - dfa->state_cap) * 256 * sizeof(uint16_t)); \
    dfa->trans = nt; \
    uint8_t *na = (uint8_t *)snobol_realloc(dfa->accepting, (new_cap + 7) / 8); \
    if (!na) goto fail; \
    memset(na + (dfa->state_cap + 7) / 8, 0, ((new_cap + 7) / 8) - ((dfa->state_cap + 7) / 8)); \
    dfa->accepting = na; \
    dfa->state_cap = new_cap; \
  } \
} while(0)

  while (q_head < q_tail && dfa->num_states < SNOBOL_DFA_MAX_STATES) {
    uint16_t cur_dfa = queue[q_head++];
    DFA_ENSURE(cur_dfa);

    const nfa_set_t *cur_set = NULL;
    for (size_t i = 0; i < DFA_HASH_CAP; i++) {
      if (ht[i].occupied && ht[i].dfa_state == cur_dfa) {
        cur_set = &ht[i].key;
        break;
      }
    }
    if (!cur_set) continue;

    bool is_accepting = false;
    for (uint16_t si = 0; si < cur_set->count && !is_accepting; si++) {
      if (nfa_is_accept(bc, bc_len, cur_set->states[si]))
        is_accepting = true;
    }
    if (is_accepting) {
      dfa->accepting[cur_dfa / 8] |= (uint8_t)(1u << (cur_dfa % 8));
    }

    /* Compute byte-by-byte transitions (union across all NFA states) */
    for (int b = 0; b < 256; b++) {
      nfa_set_t next_set;
      nfa_set_clear(&next_set);

      for (uint16_t si = 0; si < cur_set->count; si++) {
        uint16_t off = cur_set->states[si];
        if (off >= bc_len) continue;

        /* LIT data byte positions — handle before opcode switch so data
         * bytes that happen to match opcode values aren't misinterpreted. */
        if (lit_next[off] != SNOBOL_DFA_DEAD) {
          if ((uint8_t)b == bc[off]) {
            uint16_t cont = lit_next[off];
            if (cont < bc_len && cont != off &&
                lit_next[cont] != SNOBOL_DFA_DEAD) {
              /* cont is another LIT data byte — just add it */
              nfa_set_add(&next_set, cont);
            } else {
              /* cont is a real instruction — merge its epsilon closure */
              ec_merge(eps_closures, cont, &next_set);
            }
          }
          continue;
        }

        uint8_t op = bc[off];

        switch (op) {

        /* ---- LIT ---- */
        case OP_LIT: {
          if ((size_t)off + 9 > bc_len) break;
          uint32_t lit_off = search_read_u32(bc, (size_t)off + 1);
          uint32_t lit_len = search_read_u32(bc, (size_t)off + 5);
          if (lit_off >= bc_len || lit_len == 0) break;
          if (bc[lit_off] != (uint8_t)b) break;

          if (lit_len == 1) {
            /* Single-byte literal: advance past LIT + data + 1 */
            ec_merge(eps_closures, (uint16_t)(off + 9 + 1), &next_set);
          } else {
            /* Multi-byte literal: transition to second byte */
            uint16_t second_byte = (uint16_t)(lit_off + 1);
            if (second_byte < bc_len && lit_next[second_byte] != SNOBOL_DFA_DEAD)
              nfa_set_add(&next_set, second_byte);
          }
          break;
        }

        /* ---- SPAN ---- */
        case OP_SPAN: {
          uint64_t bs[4];
          if (!get_op_bytes(bc, bc_len, off, dfa_vm, bs)) break;
          bool in_class = (b < 64)   ? (bs[0] & (1ULL << b)) != 0 :
                          (b < 128)  ? (bs[1] & (1ULL << (b - 64))) != 0 :
                          (b < 192)  ? (bs[2] & (1ULL << (b - 128))) != 0 :
                                       (bs[3] & (1ULL << (b - 192))) != 0;
          if (!in_class) break;
          /* On a class byte: both stay in SPAN (loop) and exit to next instruction */
          ec_merge(eps_closures, off, &next_set);           /* stay in SPAN */
          ec_merge(eps_closures, (uint16_t)(off + 3), &next_set); /* exit SPAN */
          break;
        }

        /* ---- ANY ---- */
        case OP_ANY: {
          uint64_t bs[4];
          if (!get_op_bytes(bc, bc_len, off, dfa_vm, bs)) break;
          bool in_class = (b < 64)   ? (bs[0] & (1ULL << b)) != 0 :
                          (b < 128)  ? (bs[1] & (1ULL << (b - 64))) != 0 :
                          (b < 192)  ? (bs[2] & (1ULL << (b - 128))) != 0 :
                                       (bs[3] & (1ULL << (b - 192))) != 0;
          if (!in_class) break;
          ec_merge(eps_closures, (uint16_t)(off + 3), &next_set);
          break;
        }

        /* ---- NOTANY ---- */
        case OP_NOTANY: {
          uint64_t bs[4];
          if (!get_op_bytes(bc, bc_len, off, dfa_vm, bs)) break;
          bool in_class = (b < 64)   ? (bs[0] & (1ULL << b)) == 0 :
                          (b < 128)  ? (bs[1] & (1ULL << (b - 64))) == 0 :
                          (b < 192)  ? (bs[2] & (1ULL << (b - 128))) == 0 :
                                       (bs[3] & (1ULL << (b - 192))) == 0;
          if (!in_class) break;
          ec_merge(eps_closures, (uint16_t)(off + 3), &next_set);
          break;
        }

        /* ---- BREAK ---- */
        case OP_BREAK: {
          uint64_t bs[4];
          if (!get_op_bytes(bc, bc_len, off, dfa_vm, bs)) break;
          /* get_op_bytes for BREAK returns bytes NOT in delimiter set */
          bool is_delim = (b < 64)   ? (bs[0] & (1ULL << b)) == 0 :
                          (b < 128)  ? (bs[1] & (1ULL << (b - 64))) == 0 :
                          (b < 192)  ? (bs[2] & (1ULL << (b - 128))) == 0 :
                                       (bs[3] & (1ULL << (b - 192))) == 0;
          if (is_delim) {
            /* Delimiter: BREAK doesn't consume it. The epsilon exit to off+3
             * (set in epsilon_closure) and the next instruction handle it. */
            break;
          }
          /* Non-delimiter: stay in BREAK loop */
          ec_merge(eps_closures, off, &next_set);
          break;
        }

        /* ---- LEN ---- */
        case OP_LEN: {
          if ((size_t)off + 5 > bc_len) break;
          uint32_t n = search_read_u32(bc, (size_t)off + 1);
          if (n <= 1) {
            ec_merge(eps_closures, (uint16_t)(off + 5), &next_set);
          } else {
            /* n > 1: stay at LEN (consume one byte, loop back) */
            nfa_set_add(&next_set, off);
          }
          break;
        }

        default:
          break;
        }
      }

      /* Commit the transition for byte b */
      if (next_set.count > 0) {
        uint16_t target = dfa_hash_find_or_insert(ht, DFA_HASH_CAP, &next_set,
                                                   &next_dfa_id);
        if (target == SNOBOL_DFA_DEAD && next_dfa_id >= SNOBOL_DFA_MAX_STATES)
          goto fail;
        dfa->trans[(size_t)cur_dfa * 256 + b] = target;
        if (target != SNOBOL_DFA_DEAD && target >= dfa->num_states) {
          dfa->num_states = next_dfa_id;
          if (q_tail >= q_cap) {
            size_t new_qc = q_cap * 2;
            if (new_qc > 65536) goto fail;
            uint16_t *nq = (uint16_t *)snobol_realloc(queue, new_qc * sizeof(uint16_t));
            if (!nq) goto fail;
            queue = nq;
            q_cap = new_qc;
          }
          queue[q_tail++] = target;
        }
      } else {
        dfa->trans[(size_t)cur_dfa * 256 + b] = SNOBOL_DFA_DEAD;
      }
    }
  }

  if (dfa->num_states >= SNOBOL_DFA_MAX_STATES)
    goto fail;

  snobol_free(lit_next);
  snobol_free(eps_closures);
  snobol_free(queue);
  snobol_free(ht);

  dfa->trans = (uint16_t *)snobol_realloc(dfa->trans,
                    dfa->num_states * 256 * sizeof(uint16_t));
  dfa->accepting = (uint8_t *)snobol_realloc(dfa->accepting,
                      (dfa->num_states + 7) / 8);
  dfa->state_cap = dfa->num_states;

  return dfa;

fail:
  if (lit_next) snobol_free(lit_next);
  if (eps_closures) snobol_free(eps_closures);
  if (queue) snobol_free(queue);
  if (ht) snobol_free(ht);
  if (dfa) {
    if (dfa->trans) snobol_free(dfa->trans);
    if (dfa->accepting) snobol_free(dfa->accepting);
    snobol_free(dfa);
  }
  return NULL;
}

/* ---- DFA execution engine ---- */

/**
 * Execute the automaton over the subject from `offset`, returning
 * the first match.
 *
 * The DFA walks the subject in a single linear scan:
 *   state = start_state
 *   for each position p in subject:
 *     state = trans[state][byte]
 *     if state == DEAD: restart from start_state at next position
 *     if accepting: record match
 *
 * On success, fills out_result with match start/end.
 */
static bool search_automaton_exec(const snobol_dfa_t *dfa,
                                  const char *subject, size_t subject_len,
                                  size_t offset,
                                  snobol_search_result_t *out_result) {
  if (!dfa || dfa->num_states == 0) {
    out_result->success = false;
    return false;
  }

  const uint16_t *trans = dfa->trans;
  uint16_t num_states = dfa->num_states;
  uint16_t start_state = dfa->start_state;
  const uint8_t *accepting = dfa->accepting;

  size_t pos = offset;

  while (pos <= subject_len) {
    uint16_t state = start_state;
    size_t match_start = pos;
    size_t start_pos = pos;

    while (pos < subject_len) {
      uint8_t byte = (uint8_t)subject[pos];
      uint16_t next = trans[(size_t)state * 256 + byte];

      if (next == SNOBOL_DFA_DEAD) {
        /* DEAD: restart at next subject position */
        pos = start_pos + 1;
        state = start_state;
        goto next_position;
      }

      state = next;

      /* Check if accepting */
      if (accepting[state / 8] & (uint8_t)(1u << (state % 8))) {
        /* Match found: position tracking.
         * If the automaton tracked the match start, we'd return it.
         * For the DFA, the match starts at match_start (the position
         * where we entered the start state) and ends at pos + 1
         * (current position after consuming this byte). */
        out_result->success = true;
        out_result->match_start = match_start;
        out_result->match_end = pos + 1;

        /* Handle empty match (accepting at start position) */
        if (pos + 1 == start_pos) {
          out_result->match_end = pos + 1;
        }

        return true;
      }

      pos++;
    }

    /* Reached end of subject without accepting */
    out_result->success = false;
    return false;

  next_position:
    continue;
  }

  out_result->success = false;
  return false;
}

/** Free a DFA allocated by build_dfa(). */
void snobol_dfa_free(snobol_dfa_t *dfa) {
  if (!dfa) return;
  if (dfa->trans) snobol_free(dfa->trans);
  if (dfa->accepting) snobol_free(dfa->accepting);
  snobol_free(dfa);
}

/**
 * Lazy-build or retrieve the automaton for a pattern.
 * Returns the DFA pointer if available, NULL if not.
 */
static snobol_dfa_t *get_or_build_automaton(snobol_pattern_t *pattern,
                                              const VM *dfa_vm) {
  snobol_dfa_t *cached = snobol_pattern_get_automaton(pattern);
  if (cached) return cached;

  const snobol_search_meta_t *meta = snobol_pattern_get_meta(pattern);
  if (!meta || !meta->automaton_eligible) return NULL;

  snobol_dfa_t *dfa = build_dfa(snobol_pattern_get_bc(pattern),
                                 snobol_pattern_get_bc_len(pattern), dfa_vm);
  if (dfa)
    snobol_pattern_set_automaton(pattern, dfa);
  return dfa;
}
/* ---------------------------------------------------------------------------
 * Unified search entrypoint.  Dispatches to the most specific tier available
 * using the pre-derived (or inline-derived) metadata, then falls back to the
 * general VM.
 *
 * Tier routing (highest priority first):
 *   1. BREAK/BREAKX with ASCII bitmap  -> break_accelerated
 *   2. SPAN with ASCII bitmap           -> span_accelerated
 *   3. Literal-only (no VM)             -> literal_only
 *   4. Literal prefix (>=1 byte)        -> literal_accelerated
 *   5. Single-char alt (candidate set)  -> bitmap_accelerated
 *   6. Alternation-of-literals (trie)   -> alt_literals_try
 *   7. Search-VM eligible               -> search_vm_exec
 *   8. Automaton-eligible               -> automaton path
 *   9. General VM fallback              -> vm_exec (start-byte accelerated)
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
 * repeatedly without re-memsetting the VM. snobol_pattern_search_ex()
 * in core/src/api.c is the reference implementation.
 * ---------------------------------------------------------------------------
 */

/* ---------------------------------------------------------------------------
 * Tier handler function pointer type and wrappers
 *
 * All tier handlers share a uniform signature matching snobol_search_exec().
 * The tier_table[] array maps tier index to handler for single-dispatch
 * routing, replacing the 8 sequential if-branches.
 * ---------------------------------------------------------------------------
 */
typedef bool (*tier_fn)(VM *vm, const char *subject, size_t subject_len,
                        size_t start_offset, const snobol_search_meta_t *meta,
                        const snobol_dfa_t *dfa,
                        snobol_search_result_t *out_result,
                        snobol_search_diag_t *diag);

/* Tier 0: BREAK/BREAKX with ASCII bitmap scan */
static bool tier_break_scan(VM *vm, const char *subject, size_t subject_len,
                            size_t start_offset, const snobol_search_meta_t *meta,
                            const snobol_dfa_t *dfa,
                            snobol_search_result_t *out_result,
                            snobol_search_diag_t *diag) {
  (void)dfa;
  if (diag) diag->last_skip_reason = SNOBOL_SEARCH_SKIP_BREAK_SCAN;
  return search_break_accelerated(vm, subject, subject_len, start_offset,
                                  meta, out_result, diag);
}

/* Tier 1: SPAN with ASCII bitmap scan */
static bool tier_span_scan(VM *vm, const char *subject, size_t subject_len,
                           size_t start_offset, const snobol_search_meta_t *meta,
                           const snobol_dfa_t *dfa,
                           snobol_search_result_t *out_result,
                           snobol_search_diag_t *diag) {
  (void)dfa;
  if (diag) diag->last_skip_reason = SNOBOL_SEARCH_SKIP_SPAN_SCAN;
  return search_span_accelerated(vm, subject, subject_len, start_offset,
                                 meta, out_result, diag);
}

/* Tier 2: Literal-only fast path (no VM needed) */
static bool tier_literal_only(VM *vm, const char *subject, size_t subject_len,
                              size_t start_offset, const snobol_search_meta_t *meta,
                              const snobol_dfa_t *dfa,
                              snobol_search_result_t *out_result,
                              snobol_search_diag_t *diag) {
  (void)dfa;
  if (diag) diag->last_skip_reason = SNOBOL_SEARCH_SKIP_LITERAL;
  return search_literal_only(vm, subject, subject_len, start_offset,
                             meta, out_result, diag);
}

/* Tier 3: Literal prefix (memmem / memchr) */
static bool tier_literal_prefix(VM *vm, const char *subject, size_t subject_len,
                                size_t start_offset, const snobol_search_meta_t *meta,
                                const snobol_dfa_t *dfa,
                                snobol_search_result_t *out_result,
                                snobol_search_diag_t *diag) {
  (void)dfa;
  if (diag) {
    diag->last_skip_reason = (meta->literal_prefix_len == 1)
                                 ? SNOBOL_SEARCH_SKIP_FIRST_BYTE
                                 : SNOBOL_SEARCH_SKIP_LITERAL;
  }
  return search_literal_accelerated(vm, subject, subject_len, start_offset,
                                    meta, out_result, diag);
}

/* Tier 4: Candidate-set bitmap for single-char alternation */
static bool tier_bitmap(VM *vm, const char *subject, size_t subject_len,
                        size_t start_offset, const snobol_search_meta_t *meta,
                        const snobol_dfa_t *dfa,
                        snobol_search_result_t *out_result,
                        snobol_search_diag_t *diag) {
  (void)dfa;
  if (diag) diag->last_skip_reason = SNOBOL_SEARCH_SKIP_BITMAP;
  return search_bitmap_accelerated(vm, subject, subject_len, start_offset,
                                   meta, out_result, diag);
}

/* Tier 5: Alternation-of-literals (trie-based) */
static bool tier_alt_literals(VM *vm, const char *subject, size_t subject_len,
                              size_t start_offset, const snobol_search_meta_t *meta,
                              const snobol_dfa_t *dfa,
                              snobol_search_result_t *out_result,
                              snobol_search_diag_t *diag) {
  (void)meta; (void)dfa;
  if (diag) diag->last_skip_reason = SNOBOL_SEARCH_SKIP_NONE;
  return search_alt_literals_try(vm, subject, subject_len, start_offset, out_result);
}

/* Tier 6: Search-VM for eligible patterns */
static bool tier_search_vm(VM *vm, const char *subject, size_t subject_len,
                           size_t start_offset, const snobol_search_meta_t *meta,
                           const snobol_dfa_t *dfa,
                           snobol_search_result_t *out_result,
                           snobol_search_diag_t *diag) {
  (void)dfa;
  size_t offset = start_offset;
  while (offset <= subject_len) {
    if (diag) {
      diag->candidates_tested++;
      diag->search_vm_tests++;
    }
    search_reset_vm(vm, subject, subject_len, offset);
    search_vm_t svm;
    search_vm_init_from_vm(&svm, vm);
    svm.choices_top = 0;
    bool ok = search_vm_exec(&svm, subject, subject_len, offset, out_result);
    search_vm_writeback_to_vm(&svm, vm);
    if (ok)
      return true;
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

/* Tier 8: General VM fallback with start-byte bitmap acceleration */
static bool tier_general_fallback(VM *vm, const char *subject, size_t subject_len,
                                  size_t start_offset, const snobol_search_meta_t *meta,
                                  const snobol_dfa_t *dfa,
                                  snobol_search_result_t *out_result,
                                  snobol_search_diag_t *diag) {
  (void)dfa;
  size_t offset = start_offset;

  while (offset <= subject_len) {
    /* Start-byte bitmap filter: skip subject bytes that can never start
     * the pattern.  On average this eliminates 255/256 ≈ 99.6% of
     * candidate positions for constrained patterns. */
    if (meta->has_start_bitmap && offset < subject_len) {
      uint8_t b = (uint8_t)subject[offset];
      if (!(meta->start_bitmap[b >> 3] & (uint8_t)(1u << (b & 7)))) {
        if (diag)
          diag->candidates_skipped++;
        offset++;
        continue;
      }
    }

    /* Minimum-length filter: if too few remaining bytes, give up */
    if (meta->minlength > 0 && offset + meta->minlength > subject_len) {
      out_result->success = false;
      return false;
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

/* Tier 7: Automaton path for eligible patterns */
static bool tier_automaton(VM *vm, const char *subject, size_t subject_len,
                           size_t start_offset, const snobol_search_meta_t *meta,
                           const snobol_dfa_t *dfa,
                           snobol_search_result_t *out_result,
                           snobol_search_diag_t *diag) {
  snobol_dfa_t *owned = NULL;
  if (!dfa) {
    owned = build_dfa(vm->bc, vm->bc_len, vm);
    dfa = owned;
  }
  if (dfa) {
    size_t offset = start_offset;
    while (offset <= subject_len) {
      if (diag) {
        diag->candidates_tested++;
        diag->automaton_tests++;
      }
      bool ok =
          search_automaton_exec(dfa, subject, subject_len, offset, out_result);
      if (ok) {
        if (owned) snobol_dfa_free(owned);
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
  }
  if (owned) snobol_dfa_free(owned);
  /* Fall through to general VM */
  return tier_general_fallback(vm, subject, subject_len, start_offset,
                               meta, NULL, out_result, diag);
}

/**
 * Tier dispatch table: index by meta->tier for single-dispatch routing.
 * Replaces the 8 sequential if-branches in the old snobol_search_exec().
 *
 * Each handler has uniform signature: tier_fn(VM*, subject, len, offset,
 * meta, dfa, result, diag). Handlers for Tiers 1-5 bypass the VM entirely;
 * Tier 6 uses the lightweight search_vm_t; Tier 7 uses the DFA automaton;
 * Tier 8 uses the full SNOBOL4 VM.
 */
static const tier_fn tier_table[TIER_COUNT] = {
  [TIER_BREAK_SCAN] = tier_break_scan,     /* BREAK/BREAKX ASCII bitmap scan */
  [TIER_SPAN_SCAN]  = tier_span_scan,      /* SPAN ASCII bitmap scan */
  [TIER_LITERAL]    = tier_literal_only,   /* Literal-only (no VM) */
  [TIER_PREFIX]     = tier_literal_prefix, /* Literal prefix (memmem/memchr) */
  [TIER_BITMAP]     = tier_bitmap,         /* Candidate bitmap (single-char alt) */
  [TIER_ALT_LIT]    = tier_alt_literals,   /* Alt-of-literals (trie) */
  [TIER_SEARCH_VM]  = tier_search_vm,      /* Search-VM (backtracking NFA) */
  [TIER_AUTOMATON]  = tier_automaton,      /* DFA automaton (O(n) scan) */
  [TIER_GENERAL]    = tier_general_fallback,/* General VM fallback */
};

bool snobol_search_exec(VM *vm, const char *subject, size_t subject_len,
                        size_t start_offset, const snobol_search_meta_t *meta,
                        const snobol_dfa_t *dfa,
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

  /* If caller provided a DFA, force automaton tier regardless of meta->tier.
   * This preserves the caller's intent: passing a DFA means "use it". */
  if (dfa && meta->automaton_eligible) {
    return tier_table[TIER_AUTOMATON](vm, subject, subject_len, start_offset,
                                      meta, dfa, out_result, diag);
  }

  /* Single-dispatch via tier function pointer table */
  return tier_table[meta->tier](vm, subject, subject_len, start_offset,
                                meta, dfa, out_result, diag);
}
