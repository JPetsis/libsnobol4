/**
 * @file search_fusion.c
 * @brief Tier 10: Fused concat-pattern execution engine.
 *
 * Executes fusible concat patterns (LIT/SPAN/ANY/NOTANY/BREAK chains) via a
 * dedicated lightweight engine — no VM, no bytecode dispatch, no choice stack.
 *
 * The fusion pass in search_meta.c compiles the bytecode into a flat segment
 * list.  exec_fusion() walks the segment list directly, matching each segment
 * in sequence.  On success, returns the match start/end.  On failure, the
 * caller advances to the next candidate.
 */

#include "snobol/search.h"
#include "snobol/snobol.h"
#include "snobol/snobol_attrs.h"
#include "snobol/vm.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "search_internal.h"

/* ---------------------------------------------------------------------------
 * Bitmap helpers for 256-bit fusion bitmaps (32 bytes).
 * ---------------------------------------------------------------------------
 */

static inline bool fusion_bitmap_test(const uint8_t bm[32], uint8_t b) {
  return (bm[b >> 3] & (uint8_t)(1u << (b & 7))) != 0;
}

/* ---------------------------------------------------------------------------
 * exec_fusion: execute a fused pattern at a given position.
 *
 * Walks the segment list, matching each segment in sequence.  Returns true
 * on success (all segments matched), filling out_match_end with the position
 * after the last segment.  Returns false on first segment failure.
 *
 * @param fusion     Compiled fusion segment list
 * @param subject    Subject string
 * @param subject_len Subject length
 * @param pos        Starting position
 * @param out_match_end Output: position after last matched segment
 * @return true if all segments matched; false on failure
 * ---------------------------------------------------------------------------
 */
static bool exec_fusion(const snobol_fusion_t *fusion, const char *subject,
                        size_t subject_len, size_t pos,
                        size_t *out_match_end) {
  size_t cur = pos;

  for (uint32_t i = 0; i < fusion->count; i++) {
    const snobol_fusion_segment_t *seg = &fusion->segs[i];

    switch (seg->type) {
      case FUSION_LIT: {
        if (cur + seg->lit.len > subject_len)
          return false;
        if (memcmp(subject + cur, seg->lit.data, seg->lit.len) != 0)
          return false;
        cur += seg->lit.len;
        break;
      }

      case FUSION_RUN: {
        size_t start = cur;
        while (cur < subject_len &&
               fusion_bitmap_test(seg->run.bitmap, (uint8_t)subject[cur])) {
          cur++;
        }
        if (cur - start < seg->run.min)
          return false;
        break;
      }

      case FUSION_CHAR: {
        if (cur >= subject_len)
          return false;
        if (!fusion_bitmap_test(seg->chr.bitmap, (uint8_t)subject[cur]))
          return false;
        cur++;
        break;
      }

      case FUSION_ALT: {
        bool matched = false;
        for (uint32_t j = 0; j < seg->alt.alt_count; j++) {
          snobol_fusion_segment_t *alt_seg = seg->alt.alts[j];
          if (!alt_seg)
            continue;
          size_t save_cur = cur;
          if (exec_fusion((const snobol_fusion_t *)alt_seg, subject,
                          subject_len, cur, &cur)) {
            matched = true;
            break;
          }
          cur = save_cur;
        }
        if (!matched)
          return false;
        break;
      }

      default:
        return false;
    }
  }

  *out_match_end = cur;
  return true;
}

/* ---------------------------------------------------------------------------
 * tier_fusion: Tier 10 dispatch entry point.
 *
 * For anchored matches: run exec_fusion once at start_offset.
 * For unanchored matches: use prefilter (memchr/memmem) to find candidate
 * positions, then verify each with exec_fusion.
 * ---------------------------------------------------------------------------
 */
bool tier_fusion(VM *vm, const char *subject, size_t subject_len,
                 size_t start_offset, const snobol_search_meta_t *meta,
                 const snobol_dfa_t *dfa, snobol_search_result_t *out_result,
                 snobol_search_diag_t *diag, bool anchored) {
  (void)vm;
  (void)dfa;

  if (!meta || !meta->fusion || !meta->fusion_eligible) {
    out_result->success = false;
    return false;
  }

  const snobol_fusion_t *fusion = meta->fusion;

  if (anchored) {
    size_t match_end = 0;
    if (exec_fusion(fusion, subject, subject_len, start_offset, &match_end)) {
      out_result->success = true;
      out_result->match_start = start_offset;
      out_result->match_end = match_end;
      return true;
    }
    out_result->success = false;
    return false;
  }

  size_t offset = start_offset;

  while (offset < subject_len) {
    if (diag)
      diag->candidates_tested++;

    size_t match_end = 0;
    if (exec_fusion(fusion, subject, subject_len, offset, &match_end)) {
      out_result->success = true;
      out_result->match_start = offset;
      out_result->match_end = match_end;
      return true;
    }

    if (offset >= subject_len)
      break;
    offset++;
  }

  out_result->success = false;
  return false;
}
