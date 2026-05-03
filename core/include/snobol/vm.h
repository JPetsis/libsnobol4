#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Enable dynamic pattern and table support */
#define SNOBOL_DYNAMIC_PATTERN 1

/* Forward declare table type */
typedef struct snobol_table snobol_table_t;

#ifdef SNOBOL_DYNAMIC_PATTERN
/* Include dynamic pattern types */
#include "snobol/dynamic_pattern.h"
#endif

#ifdef SNOBOL_JIT
/* Forward declare JIT types to avoid circular dependency */
typedef struct SnobolJitStats SnobolJitStats;
typedef struct SnobolJitContext SnobolJitContext;
typedef struct SnobolJitConfig SnobolJitConfig;
/* jit_trace_fn will be defined in jit.h */
#endif

#define MAX_CAPS 64
#define MAX_VARS 64

typedef struct cp_range {
    uint32_t start;  // Start codepoint (inclusive)
    uint32_t end;    // End codepoint (inclusive)
} CpRange;

static inline int utf8_peek_next(const char *s, size_t len, size_t pos, uint32_t *out_cp, int *out_bytes) {
    if (pos >= len) return 0;
    unsigned char c = (unsigned char)s[pos];
    if (c < 0x80) {
        *out_cp = c; *out_bytes = 1; return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        if (pos + 1 >= len) return 0;
        *out_cp = ((c & 0x1F) << 6) | ((unsigned char)s[pos+1] & 0x3F);
        *out_bytes = 2; return 1;
    }
    if ((c & 0xF0) == 0xE0) {
        if (pos + 2 >= len) return 0;
        *out_cp = ((c & 0x0F) << 12) | (((unsigned char)s[pos+1] & 0x3F) << 6) | ((unsigned char)s[pos+2] & 0x3F);
        *out_bytes = 3; return 1;
    }
    if ((c & 0xF8) == 0xF0) {
        if (pos + 3 >= len) return 0;
        *out_cp = ((c & 0x07) << 18) | (((unsigned char)s[pos+1] & 0x3F) << 12) |
                  (((unsigned char)s[pos+2] & 0x3F) << 6) | ((unsigned char)s[pos+3] & 0x3F);
        *out_bytes = 4; return 1;
    }
    return 0;
}

/* helpers to read u32/u16/u8 from bc with bounds checking */
static inline uint32_t read_u32(const uint8_t *bc, size_t bc_len, size_t *ip) {
    if (*ip + 4 > bc_len) { *ip = bc_len; return 0; }
    uint32_t v = ((uint32_t)bc[*ip] << 24) | ((uint32_t)bc[*ip+1] << 16) | ((uint32_t)bc[*ip+2] << 8) | (uint32_t)bc[*ip+3];
    *ip += 4;
    return v;
}
static inline uint16_t read_u16(const uint8_t *bc, size_t bc_len, size_t *ip) {
    if (*ip + 2 > bc_len) { *ip = bc_len; return 0; }
    uint16_t v = ((uint16_t)bc[*ip] << 8) | ((uint16_t)bc[*ip+1]);
    *ip += 2;
    return v;
}
static inline uint8_t read_u8(const uint8_t *bc, size_t bc_len, size_t *ip) {
    if (*ip + 1 > bc_len) { *ip = bc_len; return 0; }
    uint8_t v = bc[(*ip)++];
    return v;
}

/* Bytecode opcodes */
typedef enum {
    OP_ACCEPT = 0,
    OP_FAIL,
    OP_JMP,
    OP_SPLIT,
    OP_LIT,       // offset (u32), len (u32)
    OP_ANY,       // charclass id (u16)
    OP_NOTANY,    // charclass id (u16)
    OP_SPAN,      // charclass id (u16)  -> match 1+
    OP_BREAK,     // charclass id (u16)  -> consume until char in set (0 or more)
    OP_CAP_START, // reg u8
    OP_CAP_END,   // reg u8
    OP_ASSIGN,    // var u16, reg u8
    OP_LEN,       // n u32 (match exactly n codepoints)
    OP_EVAL,      // fn u16, reg u8 (call back to PHP)
    OP_ANCHOR,    // type u8 (0=start, 1=end)
    OP_REPEAT_INIT, // loop_id u8, min u32, max u32, skip_target u32
    OP_REPEAT_STEP, // loop_id u8, jmp_target u32
    OP_EMIT_LITERAL, // offset u32, len u32 (Renamed from OP_EMIT_LIT)
    OP_EMIT_CAPTURE, // reg u8 (Renamed from OP_EMIT_REF)
    OP_EMIT_EXPR,    // LEGACY: reg u8, expr_type u8 (1=upper, 2=length) — use OP_EMIT_FORMAT in new code

    /* Table-backed replacement opcodes */
    /* OP_EMIT_TABLE encoding: table_id u16 (0xFFFF=unbound), key_type u8,
     *   name_len u8, name_bytes[name_len], then key payload:
     *     key_type=0 (literal): key_len u16, key_bytes[key_len]
     *     key_type=1 (capture): key_reg u8
     * Use snobol_template_bind_tables() to resolve 0xFFFF -> runtime table_id. */
    OP_EMIT_TABLE,
    /* OP_EMIT_FORMAT encoding: reg u8, format_type u8 (see SNBL_FMT_* constants)
     *   SNBL_FMT_LPAD and SNBL_FMT_RPAD also read: width u16, fill_char u8 */
    OP_EMIT_FORMAT,

    /* Control flow opcodes for labelled patterns and goto-like transfers */
    OP_LABEL,      // label_id u16 (define a label target)
    OP_GOTO,       // label_id u16 (unconditional transfer to label)
    OP_GOTO_F,     // label_id u16 (transfer to label if last match failed)
    OP_TABLE_GET,  // table_id u16, key_reg u8, dest_reg u8 (lookup table[key])
    OP_TABLE_SET,  // table_id u16, key_reg u8, value_reg u8 (set table[key] = value)
    OP_DYNAMIC,    // pattern_reg u8 (evaluate dynamic pattern from register)
    OP_DYNAMIC_DEF, // len u32, bytecode... (define dynamic pattern bytecode block)

    /* Pattern primitives */
    OP_BREAKX,     // charclass id (u16) – like BREAK but pushes retry choice for O(n) tokenization
    OP_BAL,        // open_cp u32, close_cp u32 – match balanced delimiter pair
    OP_FENCE,      // no args – cut choice stack (prevent backtracking past this point)
    OP_REM,        // no args – match remainder of subject to end
    OP_RPOS,       // n u32 – succeed only when cursor is n codepoints from end
    OP_RTAB,       // n u32 – advance cursor to n codepoints from end

    /* Optimizer no-op: emitted by the fusion pass to fill dead bytecode slots */
    OP_NOP,        // no operands – skip one byte
} OpCode;

/* --------------------------------------------------------------------------
 * OP_EMIT_FORMAT format_type discriminants (SNBL_FMT_*)
 *
 * Used by compile_template_to_bytecode() and the VM dispatch.
 * SNBL_FMT_LPAD and SNBL_FMT_RPAD have two extra operands:
 *   width:u16 (big-endian, capped at 1024) and fill_char:u8.
 * -------------------------------------------------------------------------- */
#define SNBL_FMT_UPPER   1  /**< ASCII uppercase                              */
#define SNBL_FMT_LOWER   2  /**< ASCII lowercase                              */
#define SNBL_FMT_LENGTH  3  /**< length as decimal string                     */
#define SNBL_FMT_LPAD    4  /**< left-pad to width with fill_char             */
#define SNBL_FMT_RPAD    5  /**< right-pad to width with fill_char            */

/** Sentinel table_id written by compile_template_to_bytecode() for any
 *  table reference that has not yet been bound to a runtime ID.
 *  Call snobol_template_bind_tables() to resolve these to real IDs. */
#define SNBL_TABLE_ID_UNBOUND  0xFFFFu

/* --------------------------------------------------------------------------
 * Built-in function dispatch enumeration
 *
 * These IDs are used with OP_EVAL to dispatch directly to C built-in
 * functions in the VM, bypassing the host eval_fn callback for lower latency.
 * IDs start at 1 (0 = SNOBOL_FN_NONE = host callback).
 * -------------------------------------------------------------------------- */
typedef enum {
    SNOBOL_FN_NONE       = 0,  /* Not a built-in: use eval_fn host callback */
    /* String transformation functions */
    SNOBOL_FN_SIZE       = 1,
    SNOBOL_FN_TRIM       = 2,
    SNOBOL_FN_DUPL       = 3,
    SNOBOL_FN_REVERSE    = 4,
    SNOBOL_FN_SUBSTR     = 5,
    SNOBOL_FN_REPLACE    = 6,
    SNOBOL_FN_REPLACE_CHAR = 7,
    SNOBOL_FN_LPAD       = 8,
    SNOBOL_FN_RPAD       = 9,
    SNOBOL_FN_CHAR       = 10,
    SNOBOL_FN_ORD        = 11,
    SNOBOL_FN_UPPER      = 12,
    SNOBOL_FN_LOWER      = 13,
    /* Comparison / type-check functions */
    SNOBOL_FN_IDENT      = 14,
    SNOBOL_FN_DIFFER     = 15,
    SNOBOL_FN_LEXEQ      = 16,
    SNOBOL_FN_LEXLT      = 17,
    SNOBOL_FN_LEXGT      = 18,
    SNOBOL_FN_INTEGER    = 19,
    SNOBOL_FN_REAL       = 20,
    SNOBOL_FN_NUMERIC    = 21,
    /* Sentinel – all IDs < SNOBOL_FN_MAX are known built-ins */
    SNOBOL_FN_MAX        = 22,
} snobol_builtin_fn_t;

#define MAX_LOOPS 16

/* Generic dynamic buffer */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} snobol_buf;

/* Callback for emission */
typedef void (*emit_cb)(const char *data, size_t len, void *udata);

/* --------------------------------------------------------------------------
 * Compact Choice Stack (delta/write-log based)
 * -------------------------------------------------------------------------- */

/* Write-log entry: records a single capture modification */
typedef struct {
    uint8_t cap_index;        /* Which capture register was modified */
    size_t  old_start;        /* Previous cap_start value */
    size_t  old_end;          /* Previous cap_end value */
} WriteLogEntry;

/* Compact choice record with delta data */
typedef struct {
    uint32_t total_size;      /* Total size of this record including trailing size */
    size_t  ip;               /* Instruction pointer to restore */
    size_t  pos;              /* Subject position to restore */
    size_t  var_count;        /* Snapshot of vm->var_count */
    uint8_t max_cap_used;     /* Number of captures tracked */
    uint8_t max_counter_used; /* Number of counters tracked */
    uint8_t write_log_count;  /* Number of write-log entries */
    uint8_t pad;              /* Padding for alignment */
    /* Followed by: counter snapshots (if max_counter_used > 0)
     * Followed by: loop_last_pos snapshots (max_counter_used * size_t)
     * Followed by: write-log entries (write_log_count entries)
     * Followed by: trailing uint32_t total_size */
} CompactChoiceHeader;

/* Legacy choice stack (full snapshots) */
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

typedef struct {
    const uint8_t *bc;
    size_t bc_len;

    const char *s;
    size_t len;    // bytes of s

    size_t ip;     // instruction pointer
    size_t pos;    // input byte index

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
    uint8_t max_cap_used;      // highest capture index used + 1 (0 = none used)
    uint8_t max_counter_used;  // highest counter index used + 1 (0 = none used)

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
    size_t choice_allocated;   /* Total bytes allocated for choice records (for stats) */
    size_t choice_push_count;  /* Total number of choice records pushed (for stats) */
    size_t choice_peak_depth;  /* Peak number of simultaneous choice points */
    size_t choice_peak_memory; /* Peak bytes used by the choice stack simultaneously */
    size_t choice_live_depth;  /* Current number of live (not yet popped) choice points */

    /* Write-log for compact choice stack: tracks capture modifications */
    WriteLogEntry *write_log;          /* Circular buffer of modification entries */
    size_t write_log_cap;              /* Allocated capacity (>= MAX_CAPS) */
    size_t write_log_next;             /* Next slot to use (circular) */
    uint64_t write_log_bitmap;         /* Bitmap: bit i set => entry i has valid data */
    size_t write_log_compressed_count; /* Count of entries when compressed at choice point */
    bool write_log_dirty;              /* True if write-log has un-compressed entries */

    // callback for EVAL: returns true if ok, false to cause fail
    bool (*eval_fn)(int fn_id, const char *s, size_t start, size_t end, void *udata);
    void *eval_udata;

    /* Control flow state for labelled patterns and goto-like transfers */
    uint16_t *label_offsets;  /* label_id -> bytecode offset */
    size_t label_count;         /* Number of defined labels */
    size_t label_capacity;      /* Allocated capacity */
    uint16_t current_label;     /* Current label being processed */
    bool in_goto_fail;         /* True if in GOTO_F failure handling */

#ifdef SNOBOL_DYNAMIC_PATTERN
    /* Dynamic pattern support */
    dynamic_pattern_cache_t *dyn_cache;  /* Dynamic pattern cache */
    snobol_table_t **tables;                     /* Table registry */
    size_t table_count;                          /* Number of registered tables */
    size_t table_capacity;                       /* Table registry capacity */
    char *dyn_pending_source;                    /* Pending dynamic pattern source text from OP_DYNAMIC_DEF */
    size_t dyn_pending_source_len;               /* Length of pending source text */
    uint8_t *dyn_pending_bc;                     /* Pending dynamic pattern bytecode from OP_DYNAMIC_DEF */
    size_t dyn_pending_bc_len;                   /* Length of pending bytecode */
#endif

#ifdef SNOBOL_JIT
    struct {
        uint64_t *ip_counts;
        uint64_t *op_counts;
        void **traces;
        bool enabled;
        bool search_mode; /**< true when VM is executing inside a search loop */
        struct SnobolJitStats *stats;
        struct SnobolJitContext *ctx;  /**< owning context; used for per-pattern profitability state */
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

const uint8_t *get_ranges_ptr(const VM *vm, uint16_t set_id, uint16_t *out_count, uint16_t *out_case);
bool ranges_to_ascii_bitmap(const uint8_t *ranges_ptr, size_t count, uint64_t map[2]);
bool range_contains(const uint8_t *ranges_ptr, size_t count, uint32_t cp);

static inline bool bitmap_test(const uint64_t map[2], uint8_t c) {
    if (c > 127) return false;
    if (c < 64) return (map[0] & (1ULL << c)) != 0;
    return (map[1] & (1ULL << (c - 64))) != 0;
}

/* VM entry */
bool vm_exec(VM *vm);
bool vm_run(VM *vm);

/* Control flow management */
void vm_init_labels(VM *vm);
void vm_free_labels(VM *vm);
bool vm_register_label(VM *vm, uint16_t label_id, uint32_t offset);
uint32_t vm_get_label_offset(VM *vm, uint16_t label_id);

#ifdef SNOBOL_DYNAMIC_PATTERN
/* Table registry */
void vm_init_tables(VM *vm);
void vm_free_tables(VM *vm);
bool vm_register_table(VM *vm, snobol_table_t *table, uint16_t *out_id);
snobol_table_t *vm_get_table(VM *vm, uint16_t table_id);
#endif

/* Buffer management */
void snobol_buf_init(snobol_buf *b);
void snobol_buf_append(snobol_buf *b, const char *data, size_t len);
void snobol_buf_clear(snobol_buf *b);
void snobol_buf_free(snobol_buf *b);

/* helpers */
void vm_push_choice(VM *vm, size_t ip, size_t pos);
bool vm_pop_choice(VM *vm);

/* Write-log management for compact choice stack */
void vm_write_log_init(VM *vm);
void vm_write_log_free(VM *vm);
void vm_write_log_clear(VM *vm);
void vm_write_log_track_cap_start(VM *vm, uint8_t cap, size_t old_start);
void vm_write_log_track_cap_end(VM *vm, uint8_t cap, size_t old_end);
size_t vm_write_log_count_entries(const VM *vm);
void vm_write_log_copy_entries(const VM *vm, WriteLogEntry *dst, size_t dst_cap);
void vm_write_log_restore(VM *vm, const CompactChoiceHeader *hdr);
size_t vm_compact_choice_record_size(const CompactChoiceHeader *hdr);

/* Choice stack statistics */
size_t vm_choice_stack_memory_usage(VM *vm);          /* Current bytes used by choice stack */
size_t vm_choice_record_average_size(VM *vm);         /* Average record size in bytes (from stats) */
size_t vm_choice_stack_depth(VM *vm);                 /* Number of choice points on stack */

