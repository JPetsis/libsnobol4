#ifndef SNOBOL_VM_H
#define SNOBOL_VM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_CAPS 64
#define MAX_VARS 64
#define CHARCLASS_BITMAP_BYTES 16 /* 128 bits / 8 for character class bitmaps */

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
    OP_EMIT_LIT,    // offset u32, len u32
    OP_EMIT_REF,    // reg u8
} OpCode;

#define MAX_LOOPS 16

/* Callback for emission */
typedef void (*emit_cb)(const char *data, size_t len, void *udata);

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

    // emit callback
    emit_cb emit_fn;
    void *emit_udata;

    // choice stack for backtracking
    struct choice {
        size_t ip;
        size_t pos;
        // minimal snapshot of captures that might have changed: store changed reg id count and entries
        // For simplicity we snapshot all captures here (safe but heavier).
        size_t cap_start_snapshot[MAX_CAPS];
        size_t cap_end_snapshot[MAX_CAPS];
        size_t var_count_snapshot;
        uint32_t counters_snapshot[MAX_LOOPS];
    } *choices;
    size_t choices_cap;
    size_t choices_top;

    // callback for EVAL: returns true if ok, false to cause fail
    bool (*eval_fn)(int fn_id, const char *s, size_t start, size_t end, void *udata);
    void *eval_udata;
} VM;

/* VM entry */
bool vm_exec(VM *vm);

/* helpers */
void vm_push_choice(VM *vm, size_t ip, size_t pos);
bool vm_pop_choice(VM *vm);

#endif // SNOBOL_VM_H
