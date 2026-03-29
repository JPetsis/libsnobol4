#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "snobol/vm.h"

extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

static void emit_u8(uint8_t *bc, size_t *ip, uint8_t v) { bc[(*ip)++] = v; }
static void emit_u32(uint8_t *bc, size_t *ip, uint32_t v) {
    bc[(*ip)++] = (v >> 24) & 0xFF;
    bc[(*ip)++] = (v >> 16) & 0xFF;
    bc[(*ip)++] = (v >> 8) & 0xFF;
    bc[(*ip)++] = v & 0xFF;
}

static void test_catastrophic_nested_arbno(void)
{
    // Construct bytecode for: ARBNO(ARBNO('a')) 'b'
    // Pattern: ((a*)*) b
    // Subject: "aaaaaaaaaa" (10 'a's) -> should fail (no 'b')
    // Using OP_REPEAT infrastructure instead of raw SPLITs to benefit from optimization.

    uint8_t bc[1024];
    size_t ip = 0;
    
    // Outer ARBNO: REPEAT_INIT(id=0, min=0, max=-1, skip=L1_DONE)
    emit_u8(bc, &ip, OP_REPEAT_INIT);
    emit_u8(bc, &ip, 0); // id=0
    emit_u32(bc, &ip, 0); // min
    emit_u32(bc, &ip, (uint32_t)-1); // max
    size_t l1_skip_ref = ip; emit_u32(bc, &ip, 0);

    size_t l1_body = ip;

    // Inner ARBNO: REPEAT_INIT(id=1, min=0, max=-1, skip=L2_DONE)
    emit_u8(bc, &ip, OP_REPEAT_INIT);
    emit_u8(bc, &ip, 1); // id=1
    emit_u32(bc, &ip, 0);
    emit_u32(bc, &ip, (uint32_t)-1);
    size_t l2_skip_ref = ip; emit_u32(bc, &ip, 0);

    size_t l2_body = ip;
    // Literal 'a'
    uint32_t lit_a_off = 500;
    uint32_t lit_b_off = 501;
    bc[lit_a_off] = 'a';
    bc[lit_b_off] = 'b';

    emit_u8(bc, &ip, OP_LIT);
    emit_u32(bc, &ip, lit_a_off);
    emit_u32(bc, &ip, 1); // len

    // Inner ARBNO Step: REPEAT_STEP(id=1, target=L2_BODY)
    emit_u8(bc, &ip, OP_REPEAT_STEP);
    emit_u8(bc, &ip, 1);
    emit_u32(bc, &ip, (uint32_t)l2_body);

    size_t l2_done = ip;
    // Patch L2 skip
    uint32_t v = (uint32_t)l2_done;
    bc[l2_skip_ref+0]=v>>24; bc[l2_skip_ref+1]=v>>16; bc[l2_skip_ref+2]=v>>8; bc[l2_skip_ref+3]=v&0xFF;

    // Outer ARBNO Step: REPEAT_STEP(id=0, target=L1_BODY)
    emit_u8(bc, &ip, OP_REPEAT_STEP);
    emit_u8(bc, &ip, 0);
    emit_u32(bc, &ip, (uint32_t)l1_body);

    size_t l1_done = ip;
    // Patch L1 skip
    v = (uint32_t)l1_done;
    bc[l1_skip_ref+0]=v>>24; bc[l1_skip_ref+1]=v>>16; bc[l1_skip_ref+2]=v>>8; bc[l1_skip_ref+3]=v&0xFF;
    
    // Match 'b'
    emit_u8(bc, &ip, OP_LIT);
    emit_u32(bc, &ip, lit_b_off);
    emit_u32(bc, &ip, 1);

    emit_u8(bc, &ip, OP_ACCEPT);

    // SUBJECT
    // N=10
    const char *subject = "aaaaaaaaaa";
    
    VM vm;
    memset(&vm, 0, sizeof(vm));
    vm.bc = bc;
    vm.bc_len = 1024;
    vm.s = subject;
    vm.len = strlen(subject);

    // Run
    bool result = vm_exec(&vm);

    test_assert(result == false, "Catastrophic case should fail (no 'b' in subject)");

#ifdef SNOBOL_PROFILE
    printf("  > Profile: dispatch=%llu push=%llu pop=%llu max_depth=%zu\n",
           (unsigned long long)vm.profile.dispatch_count,
           (unsigned long long)vm.profile.push_count,
           (unsigned long long)vm.profile.pop_count,
           vm.profile.max_depth);
           
    // With N=10, if exponential, it would be millions.
    // With fix, it should be linear-ish (maybe quadratic but small).
    // Let's set a generous limit of 50k ops (vs millions).
    
    test_assert(vm.profile.dispatch_count < 50000, "Catastrophic case should be optimized (dispatch count < 50k)");
#else
    printf("  [SKIP] Profiling not enabled, cannot assert op counts.\n");
#endif
}

void test_catastrophic_suite(void) {
    test_suite("Catastrophic Backtracking Reproduction");
    test_catastrophic_nested_arbno();
}
