/**
 * @file jit_backend.h
 * @brief JIT backend vtable API for the SNOBOL4 micro-JIT.
 *
 * A backend implements three operations:
 *  - lower():       Translate IR instructions to machine code.
 *  - flush_icache():Ensure instruction-cache coherence after code emission.
 *  - name:          Static string identifying the backend.
 *
 * Backends are registered at compile time via CMake option SNOBOL_JIT_BACKEND
 * (default: "arm64").  Only one backend is active per binary.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef SNOBOL_JIT

#include "snobol/jit_ir.h"

/* -------------------------------------------------------------------------
 * Code emission buffer (the output region written by the backend)
 * Previously named JITState inside jit.c.
 * -------------------------------------------------------------------------
 */
typedef struct {
    uint32_t *p;           /**< Current write pointer (advanced by emit_instr) */
    uint32_t *code_start;  /**< Start of the allocated code buffer */
    size_t    code_size;   /**< Total size of the allocated code buffer in bytes */
} jit_region_t;

/* -------------------------------------------------------------------------
 * Backend vtable
 * -------------------------------------------------------------------------
 */
typedef struct jit_backend {
    /** Identifying name of the backend, e.g. "arm64".  Must match the CMake
     *  SNOBOL_JIT_BACKEND string.  Static storage. */
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
     * Called by the backend after writing machine code to a RWX page.
     */
    void (*flush_icache)(void *code, size_t size);
} jit_backend_t;

/* -------------------------------------------------------------------------
 * Registration API (implemented in jit.c)
 * -------------------------------------------------------------------------
 */

/** Register the active backend.  Call from an arch-specific init function
 *  during snobol_jit_init().  Only one backend may be active. */
void                  jit_backend_register(const jit_backend_t *backend);

/** Return the currently registered backend, or NULL if none. */
const jit_backend_t  *jit_backend_get(void);

/** Return the name of the active backend, or "(none)" if unregistered. */
const char           *jit_backend_name(void);

/* -------------------------------------------------------------------------
 * ARM64 backend init (declaration; definition in jit_backend_arm64.c)
 * -------------------------------------------------------------------------
 */
#if defined(__aarch64__) || defined(__arm64__)
void snobol_jit_arm64_register(void);
#endif

#endif /* SNOBOL_JIT */



