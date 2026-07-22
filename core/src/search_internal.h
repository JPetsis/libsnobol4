#pragma once

/*
 * search_internal.h — shared (non-public) infrastructure for the search
 * translation units.
 *
 * The search engine is split across several .c files:
 *   - search_meta.c  : meta derivation, eligibility analysis, tier selection
 *   - search_tiers.c : tier handlers, search-VM, NFA/DFA build+exec, dispatch
 *   - search_simd.c  : SIMD Thompson NFA (already separate)
 *
 * Declarations needed by more than one of those files live here.  Tiny hot
 * bytecode readers are defined `static inline` so each TU keeps its own
 * inlined copy at zero cost (no linkage change on the hot path).  Genuine
 * cross-TU functions are declared with external linkage; only the trie
 * struct layout and its pool macros are shared as full definitions.
 * No public API signature changes.
 */

#include "snobol/search.h"
#include "snobol/vm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Forward declarations for cached state types (defined in search_simd.c).
 * --------------------------------------------------------------------------- */
struct simd_nfa;

/* ---------------------------------------------------------------------------
 * Internal pattern accessors (implemented in api.c) — shared by both TUs.
 * --------------------------------------------------------------------------- */
extern snobol_dfa_t *snobol_pattern_get_automaton(
    const snobol_pattern_t *pattern);
extern void snobol_pattern_set_automaton(snobol_pattern_t *pattern,
                                         snobol_dfa_t *dfa);
extern const snobol_search_meta_t *snobol_pattern_get_meta(
    const snobol_pattern_t *pattern);
extern snobol_auto_trie_t *snobol_pattern_get_trie_cache(
    const snobol_pattern_t *pattern);
extern void snobol_pattern_set_trie_cache(snobol_pattern_t *pattern,
                                          snobol_auto_trie_t *trie);

/* ---------------------------------------------------------------------------
 * Bytecode readers (shared, static inline — duplicated per-TU, zero cost).
 * --------------------------------------------------------------------------- */

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
 * Trie data structures for alternation-of-literals matching (shared).
 *
 * Uses a compact edge-list representation: each trie node references its
 * first outgoing edge via `first_edge`; edges for the same parent form a
 * singly-linked list via `sibling`.  Memory is pre-allocated as a fixed
 * pool (no malloc/free in the hot path).
 * --------------------------------------------------------------------------- */

/** Sentinel: no edge / no node. */
#define SNOBOL_AUTO_NULL UINT16_MAX

/** Maximum nodes in the automaton trie (stack-allocated pool). */
#define SNOBOL_AUTO_MAX_NODES 256

/** Maximum edges in the automaton trie (stack-allocated pool). */
#define SNOBOL_AUTO_MAX_EDGES 1024

/** A single outgoing edge from a trie node. */
typedef struct {
  uint8_t byte;     /**< Transition byte. */
  uint16_t next;    /**< Target node index (SNOBOL_AUTO_NULL = none). */
  uint16_t sibling; /**< Next sibling edge index (SNOBOL_AUTO_NULL = end). */
} snobol_auto_edge_t;

/** A single trie node. */
typedef struct {
  uint16_t first_edge; /**< Index of first outgoing edge (NULL = none). */
  bool is_end;         /**< True when this node terminates a valid pattern. */
} snobol_auto_node_t;

/** Pre-allocated trie storage (stack-friendly, ~7 KB). */
typedef struct snobol_auto_trie_t {
  snobol_auto_node_t nodes[SNOBOL_AUTO_MAX_NODES];
  snobol_auto_edge_t edges[SNOBOL_AUTO_MAX_EDGES];
  uint16_t node_count;
  uint16_t edge_count;
} snobol_auto_trie_t;

/* ---------------------------------------------------------------------------
 * Cross-TU function: cost-model tier selection lives in search_meta.c but is
 * invoked by dispatch_search_impl() in search_tiers.c.
 * --------------------------------------------------------------------------- */
snobol_search_tier_t select_tier_by_cost(const snobol_search_meta_t *meta,
                                         size_t subject_len, bool dfa_available,
                                         bool anchored);
