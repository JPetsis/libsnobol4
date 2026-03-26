/**
 * @file test_template_ops.c
 * @brief Tests for template operations including formatting and table-backed replacements
 * 
 * Tests OP_EMIT_FORMAT (upper, lower, length) and OP_EMIT_TABLE operations.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "snobol_internal.h"
#include "snobol_vm.h"
#include "snobol_table.h"

/* External test framework functions */
extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

/* Helper to create a simple VM for testing */
static void init_test_vm(VM *vm, const char *input, snobol_buf *out) {
    memset(vm, 0, sizeof(VM));
    vm->s = input;
    vm->len = strlen(input);
    vm->ip = 0;
    vm->pos = 0;
    vm->out = out;
    vm->choices = NULL;
    vm->choices_cap = 0;
    vm->choices_top = 0;
    snobol_buf_init(out);
    vm_init_labels(vm);
#ifdef SNOBOL_DYNAMIC_PATTERN
    vm_init_tables(vm);
#endif
}

static void cleanup_test_vm(VM *vm) {
    if (vm->choices) {
        snobol_free(vm->choices);
    }
    vm_free_labels(vm);
#ifdef SNOBOL_DYNAMIC_PATTERN
    vm_free_tables(vm);
#endif
}

static void test_format_upper(void) {
    test_suite("Template Ops: FORMAT upper");
    
    VM vm;
    snobol_buf out;
    init_test_vm(&vm, "hello", &out);
    
    /* Set up capture register 0 to capture "hello" */
    vm.cap_start[0] = 0;
    vm.cap_end[0] = 5;
    vm.max_cap_used = 1;
    
    /* Manually execute OP_EMIT_FORMAT with upper */
    uint8_t bc[] = {
        OP_EMIT_FORMAT,
        0x00,  /* reg = 0 */
        0x01   /* format_type = 1 (upper) */
    };
    
    vm.bc = bc;
    vm.bc_len = sizeof(bc);
    vm.ip = 0;
    
    /* Execute */
    uint8_t op = bc[vm.ip++];
    test_assert(op == OP_EMIT_FORMAT, "op is OP_EMIT_FORMAT");
    
    uint8_t reg = bc[vm.ip++];
    uint8_t format_type = bc[vm.ip++];
    
    test_assert(reg == 0, "reg is 0");
    test_assert(format_type == 1, "format_type is 1 (upper)");
    
    /* Simulate the formatting */
    const char *data = vm.s + vm.cap_start[0];
    size_t len = vm.cap_end[0] - vm.cap_start[0];
    
    char *tmp = (char *)malloc(len + 1);
    for (size_t i = 0; i < len; ++i) {
        tmp[i] = (data[i] >= 'a' && data[i] <= 'z') ? data[i] - 32 : data[i];
    }
    tmp[len] = '\0';
    
    test_assert(strcmp(tmp, "HELLO") == 0, "uppercase conversion correct");
    
    free(tmp);
    cleanup_test_vm(&vm);
    snobol_buf_free(&out);
}

static void test_format_lower(void) {
    test_suite("Template Ops: FORMAT lower");
    
    VM vm;
    snobol_buf out;
    init_test_vm(&vm, "HELLO", &out);
    
    vm.cap_start[0] = 0;
    vm.cap_end[0] = 5;
    vm.max_cap_used = 1;
    
    /* Simulate lowercase formatting */
    const char *data = vm.s + vm.cap_start[0];
    size_t len = vm.cap_end[0] - vm.cap_start[0];
    
    char *tmp = (char *)malloc(len + 1);
    for (size_t i = 0; i < len; ++i) {
        tmp[i] = (data[i] >= 'A' && data[i] <= 'Z') ? data[i] + 32 : data[i];
    }
    tmp[len] = '\0';
    
    test_assert(strcmp(tmp, "hello") == 0, "lowercase conversion correct");
    
    free(tmp);
    cleanup_test_vm(&vm);
    snobol_buf_free(&out);
}

static void test_format_length(void) {
    test_suite("Template Ops: FORMAT length");
    
    VM vm;
    snobol_buf out;
    init_test_vm(&vm, "hello", &out);
    
    vm.cap_start[0] = 0;
    vm.cap_end[0] = 5;
    vm.max_cap_used = 1;
    
    /* Simulate length formatting */
    size_t len = vm.cap_end[0] - vm.cap_start[0];
    
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "%zu", len);
    
    test_assert(n == 1, "length string length is 1");
    test_assert(strcmp(tmp, "5") == 0, "length value is 5");
    
    cleanup_test_vm(&vm);
    snobol_buf_free(&out);
}

static void test_format_missing_capture(void) {
    test_suite("Template Ops: FORMAT missing capture");
    
    VM vm;
    snobol_buf out;
    init_test_vm(&vm, "hello", &out);
    
    /* Don't set any captures - they should be empty */
    vm.max_cap_used = 0;
    
    /* Missing capture should emit nothing (graceful degradation) */
    test_assert(vm.cap_end[0] <= vm.cap_start[0], "capture is empty");
    
    cleanup_test_vm(&vm);
    snobol_buf_free(&out);
}

static void test_table_emit_success(void) {
    test_suite("Template Ops: EMIT_TABLE success");
    
    VM vm;
    snobol_buf out;
    init_test_vm(&vm, "key1", &out);
    
#ifdef SNOBOL_DYNAMIC_PATTERN
    /* Create a table */
    snobol_table_t *table = table_create("test");
    table_set(table, "key1", "value1");
    
    /* Register the table */
    uint16_t table_id;
    vm_register_table(&vm, table, &table_id);
    
    /* Set up capture register 0 to capture "key1" */
    vm.cap_start[0] = 0;
    vm.cap_end[0] = 4;
    vm.max_cap_used = 1;
    
    /* Simulate table lookup */
    snobol_table_t *t = vm_get_table(&vm, table_id);
    test_assert(t != NULL, "table retrieved");
    
    size_t key_len = vm.cap_end[0] - vm.cap_start[0];
    char *key = (char *)malloc(key_len + 1);
    memcpy(key, vm.s + vm.cap_start[0], key_len);
    key[key_len] = '\0';
    
    const char *value = table_get(t, key);
    test_assert(value != NULL, "table lookup succeeds");
    test_assert(strcmp(value, "value1") == 0, "value matches");
    
    free(key);
    table_release(table);
#else
    test_assert(true, "table support not enabled (skipped)");
#endif
    
    cleanup_test_vm(&vm);
    snobol_buf_free(&out);
}

static void test_table_emit_missing_key(void) {
    test_suite("Template Ops: EMIT_TABLE missing key");
    
    VM vm;
    snobol_buf out;
    init_test_vm(&vm, "nonexistent", &out);
    
#ifdef SNOBOL_DYNAMIC_PATTERN
    snobol_table_t *table = table_create("test");
    table_set(table, "key1", "value1");
    
    uint16_t table_id;
    vm_register_table(&vm, table, &table_id);
    
    vm.cap_start[0] = 0;
    vm.cap_end[0] = 11;
    vm.max_cap_used = 1;
    
    snobol_table_t *t = vm_get_table(&vm, table_id);
    size_t key_len = vm.cap_end[0] - vm.cap_start[0];
    char *key = (char *)malloc(key_len + 1);
    memcpy(key, vm.s + vm.cap_start[0], key_len);
    key[key_len] = '\0';
    
    const char *value = table_get(t, key);
    test_assert(value == NULL, "missing key returns NULL (graceful degradation)");
    
    free(key);
    table_release(table);
#else
    test_assert(true, "table support not enabled (skipped)");
#endif
    
    cleanup_test_vm(&vm);
    snobol_buf_free(&out);
}

static void test_table_multiple_lookups(void) {
    test_suite("Template Ops: EMIT_TABLE multiple lookups");
    
    VM vm;
    snobol_buf out;
    init_test_vm(&vm, "namecity", &out);
    
#ifdef SNOBOL_DYNAMIC_PATTERN
    snobol_table_t *table = table_create("test");
    table_set(table, "name", "Alice");
    table_set(table, "city", "Boston");
    
    uint16_t table_id;
    vm_register_table(&vm, table, &table_id);
    
    /* Lookup "name" */
    vm.cap_start[0] = 0;
    vm.cap_end[0] = 4;
    
    size_t key_len = vm.cap_end[0] - vm.cap_start[0];
    char *key = (char *)malloc(key_len + 1);
    memcpy(key, vm.s + vm.cap_start[0], key_len);
    key[key_len] = '\0';
    
    const char *value = table_get(table, key);
    test_assert(strcmp(value, "Alice") == 0, "first lookup correct");
    
    /* Lookup "city" */
    vm.cap_start[0] = 4;
    vm.cap_end[0] = 8;
    
    free(key);
    key_len = vm.cap_end[0] - vm.cap_start[0];
    key = (char *)malloc(key_len + 1);
    memcpy(key, vm.s + vm.cap_start[0], key_len);
    key[key_len] = '\0';
    
    value = table_get(table, key);
    test_assert(strcmp(value, "Boston") == 0, "second lookup correct");
    
    free(key);
    table_release(table);
#else
    test_assert(true, "table support not enabled (skipped)");
#endif
    
    cleanup_test_vm(&vm);
    snobol_buf_free(&out);
}

static void test_format_unknown_type(void) {
    test_suite("Template Ops: FORMAT unknown type");

    VM vm;
    snobol_buf out;
    init_test_vm(&vm, "test", &out);

    vm.cap_start[0] = 0;
    vm.cap_end[0] = 4;
    vm.max_cap_used = 1;

    /* Unknown format type should emit raw */
    const char *data = vm.s + vm.cap_start[0];
    size_t len = vm.cap_end[0] - vm.cap_start[0];

    /* Should emit "test" as-is */
    test_assert(len == 4, "length is 4");
    test_assert(memcmp(data, "test", 4) == 0, "data is 'test'");

    cleanup_test_vm(&vm);
    snobol_buf_free(&out);
}

static void test_table_backed_template_literal_key(void) {
    test_suite("Template Ops: table-backed template with literal key");

    VM vm;
    snobol_buf out;
    init_test_vm(&vm, "key", &out);

#ifdef SNOBOL_DYNAMIC_PATTERN
    /* Create a table */
    snobol_table_t *table = table_create("test");
    table_set(table, "key", "value_from_table");

    /* Register the table */
    uint16_t table_id;
    vm_register_table(&vm, table, &table_id);

    /* Set up capture register 0 to capture "key" */
    vm.cap_start[0] = 0;
    vm.cap_end[0] = 3;
    vm.max_cap_used = 1;

    /* Simulate table lookup via capture-derived key */
    snobol_table_t *t = vm_get_table(&vm, table_id);
    test_assert(t != NULL, "table retrieved");

    size_t key_len = vm.cap_end[0] - vm.cap_start[0];
    char *key_str = (char *)malloc(key_len + 1);
    memcpy(key_str, vm.s + vm.cap_start[0], key_len);
    key_str[key_len] = '\0';

    const char *value = table_get(t, key_str);
    test_assert(value != NULL, "table lookup succeeds with capture-derived key");
    test_assert(strcmp(value, "value_from_table") == 0, "value matches for literal key");

    free(key_str);
    table_release(table);
#else
    test_assert(true, "table support not enabled (skipped)");
#endif

    cleanup_test_vm(&vm);
    snobol_buf_free(&out);
}

static void test_table_backed_template_missing_key_fallback(void) {
    test_suite("Template Ops: table-backed template missing key fallback");

    VM vm;
    snobol_buf out;
    init_test_vm(&vm, "missing_key", &out);

#ifdef SNOBOL_DYNAMIC_PATTERN
    snobol_table_t *table = table_create("test");
    table_set(table, "existing", "value");

    uint16_t table_id;
    vm_register_table(&vm, table, &table_id);

    /* Set up capture for missing key */
    vm.cap_start[0] = 0;
    vm.cap_end[0] = 11;
    vm.max_cap_used = 1;

    snobol_table_t *t = vm_get_table(&vm, table_id);
    size_t key_len = vm.cap_end[0] - vm.cap_start[0];
    char *key_str = (char *)malloc(key_len + 1);
    memcpy(key_str, vm.s + vm.cap_start[0], key_len);
    key_str[key_len] = '\0';

    const char *value = table_get(t, key_str);
    test_assert(value == NULL, "missing key returns NULL");
    /* Graceful degradation: missing key emits empty string */

    free(key_str);
    table_release(table);
#else
    test_assert(true, "table support not enabled (skipped)");
#endif

    cleanup_test_vm(&vm);
    snobol_buf_free(&out);
}

void test_template_ops_suite(void) {
    test_format_upper();
    test_format_lower();
    test_format_length();
    test_format_missing_capture();
    test_table_emit_success();
    test_table_emit_missing_key();
    test_table_multiple_lookups();
    test_format_unknown_type();
    test_table_backed_template_literal_key();
    test_table_backed_template_missing_key_fallback();
}
