#ifndef SNOBOL_VM_H
#define SNOBOL_VM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Enable dynamic pattern and table support */
#define SNOBOL_DYNAMIC_PATTERN 1

/* Forward declare table type */
typedef struct snobol_table snobol_table_t;

#ifdef SNOBOL_DYNAMIC_PATTERN
/* Include dynamic pattern types */
#include "snobol_dynamic_pattern.h"
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
    OP_EMIT_EXPR,    // reg u8, expr_type u8 (New: for .upper(), .length() etc)
    
    /* Table-backed replacement opcodes */
    OP_EMIT_TABLE,   // table_id u16, key_reg u8 (lookup table[key] and emit)
    OP_EMIT_FORMAT,  // reg u8, format_type u8 (format capture: 1=upper, 2=lower, 3=length)
    
    /* Control flow opcodes for labelled patterns and goto-like transfers */
    OP_LABEL,      // label_id u16 (define a label target)
    OP_GOTO,       // label_id u16 (unconditional transfer to label)
    OP_GOTO_F,     // label_id u16 (transfer to label if last match failed)
    OP_TABLE_GET,  // table_id u16, key_reg u8, dest_reg u8 (lookup table[key])
    OP_TABLE_SET,  // table_id u16, key_reg u8, value_reg u8 (set table[key] = value)
    OP_DYNAMIC,    // pattern_reg u8 (evaluate dynamic pattern from register)
    OP_DYNAMIC_DEF, // len u32, bytecode... (define dynamic pattern bytecode block)
} OpCode;

#define MAX_LOOPS 16

/* Generic dynamic buffer */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} snobol_buf;

/* Callback for emission */
typedef void (*emit_cb)(const char *data, size_t len, void *udata);

// choice stack for backtracking (Legacy format)
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

#ifdef SNOBOL_PROFILE
    struct {
        uint64_t dispatch_count;
        uint64_t push_count;
        uint64_t pop_count;
        size_t max_depth;
    } profile;
#endif

#ifdef SNOBOL_JIT
    struct {
        uint64_t *ip_counts;
        uint64_t *op_counts;
        void **traces;
        bool enabled;
        struct SnobolJitStats *stats;
        struct SnobolJitContext *ctx;  /**< owning context; used for per-pattern profitability state */
    } jit;
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

#endif // SNOBOL_VM_H