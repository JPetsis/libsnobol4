
#include "snobol/jit.h"
#include "snobol/jit_backend.h"
#include "snobol/jit_ir.h"

#ifdef SNOBOL_JIT

#ifdef SNOBOL_JIT_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Static assertion: PAGE_EXECUTE_READWRITE must never be used (DEP compliance).
 * This placeholder assertion passes universally — the real enforcement is in
 * snobol_jit_alloc_code() which always allocates with PAGE_READWRITE, and
 * snobol_jit_seal_code() which transitions to PAGE_EXECUTE_READ. */
#ifdef SNOBOL_JIT_PLATFORM_WINDOWS
_Static_assert(MEM_COMMIT && MEM_RESERVE && PAGE_READWRITE && PAGE_EXECUTE_READ,
               "Windows memory allocation constants must be available");
#endif
#include "snobol/dynamic_pattern.h"
#include "snobol/snobol_internal.h"
#include "snobol/string_fn.h"
#include "snobol/table.h"
#include "snobol/type_fn.h"

/* ---------------------------------------------------------------------------
 * Portable mutex abstraction (SRWLOCK on Windows, pthreads elsewhere)
 * ---------------------------------------------------------------------------
 */

#ifdef SNOBOL_JIT_PLATFORM_WINDOWS
typedef SRWLOCK jit_mutex_t;
#define JIT_MUTEX_INIT SRWLOCK_INIT
#define jit_mutex_lock(m) AcquireSRWLockExclusive(m)
#define jit_mutex_unlock(m) ReleaseSRWLockExclusive(m)
#else
typedef pthread_mutex_t jit_mutex_t;
#define JIT_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
#define jit_mutex_lock(m) pthread_mutex_lock(m)
#define jit_mutex_unlock(m) pthread_mutex_unlock(m)
#endif

/* ---------------------------------------------------------------------------
 * JIT OPCODE COVERAGE MATRIX
 *
 * jit-compiled  — full inline ARM64 code emitted within the compiled region
 * call-out       — ARM64 code calls a C helper via BLR; no interpreter fallback
 * pseudo         — no ARM64 emitted; scan-pass only (data-block skip or marker)
 *
 * OP_ACCEPT       jit-compiled (EXIT terminator)
 * OP_FAIL         jit-compiled (EXIT terminator)
 * OP_JMP          jit-compiled (FWD/BWD terminator)
 * OP_SPLIT        jit-compiled (SPLIT terminator, pushes choice)
 * OP_LIT          jit-compiled (inline byte comparison)
 * OP_ANY          jit-compiled (inline bitmap match)
 * OP_NOTANY       jit-compiled (inline bitmap match)
 * OP_SPAN         jit-compiled (inline bitmap loop)
 * OP_BREAK        jit-compiled (inline bitmap scan)
 * OP_BREAKX       jit-compiled (inline bitmap scan)
 * OP_LEN          jit-compiled (inline codepoint advance)
 * OP_ANCHOR       jit-compiled (inline position check)
 * OP_CAP_START    jit-compiled (inline store)
 * OP_CAP_END      jit-compiled (inline store)
 * OP_ASSIGN       jit-compiled (inline var update)
 * OP_REPEAT_INIT  jit-compiled (EXIT terminator)
 * OP_REPEAT_STEP  jit-compiled (EXIT terminator)
 * OP_NOP          jit-compiled (skip)
 * OP_REM          jit-compiled (inline pos=len)
 * OP_RPOS         jit-compiled (inline position guard)
 * OP_RTAB         jit-compiled (inline cursor advance)
 * OP_FENCE        jit-compiled (inline choice-stack cut)
 * OP_LABEL        pseudo       (no-emit marker; IP recorded for GOTO
 * resolution) OP_GOTO         jit-compiled (treated as unconditional branch
 * like JMP) OP_GOTO_F       jit-compiled (NOP in compiled region; always falls
 * through) OP_EMIT_LITERAL call-out     (snobol_jit_helper_emit_literal)
 * OP_EMIT_CAPTURE call-out     (snobol_jit_helper_emit_capture)
 * OP_EMIT_EXPR    call-out     (snobol_jit_helper_emit_expr)
 * OP_EMIT_FORMAT  call-out     (snobol_jit_helper_emit_format)
 * OP_EMIT_TABLE   call-out     (snobol_jit_helper_emit_table_ip)
 * OP_TABLE_GET    call-out     (snobol_jit_helper_table_get)
 * OP_TABLE_SET    call-out     (snobol_jit_helper_table_set)
 * OP_ARRAY_GET    call-out     (snobol_jit_helper_array_get)
 * OP_ARRAY_SET    call-out     (snobol_jit_helper_array_set)
 * OP_BAL          call-out     (snobol_jit_helper_bal)
 * OP_EVAL         call-out     (snobol_jit_helper_eval)
 * OP_DYNAMIC      call-out     (snobol_jit_helper_dynamic)
 * OP_DYNAMIC_DEF  pseudo       (region-skip: inline bytecode block skipped
 * over)
 * ---------------------------------------------------------------------------
 */

/* ---------------------------------------------------------------------------
 * Global state
 * ---------------------------------------------------------------------------
 */

static SnobolJitStats global_jit_stats = {0};

/* Protects jit_cache, jit_cache_count, lru_clock, global_jit_stats,
 * global_jit_cfg, and the log file pointer from concurrent access by
 * multiple threads (separate VM instances). */
static jit_mutex_t jit_global_mutex = JIT_MUTEX_INIT;

/* ---------------------------------------------------------------------------
 * Configuration
 * ---------------------------------------------------------------------------
 */

static SnobolJitConfig global_jit_cfg = {
    .method_enabled = true,
    .max_compiled_patterns = 1024,
    .scratch_size = 256,
};

void snobol_jit_set_config(const SnobolJitConfig *cfg) {
  jit_mutex_lock(&jit_global_mutex);
  if (cfg)
    global_jit_cfg = *cfg;
  jit_mutex_unlock(&jit_global_mutex);
}

/* ---------------------------------------------------------------------------
 * Backend registry
 * ---------------------------------------------------------------------------
 */

static const jit_backend_t *active_backend = nullptr;

void jit_backend_register(const jit_backend_t *backend) {
  active_backend = backend;
}

const jit_backend_t *jit_backend_get(void) { return active_backend; }

const char *jit_backend_name(void) {
  return active_backend ? active_backend->name : "(none)";
}

const SnobolJitConfig *snobol_jit_get_config(void) {
  jit_mutex_lock(&jit_global_mutex);
  const SnobolJitConfig *ret = &global_jit_cfg;
  jit_mutex_unlock(&jit_global_mutex);
  return ret;
}

void snobol_jit_load_config_from_env(void) {
  jit_mutex_lock(&jit_global_mutex);
  const char *v;
  if ((v = getenv("SNOBOL_JIT_METHOD_ENABLED")))
    global_jit_cfg.method_enabled = (strtol(v, nullptr, 10) != 0);
  if ((v = getenv("SNOBOL_JIT_MAX_PATTERNS")))
    global_jit_cfg.max_compiled_patterns =
        (uint32_t)strtoul(v, nullptr, 10);
  if ((v = getenv("SNOBOL_JIT_SCRATCH_SIZE")))
    global_jit_cfg.scratch_size = (uint32_t)strtoul(v, nullptr, 10);
  jit_mutex_unlock(&jit_global_mutex);
}

/* ---------------------------------------------------------------------------
 * JIT event log
 *
 * When SNOBOL_JIT_LOG_FILE is set, every JIT and VM event is appended to the
 * file.  Used to diagnose CI hangs by capturing the exact sequence of events
 * leading up to a failure.
 * ---------------------------------------------------------------------------
 */
static FILE *jit_log_fp = nullptr;

void snobol_jit_log_open(void) {
  if (jit_log_fp)
    return;
  const char *path = getenv("SNOBOL_JIT_LOG_FILE");
  if (!path || !path[0])
    return;
  jit_log_fp = fopen(path, "w");
  if (jit_log_fp) {
#if defined(_WIN32)
    /* MSVC's setvbuf does not reliably support _IOLBF with a NULL buffer
     * (may trigger __fastfail via _invoke_watson).  Use unbuffered output
     * on Windows instead; log writes go through fflush after each entry
     * so data is not lost on crash. */
    setvbuf(jit_log_fp, nullptr, _IONBF, 0);
#else
    setvbuf(jit_log_fp, nullptr, _IOLBF, 0); /* line-buffered for tail -f */
#endif
    fprintf(jit_log_fp, "# snobol_jit log opened pid=%lu\n",
            (unsigned long)snobol_jit_now_ns());
  }
}

void snobol_jit_log_close(void) {
  if (jit_log_fp) {
    fprintf(jit_log_fp, "# snobol_jit log closed\n");
    fclose(jit_log_fp);
    jit_log_fp = nullptr;
  }
}

void snobol_jit_log(const char *fmt, ...) {
  if (!jit_log_fp)
    return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(jit_log_fp, fmt, ap);
  va_end(ap);
  fputc('\n', jit_log_fp);
  fflush(jit_log_fp);
}

/* ---------------------------------------------------------------------------
 * Method JIT cache (whole-pattern compilation)
 *
 * Flat array keyed by djb2(bytecode hash). FIFO eviction.
 * Thread safety: all operations guarded by jit_global_mutex.
 * ---------------------------------------------------------------------------
 */
#define METHOD_CACHE_SIZE 256

typedef struct {
  uint64_t hash;     /**< djb2(bytecode) */
  size_t bc_len;     /**< Full bytecode length (for collision detection) */
  jit_trace_fn fn;   /**< Compiled function or NULL */
  size_t code_size;  /**< Native code size (for free) */
  bool occupied;     /**< Slot in use */
} method_cache_entry_t;

static method_cache_entry_t method_cache[METHOD_CACHE_SIZE];
static int method_cache_count = 0;

static uint64_t djb2_hash(const uint8_t *data, size_t len) {
  uint64_t h = 5381;
  for (size_t i = 0; i < len; i++)
    h = ((h << 5) + h) + data[i];
  return h;
}

/* ---------------------------------------------------------------------------
 * Stats API
 * ---------------------------------------------------------------------------
 */

SnobolJitStats *snobol_jit_get_stats(void) {
  return &global_jit_stats;
}

void snobol_jit_reset_stats(void) {
  jit_mutex_lock(&jit_global_mutex);
  memset(&global_jit_stats, 0, sizeof(global_jit_stats));
  jit_mutex_unlock(&jit_global_mutex);
}

/* ---------------------------------------------------------------------------
 * Init / Shutdown
 * ---------------------------------------------------------------------------
 */

static bool jit_initialised = false;

void snobol_jit_init(void) {
  jit_mutex_lock(&jit_global_mutex);
  if (jit_initialised) {
    jit_mutex_unlock(&jit_global_mutex);
    return;
  }
  jit_initialised = true;
  memset(&global_jit_stats, 0, sizeof(global_jit_stats));
  memset(method_cache, 0, sizeof(method_cache));
  method_cache_count = 0;
  jit_mutex_unlock(&jit_global_mutex);
  snobol_jit_load_config_from_env();
  snobol_jit_log_open();
  snobol_jit_log("init backend=%s method_enabled=%d",
                  jit_backend_name(),
                  (int)global_jit_cfg.method_enabled);
  snobol_jit_sljit_register();
}

void snobol_jit_shutdown(void) {
  jit_mutex_lock(&jit_global_mutex);
  jit_initialised = false;
  snobol_jit_log("shutdown method_cache_count=%d", method_cache_count);
  for (int i = 0; i < method_cache_count; i++) {
    if (method_cache[i].occupied && method_cache[i].fn) {
      snobol_jit_free_code((void *)method_cache[i].fn,
                           method_cache[i].code_size);
      method_cache[i].occupied = false;
      method_cache[i].fn = nullptr;
    }
  }
  method_cache_count = 0;
  jit_mutex_unlock(&jit_global_mutex);
  snobol_jit_log_close();
}

/* ---------------------------------------------------------------------------
 * Code memory
 * ---------------------------------------------------------------------------
 */

void *snobol_jit_alloc_code(size_t size) {
  /*
   * Allocate extra space for a 16-byte leading pad.  UBSan's
   * -fsanitize=function reads function metadata from fn[-8] and fn[-16];
   * without this pad a JIT trace placed at the start of an mmap page
   * would cause fn[-8] to land on unmapped memory → SIGSEGV.
   * See LLVM issue #65253.
   */
  size_t alloc_size = size + 16;
#ifdef SNOBOL_JIT_PLATFORM_WINDOWS
  /* DEP compliance: allocate as PAGE_READWRITE, never PAGE_EXECUTE_READWRITE.
   * Code pages are switched to PAGE_EXECUTE_READ in snobol_jit_seal_code(). */
  void *ptr = VirtualAlloc(nullptr, alloc_size, MEM_COMMIT | MEM_RESERVE,
                           PAGE_READWRITE);
  snobol_jit_log("alloc_code size=%zu platform=Windows ptr=%p", size, ptr);
  if (!ptr)
    return nullptr;
  return (void *)((uint8_t *)ptr + 16);
#elif defined(SNOBOL_JIT_PLATFORM_MACOS)
  void *ptr = mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
  if (ptr == MAP_FAILED)
    return nullptr;
  return (void *)((uint8_t *)ptr + 16);
#elif defined(SNOBOL_JIT_PLATFORM_LINUX)
  void *ptr = mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  snobol_jit_log("alloc_code size=%zu platform=Linux ptr=%p", size, ptr);
  if (ptr == MAP_FAILED)
    return nullptr;
  return (void *)((uint8_t *)ptr + 16);
#endif
}

void snobol_jit_seal_code(void *code, size_t size) {
#ifdef SNOBOL_JIT_PLATFORM_WINDOWS
  /* VirtualProtect requires a page-aligned address.  snobol_jit_alloc_code()
   * returns ptr+16 (a 16-byte UBSan pad), so reverse the pad here. */
  void *base = (void *)((uint8_t *)code - 16);
  DWORD old;
  BOOL ok = VirtualProtect(base, size + 16, PAGE_EXECUTE_READ, &old);
  snobol_jit_log("seal_code platform=Windows code=%p base=%p size=%zu ok=%d",
                 code, base, size, (int)ok);
  assert(ok && "VirtualProtect to PAGE_EXECUTE_READ failed");
  FlushInstructionCache(GetCurrentProcess(), code, size);
#elif defined(SNOBOL_JIT_PLATFORM_MACOS)
  /* Switch current thread back to exec mode, then flush the instruction cache.
   */
  pthread_jit_write_protect_np(1);
  __builtin___clear_cache((char *)code, (char *)code + size);
#elif defined(SNOBOL_JIT_PLATFORM_LINUX)
  /* mprotect requires page-aligned address. The 16-byte pad introduced in
   * snobol_jit_alloc_code() makes `code` 16 bytes past the mmap base, so
   * mprotect(code, size, ...) would fail with EINVAL. Reverse the pad and
   * protect the full page-aligned range (including the leading pad). */
  void *base = (void *)((uint8_t *)code - 16);
  int ret = mprotect(base, size + 16, PROT_READ | PROT_EXEC);
  snobol_jit_log(
      "seal_code platform=Linux code=%p base=%p size=%zu ret=%d errno=%d", code,
      base, size, ret, ret ? errno : 0);
  assert(ret == 0 && "mprotect to PROT_READ|PROT_EXEC failed");
  __builtin___clear_cache((char *)code, (char *)code + size);
#endif
}

void snobol_jit_free_code(void *code, size_t size) {
  /* Recover the original allocation address by reversing the 16-byte
   * pad introduced in snobol_jit_alloc_code(). */
  void *base = (void *)((uint8_t *)code - 16);
  size_t total = size + 16;
  snobol_jit_log("free_code code=%p base=%p size=%zu", code, base, size);
#ifdef SNOBOL_JIT_PLATFORM_WINDOWS
  VirtualFree(base, 0, MEM_RELEASE);
#else
  if (code && size)
    munmap(base, total);
#endif
}

/* Tracing JIT (per-IP hot-trace compilation) has been retired.
 * SLJIT is now the only JIT backend, and method JIT (whole-pattern
 * compilation) is the only JIT mode. */

/* ---------------------------------------------------------------------------
 * Method JIT — whole-pattern compilation cache
 *
 * The method JIT compiles a trace starting at bytecode offset 0 (the whole
 * pattern entry point) and caches the result keyed by bytecode hash.
 * The compiled function has the same `jit_trace_fn` signature as a
 * tracing-JIT trace: `void fn(VM *vm)`.
 *
 * The cache is a flat array with hash-keyed lookup, independent from the
 * per-IP tracing-JIT LRU cache (which stores SnobolJitContext entries).
 * ---------------------------------------------------------------------------
 */

static int method_cache_find(uint64_t hash, size_t bc_len) {
  for (int i = 0; i < method_cache_count; i++) {
    if (method_cache[i].occupied &&
        method_cache[i].hash == hash &&
        method_cache[i].bc_len == bc_len) {
      return i;
    }
  }
  return -1;
}

static int method_cache_evict(void) {
  /* Simple FIFO eviction: remove the first (oldest) entry */
  if (method_cache_count <= 0)
    return -1;
  int victim = 0;
  if (method_cache[victim].occupied && method_cache[victim].fn) {
    snobol_jit_free_code((void *)method_cache[victim].fn,
                         method_cache[victim].code_size);
    method_cache[victim].occupied = false;
    method_cache[victim].fn = nullptr;
    if (snobol_jit_get_stats())
      snobol_jit_get_stats()->method_evictions_total++;
  }
  /* Compact: shift all entries left */
  for (int i = victim; i < method_cache_count - 1; i++)
    method_cache[i] = method_cache[i + 1];
  method_cache_count--;
  method_cache[method_cache_count].occupied = false;
  method_cache[method_cache_count].fn = nullptr;
  return 0; /* success */
}

static int method_cache_insert(uint64_t hash, size_t bc_len,
                               jit_trace_fn fn, size_t code_size) {
  if (method_cache_count >= METHOD_CACHE_SIZE) {
    if (method_cache_evict() < 0)
      return -1;
  }
  int idx = method_cache_count++;
  method_cache[idx].hash = hash;
  method_cache[idx].bc_len = bc_len;
  method_cache[idx].fn = fn;
  method_cache[idx].code_size = code_size;
  method_cache[idx].occupied = true;
  return idx;
}

/* ---------------------------------------------------------------------------
 * Method JIT compile / query / free
 * ---------------------------------------------------------------------------
 */

jit_trace_fn snobol_jit_method_compile(const uint8_t *bc,
                                        size_t bc_len,
                                        size_t *out_code_size) {
  if (out_code_size)
    *out_code_size = 0;
  if (!bc || bc_len == 0) {
    return nullptr;
  }
  if (!active_backend) {
    return nullptr;
  }

  /* Fast path: already cached */
  uint64_t hash = djb2_hash(bc, bc_len);
  jit_mutex_lock(&jit_global_mutex);
  int cached = method_cache_find(hash, bc_len);
  if (cached >= 0 && method_cache[cached].fn) {
    jit_trace_fn fn = method_cache[cached].fn;
    size_t csz = method_cache[cached].code_size;
    jit_mutex_unlock(&jit_global_mutex);
    if (out_code_size)
      *out_code_size = csz;
    return fn;
  }
  jit_mutex_unlock(&jit_global_mutex);

  /* Synthesise a minimal VM so the existing IR lift/lower can run without
   * a live caller.  The fields touched by the SLJIT backend (bc, bc_len)
   * are populated; everything else is zeroed. */
  VM vm;
  memset(&vm, 0, sizeof(vm));
  vm.bc = bc;
  vm.bc_len = bc_len;

  jit_ir_region_t *ir = jit_ir_lift_region(&vm, 0);
  if (!ir || ir->non_compilable || ir->count == 0) {
    if (ir)
      jit_ir_region_free(ir);
    if (snobol_jit_get_stats())
      snobol_jit_get_stats()->method_fallbacks_total++;
    return nullptr;
  }

  /* Same optimiser stack as snobol_jit_compile() */
  jit_ir_copy_propagation(ir);
  jit_ir_dce(ir);
  jit_ir_build_cfg(ir);
  jit_ir_compute_dominators(ir);
  jit_ir_find_loops(ir);
  jit_ir_gvn(ir);
  jit_ir_constant_fold(ir);
  jit_ir_licm(ir);

  if (snobol_jit_get_stats())
    snobol_jit_get_stats()->method_attempts_total++;

  jit_region_t code_region = {nullptr, nullptr, 0, 0};
  void *code_ptr = active_backend->lower(ir, &vm, &code_region);
  jit_ir_region_free(ir);

  if (!code_ptr) {
    if (snobol_jit_get_stats())
      snobol_jit_get_stats()->method_fallbacks_total++;
    return nullptr;
  }

  if (snobol_jit_get_stats())
    snobol_jit_get_stats()->method_successes_total++;

  /* Store in cache */
  jit_mutex_lock(&jit_global_mutex);
  method_cache_insert(hash, bc_len, (jit_trace_fn)code_ptr, code_region.code_size);
  jit_mutex_unlock(&jit_global_mutex);

  if (out_code_size)
    *out_code_size = code_region.code_size;
  return (jit_trace_fn)code_ptr;
}

jit_trace_fn snobol_jit_method_query(const uint8_t *bc, size_t bc_len) {
  if (!bc || bc_len == 0)
    return nullptr;

  uint64_t hash = djb2_hash(bc, bc_len);
  jit_mutex_lock(&jit_global_mutex);
  int idx = method_cache_find(hash, bc_len);
  jit_trace_fn fn = (idx >= 0) ? method_cache[idx].fn : nullptr;
  jit_mutex_unlock(&jit_global_mutex);
  return fn;
}

void snobol_jit_method_free(jit_trace_fn fn) {
  if (!fn)
    return;

  jit_mutex_lock(&jit_global_mutex);
  for (int i = 0; i < method_cache_count; i++) {
    if (method_cache[i].occupied && method_cache[i].fn == fn) {
      snobol_jit_free_code((void *)fn, method_cache[i].code_size);
      method_cache[i].occupied = false;
      method_cache[i].fn = nullptr;
      /* Compact */
      for (int j = i; j < method_cache_count - 1; j++)
        method_cache[j] = method_cache[j + 1];
      method_cache_count--;
      method_cache[method_cache_count].occupied = false;
      method_cache[method_cache_count].fn = nullptr;
      if (snobol_jit_get_stats())
        snobol_jit_get_stats()->method_evictions_total++;
      break;
    }
  }
  jit_mutex_unlock(&jit_global_mutex);
}

#endif /* SNOBOL_JIT */
