#pragma once

/**
 * @file snobol_attrs.h
 * @brief Portable function-attribute macros for SNOBOL4
 *
 * Provides SNOBOL_NODISCARD and SNOBOL_MAYBE_UNUSED that map to the
 * appropriate compiler-specific spelling:
 *
 *   Compiler / standard        SNOBOL_NODISCARD                 SNOBOL_MAYBE_UNUSED
 *   ─────────────────────────  ──────────────────────────────── ────────────────────────────────
 *   C23 / C++17+               [[nodiscard]]                    [[maybe_unused]]
 *   GCC / Clang (pre-C23)      __attribute__((warn_unused_result)) __attribute__((unused))
 *   MSVC / unknown             (empty — silently drop the hint) (empty)
 */

/* ── SNOBOL_NODISCARD ──────────────────────────────────────────────────── */
#ifndef SNOBOL_NODISCARD
#  if defined(__cplusplus) && __cplusplus >= 201703L
#    define SNOBOL_NODISCARD [[nodiscard]]
#  elif !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#    define SNOBOL_NODISCARD [[nodiscard]]
#  elif defined(__GNUC__) || defined(__clang__)
#    define SNOBOL_NODISCARD __attribute__((warn_unused_result))
#  else
#    define SNOBOL_NODISCARD
#  endif
#endif

/* ── SNOBOL_MAYBE_UNUSED ───────────────────────────────────────────────── */
#ifndef SNOBOL_MAYBE_UNUSED
#  if defined(__cplusplus) && __cplusplus >= 201703L
#    define SNOBOL_MAYBE_UNUSED [[maybe_unused]]
#  elif !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#    define SNOBOL_MAYBE_UNUSED [[maybe_unused]]
#  elif defined(__GNUC__) || defined(__clang__)
#    define SNOBOL_MAYBE_UNUSED __attribute__((unused))
#  else
#    define SNOBOL_MAYBE_UNUSED
#  endif
#endif

