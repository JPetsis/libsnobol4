/**
 * @file jit_ir.c
 * @brief Architecture-neutral intermediate representation (IR) for the SNOBOL4 micro-JIT.
 *
 * Implements:
 *  - Region builder  (jit_ir_region_new, jit_ir_append, ...)
 *  - VM opcode lifter (jit_ir_lift_region)
 *  - Optimiser passes (jit_ir_dce, jit_ir_copy_propagation)
 *  - Debug dump      (jit_ir_dump)
 */

#include "snobol/jit_ir.h"
#include "snobol/snobol_internal.h"

#ifdef SNOBOL_JIT

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Context helpers (bytecode reading mirrors jit.c cfg_read_u32)
 * -------------------------------------------------------------------------
 */
static inline uint32_t ir_read_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}
static inline uint16_t ir_read_u16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

/* -------------------------------------------------------------------------
 * Region builder
 * -------------------------------------------------------------------------
 */

#define IR_INITIAL_CAPACITY 32

jit_ir_region_t *jit_ir_region_new(void) {
    jit_ir_region_t *r = (jit_ir_region_t *)snobol_calloc(1, sizeof(jit_ir_region_t));
    if (!r) return nullptr;
    r->instrs = (jit_ir_instr_t *)snobol_malloc(IR_INITIAL_CAPACITY * sizeof(jit_ir_instr_t));
    if (!r->instrs) { snobol_free(r); return nullptr; }
    r->capacity     = IR_INITIAL_CAPACITY;
    r->count        = 0;
    r->vreg_next    = 1; /* 0 = VREG_NONE; allocations start at 1 */
    r->non_compilable = false;
    memset(r->use_count, 0, sizeof(r->use_count));
    return r;
}

void jit_ir_region_free(jit_ir_region_t *r) {
    if (!r) return;
    if (r->instrs) snobol_free(r->instrs);
    snobol_free(r);
}

bool jit_ir_append(jit_ir_region_t *r, jit_ir_opcode_t op,
                   uint8_t flags, size_t bc_ip,
                   uint8_t dst_reg, uint8_t src0, uint8_t src1) {
    if (!r || r->non_compilable) return false;
    if (r->count >= r->capacity) {
        size_t new_cap = r->capacity * 2;
        jit_ir_instr_t *nb = (jit_ir_instr_t *)snobol_realloc(
            r->instrs, new_cap * sizeof(jit_ir_instr_t));
        if (!nb) return false;
        r->instrs   = nb;
        r->capacity = new_cap;
    }
    jit_ir_instr_t *ins = &r->instrs[r->count++];
    memset(ins, 0, sizeof(*ins));
    ins->opcode     = op;
    ins->flags      = flags;
    ins->dst_reg    = dst_reg;
    ins->src_reg[0] = src0;
    ins->src_reg[1] = src1;
    ins->bc_ip      = bc_ip;
    /* Increment use counts for src registers */
    if (src0 != JIT_IR_VREG_NONE) jit_ir_inc_use(r, src0);
    if (src1 != JIT_IR_VREG_NONE) jit_ir_inc_use(r, src1);
    return true;
}

uint8_t jit_ir_alloc_vreg(jit_ir_region_t *r) {
    if (!r) return JIT_IR_VREG_NONE;
    if (r->vreg_next > JIT_IR_VREG_MAX) {
        /* Exceeded 256 virtual registers — mark region non-compilable */
        if (!r->non_compilable) {
            r->non_compilable = true;
            fprintf(stderr,
                "[snobol JIT] IR vreg limit exceeded (> 256): region marked non-compilable\n");
        }
        return JIT_IR_VREG_NONE;
    }
    return r->vreg_next++;
}

void jit_ir_inc_use(jit_ir_region_t *r, uint8_t reg) {
    if (!r || reg == JIT_IR_VREG_NONE) return;
    if (r->use_count[reg] < UINT16_MAX)
        r->use_count[reg]++;
}

/* -------------------------------------------------------------------------
 * Optimiser passes
 * -------------------------------------------------------------------------
 */

void jit_ir_dce(jit_ir_region_t *r) {
    if (!r || r->count == 0) return;

    /* Mark dead: instructions that are pure AND whose dst_reg has zero uses */
    for (size_t i = 0; i < r->count; i++) {
        jit_ir_instr_t *ins = &r->instrs[i];
        if (ins->flags & JIT_IR_FLAG_DEAD) continue;
        if (ins->flags & JIT_IR_FLAG_SIDE_EFFECT) continue;
        if (!(ins->flags & JIT_IR_FLAG_PURE)) continue;
        if (ins->dst_reg == JIT_IR_VREG_NONE) continue;
        if (r->use_count[ins->dst_reg] == 0)
            ins->flags |= JIT_IR_FLAG_DEAD;
    }

    /* Compact: remove dead instructions */
    size_t write = 0;
    for (size_t read = 0; read < r->count; read++) {
        if (!(r->instrs[read].flags & JIT_IR_FLAG_DEAD)) {
            if (write != read)
                r->instrs[write] = r->instrs[read];
            write++;
        }
    }
    r->count = write;
}

void jit_ir_copy_propagation(jit_ir_region_t *r) {
    if (!r || r->count == 0) return;

    /* Build a copy-of table: copy_source[dst_reg] = src_reg for COPY instrs */
    uint8_t copy_source[256];
    memset(copy_source, JIT_IR_VREG_NONE, sizeof(copy_source));

    for (size_t i = 0; i < r->count; i++) {
        jit_ir_instr_t *ins = &r->instrs[i];
        if (ins->opcode == JIT_IR_COPY && ins->dst_reg != JIT_IR_VREG_NONE)
            copy_source[ins->dst_reg] = ins->src_reg[0];
    }

    /* Replace uses of copy-destination registers with their sources */
    bool any_change = false;
    for (size_t i = 0; i < r->count; i++) {
        jit_ir_instr_t *ins = &r->instrs[i];
        if (ins->opcode == JIT_IR_COPY) continue; /* COPY itself — skip */
        for (int k = 0; k < 2; k++) {
            uint8_t s = ins->src_reg[k];
            if (s != JIT_IR_VREG_NONE && copy_source[s] != JIT_IR_VREG_NONE) {
                /* Decrement old use count, increment new */
                if (r->use_count[s] > 0) r->use_count[s]--;
                uint8_t ns = copy_source[s];
                ins->src_reg[k] = ns;
                jit_ir_inc_use(r, ns);
                any_change = true;
            }
        }
    }

    /* Mark COPY instructions whose dst_reg now has zero uses as dead (pure) */
    if (any_change) {
        for (size_t i = 0; i < r->count; i++) {
            jit_ir_instr_t *ins = &r->instrs[i];
            if (ins->opcode == JIT_IR_COPY &&
                ins->dst_reg != JIT_IR_VREG_NONE &&
                r->use_count[ins->dst_reg] == 0) {
                ins->flags |= JIT_IR_FLAG_PURE; /* ensure DCE can remove it */
            }
        }
        /* Run DCE to clean up dead COPYs */
        jit_ir_dce(r);
    }
}

/* -------------------------------------------------------------------------
 * Debug dump
 * -------------------------------------------------------------------------
 */

static const char *ir_opcode_name(jit_ir_opcode_t op) {
    switch (op) {
    case JIT_IR_NOP:          return "NOP";
    case JIT_IR_ACCEPT:       return "ACCEPT";
    case JIT_IR_FAIL:         return "FAIL";
    case JIT_IR_JMP:          return "JMP";
    case JIT_IR_SPLIT:        return "SPLIT";
    case JIT_IR_LIT:          return "LIT";
    case JIT_IR_ANY:          return "ANY";
    case JIT_IR_NOTANY:       return "NOTANY";
    case JIT_IR_SPAN:         return "SPAN";
    case JIT_IR_BREAK:        return "BREAK";
    case JIT_IR_BREAKX:       return "BREAKX";
    case JIT_IR_LEN:          return "LEN";
    case JIT_IR_ANCHOR:       return "ANCHOR";
    case JIT_IR_CAP_START:    return "CAP_START";
    case JIT_IR_CAP_END:      return "CAP_END";
    case JIT_IR_ASSIGN:       return "ASSIGN";
    case JIT_IR_REPEAT_INIT:  return "REPEAT_INIT";
    case JIT_IR_REPEAT_STEP:  return "REPEAT_STEP";
    case JIT_IR_REM:          return "REM";
    case JIT_IR_RPOS:         return "RPOS";
    case JIT_IR_RTAB:         return "RTAB";
    case JIT_IR_FENCE:        return "FENCE";
    case JIT_IR_GOTO:         return "GOTO";
    case JIT_IR_GOTO_F:       return "GOTO_F";
    case JIT_IR_EMIT_LITERAL: return "EMIT_LITERAL";
    case JIT_IR_EMIT_CAPTURE: return "EMIT_CAPTURE";
    case JIT_IR_EMIT_EXPR:    return "EMIT_EXPR";
    case JIT_IR_EMIT_FORMAT:  return "EMIT_FORMAT";
    case JIT_IR_EMIT_TABLE:   return "EMIT_TABLE";
    case JIT_IR_TABLE_GET:    return "TABLE_GET";
    case JIT_IR_TABLE_SET:    return "TABLE_SET";
    case JIT_IR_BAL:          return "BAL";
    case JIT_IR_EVAL:         return "EVAL";
    case JIT_IR_DYNAMIC:      return "DYNAMIC";
    case JIT_IR_LABEL:        return "LABEL";
    case JIT_IR_DYNAMIC_DEF:  return "DYNAMIC_DEF";
    case JIT_IR_COPY:         return "COPY";
    default:                  return "UNKNOWN";
    }
}

void jit_ir_dump(const jit_ir_region_t *r, FILE *out) {
    if (!r || !out) return;
    fprintf(out, "[snobol JIT-IR] region   count=%zu  vregs=%u  compilable=%s\n",
            r->count, (unsigned)r->vreg_next - 1u,
            r->non_compilable ? "NO" : "YES");
    for (size_t i = 0; i < r->count; i++) {
        const jit_ir_instr_t *ins = &r->instrs[i];
        fprintf(out, "  %4zu  bc_ip=%-5zu  %-14s", i, ins->bc_ip,
                ir_opcode_name(ins->opcode));
        if (ins->dst_reg != JIT_IR_VREG_NONE)
            fprintf(out, "  v%u=", (unsigned)ins->dst_reg);
        else
            fprintf(out, "       ");
        if (ins->src_reg[0]) fprintf(out, " v%u", (unsigned)ins->src_reg[0]);
        if (ins->src_reg[1]) fprintf(out, ",v%u", (unsigned)ins->src_reg[1]);

        /* Operand summary */
        switch (ins->opcode) {
        case JIT_IR_LIT:
            fprintf(out, "  len=%u", ins->u.lit.len);
            break;
        case JIT_IR_ANY: case JIT_IR_NOTANY: case JIT_IR_SPAN:
        case JIT_IR_BREAK: case JIT_IR_BREAKX:
            fprintf(out, "  set=%u", (unsigned)ins->u.set.set_id);
            break;
        case JIT_IR_SPLIT:
            fprintf(out, "  a=@%zu b=@%zu", ins->u.split.target_a, ins->u.split.target_b);
            break;
        case JIT_IR_JMP: case JIT_IR_GOTO:
            fprintf(out, "  tgt=@%zu", ins->u.jmp.target);
            break;
        case JIT_IR_GOTO_F:
            fprintf(out, "  tgt=@%zu (label %u)", ins->u.goto_f.target, ins->u.goto_f.label_id);
            break;
        case JIT_IR_LEN:
            fprintf(out, "  n=%u", ins->u.len.n);
            break;
        case JIT_IR_RPOS: case JIT_IR_RTAB:
            fprintf(out, "  n=%u", ins->u.rpos_rtab.n);
            break;
        case JIT_IR_CAP_START: case JIT_IR_CAP_END:
            fprintf(out, "  reg=%u", (unsigned)ins->u.cap.reg);
            break;
        case JIT_IR_ANCHOR:
            fprintf(out, "  type=%u", (unsigned)ins->u.anchor.type);
            break;
        case JIT_IR_ASSIGN:
            fprintf(out, "  var=%u reg=%u", (unsigned)ins->u.assign.var, (unsigned)ins->u.assign.reg);
            break;
        case JIT_IR_EMIT_LITERAL:
            fprintf(out, "  off=%u len=%u", ins->u.emit_lit.offset, ins->u.emit_lit.len);
            break;
        case JIT_IR_EMIT_CAPTURE:
            fprintf(out, "  reg=%u", (unsigned)ins->u.emit_cap.reg);
            break;
        case JIT_IR_EMIT_EXPR:
            fprintf(out, "  reg=%u type=%u", (unsigned)ins->u.emit_expr.reg, (unsigned)ins->u.emit_expr.expr_type);
            break;
        case JIT_IR_EMIT_FORMAT:
            fprintf(out, "  reg=%u fmt=%u width=%u", (unsigned)ins->u.emit_fmt.reg,
                    (unsigned)ins->u.emit_fmt.fmt_type, (unsigned)ins->u.emit_fmt.width);
            break;
        case JIT_IR_TABLE_GET:
            fprintf(out, "  tbl=%u key=%u dst=%u",
                    (unsigned)ins->u.tget.table_id, (unsigned)ins->u.tget.key_reg, (unsigned)ins->u.tget.dest_reg);
            break;
        case JIT_IR_TABLE_SET:
            fprintf(out, "  tbl=%u key=%u val=%u",
                    (unsigned)ins->u.tset.table_id, (unsigned)ins->u.tset.key_reg, (unsigned)ins->u.tset.val_reg);
            break;
        case JIT_IR_BAL:
            fprintf(out, "  open=%u close=%u", ins->u.bal.open_cp, ins->u.bal.close_cp);
            break;
        case JIT_IR_EVAL:
            fprintf(out, "  fn=%u reg=%u", (unsigned)ins->u.eval.fn_id, (unsigned)ins->u.eval.reg);
            break;
        case JIT_IR_LABEL:
            fprintf(out, "  label_id=%u", (unsigned)ins->u.label.label_id);
            break;
        default:
            break;
        }
        fprintf(out, "\n");
    }
    fflush(out);
}

/* -------------------------------------------------------------------------
 * VM opcode lifter
 *
 * Creates one IR instruction per VM opcode, pre-decoding all operands.
 * Mirrors the bytecode scanning logic in snobol_jit_compile() pass-1 and
 * jit_cfg_scan_block(), but stores decoded state in IR instead of emitting
 * ARM64 directly.
 * -------------------------------------------------------------------------
 */

/* Helper: append a side-effecting instruction (most VM ops are side-effecting) */
#define LIFT_SE(r, op, bc_ip) \
    jit_ir_append((r), (op), JIT_IR_FLAG_SIDE_EFFECT, (bc_ip), JIT_IR_VREG_NONE, 0, 0)

/* Helper: append a pure instruction with a fresh virtual register as output */
#define LIFT_PURE_VREG(r, op, bc_ip, dst) \
    jit_ir_append((r), (op), JIT_IR_FLAG_PURE, (bc_ip), (dst), 0, 0)

jit_ir_region_t *jit_ir_lift_region(const VM *vm, size_t start_ip) {
    if (!vm || !vm->bc || start_ip >= vm->bc_len) return nullptr;

    jit_ir_region_t *r = jit_ir_region_new();
    if (!r) return nullptr;

    const uint8_t *bc     = vm->bc;
    size_t         bc_len = vm->bc_len;

    size_t scan = start_ip;

    while (scan < bc_len && !r->non_compilable) {
        uint8_t op  = bc[scan];
        size_t  oip = scan + 1; /* operand start */
        size_t  cur = scan;     /* saved ip for this instruction */

        /* -----------------------------------------------------------------
         * Terminator opcodes: append IR and stop scanning
         * ----------------------------------------------------------------- */
        if (op == OP_ACCEPT) {
            LIFT_SE(r, JIT_IR_ACCEPT, cur);
            break;
        }
        if (op == OP_FAIL) {
            LIFT_SE(r, JIT_IR_FAIL, cur);
            break;
        }
        if (op == OP_REPEAT_INIT) {
            LIFT_SE(r, JIT_IR_REPEAT_INIT, cur);
            break;
        }
        if (op == OP_REPEAT_STEP) {
            LIFT_SE(r, JIT_IR_REPEAT_STEP, cur);
            break;
        }

        /* -----------------------------------------------------------------
         * Control-flow opcodes: append IR and stop scanning
         * (Linear lift: CFG expansion is done by the ARM64 backend)
         * ----------------------------------------------------------------- */
        if (op == OP_JMP) {
            if (oip + 4 > bc_len) break;
            size_t tgt = (size_t)ir_read_u32(bc + oip);
            if (!jit_ir_append(r, JIT_IR_JMP, JIT_IR_FLAG_SIDE_EFFECT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.jmp.target = tgt;
            /* Follow forward JMPs inline (same as current pass-1) */
            if (tgt > scan && tgt < bc_len) {
                scan = tgt;
            } else {
                break; /* backward or out-of-range: stop */
            }
            continue;
        }

        if (op == OP_SPLIT) {
            if (oip + 8 > bc_len) break;
            size_t ta = (size_t)ir_read_u32(bc + oip);
            size_t tb = (size_t)ir_read_u32(bc + oip + 4);
            if (!jit_ir_append(r, JIT_IR_SPLIT, JIT_IR_FLAG_SIDE_EFFECT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.split.target_a = ta;
            r->instrs[r->count - 1].u.split.target_b = tb;
            /* Teleport to arm-a for forward branches (mirrors pass-1 in snobol_jit_compile).
             * The ARM64 backend handles arm-b via CFG/choice-push. */
            if (ta > scan && ta < bc_len && tb > scan && tb < bc_len) {
                scan = ta; /* inline arm-a */
            } else {
                break; /* backward target or out-of-range: hand off to interpreter */
            }
            continue;
        }

        if (op == OP_GOTO) {
            if (oip + 2 > bc_len) break;
            uint16_t label_id = ir_read_u16(bc + oip);
            size_t tgt = 0;
            if (vm->label_offsets && label_id < vm->label_count)
                tgt = (size_t)vm->label_offsets[label_id];
            if (!jit_ir_append(r, JIT_IR_GOTO, JIT_IR_FLAG_SIDE_EFFECT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.jmp.target = tgt;
            scan = oip + 2;
            break; /* GOTO terminates the linear scan */
        }

        /* -----------------------------------------------------------------
         * Linear (non-branching) opcodes
         * ----------------------------------------------------------------- */

        if (op == OP_NOP) {
            /* Pure no-op: allocate a virtual register so DCE can remove it */
            uint8_t vr = jit_ir_alloc_vreg(r);
            LIFT_PURE_VREG(r, JIT_IR_NOP, cur, vr);
            scan = oip;
            continue;
        }

        if (op == OP_LIT) {
            if (oip + 8 > bc_len) break;
            uint32_t off = ir_read_u32(bc + oip);
            uint32_t len = ir_read_u32(bc + oip + 4);
            size_t   next = oip + 8 + (size_t)len;
            if (!jit_ir_append(r, JIT_IR_LIT, JIT_IR_FLAG_SIDE_EFFECT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.lit.data   = bc + off;
            r->instrs[r->count - 1].u.lit.len    = len;
            scan = next;
            continue;
        }

        if (op == OP_ANY || op == OP_NOTANY || op == OP_SPAN ||
            op == OP_BREAK || op == OP_BREAKX) {
            if (oip + 2 > bc_len) break;
            uint16_t set_id = ir_read_u16(bc + oip);
            jit_ir_opcode_t irop;
            switch (op) {
            case OP_ANY:    irop = JIT_IR_ANY;    break;
            case OP_NOTANY: irop = JIT_IR_NOTANY; break;
            case OP_SPAN:   irop = JIT_IR_SPAN;   break;
            case OP_BREAK:  irop = JIT_IR_BREAK;  break;
            default:        irop = JIT_IR_BREAKX; break;
            }
            if (!jit_ir_append(r, irop, JIT_IR_FLAG_SIDE_EFFECT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.set.set_id = set_id;
            scan = oip + 2;
            continue;
        }

        if (op == OP_LEN) {
            if (oip + 4 > bc_len) break;
            uint32_t n = ir_read_u32(bc + oip);
            if (!jit_ir_append(r, JIT_IR_LEN, JIT_IR_FLAG_SIDE_EFFECT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.len.n = n;
            scan = oip + 4;
            continue;
        }

        if (op == OP_ANCHOR) {
            if (oip + 1 > bc_len) break;
            uint8_t type = bc[oip];
            if (!jit_ir_append(r, JIT_IR_ANCHOR, JIT_IR_FLAG_SIDE_EFFECT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.anchor.type = type;
            scan = oip + 1;
            continue;
        }

        if (op == OP_CAP_START || op == OP_CAP_END) {
            if (oip + 1 > bc_len) break;
            uint8_t reg = bc[oip];
            if (!jit_ir_append(r,
                               (op == OP_CAP_START ? JIT_IR_CAP_START : JIT_IR_CAP_END),
                               JIT_IR_FLAG_SIDE_EFFECT, cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.cap.reg = reg;
            scan = oip + 1;
            continue;
        }

        if (op == OP_ASSIGN) {
            if (oip + 3 > bc_len) break;
            uint16_t var = ir_read_u16(bc + oip);
            uint8_t  reg = bc[oip + 2];
            if (!jit_ir_append(r, JIT_IR_ASSIGN, JIT_IR_FLAG_SIDE_EFFECT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.assign.var = var;
            r->instrs[r->count - 1].u.assign.reg = reg;
            scan = oip + 3;
            continue;
        }

        if (op == OP_REM) {
            LIFT_SE(r, JIT_IR_REM, cur);
            scan = oip;
            continue;
        }

        if (op == OP_RPOS) {
            if (oip + 4 > bc_len) break;
            uint32_t n = ir_read_u32(bc + oip);
            if (!jit_ir_append(r, JIT_IR_RPOS, JIT_IR_FLAG_SIDE_EFFECT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.rpos_rtab.n = n;
            scan = oip + 4;
            continue;
        }

        if (op == OP_RTAB) {
            if (oip + 4 > bc_len) break;
            uint32_t n = ir_read_u32(bc + oip);
            if (!jit_ir_append(r, JIT_IR_RTAB, JIT_IR_FLAG_SIDE_EFFECT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.rpos_rtab.n = n;
            scan = oip + 4;
            continue;
        }

        if (op == OP_POS) {
            if (oip + 4 > bc_len) break;
            uint32_t n = ir_read_u32(bc + oip);
            if (!jit_ir_append(r, JIT_IR_POS, JIT_IR_FLAG_SIDE_EFFECT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.rpos_rtab.n = n;
            scan = oip + 4;
            continue;
        }

        if (op == OP_TAB) {
            if (oip + 4 > bc_len) break;
            uint32_t n = ir_read_u32(bc + oip);
            if (!jit_ir_append(r, JIT_IR_TAB, JIT_IR_FLAG_SIDE_EFFECT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.rpos_rtab.n = n;
            scan = oip + 4;
            continue;
        }

        if (op == OP_ABORT) {
            LIFT_SE(r, JIT_IR_ABORT, cur);
            break;
        }

        if (op == OP_SUCCEED) {
            LIFT_SE(r, JIT_IR_SUCCEED, cur);
            break;
        }

        if (op == OP_FENCE) {
            LIFT_SE(r, JIT_IR_FENCE, cur);
            scan = oip;
            continue;
        }

        if (op == OP_LABEL) {
            if (oip + 2 > bc_len) break;
            uint16_t label_id = ir_read_u16(bc + oip);
            /* Pure pseudo-op: allocate vreg so DCE can remove if unused */
            uint8_t vr = jit_ir_alloc_vreg(r);
            if (!jit_ir_append(r, JIT_IR_LABEL, JIT_IR_FLAG_PURE | JIT_IR_FLAG_PSEUDO,
                               cur, vr, 0, 0)) break;
            r->instrs[r->count - 1].u.label.label_id = label_id;
            scan = oip + 2;
            continue;
        }

        if (op == OP_GOTO_F) {
            if (oip + 2 > bc_len) break;
            uint16_t label_id = ir_read_u16(bc + oip);
            size_t tgt = 0;
            if (vm->label_offsets && label_id < vm->label_count)
                tgt = (size_t)vm->label_offsets[label_id];
            /* NOP in JIT compiled regions (always falls through) — pure */
            uint8_t vr = jit_ir_alloc_vreg(r);
            if (!jit_ir_append(r, JIT_IR_GOTO_F, JIT_IR_FLAG_PURE | JIT_IR_FLAG_PSEUDO,
                               cur, vr, 0, 0)) break;
            r->instrs[r->count - 1].u.goto_f.label_id = label_id;
            r->instrs[r->count - 1].u.goto_f.target   = tgt;
            scan = oip + 2;
            continue;
        }

        if (op == OP_DYNAMIC_DEF) {
            if (oip + 4 > bc_len) break;
            uint32_t src_len = ir_read_u32(bc + oip);
            size_t after_src = oip + 4 + src_len;
            if (after_src + 4 > bc_len) break;
            uint32_t dyn_bcl = ir_read_u32(bc + after_src);
            /* Pure pseudo-op */
            uint8_t vr = jit_ir_alloc_vreg(r);
            if (!jit_ir_append(r, JIT_IR_DYNAMIC_DEF, JIT_IR_FLAG_PURE | JIT_IR_FLAG_PSEUDO,
                               cur, vr, 0, 0)) break;
            scan = after_src + 4 + dyn_bcl;
            continue;
        }

        /* Call-out opcodes ------------------------------------------------- */

        if (op == OP_EMIT_LITERAL) {
            if (oip + 8 > bc_len) break;
            uint32_t elt_off = ir_read_u32(bc + oip);
            uint32_t elt_len = ir_read_u32(bc + oip + 4);
            size_t   next    = oip + 8;
            if ((size_t)elt_off == next) next += (size_t)elt_len;
            if (!jit_ir_append(r, JIT_IR_EMIT_LITERAL,
                               JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.emit_lit.data   = bc + elt_off;
            r->instrs[r->count - 1].u.emit_lit.len    = elt_len;
            r->instrs[r->count - 1].u.emit_lit.offset = elt_off;
            scan = next;
            continue;
        }

        if (op == OP_EMIT_CAPTURE) {
            if (oip + 1 > bc_len) break;
            uint8_t reg = bc[oip];
            if (!jit_ir_append(r, JIT_IR_EMIT_CAPTURE,
                               JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.emit_cap.reg = reg;
            scan = oip + 1;
            continue;
        }

        if (op == OP_EMIT_EXPR) {
            if (oip + 2 > bc_len) break;
            uint8_t reg       = bc[oip];
            uint8_t expr_type = bc[oip + 1];
            if (!jit_ir_append(r, JIT_IR_EMIT_EXPR,
                               JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.emit_expr.reg       = reg;
            r->instrs[r->count - 1].u.emit_expr.expr_type = expr_type;
            scan = oip + 2;
            continue;
        }

        if (op == OP_EMIT_FORMAT) {
            if (oip + 2 > bc_len) break;
            uint8_t  reg      = bc[oip];
            uint8_t  fmt_type = bc[oip + 1];
            uint16_t width    = 0;
            uint8_t  fill     = 0;
            size_t   next     = oip + 2;
            if (fmt_type == SNBL_FMT_LPAD || fmt_type == SNBL_FMT_RPAD) {
                if (next + 3 > bc_len) break;
                width = ir_read_u16(bc + next);
                fill  = bc[next + 2];
                next += 3;
            }
            if (!jit_ir_append(r, JIT_IR_EMIT_FORMAT,
                               JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.emit_fmt.reg       = reg;
            r->instrs[r->count - 1].u.emit_fmt.fmt_type  = fmt_type;
            r->instrs[r->count - 1].u.emit_fmt.width     = width;
            r->instrs[r->count - 1].u.emit_fmt.fill_char = fill;
            scan = next;
            continue;
        }

        if (op == OP_EMIT_TABLE) {
            if (oip + 4 > bc_len) break;
            uint8_t ktype = bc[oip + 2];
            uint8_t nlen  = bc[oip + 3];
            size_t  after = oip + 4 + nlen;
            if (after > bc_len) break;
            size_t next;
            if (ktype == 0) {
                if (after + 2 > bc_len) break;
                uint16_t kl = ir_read_u16(bc + after);
                next = after + 2 + kl;
            } else if (ktype == 1) {
                next = after + 1;
            } else {
                break;
            }
            /* bc_ip carries the instruction offset needed by the runtime helper */
            if (!jit_ir_append(r, JIT_IR_EMIT_TABLE,
                               JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            scan = next;
            continue;
        }

        if (op == OP_TABLE_GET) {
            if (oip + 5 > bc_len) break;
            uint16_t tid  = ir_read_u16(bc + oip);
            uint8_t  kreg = bc[oip + 2];
            uint8_t  dreg = bc[oip + 3];
            uint8_t  nlen = bc[oip + 4];
            if (!jit_ir_append(r, JIT_IR_TABLE_GET,
                               JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.tget.table_id = tid;
            r->instrs[r->count - 1].u.tget.key_reg  = kreg;
            r->instrs[r->count - 1].u.tget.dest_reg = dreg;
            scan = oip + 5 + nlen;
            continue;
        }

        if (op == OP_TABLE_SET) {
            if (oip + 5 > bc_len) break;
            uint16_t tid  = ir_read_u16(bc + oip);
            uint8_t  kreg = bc[oip + 2];
            uint8_t  vreg = bc[oip + 3];
            uint8_t  nlen = bc[oip + 4];
            if (!jit_ir_append(r, JIT_IR_TABLE_SET,
                               JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.tset.table_id = tid;
            r->instrs[r->count - 1].u.tset.key_reg  = kreg;
            r->instrs[r->count - 1].u.tset.val_reg  = vreg;
            scan = oip + 5 + nlen;
            continue;
        }

        if (op == OP_BAL) {
            if (oip + 8 > bc_len) break;
            uint32_t open  = ir_read_u32(bc + oip);
            uint32_t close = ir_read_u32(bc + oip + 4);
            if (!jit_ir_append(r, JIT_IR_BAL,
                               JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.bal.open_cp  = open;
            r->instrs[r->count - 1].u.bal.close_cp = close;
            scan = oip + 8;
            continue;
        }

        if (op == OP_EVAL) {
            if (oip + 3 > bc_len) break;
            uint16_t fn_id = ir_read_u16(bc + oip);
            uint8_t  reg   = bc[oip + 2];
            if (!jit_ir_append(r, JIT_IR_EVAL,
                               JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT,
                               cur, JIT_IR_VREG_NONE, 0, 0)) break;
            r->instrs[r->count - 1].u.eval.fn_id = fn_id;
            r->instrs[r->count - 1].u.eval.reg   = reg;
            scan = oip + 3;
            continue;
        }

        if (op == OP_DYNAMIC) {
            jit_ir_append(r, JIT_IR_DYNAMIC,
                          JIT_IR_FLAG_SIDE_EFFECT | JIT_IR_FLAG_CALLOUT,
                          cur, JIT_IR_VREG_NONE, 0, 0);
            scan = oip;
            continue;
        }

        /* Unknown opcode: stop scanning */
        break;
    }

    return r;
}

#undef LIFT_SE
#undef LIFT_PURE_VREG

#endif /* SNOBOL_JIT */



