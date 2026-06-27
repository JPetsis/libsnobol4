#pragma once

/**
 * @file vm.h
 * @brief Virtual machine types, opcodes, and execution API for libsnobol4
 *
 * Defines the bytecode instruction set, VM state structure, and all
 * functions for executing compiled SNOBOL4 patterns.  This is an internal
 * API used by the language bindings and the public @c snobol.h wrapper.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Enable dynamic pattern and table support */
#define SNOBOL_DYNAMIC_PATTERN 1

/* Forward declare table and array types */
typedef struct snobol_table snobol_table_t;
typedef struct snobol_array snobol_array_t;

#ifdef SNOBOL_DYNAMIC_PATTERN
/* Include dynamic pattern types */
#include "snobol/dynamic_pattern.h"
#endif

#ifdef SNOBOL_JIT
/* Forward declare JIT types to avoid circular dependency */
typedef struct SnobolJitStats SnobolJitStats;
typedef struct SnobolJitConfig SnobolJitConfig;
#endif

/** @brief Maximum number of capture registers per VM execution. */
#define MAX_CAPS 64
/** @brief Maximum number of named variable registers per VM execution. */
#define MAX_VARS 64

/**
 * @brief Unicode codepoint range [start, end] (both inclusive).
 *
 * Used to represent character class ranges in the bytecode charclass tables.
 */
typedef struct cp_range {
  uint32_t start; /**< Start codepoint (inclusive) */
  uint32_t end;   /**< End codepoint (inclusive) */
} CpRange;

/**
 * @brief Decode the next UTF-8 codepoint from a byte string.
 *
 * @param[in]  s        Input string (UTF-8 bytes).
 * @param[in]  len      Byte length of @p s.
 * @param[in]  pos      Current byte offset in @p s.
 * @param[out] out_cp   Decoded codepoint.
 * @param[out] out_bytes Number of bytes consumed.
 * @return 1 on success, 0 if @p pos is at end or the sequence is invalid.
 */
static inline int utf8_peek_next(const char *s, size_t len, size_t pos,
                                 uint32_t *out_cp, int *out_bytes) {
  if (pos >= len)
    return 0;
  unsigned char c = (unsigned char)s[pos];
  if (c < 0x80) {
    *out_cp = c;
    *out_bytes = 1;
    return 1;
  }
  if ((c & 0xE0) == 0xC0) {
    if (pos + 1 >= len)
      return 0;
    *out_cp = ((c & 0x1F) << 6) | ((unsigned char)s[pos + 1] & 0x3F);
    *out_bytes = 2;
    return 1;
  }
  if ((c & 0xF0) == 0xE0) {
    if (pos + 2 >= len)
      return 0;
    *out_cp = ((c & 0x0F) << 12) | (((unsigned char)s[pos + 1] & 0x3F) << 6) |
              ((unsigned char)s[pos + 2] & 0x3F);
    *out_bytes = 3;
    return 1;
  }
  if ((c & 0xF8) == 0xF0) {
    if (pos + 3 >= len)
      return 0;
    *out_cp = ((c & 0x07) << 18) | (((unsigned char)s[pos + 1] & 0x3F) << 12) |
              (((unsigned char)s[pos + 2] & 0x3F) << 6) |
              ((unsigned char)s[pos + 3] & 0x3F);
    *out_bytes = 4;
    return 1;
  }
  return 0;
}

/** @brief Read a big-endian uint32 from bytecode, advancing @p ip by 4. Returns
 * 0 on overrun. */
static inline uint32_t read_u32(const uint8_t *bc, size_t bc_len, size_t *ip) {
  if (*ip + 4 > bc_len) {
    *ip = bc_len;
    return 0;
  }
  uint32_t v = ((uint32_t)bc[*ip] << 24) | ((uint32_t)bc[*ip + 1] << 16) |
               ((uint32_t)bc[*ip + 2] << 8) | (uint32_t)bc[*ip + 3];
  *ip += 4;
  return v;
}
/** @brief Read a big-endian uint16 from bytecode, advancing @p ip by 2. Returns
 * 0 on overrun. */
static inline uint16_t read_u16(const uint8_t *bc, size_t bc_len, size_t *ip) {
  if (*ip + 2 > bc_len) {
    *ip = bc_len;
    return 0;
  }
  uint16_t v = ((uint16_t)bc[*ip] << 8) | ((uint16_t)bc[*ip + 1]);
  *ip += 2;
  return v;
}
/** @brief Read a uint8 from bytecode, advancing @p ip by 1. Returns 0 on
 * overrun. */
static inline uint8_t read_u8(const uint8_t *bc, size_t bc_len, size_t *ip) {
  if (*ip + 1 > bc_len) {
    *ip = bc_len;
    return 0;
  }
  uint8_t v = bc[(*ip)++];
  return v;
}

/**
 * @brief Bytecode opcodes for the SNOBOL4 VM.
 *
 * Each opcode is followed by zero or more operand bytes as documented
 * in the inline comments.  Operands are big-endian.
 */
typedef enum {
  OP_ACCEPT = 0, /**< Pattern succeeds; no operands */
  OP_FAIL,       /**< Pattern fails unconditionally; no operands */
  OP_JMP,        /**< Unconditional jump; target: u32 */
  OP_SPLIT,      /**< Non-deterministic branch; target_a: u32, target_b: u32 */
  OP_LIT,        /**< Match literal bytes; offset: u32, len: u32 */
  OP_ANY,        /**< Match char in class; charclass id: u16 */
  OP_NOTANY,     /**< Match char NOT in class; charclass id: u16 */
  OP_SPAN,       /**< Match 1+ chars in class; charclass id: u16 */
  OP_BREAK,     /**< Consume until char in set (0 or more); charclass id: u16 */
  OP_CAP_START, /**< Begin capture region; reg: u8 */
  OP_CAP_END,   /**< End capture region; reg: u8 */
  OP_ASSIGN,    /**< Assign capture register to variable; var: u16, reg: u8 */
  OP_LEN,       /**< Match exactly n codepoints; n: u32 */
  OP_EVAL,      /**< Call back to host; fn: u16, reg: u8 */
  OP_ANCHOR,    /**< Position anchor; type: u8 (0=start, 1=end) */
  OP_REPEAT_INIT,  /**< Begin bounded repetition; loop_id: u8, min: u32, max:
                      u32, skip_target: u32 */
  OP_REPEAT_STEP,  /**< Step bounded repetition; loop_id: u8, jmp_target: u32 */
  OP_EMIT_LITERAL, /**< Append literal to output; offset: u32, len: u32 */
  OP_EMIT_CAPTURE, /**< Append capture register to output; reg: u8 */
  OP_EMIT_EXPR, /**< LEGACY: emit with expression; reg: u8, expr_type: u8 — use
                   OP_EMIT_FORMAT in new code */

  /* Table-backed replacement opcodes */
  OP_EMIT_TABLE,  /**< Table-backed lookup emit; see vm.h inline encoding doc */
  OP_EMIT_FORMAT, /**< Formatted capture emit; reg: u8, format_type: u8 (see
                     SNBL_FMT_*) */

  /* Control flow opcodes */
  OP_LABEL,     /**< Define label target; label_id: u16 */
  OP_GOTO,      /**< Unconditional label jump; label_id: u16 */
  OP_GOTO_F,    /**< Jump to label if last match failed; label_id: u16 */
  OP_TABLE_GET, /**< Lookup: dest_reg = table[key_reg]; table_id: u16, key_reg:
                   u8, dest_reg: u8, name_len: u8, name: bytes[name_len] */
  OP_TABLE_SET, /**< Update: table[key_reg] = value_reg; table_id: u16, key_reg:
                   u8, value_reg: u8, name_len: u8, name: bytes[name_len] */
  OP_ARRAY_GET, /**< Lookup: dest_reg = array[key_reg]; array_id: u16, key_reg:
                   u8, dest_reg: u8, name_len: u8, name: bytes[name_len] */
  OP_ARRAY_SET, /**< Update: array[key_reg] = value_reg; array_id: u16, key_reg:
                   u8, value_reg: u8, name_len: u8, name: bytes[name_len] */
  OP_DYNAMIC, /**< Evaluate dynamic pattern from pending definition; no operands
               */
  OP_DYNAMIC_DEF, /**< Define inline dynamic pattern block; len: u32,
                     bytecode... */

  /* Pattern primitives */
  OP_BREAKX, /**< BREAK with retry choice for O(n) tokenization; charclass id:
                u16 */
  OP_BAL,    /**< Match balanced delimiter pair; open_cp: u32, close_cp: u32 */
  OP_FENCE, /**< Cut choice stack (no backtrack past this point); no operands */
  OP_REM,   /**< Match remainder of subject to end; no operands */
  OP_RPOS,  /**< Succeed when cursor is n codepoints from end; n: u32 */
  OP_RTAB,  /**< Advance cursor to n codepoints from end; n: u32 */
  OP_POS, /**< Succeed when cursor is exactly n codepoints from start; n: u32 */
  OP_TAB, /**< Advance cursor to n codepoints from start; n: u32 */
  OP_ABORT,   /**< Terminate entire match immediately; no operands */
  OP_SUCCEED, /**< Force immediate match success at current position; no
                 operands */

  OP_NOP, /**< No-op — skip one byte (emitted by fusion pass) */
} OpCode;

/* --------------------------------------------------------------------------
 * OP_EMIT_FORMAT format_type discriminants (SNBL_FMT_*)
 *
 * Used by compile_template_to_bytecode() and the VM dispatch.
 * SNBL_FMT_LPAD and SNBL_FMT_RPAD have two extra operands:
 *   width:u16 (big-endian, capped at 1024) and fill_char:u8.
 * -------------------------------------------------------------------------- */
#define SNBL_FMT_UPPER 1  /**< ASCII uppercase                              */
#define SNBL_FMT_LOWER 2  /**< ASCII lowercase                              */
#define SNBL_FMT_LENGTH 3 /**< length as decimal string                     */
#define SNBL_FMT_LPAD 4   /**< left-pad to width with fill_char             */
#define SNBL_FMT_RPAD 5   /**< right-pad to width with fill_char            */

/** Sentinel table_id written by compile_template_to_bytecode() for any
 *  table reference that has not yet been bound to a runtime ID.
 *  Call snobol_template_bind_tables() to resolve these to real IDs. */
#define SNBL_TABLE_ID_UNBOUND 0xFFFFu

/* --------------------------------------------------------------------------
 * Built-in function dispatch enumeration
 *
 * These IDs are used with OP_EVAL to dispatch directly to C built-in
 * functions in the VM, bypassing the host eval_fn callback for lower latency.
 * IDs start at 1 (0 = SNOBOL_FN_NONE = host callback).
 * -------------------------------------------------------------------------- */
/**
 * @brief Built-in function dispatch IDs for direct C dispatch via @c OP_EVAL.
 *
 * IDs start at 1; ID 0 (@c SNOBOL_FN_NONE) means "host callback".
 * All IDs < @c SNOBOL_FN_MAX are recognised built-ins.
 */
typedef enum {
  SNOBOL_FN_NONE = 0, /**< Not a built-in: use eval_fn host callback */
  /* String transformation functions */
  SNOBOL_FN_SIZE = 1,         /**< @see snobol_size() */
  SNOBOL_FN_TRIM = 2,         /**< @see snobol_trim() */
  SNOBOL_FN_DUPL = 3,         /**< @see snobol_dupl() */
  SNOBOL_FN_REVERSE = 4,      /**< @see snobol_reverse() */
  SNOBOL_FN_SUBSTR = 5,       /**< @see snobol_substr() */
  SNOBOL_FN_REPLACE = 6,      /**< @see snobol_replace() */
  SNOBOL_FN_REPLACE_CHAR = 7, /**< @see snobol_replace_char() */
  SNOBOL_FN_LPAD = 8,         /**< @see snobol_lpad() */
  SNOBOL_FN_RPAD = 9,         /**< @see snobol_rpad() */
  SNOBOL_FN_CHAR = 10,        /**< @see snobol_char_fn() */
  SNOBOL_FN_ORD = 11,         /**< @see snobol_ord() */
  SNOBOL_FN_UPPER = 12,       /**< @see snobol_upper() */
  SNOBOL_FN_LOWER = 13,       /**< @see snobol_lower() */
  /* Comparison / type-check functions */
  SNOBOL_FN_IDENT = 14,   /**< @see snobol_ident() */
  SNOBOL_FN_DIFFER = 15,  /**< @see snobol_differ() */
  SNOBOL_FN_LEXEQ = 16,   /**< @see snobol_lexeq() */
  SNOBOL_FN_LEXLT = 17,   /**< @see snobol_lexlt() */
  SNOBOL_FN_LEXGT = 18,   /**< @see snobol_lexgt() */
  SNOBOL_FN_INTEGER = 19, /**< @see snobol_integer() */
  SNOBOL_FN_REAL = 20,    /**< @see snobol_real() */
  SNOBOL_FN_NUMERIC = 21, /**< @see snobol_numeric() */
  /* Numeric comparison functions (multi-arg, routed to host eval_fn) */
  SNOBOL_FN_EQ = 22, /**< @see snobol_eq() */
  SNOBOL_FN_NE = 23, /**< @see snobol_ne() */
  SNOBOL_FN_LT = 24, /**< @see snobol_lt() */
  SNOBOL_FN_GT = 25, /**< @see snobol_gt() */
  SNOBOL_FN_LE = 26, /**< @see snobol_le() */
  SNOBOL_FN_GE = 27, /**< @see snobol_ge() */
  SNOBOL_FN_MAX =
      28, /**< Sentinel: all IDs < SNOBOL_FN_MAX are known built-ins */
} snobol_builtin_fn_t;

/** @brief Maximum number of bounded-repetition loop counters per VM. */
#define MAX_LOOPS 16

/**
 * @brief Generic growable byte buffer used for VM output accumulation.
 */
typedef struct {
  char *data; /**< Buffer data pointer (heap-allocated, may be NULL). */
  size_t len; /**< Current used byte count. */
  size_t cap; /**< Allocated capacity in bytes. */
} snobol_buf;

/** @brief Callback invoked by @c OP_EMIT_* instructions to stream output. */
typedef void (*emit_cb)(const char *data, size_t len, void *udata);

/**
 * @brief Write-log entry recording a single capture register modification.
 *
 * Used by the compact choice stack to reconstruct capture state on backtrack.
 */
typedef struct {
  uint8_t cap_index; /**< Index of the capture register that was modified */
  size_t old_start;  /**< Previous cap_start value */
  size_t old_end;    /**< Previous cap_end value */
} WriteLogEntry;

/**
 * @brief Header for a compact choice stack record (delta/write-log based).
 *
 * Followed in memory by: counter snapshots, loop_last_pos snapshots,
 * write-log entries, and a trailing uint32_t copy of total_size.
 */
typedef struct {
  uint32_t total_size; /**< Total size of this record including trailing size */
  size_t ip;           /**< Instruction pointer to restore */
  size_t pos;          /**< Subject position to restore */
  size_t var_count;    /**< Snapshot of vm->var_count */
  uint8_t max_cap_used;     /**< Number of captures tracked */
  uint8_t max_counter_used; /**< Number of counters tracked */
  uint8_t write_log_count;  /**< Number of write-log entries */
  uint8_t pad;              /**< Padding for alignment */
} CompactChoiceHeader;

/**
 * @brief Legacy full-snapshot choice stack entry.
 *
 * Used when @c use_compact_choice is false (SNOBOL_LEGACY_CHOICE=1).
 * Stores a complete copy of all capture, variable, and counter state.
 */
struct choice {
  size_t ip;
  size_t pos;
  size_t cap_start_snapshot[MAX_CAPS];
  size_t cap_end_snapshot[MAX_CAPS];
  size_t var_count_snapshot;
  uint32_t counters_snapshot[MAX_LOOPS];
  size_t loop_last_pos_snapshot[MAX_LOOPS];
  uint8_t max_cap_used_snapshot;
  uint8_t max_counter_used_snapshot;
};

/**
 * @brief VM state: all mutable state for a single pattern execution.
 *
 * Callers initialise the @c bc, @c bc_len, @c s, @c len fields and any
 * desired callbacks, then call vm_exec() or vm_run().
 * All heap allocations within the struct are managed by vm_push_choice(),
 * vm_pop_choice(), vm_write_log_init(), and the buffer helpers.
 */
typedef struct {
  const uint8_t *bc; /**< Compiled bytecode pointer (not owned). */
  size_t bc_len;     /**< Bytecode length in bytes. */

  const char *s;
  size_t len; // bytes of s

  size_t ip;  // instruction pointer
  size_t pos; // input byte index

  // captures (byte offsets)
  size_t cap_start[MAX_CAPS];
  size_t cap_end[MAX_CAPS];

  // named vars (filled by ASSIGN): start/end pairs
  size_t var_start[MAX_VARS];
  size_t var_end[MAX_VARS];
  size_t var_count;

  // loop counters
  uint32_t counters[MAX_LOOPS];
  uint32_t loop_min[MAX_LOOPS];
  uint32_t loop_max[MAX_LOOPS];
  size_t loop_last_pos[MAX_LOOPS];

  // optimization: track which captures/counters are actually used
  uint8_t max_cap_used;     // highest capture index used + 1 (0 = none used)
  uint8_t max_counter_used; // highest counter index used + 1 (0 = none used)

  // output buffer
  snobol_buf *out;

  // emit callback
  emit_cb emit_fn;
  void *emit_udata;

  // choice stack for backtracking
  void *choices;
  size_t choices_cap;
  size_t choices_top;
  bool use_compact_choice;
  size_t choice_allocated; /* Total bytes allocated for choice records (for
                              stats) */
  size_t
      choice_push_count; /* Total number of choice records pushed (for stats) */
  size_t choice_peak_depth;  /* Peak number of simultaneous choice points */
  size_t choice_peak_memory; /* Peak bytes used by the choice stack
                                simultaneously */
  size_t choice_live_depth;  /* Current number of live (not yet popped) choice
                                points */

  /* Write-log for compact choice stack: tracks capture modifications */
  WriteLogEntry *write_log;  /* Circular buffer of modification entries */
  size_t write_log_cap;      /* Allocated capacity (>= MAX_CAPS) */
  size_t write_log_next;     /* Next slot to use (circular) */
  uint64_t write_log_bitmap; /* Bitmap: bit i set => entry i has valid data */
  size_t write_log_compressed_count; /* Count of entries when compressed at
                                        choice point */
  bool write_log_dirty; /* True if write-log has un-compressed entries */

  // callback for EVAL: returns true if ok, false to cause fail
  bool (*eval_fn)(int fn_id, const char *s, size_t start, size_t end,
                  void *udata);
  void *eval_udata;

  /* Control flow state for labelled patterns and goto-like transfers */
  uint16_t *label_offsets; /* label_id -> bytecode offset */
  size_t label_count;      /* Number of defined labels */
  size_t label_capacity;   /* Allocated capacity */
  uint16_t current_label;  /* Current label being processed */
  bool in_goto_fail;       /* True if in GOTO_F failure handling */
  bool abort_flag;         /* True if ABORT opcode was executed */
  bool keep_choices; /* If true, vm_run preserves choice stack across calls */

#ifdef SNOBOL_DYNAMIC_PATTERN
  /* Dynamic pattern support */
  dynamic_pattern_cache_t *dyn_cache; /* Dynamic pattern cache */
  snobol_table_t **tables;            /* Table registry */
  size_t table_count;                 /* Number of registered tables */
  size_t table_capacity;              /* Table registry capacity */
  snobol_array_t **arrays;            /* Array registry */
  size_t array_count;                 /* Number of registered arrays */
  size_t array_capacity;              /* Array registry capacity */
  char *dyn_pending_source;      /* Pending dynamic pattern source text from
                                    OP_DYNAMIC_DEF */
  size_t dyn_pending_source_len; /* Length of pending source text */
  uint8_t *
      dyn_pending_bc; /* Pending dynamic pattern bytecode from OP_DYNAMIC_DEF */
  size_t dyn_pending_bc_len; /* Length of pending bytecode */
#endif

#ifdef SNOBOL_JIT
  struct {
    bool enabled;
    struct SnobolJitStats *stats;
  } jit;
#endif

#ifdef SNOBOL_PROFILE
  struct {
    uint64_t dispatch_count;
    uint64_t push_count;
    uint64_t pop_count;
    size_t max_depth;
  } profile;
#endif
} VM;

/**
 * @brief Retrieve charclass range data for a set ID.
 * @param[in]  vm         VM instance (for bytecode range table).
 * @param[in]  set_id     Charclass set ID from the bytecode.
 * @param[out] out_count  Number of ranges returned.
 * @param[out] out_case   Non-zero if set is case-insensitive.
 * @return Pointer to packed range data, or NULL if set_id is out of range.
 */
const uint8_t *get_ranges_ptr(const VM *vm, uint16_t set_id,
                              uint16_t *out_count, uint16_t *out_case);

/**
 * @brief Build a 128-bit ASCII bitmap from range data; returns false if any
 * range exceeds 127.
 * @param[in]  ranges_ptr  Range data from get_ranges_ptr().
 * @param[in]  count       Number of ranges.
 * @param[out] map         Output 2×uint64 bitmap, one bit per ASCII code.
 * @return true if all codepoints fit in ASCII; false otherwise.
 */
bool ranges_to_ascii_bitmap(const uint8_t *ranges_ptr, size_t count,
                            uint64_t map[2]);

/**
 * @brief Test whether a codepoint is contained in a packed range array.
 * @param[in] ranges_ptr  Range data from get_ranges_ptr().
 * @param[in] count       Number of ranges.
 * @param[in] cp          Codepoint to test.
 * @return true if @p cp falls within any range.
 */
bool range_contains(const uint8_t *ranges_ptr, size_t count, uint32_t cp);

/** @brief Test bit @p c in a 128-bit ASCII bitmap. Returns false for c > 127.
 */
static inline bool bitmap_test(const uint64_t map[2], uint8_t c) {
  if (c > 127)
    return false;
  if (c < 64)
    return (map[0] & (1ULL << c)) != 0;
  return (map[1] & (1ULL << (c - 64))) != 0;
}

/* VM entry */
/**
 * @brief Execute a single pass of VM bytecode (used internally by vm_run).
 * @param[in,out] vm  VM state.
 * @return true if the pattern matched; false on failure.
 */
bool vm_exec(VM *vm);

/**
 * @brief Run the VM, re-executing on backtrack until accept or exhausted.
 * @param[in,out] vm  VM state.
 * @return true if the pattern matched; false if all paths failed.
 */
bool vm_run(VM *vm);

/* Control flow management */
/** @brief Allocate label offset table; call before vm_run(). */
void vm_init_labels(VM *vm);
/** @brief Free label offset table. */
void vm_free_labels(VM *vm);
/** @brief Register a label at a given bytecode offset. */
bool vm_register_label(VM *vm, uint16_t label_id, uint32_t offset);
/** @brief Retrieve the bytecode offset for a label; returns UINT32_MAX if not
 * found. */
uint32_t vm_get_label_offset(VM *vm, uint16_t label_id);

#ifdef SNOBOL_DYNAMIC_PATTERN
/* Table registry */
/** @brief Initialise the VM's table registry. */
void vm_init_tables(VM *vm);
/** @brief Free the VM's table registry. */
void vm_free_tables(VM *vm);
/** @brief Register a table and return its runtime ID via @p out_id. */
bool vm_register_table(VM *vm, snobol_table_t *table, uint16_t *out_id);
/** @brief Look up a table by runtime ID; returns NULL if not found. */
snobol_table_t *vm_get_table(VM *vm, uint16_t table_id);
/** @brief Initialise the VM's array registry. */
void vm_init_arrays(VM *vm);
/** @brief Free the VM's array registry. */
void vm_free_arrays(VM *vm);
/** @brief Register an array and return its runtime ID via @p out_id. */
bool vm_register_array(VM *vm, snobol_array_t *array, uint16_t *out_id);
/** @brief Look up an array by runtime ID; returns NULL if not found. */
snobol_array_t *vm_get_array(VM *vm, uint16_t array_id);
#endif

/* Buffer management */
/** @brief Initialise a snobol_buf to empty state (no allocation). */
void snobol_buf_init(snobol_buf *b);
/** @brief Append @p len bytes from @p data to the buffer, growing as needed. */
void snobol_buf_append(snobol_buf *b, const char *data, size_t len);
/** @brief Reset buffer length to 0 (keeps allocation). */
void snobol_buf_clear(snobol_buf *b);
/** @brief Free buffer memory and reset to empty state. */
void snobol_buf_free(snobol_buf *b);

/* helpers */
/** @brief Push a backtracking choice point onto the VM's choice stack. */
void vm_push_choice(VM *vm, size_t ip, size_t pos);
/** @brief Pop the most recent choice point; return false if stack is empty. */
bool vm_pop_choice(VM *vm);

/** @brief Reset VM state between match attempts while keeping
 * choice/allocation. Clears captures, counters, and rewinds the choice stack.
 *  The caller is responsible for setting ip/pos to appropriate values. */
void snobol_vm_reset(VM *vm);

/* Write-log management for compact choice stack */
/** @brief Allocate write-log circular buffer. */
void vm_write_log_init(VM *vm);
/** @brief Free write-log buffer. */
void vm_write_log_free(VM *vm);
/** @brief Clear all write-log entries without freeing memory. */
void vm_write_log_clear(VM *vm);
/** @brief Track an old cap_start value before overwriting register @p cap. */
void vm_write_log_track_cap_start(VM *vm, uint8_t cap, size_t old_start);
/** @brief Track an old cap_end value before overwriting register @p cap. */
void vm_write_log_track_cap_end(VM *vm, uint8_t cap, size_t old_end);
/** @brief Return the current number of valid write-log entries. */
size_t vm_write_log_count_entries(const VM *vm);
/** @brief Copy write-log entries into a caller-provided buffer. */
void vm_write_log_copy_entries(const VM *vm, WriteLogEntry *dst,
                               size_t dst_cap);
/** @brief Restore capture state from a compact choice record header. */
void vm_write_log_restore(VM *vm, const CompactChoiceHeader *hdr);
/** @brief Return the total size in bytes of a compact choice record. */
size_t vm_compact_choice_record_size(const CompactChoiceHeader *hdr);

/* Choice stack statistics */
/** @brief Return the current bytes used by the choice stack. */
size_t vm_choice_stack_memory_usage(VM *vm);
/** @brief Return the average choice record size in bytes (computed from
 * accumulated stats). */
size_t vm_choice_record_average_size(VM *vm);
/** @brief Return the current number of live choice points on the stack. */
size_t vm_choice_stack_depth(VM *vm);
