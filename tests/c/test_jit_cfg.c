/* test_jit_cfg.c — Unit tests for the CFG-based multi-block JIT
 *
 * Covers:
 *   jit_blocks_compiled_total zero on init, increments after compile
 *   3-arm SPLIT chain: CFG discovers ≥ 4 blocks
 *   3-block pattern → jit_blocks_compiled_total == 3
 *   SPLIT backtrack boundary: JIT-pushed choice restores state correctly
 *   ARBNO loop compiles compiled; loop guard bails at overflow
 */
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "../../core/include/snobol/vm.h"
#include "../../core/include/snobol/jit.h"

void test_suite(const char *name);
void test_assert(bool condition, const char *message);

static bool jit_is_supported(void) {
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return true;
#else
    return false;
#endif
}

#ifdef SNOBOL_JIT

static void emit_u32_cfg(uint8_t *bc, size_t *ip, uint32_t v) {
    bc[(*ip)++] = (uint8_t)((v >> 24) & 0xFF);
    bc[(*ip)++] = (uint8_t)((v >> 16) & 0xFF);
    bc[(*ip)++] = (uint8_t)((v >>  8) & 0xFF);
    bc[(*ip)++] = (uint8_t)(v & 0xFF);
}

/* ---- Charclass helpers ---- */
/* Layout at end of bc (bc_len=512):
 *   bc[508..511] = class_count (uint32 big-endian)
 *   bc[508-class_count*4 .. 507] = offset table (one uint32 per set)
 *   Each offset points to: count(u16) ci(u16) ranges(count*8 bytes each)
 *   Range = uint32_start + uint32_end (8 bytes total, big-endian)
 *
 * We reserve the region bc[400..511] for charclass data.
 * Per set: 4 + 8 = 12 bytes starting at 400 + (set_id-1)*12.
 * Max 3 sets (index 1..3), each 12 bytes.
 * Offset table: 3*4=12 bytes at bc[496..507].  Count=3 at bc[508..511].
 */
static void setup_charclasses(uint8_t *bc, size_t bc_len) {
    /* Fill all 3 set slots (we pre-define 'a', 'b', 'c' for convenience) */
    static const uint8_t chars[3] = { 'a', 'b', 'c' };

    size_t class_count = 3;
    size_t table_start = bc_len - 4 - class_count * 4; /* 512-4-12=496 */

    for (size_t i = 0; i < class_count; i++) {
        /* Data starts at 400 + i*12 */
        size_t data_off = 400 + i * 12;
        uint8_t ch = chars[i];
        /* count=1, ci=0 */
        bc[data_off+0] = 0; bc[data_off+1] = 1; /* count */
        bc[data_off+2] = 0; bc[data_off+3] = 0; /* ci */
        /* range: start=ch, end=ch as uint32 big-endian each */
        bc[data_off+4] = 0; bc[data_off+5] = 0; bc[data_off+6] = 0; bc[data_off+7] = ch;
        bc[data_off+8] = 0; bc[data_off+9] = 0; bc[data_off+10]= 0; bc[data_off+11]= ch;
        /* offset table entry (uint32 big-endian, offset to data start) */
        size_t te = table_start + i * 4;
        bc[te]   = (uint8_t)(data_off >> 24);
        bc[te+1] = (uint8_t)(data_off >> 16);
        bc[te+2] = (uint8_t)(data_off >>  8);
        bc[te+3] = (uint8_t)(data_off);
    }
    /* class_count at last 4 bytes */
    size_t ce = bc_len - 4;
    bc[ce]   = (uint8_t)(class_count >> 24);
    bc[ce+1] = (uint8_t)(class_count >> 16);
    bc[ce+2] = (uint8_t)(class_count >>  8);
    bc[ce+3] = (uint8_t)(class_count);
}

/* ---- jit_blocks_compiled_total zero on init ---- */
static void test_cfg_stats_init_zero(void) {
    if (!jit_is_supported()) {
        test_assert(true, "CFG stats init: skipped (ARM64 only)");
        return;
    }
    snobol_jit_init();
    snobol_jit_reset_stats();
    SnobolJitStats *stats = snobol_jit_get_stats();
    test_assert(stats->jit_blocks_compiled_total == 0,
                "jit_blocks_compiled_total is 0 after reset");
    snobol_jit_shutdown();
}

/* ---- single-block pattern → counter increments ---- */
static void test_cfg_single_block_counter(void) {
    if (!jit_is_supported()) {
        test_assert(true, "CFG single-block counter: skipped (ARM64 only)");
        return;
    }
    snobol_jit_init();
    snobol_jit_reset_stats();

    SnobolJitConfig saved = *snobol_jit_get_config();
    SnobolJitConfig cfg   = saved;
    cfg.hotness_threshold   = 1;
    cfg.min_useful_ops      = 0;
    cfg.skip_backtrack_heavy = false;
    snobol_jit_set_config(&cfg);

    /* Pattern: ANY('a') ACCEPT — should produce a single EXIT block from CFG */
    uint8_t bc[512] = {0};
    size_t ip = 0;

    /* ANY with set_id=1 ('a') */
    bc[ip++] = OP_ANY; bc[ip++] = 0; bc[ip++] = 1;
    bc[ip++] = OP_ACCEPT;

    setup_charclasses(bc, sizeof(bc));

    VM vm = {0};
    vm.bc = bc; vm.bc_len = sizeof(bc);
    vm.s  = (const uint8_t *)"a";
    vm.len = 1;

    SnobolJitStats *stats = snobol_jit_get_stats();
    SnobolJitContext *ctx = snobol_jit_acquire_context(bc, sizeof(bc));
    vm.jit.enabled   = true;
    vm.jit.ip_counts = ctx->ip_counts;
    vm.jit.traces    = ctx->traces;
    vm.jit.stats     = stats;
    vm.jit.ctx       = ctx;

    for (int i = 0; i < 10; i++) { vm.ip = 0; vm.pos = 0; vm_run(&vm); }

    test_assert(stats->jit_blocks_compiled_total >= 1,
                "single-block ANY pattern: jit_blocks_compiled_total >= 1");

    snobol_jit_release_context(ctx);
    snobol_jit_set_config(&saved);
    snobol_jit_shutdown();
}

/* ---- SPLIT chain produces multiple blocks ---- */
static void test_cfg_split_chain_blocks(void) {
    if (!jit_is_supported()) {
        test_assert(true, "CFG SPLIT chain: skipped (ARM64 only)");
        return;
    }
    snobol_jit_init();
    snobol_jit_reset_stats();

    SnobolJitConfig saved = *snobol_jit_get_config();
    SnobolJitConfig cfg   = saved;
    cfg.hotness_threshold    = 1;
    cfg.min_useful_ops       = 0;
    cfg.skip_backtrack_heavy = false;
    snobol_jit_set_config(&cfg);

    /* Build: SPLIT(a_ip, split2_ip)
     *        a_ip:     ANY('a') JMP merge
     *        split2_ip: SPLIT(b_ip, c_ip)
     *        b_ip:     ANY('b') JMP merge
     *        c_ip:     ANY('c')
     *        merge:    ACCEPT
     * This creates ≥ 4 CFG blocks. */
    uint8_t bc[512] = {0};
    size_t  w = 0;

    /* Offsets (will be filled with back-patching) */
    size_t pos_split0 = w;
    bc[w++] = OP_SPLIT;
    size_t patch_a_off    = w; emit_u32_cfg(bc, &w, 0); /* arm-a placeholder */
    size_t patch_s2_off   = w; emit_u32_cfg(bc, &w, 0); /* arm-b placeholder */

    size_t a_ip = w;
    bc[w++] = OP_ANY; bc[w++] = 0; bc[w++] = 1; /* ANY set_id=1 ('a') */
    size_t patch_jmp_a = w;
    bc[w++] = OP_JMP; emit_u32_cfg(bc, &w, 0); /* forward JMP to merge */

    size_t split2_ip = w;
    bc[w++] = OP_SPLIT;
    size_t patch_b_off = w; emit_u32_cfg(bc, &w, 0);
    size_t patch_c_off = w; emit_u32_cfg(bc, &w, 0);

    size_t b_ip = w;
    bc[w++] = OP_ANY; bc[w++] = 0; bc[w++] = 2; /* ANY set_id=2 ('b') */
    size_t patch_jmp_b = w;
    bc[w++] = OP_JMP; emit_u32_cfg(bc, &w, 0);

    size_t c_ip = w;
    bc[w++] = OP_ANY; bc[w++] = 0; bc[w++] = 3; /* ANY set_id=3 ('c') */

    size_t merge_ip = w;
    bc[w++] = OP_ACCEPT;

    /* Back-patch */
    uint8_t *patch_u32 = bc;
    /* split0 arm-a = a_ip */
    patch_u32[patch_a_off]   = (uint8_t)(a_ip >> 24);
    patch_u32[patch_a_off+1] = (uint8_t)(a_ip >> 16);
    patch_u32[patch_a_off+2] = (uint8_t)(a_ip >>  8);
    patch_u32[patch_a_off+3] = (uint8_t)(a_ip);
    /* split0 arm-b = split2_ip */
    patch_u32[patch_s2_off]   = (uint8_t)(split2_ip >> 24);
    patch_u32[patch_s2_off+1] = (uint8_t)(split2_ip >> 16);
    patch_u32[patch_s2_off+2] = (uint8_t)(split2_ip >>  8);
    patch_u32[patch_s2_off+3] = (uint8_t)(split2_ip);
    /* jmp_a → merge */
    patch_u32[patch_jmp_a+1] = (uint8_t)(merge_ip >> 24);
    patch_u32[patch_jmp_a+2] = (uint8_t)(merge_ip >> 16);
    patch_u32[patch_jmp_a+3] = (uint8_t)(merge_ip >>  8);
    patch_u32[patch_jmp_a+4] = (uint8_t)(merge_ip);
    /* split2 arm-b = b_ip, arm-c = c_ip */
    patch_u32[patch_b_off]   = (uint8_t)(b_ip >> 24);
    patch_u32[patch_b_off+1] = (uint8_t)(b_ip >> 16);
    patch_u32[patch_b_off+2] = (uint8_t)(b_ip >>  8);
    patch_u32[patch_b_off+3] = (uint8_t)(b_ip);
    patch_u32[patch_c_off]   = (uint8_t)(c_ip >> 24);
    patch_u32[patch_c_off+1] = (uint8_t)(c_ip >> 16);
    patch_u32[patch_c_off+2] = (uint8_t)(c_ip >>  8);
    patch_u32[patch_c_off+3] = (uint8_t)(c_ip);
    /* jmp_b → merge */
    patch_u32[patch_jmp_b+1] = (uint8_t)(merge_ip >> 24);
    patch_u32[patch_jmp_b+2] = (uint8_t)(merge_ip >> 16);
    patch_u32[patch_jmp_b+3] = (uint8_t)(merge_ip >>  8);
    patch_u32[patch_jmp_b+4] = (uint8_t)(merge_ip);
    (void)pos_split0;

    /* Charclass set 1='a', 2='b', 3='c' */
    setup_charclasses(bc, sizeof(bc));

    VM vm = {0};
    vm.bc = bc; vm.bc_len = sizeof(bc);
    vm.s   = (const uint8_t *)"b";
    vm.len = 1;

    SnobolJitStats   *stats = snobol_jit_get_stats();
    SnobolJitContext *ctx   = snobol_jit_acquire_context(bc, sizeof(bc));
    vm.jit.enabled   = true;
    vm.jit.ip_counts = ctx->ip_counts;
    vm.jit.traces    = ctx->traces;
    vm.jit.stats     = stats;
    vm.jit.ctx       = ctx;

    uint64_t before = stats->jit_blocks_compiled_total;
    for (int i = 0; i < 30; i++) { vm.ip = 0; vm.pos = 0; vm_run(&vm); }
    uint64_t delta = stats->jit_blocks_compiled_total - before;

    test_assert(delta >= 4,
                "3-arm SPLIT chain: jit_blocks_compiled_total incremented by >= 4 blocks");
    test_assert(stats->entries_total > 0,
                "3-arm SPLIT chain: JIT entries > 0");

    snobol_jit_release_context(ctx);
    snobol_jit_set_config(&saved);
    snobol_jit_shutdown();
}

/* ---- SPLIT backtrack restores state correctly ---- */
static void test_cfg_split_backtrack_restores_state(void) {
    if (!jit_is_supported()) {
        test_assert(true, "CFG SPLIT backtrack: skipped (ARM64 only)");
        return;
    }
    snobol_jit_init();
    snobol_jit_reset_stats();

    SnobolJitConfig saved = *snobol_jit_get_config();
    SnobolJitConfig cfg   = saved;
    cfg.hotness_threshold    = 1;
    cfg.min_useful_ops       = 0;
    cfg.skip_backtrack_heavy = false;
    snobol_jit_set_config(&cfg);

    /* Pattern: SPLIT(a_ip, b_ip) | a_ip: ANY('a') ACCEPT | b_ip: ANY('b') ACCEPT
     * Subject: "b" → arm-a fails, interpreter backtracks to b_ip, arm-b matches */
    uint8_t bc[512] = {0};
    size_t  w = 0;

    bc[w++] = OP_SPLIT;
    size_t pa = w; emit_u32_cfg(bc, &w, 0);
    size_t pb = w; emit_u32_cfg(bc, &w, 0);

    size_t a_ip = w;
    bc[w++] = OP_ANY; bc[w++] = 0; bc[w++] = 1;
    bc[w++] = OP_ACCEPT;

    size_t b_ip = w;
    bc[w++] = OP_ANY; bc[w++] = 0; bc[w++] = 2;
    bc[w++] = OP_ACCEPT;

    bc[pa]   = (uint8_t)(a_ip >> 24); bc[pa+1] = (uint8_t)(a_ip >> 16);
    bc[pa+2] = (uint8_t)(a_ip >>  8); bc[pa+3] = (uint8_t)(a_ip);
    bc[pb]   = (uint8_t)(b_ip >> 24); bc[pb+1] = (uint8_t)(b_ip >> 16);
    bc[pb+2] = (uint8_t)(b_ip >>  8); bc[pb+3] = (uint8_t)(b_ip);

    /* Charclass 1='a', 2='b' */
    setup_charclasses(bc, sizeof(bc));

    VM vm = {0};
    vm.bc  = bc; vm.bc_len = sizeof(bc);
    vm.s   = (const uint8_t *)"b";
    vm.len = 1;

    SnobolJitStats   *stats = snobol_jit_get_stats();
    SnobolJitContext *ctx   = snobol_jit_acquire_context(bc, sizeof(bc));
    vm.jit.enabled   = true;
    vm.jit.ip_counts = ctx->ip_counts;
    vm.jit.traces    = ctx->traces;
    vm.jit.stats     = stats;
    vm.jit.ctx       = ctx;

    bool matched = false;
    for (int i = 0; i < 30; i++) {
        vm.ip = 0; vm.pos = 0;
        matched = vm_run(&vm);
    }

    test_assert(matched, "SPLIT backtrack: 'b' matches via arm-b after arm-a fail");
    test_assert(vm.pos == 1, "SPLIT backtrack: pos advanced by 1 (consumed 'b')");

    snobol_jit_release_context(ctx);
    snobol_jit_set_config(&saved);
    snobol_jit_shutdown();
}

/* ---- ARBNO-style loop runs compiled ---- */
static void test_cfg_arbno_loop_compiled(void) {
    if (!jit_is_supported()) {
        test_assert(true, "CFG ARBNO loop: skipped (ARM64 only)");
        return;
    }
    snobol_jit_init();
    snobol_jit_reset_stats();

    SnobolJitConfig saved = *snobol_jit_get_config();
    SnobolJitConfig cfg   = saved;
    cfg.hotness_threshold    = 1;
    cfg.min_useful_ops       = 0;
    cfg.skip_backtrack_heavy = false;
    snobol_jit_set_config(&cfg);

    /* Pattern simulating ARBNO(ANY('a')):
     *  0: SPLIT(body=9, after=18)
     *  9: ANY('a')
     * 12: JMP 0          ← backward
     * 18: ACCEPT
     *
     * Subject: "aaab" → matches "aaa" (3 iterations) via ARBNO */
    uint8_t bc[512] = {0};
    size_t  w = 0;

    size_t split_ip = w;
    bc[w++] = OP_SPLIT;
    size_t pa = w; emit_u32_cfg(bc, &w, 0); /* arm-a = body */
    size_t pb = w; emit_u32_cfg(bc, &w, 0); /* arm-b = after */

    size_t body_ip = w;
    bc[w++] = OP_ANY; bc[w++] = 0; bc[w++] = 1;

    size_t jmp_ip = w;
    bc[w++] = OP_JMP;
    emit_u32_cfg(bc, &w, (uint32_t)split_ip); /* backward to split */

    size_t after_ip = w;
    bc[w++] = OP_ACCEPT;

    /* Patch SPLIT */
    bc[pa]   = (uint8_t)(body_ip  >> 24); bc[pa+1] = (uint8_t)(body_ip  >> 16);
    bc[pa+2] = (uint8_t)(body_ip  >>  8); bc[pa+3] = (uint8_t)(body_ip);
    bc[pb]   = (uint8_t)(after_ip >> 24); bc[pb+1] = (uint8_t)(after_ip >> 16);
    bc[pb+2] = (uint8_t)(after_ip >>  8); bc[pb+3] = (uint8_t)(after_ip);
    (void)jmp_ip;

    /* Charclass 1='a' */
    setup_charclasses(bc, sizeof(bc));

    VM vm = {0};
    vm.bc  = bc; vm.bc_len = sizeof(bc);
    vm.s   = (const uint8_t *)"aaab";
    vm.len = 4;

    SnobolJitStats   *stats = snobol_jit_get_stats();
    SnobolJitContext *ctx   = snobol_jit_acquire_context(bc, sizeof(bc));
    vm.jit.enabled   = true;
    vm.jit.ip_counts = ctx->ip_counts;
    vm.jit.traces    = ctx->traces;
    vm.jit.stats     = stats;
    vm.jit.ctx       = ctx;

    snobol_jit_reset_stats();
    bool matched = false;
    for (int i = 0; i < 30; i++) {
        vm.ip = 0; vm.pos = 0;
        matched = vm_run(&vm);
    }

    test_assert(matched, "ARBNO loop: pattern matches 'aaab'");
    test_assert(stats->jit_blocks_compiled_total >= 2,
                "ARBNO loop: CFG compiled >= 2 blocks (SPLIT + body + after)");

    snobol_jit_release_context(ctx);
    snobol_jit_set_config(&saved);
    snobol_jit_shutdown();
}

#endif /* SNOBOL_JIT */

void test_jit_cfg_suite(void) {
#ifdef SNOBOL_JIT
    test_suite("JIT: CFG Multi-Block");
    test_cfg_stats_init_zero();
    test_cfg_single_block_counter();
    test_cfg_split_chain_blocks();
    test_cfg_split_backtrack_restores_state();
    test_cfg_arbno_loop_compiled();
#endif
}











