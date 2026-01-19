#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "snobol_jit.h"

#ifdef SNOBOL_JIT

#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stddef.h>

void snobol_jit_init(void) {
    fprintf(stderr, "[SNOBOL JIT] ARM64 Micro-JIT Initialized\n");
}

void snobol_jit_shutdown(void) {
}

void *snobol_jit_alloc_code(size_t size) {
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (ptr == MAP_FAILED) ? NULL : ptr;
}

void snobol_jit_seal_code(void *code, size_t size) {
    mprotect(code, size, PROT_READ | PROT_EXEC);
    __builtin___clear_cache((char*)code, (char*)code + size);
}

void snobol_jit_free_code(void *code, size_t size) {
    munmap(code, size);
}

/* ARM64 instruction helpers */
#define A64_RET 0xd65f03c0
#define A64_LDR_X_X_IMM(rt, rn, imm) (0xf9400000 | ((rt) & 31) | (((rn) & 31) << 5) | (((imm) / 8) << 10))
#define A64_STR_X_X_IMM(rt, rn, imm) (0xf9000000 | ((rt) & 31) | (((rn) & 31) << 5) | (((imm) / 8) << 10))
#define A64_ADD_X_X_X(rd, rn, rm)    (0x8b000000 | ((rd) & 31) | (((rn) & 31) << 5) | (((rm) & 31) << 16))
#define A64_CMP_X_X(rn, rm)          (0xeb00001f | (((rn) & 31) << 5) | (((rm) & 31) << 16))
#define A64_CMP_W_W(rn, rm)          (0x6b00001f | (((rn) & 31) << 5) | (((rm) & 31) << 16))
#define A64_B_GE(imm)                (0x5400000a | (((imm) & 0x3ffff) << 5))
#define A64_B(imm)                   (0x14000000 | ((imm) & 0x3ffffff))
#define A64_LDRB_W_X_X(rt, rn, rm)   (0x38606800 | ((rt) & 31) | (((rn) & 31) << 5) | (((rm) & 31) << 16))
#define A64_MOV_X_IMM(rd, imm)       (0xd2800000 | ((rd) & 31) | (((imm) & 0xffff) << 5))
#define A64_MOV_W_IMM(rd, imm)       (0x52800000 | ((rd) & 31) | (((imm) & 0xffff) << 5))
#define A64_MOVK_X_IMM_LSL16(rd, imm) (0xf2a00000 | ((rd) & 31) | (((imm) & 0xffff) << 5))
#define A64_MOVK_X_IMM_LSL32(rd, imm) (0xf2c00000 | ((rd) & 31) | (((imm) & 0xffff) << 5))
#define A64_MOVK_X_IMM_LSL48(rd, imm) (0xf2e00000 | ((rd) & 31) | (((imm) & 0xffff) << 5))
#define A64_TBZ(rt, bit, imm)        (0xb6000000 | ((rt) & 31) | (((bit) & 0x1f) << 19) | ((((bit) >> 5) & 1) << 31) | (((imm) & 0x3fff) << 5))
#define A64_TBNZ(rt, bit, imm)       (0xb7000000 | ((rt) & 31) | (((bit) & 0x1f) << 19) | ((((bit) >> 5) & 1) << 31) | (((imm) & 0x3fff) << 5))
#define A64_AND_W_W_W(rd, rn, rm)    (0x0a000000 | ((rd) & 31) | (((rn) & 31) << 5) | (((rm) & 31) << 16))
#define A64_LSR_X_X_X(rd, rn, rm)    (0x9ac02000 | ((rd) & 31) | (((rn) & 31) << 5) | (((rm) & 31) << 16))
#define A64_B_EQ(imm)                (0x54000000 | (((imm) & 0x3ffff) << 5))
#define A64_B_NE(imm)                (0x54000001 | (((imm) & 0x3ffff) << 5))
#define A64_B_HI(imm)                (0x54000008 | (((imm) & 0x3ffff) << 5))

typedef struct {
    uint32_t *p;
    uint32_t *code_start;
    size_t code_size;
} JITState;

typedef struct {
    uint32_t *instr_p;
    size_t ip;
} FailPatch;

static void emit_instr(JITState *js, uint32_t ins) {
    *(js->p)++ = ins;
}

static void emit_mov_x64(JITState *js, int rd, uint64_t val) {
    emit_instr(js, A64_MOV_X_IMM(rd, (uint32_t)(val & 0xffff)));
    emit_instr(js, A64_MOVK_X_IMM_LSL16(rd, (uint32_t)((val >> 16) & 0xffff)));
    emit_instr(js, A64_MOVK_X_IMM_LSL32(rd, (uint32_t)((val >> 32) & 0xffff)));
    emit_instr(js, A64_MOVK_X_IMM_LSL48(rd, (uint32_t)((val >> 48) & 0xffff)));
}

static void emit_mov_w32(JITState *js, int rd, uint32_t val) {
    emit_instr(js, A64_MOV_W_IMM(rd, val & 0xffff));
    if (val > 0xffff) {
        emit_instr(js, 0x72a00000 | (rd & 31) | ((val >> 16) << 5)); 
    }
}

static void emit_ldrb_w_x_imm(JITState *js, int rt, int rn, uint32_t imm) {
    emit_instr(js, 0x39400000 | (rt & 31) | ((rn & 31) << 5) | (imm << 10));
}

static void emit_strb_w_x_imm(JITState *js, int rt, int rn, uint32_t imm) {
    emit_instr(js, 0x39000000 | (rt & 31) | ((rn & 31) << 5) | (imm << 10));
}

static void emit_ubfx_w(JITState *js, int rd, int rn, int lsb, int width) {
    emit_instr(js, 0x53000000 | (rd & 31) | ((rn & 31) << 5) | (lsb << 10) | ((lsb + width - 1) << 16));
}

static void emit_patch_b_cond(uint32_t *p, uint32_t *target, uint32_t base_opcode) {
    intptr_t diff = target - p;
    *p = base_opcode | (((uint32_t)diff & 0x3ffff) << 5);
}

static void emit_patch_b(uint32_t *p, uint32_t *target) {
    intptr_t diff = target - p;
    *p = A64_B((uint32_t)diff & 0x3ffffff);
}

static void emit_patch_tbz(uint32_t *p, uint32_t *target, uint32_t base_opcode) {
    intptr_t diff = target - p;
    *p = base_opcode | (((uint32_t)diff & 0x3fff) << 5);
}

jit_trace_fn snobol_jit_compile(VM *vm, size_t start_ip) {
    size_t scan_ip = start_ip;
    size_t ops_count = 0;
    bool worthy = false;

    while (scan_ip < vm->bc_len && ops_count < 32) {
        uint8_t op = vm->bc[scan_ip];
        if (op == OP_ACCEPT || op == OP_FAIL || op == OP_SPLIT || op == OP_JMP || 
            op == OP_REPEAT_INIT || op == OP_REPEAT_STEP) break;
        
        size_t next_ip = scan_ip + 1;
        if (op == OP_LIT) {
            read_u32(vm->bc, vm->bc_len, &next_ip);
            uint32_t len = read_u32(vm->bc, vm->bc_len, &next_ip);
            next_ip += len;
            worthy = true; 
        } else if (op == OP_ANY || op == OP_NOTANY || op == OP_SPAN || op == OP_BREAK) {
            next_ip += 2;
            worthy = true;
        } else if (op == OP_LEN) {
            next_ip += 4;
        } else if (op == OP_ANCHOR || op == OP_CAP_START || op == OP_CAP_END) {
            next_ip += 1;
        } else if (op == OP_ASSIGN) {
            next_ip += 3;
        } else break;
        scan_ip = next_ip;
        ops_count++;
    }

    if (ops_count == 0 || !worthy) return NULL;

    size_t code_size = 16384;
    uint32_t *code = (uint32_t *)snobol_jit_alloc_code(code_size);
    if (!code) return NULL;
    JITState js = {code, code, code_size};

    FailPatch fail_patches[512];
    size_t fail_patch_count = 0;

    emit_instr(&js, A64_LDR_X_X_IMM(1, 0, offsetof(VM, s)));
    emit_instr(&js, A64_LDR_X_X_IMM(2, 0, offsetof(VM, pos)));
    emit_instr(&js, A64_LDR_X_X_IMM(3, 0, offsetof(VM, len)));

    scan_ip = start_ip;
    for (size_t i = 0; i < ops_count; i++) {
        uint8_t op = vm->bc[scan_ip];
        size_t cur_op_ip = scan_ip;
        size_t next_op_ip = scan_ip + 1;

        if (op == OP_LIT) {
            uint32_t off = read_u32(vm->bc, vm->bc_len, &next_op_ip);
            uint32_t lit_len = read_u32(vm->bc, vm->bc_len, &next_op_ip);
            next_op_ip += lit_len;
            emit_mov_x64(&js, 7, lit_len);
            emit_instr(&js, A64_ADD_X_X_X(8, 2, 7)); 
            emit_instr(&js, A64_CMP_X_X(8, 3));
            fail_patches[fail_patch_count++] = (FailPatch){js.p, cur_op_ip};
            emit_instr(&js, A64_B_HI(0));
            for (uint32_t j = 0; j < lit_len; j++) {
                emit_instr(&js, A64_LDRB_W_X_X(4, 1, 2));
                emit_mov_w32(&js, 7, vm->bc[off + j]);
                emit_instr(&js, A64_CMP_W_W(4, 7));
                fail_patches[fail_patch_count++] = (FailPatch){js.p, cur_op_ip};
                emit_instr(&js, A64_B_NE(0)); 
                emit_instr(&js, A64_MOV_X_IMM(7, 1));
                emit_instr(&js, A64_ADD_X_X_X(2, 2, 7)); 
            }
        } else if (op == OP_SPAN || op == OP_ANY || op == OP_NOTANY) {
            uint16_t set_id = read_u16(vm->bc, vm->bc_len, &next_op_ip);
            uint16_t count, ci;
            const uint8_t *ranges = get_ranges_ptr(vm, set_id, &count, &ci);
            uint64_t ascii_map[2];
            if (!ranges || !ranges_to_ascii_bitmap(ranges, count, ascii_map)) break;
            emit_mov_x64(&js, 5, ascii_map[0]);
            emit_mov_x64(&js, 6, ascii_map[1]);
            emit_instr(&js, A64_CMP_X_X(2, 3));
            fail_patches[fail_patch_count++] = (FailPatch){js.p, cur_op_ip};
            emit_instr(&js, A64_B_GE(0));
            emit_instr(&js, A64_LDRB_W_X_X(4, 1, 2));
            emit_instr(&js, A64_MOV_W_IMM(7, 127));
            emit_instr(&js, A64_CMP_W_W(4, 7));
            fail_patches[fail_patch_count++] = (FailPatch){js.p, cur_op_ip};
            emit_instr(&js, A64_B_HI(0));
            uint32_t *tbz_instr = js.p; emit_instr(&js, 0);
            emit_ubfx_w(&js, 7, 4, 0, 6);
            emit_instr(&js, A64_LSR_X_X_X(8, 6, 7));
            if (op == OP_NOTANY) {
                // NOTANY fails when the current byte IS in the set.
                fail_patches[fail_patch_count++] = (FailPatch){js.p, cur_op_ip};
                emit_instr(&js, A64_TBZ(8, 0, 0));
            } else {
                // ANY/SPAN succeed only when the byte IS in the set.
                fail_patches[fail_patch_count++] = (FailPatch){js.p, cur_op_ip};
                emit_instr(&js, A64_TBZ(8, 0, 0));
            }
            uint32_t *b_success1 = js.p; emit_instr(&js, 0);
            *tbz_instr = A64_TBZ(4, 6, (js.p - tbz_instr));
            emit_ubfx_w(&js, 7, 4, 0, 6);
            emit_instr(&js, A64_LSR_X_X_X(8, 5, 7));
            if (op == OP_NOTANY) {
                // NOTANY fails when the current byte IS in the set.
                fail_patches[fail_patch_count++] = (FailPatch){js.p, cur_op_ip};
                emit_instr(&js, A64_TBZ(8, 0, 0));
            } else {
                fail_patches[fail_patch_count++] = (FailPatch){js.p, cur_op_ip};
                emit_instr(&js, A64_TBZ(8, 0, 0));
            }
            uint32_t *success_label = js.p;
            emit_patch_b(b_success1, success_label);
            emit_instr(&js, A64_MOV_X_IMM(7, 1));
            emit_instr(&js, A64_ADD_X_X_X(2, 2, 7));
            if (op == OP_SPAN) {
                uint32_t *loop_start = js.p;
                emit_instr(&js, A64_CMP_X_X(2, 3));
                uint32_t *loop_ge_done = js.p; emit_instr(&js, A64_B_GE(0));
                emit_instr(&js, A64_LDRB_W_X_X(4, 1, 2));
                emit_instr(&js, A64_MOV_W_IMM(7, 127));
                emit_instr(&js, A64_CMP_W_W(4, 7));
                uint32_t *loop_hi_done = js.p; emit_instr(&js, A64_B_HI(0));
                uint32_t *loop_tbz = js.p; emit_instr(&js, 0);
                emit_ubfx_w(&js, 7, 4, 0, 6);
                emit_instr(&js, A64_LSR_X_X_X(8, 6, 7));
                uint32_t *loop_bit64_done = js.p; emit_instr(&js, A64_TBZ(8, 0, 0));
                emit_instr(&js, A64_MOV_X_IMM(7, 1));
                emit_instr(&js, A64_ADD_X_X_X(2, 2, 7));
                emit_instr(&js, A64_B(loop_start - js.p));
                *loop_tbz = A64_TBZ(4, 6, (js.p - loop_tbz));
                emit_ubfx_w(&js, 7, 4, 0, 6);
                emit_instr(&js, A64_LSR_X_X_X(8, 5, 7));
                uint32_t *loop_bit0_done = js.p; emit_instr(&js, A64_TBZ(8, 0, 0));
                emit_instr(&js, A64_MOV_X_IMM(7, 1));
                emit_instr(&js, A64_ADD_X_X_X(2, 2, 7));
                emit_instr(&js, A64_B(loop_start - js.p));
                uint32_t *span_done = js.p;
                emit_patch_b_cond(loop_ge_done, span_done, 0x5400000a); 
                emit_patch_b_cond(loop_hi_done, span_done, 0x54000008); 
                emit_patch_tbz(loop_bit64_done, span_done, 0xb6000000);
                emit_patch_tbz(loop_bit0_done, span_done, 0xb6000000);
            }
        } else if (op == OP_CAP_START || op == OP_CAP_END) {
            uint8_t r = read_u8(vm->bc, vm->bc_len, &next_op_ip);
            size_t off = (op == OP_CAP_START) ? offsetof(VM, cap_start) : offsetof(VM, cap_end);
            emit_instr(&js, A64_STR_X_X_IMM(2, 0, off + r * 8));
            emit_ldrb_w_x_imm(&js, 7, 0, (uint32_t)offsetof(VM, max_cap_used));
            emit_instr(&js, A64_MOV_W_IMM(8, r + 1));
            emit_instr(&js, A64_CMP_W_W(7, 8));
            uint32_t *lt_skip = js.p; emit_instr(&js, A64_B_GE(0)); 
            emit_strb_w_x_imm(&js, 8, 0, (uint32_t)offsetof(VM, max_cap_used)); 
            emit_patch_b_cond(lt_skip, js.p, 0x5400000a);
        } else if (op == OP_LEN) {
            uint32_t n = read_u32(vm->bc, vm->bc_len, &next_op_ip);
            emit_mov_x64(&js, 7, n);
            emit_instr(&js, A64_ADD_X_X_X(8, 2, 7));
            emit_instr(&js, A64_CMP_X_X(8, 3));
            fail_patches[fail_patch_count++] = (FailPatch){js.p, cur_op_ip};
            emit_instr(&js, A64_B_HI(0));
            emit_instr(&js, A64_ADD_X_X_X(2, 2, 7));
        } else if (op == OP_ANCHOR) {
            uint8_t type = read_u8(vm->bc, vm->bc_len, &next_op_ip);
            if (type == 0) { emit_instr(&js, A64_MOV_X_IMM(7, 0)); emit_instr(&js, A64_CMP_X_X(2, 7)); }
            else emit_instr(&js, A64_CMP_X_X(2, 3));
            fail_patches[fail_patch_count++] = (FailPatch){js.p, cur_op_ip};
            emit_instr(&js, A64_B_NE(0)); 
        } else if (op == OP_ASSIGN) {
            uint16_t var = read_u16(vm->bc, vm->bc_len, &next_op_ip);
            uint8_t reg = read_u8(vm->bc, vm->bc_len, &next_op_ip);
            emit_instr(&js, A64_LDR_X_X_IMM(7, 0, offsetof(VM, cap_start) + reg * 8));
            emit_instr(&js, A64_STR_X_X_IMM(7, 0, offsetof(VM, var_start) + var * 8));
            emit_instr(&js, A64_LDR_X_X_IMM(7, 0, offsetof(VM, cap_end) + reg * 8));
            emit_instr(&js, A64_STR_X_X_IMM(7, 0, offsetof(VM, var_end) + var * 8));
            emit_instr(&js, A64_LDR_X_X_IMM(7, 0, offsetof(VM, var_count)));
            emit_mov_x64(&js, 8, var + 1);
            emit_instr(&js, A64_CMP_X_X(7, 8));
            uint32_t *lt_skip = js.p; emit_instr(&js, A64_B_GE(0));
            emit_instr(&js, A64_STR_X_X_IMM(8, 0, offsetof(VM, var_count)));
            emit_patch_b_cond(lt_skip, js.p, 0x5400000a);
        }
        scan_ip = next_op_ip;
    }

    uint32_t *final_success = js.p;
    emit_instr(&js, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
    emit_mov_x64(&js, 7, scan_ip);
    emit_instr(&js, A64_STR_X_X_IMM(7, 0, offsetof(VM, ip)));
    emit_instr(&js, A64_RET);

    for (size_t f = 0; f < fail_patch_count; f++) {
        uint32_t *fail_stub = js.p;
        emit_instr(&js, A64_STR_X_X_IMM(2, 0, offsetof(VM, pos)));
        emit_mov_x64(&js, 7, fail_patches[f].ip);
        emit_instr(&js, A64_STR_X_X_IMM(7, 0, offsetof(VM, ip)));
        emit_instr(&js, A64_RET);
        uint32_t orig = *fail_patches[f].instr_p;
        if ((orig & 0xff000000) == 0x54000000) {
            emit_patch_b_cond(fail_patches[f].instr_p, fail_stub, orig & 0xff00001f);
        } else if ((orig & 0x7e000000) == 0x36000000) {
            emit_patch_tbz(fail_patches[f].instr_p, fail_stub, orig & 0xfff8001f);
        }
    }
    snobol_jit_seal_code(code, code_size);
    return (jit_trace_fn)code;
}

#endif
