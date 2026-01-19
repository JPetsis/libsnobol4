#ifndef SNOBOL_JIT_H
#define SNOBOL_JIT_H

#include <stddef.h>
#include <stdint.h>
#include "snobol_vm.h"

#ifdef SNOBOL_JIT

/* Opaque handle for a JIT trace */
typedef void (*jit_trace_fn)(VM *vm);

/* Initialize JIT subsystem (global) */
void snobol_jit_init(void);

/* Shutdown JIT subsystem */
void snobol_jit_shutdown(void);

/* Allocator for JIT code */
void *snobol_jit_alloc_code(size_t size);

/* Finalize code buffer (make executable) */
void snobol_jit_seal_code(void *code, size_t size);

/* Free code buffer */
void snobol_jit_free_code(void *code, size_t size);

/* Compile a trace starting at ip */
jit_trace_fn snobol_jit_compile(VM *vm, size_t ip);

#endif

#endif // SNOBOL_JIT_H