/**
 * @file jit_backend.h
 * @brief JIT backend vtable API for the SNOBOL4 JIT.
 *
 * A backend implements:
 *  - lower:         Translate IR instructions to machine code.
 *  - flush_icache:  Ensure instruction-cache coherence after code emission.
 *  - name:          Static string identifying the backend.
 *
 * SLJIT is the only JIT backend (arch-specific backends retired).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef SNOBOL_JIT

#include "snobol/jit_ir.h"

/* -------------------------------------------------------------------------
 * Code emission buffer (the output region written by the backend)
 * -------------------------------------------------------------------------
 */
typedef struct {
  uint32_t *p;          /**< Current write pointer (advanced by emit_instr) */
  uint32_t *code_start; /**< Start of the allocated code buffer */
  size_t code_size;     /**< Total size of the allocated code buffer in bytes */
  size_t n_blocks;      /**< Number of CFG blocks compiled in this region */
} jit_region_t;

/* -------------------------------------------------------------------------
 * Backend vtable
 * -------------------------------------------------------------------------
 */
typedef struct jit_backend {
  const char *name;

  /**
   * Lower IR instructions to machine code.
   *
   * @param ir    IR region produced by jit_ir_lift_region().
   * @param vm    VM state (charclass tables, bytecode for data-only accesses).
   * @param out   Output code region; caller allocates and passes a pointer.
   *              On success the backend fills out->p and out->code_size.
   * @return Pointer to the compiled function (cast of out->code_start on
   *         success), or NULL on failure.  The backend is responsible for
   *         calling snobol_jit_seal_code() before returning.
   */
  void *(*lower)(const jit_ir_region_t *ir, VM *vm, jit_region_t *out);

  /**
   * Flush the instruction cache for the given code range.
   */
  void (*flush_icache)(void *code, size_t size);
} jit_backend_t;

/* -------------------------------------------------------------------------
 * Registration API (implemented in jit.c)
 * -------------------------------------------------------------------------
 */

/** Register the active backend.  Call from an arch-specific init function
 *  during snobol_jit_init().  Only one backend may be active. */
void jit_backend_register(const jit_backend_t *backend);

/** Return the currently registered backend, or NULL if none. */
const jit_backend_t *jit_backend_get(void);

/** Return the name of the active backend, or "(none)" if unregistered. */
const char *jit_backend_name(void);

/* -------------------------------------------------------------------------
 * Backend init declarations
 * -------------------------------------------------------------------------
 */
#ifdef SNOBOL_JIT_BACKEND_SLJIT
void snobol_jit_sljit_register(void);
#endif

#endif /* SNOBOL_JIT */
