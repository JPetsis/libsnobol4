#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "../../core/include/snobol/snobol_internal.h"
#include "snobol/array.h"

extern void test_suite(const char *name);
extern void test_assert(bool condition, const char *message);

static void test_array_create_free(void) {
    test_suite("Array: create and free");

    snobol_array_t *array = snobol_array_create(10);
    test_assert(array != NULL, "snobol_array_create returns non-NULL");
    test_assert(snobol_array_size(array) == 0, "new array has size 0");

    snobol_array_release(array);
    test_assert(true, "snobol_array_release completes without error");
}

static void test_array_create_no_hint(void) {
    test_suite("Array: create without hint");

    snobol_array_t *array = snobol_array_create(0);
    test_assert(array != NULL, "snobol_array_create(0) returns non-NULL");

    snobol_array_release(array);
}

static void test_array_set_get(void) {
    test_suite("Array: set and get");

    snobol_array_t *array = snobol_array_create(0);

    bool result = snobol_array_set(array, 1, "hello");
    test_assert(result == true, "snobol_array_set returns true");
    test_assert(snobol_array_size(array) == 1, "size is 1 after insert");

    const char *value = snobol_array_get(array, 1);
    test_assert(value != NULL, "snobol_array_get returns non-NULL for existing key");
    test_assert(strcmp(value, "hello") == 0, "value matches inserted value");

    snobol_array_release(array);
}

static void test_array_get_unset(void) {
    test_suite("Array: get unset element");

    snobol_array_t *array = snobol_array_create(0);

    const char *value = snobol_array_get(array, 42);
    test_assert(value == NULL, "unset element returns NULL");

    snobol_array_release(array);
}

static void test_array_update(void) {
    test_suite("Array: update existing key");

    snobol_array_t *array = snobol_array_create(0);

    (void)snobol_array_set(array, 5, "original");
    test_assert(snobol_array_size(array) == 1, "size is 1 after first insert");

    (void)snobol_array_set(array, 5, "updated");
    test_assert(snobol_array_size(array) == 1, "size is still 1 after update");

    const char *value = snobol_array_get(array, 5);
    test_assert(strcmp(value, "updated") == 0, "value is updated");

    snobol_array_release(array);
}

static void test_array_delete(void) {
    test_suite("Array: delete key");

    snobol_array_t *array = snobol_array_create(0);

    (void)snobol_array_set(array, 1, "value1");
    (void)snobol_array_set(array, 2, "value2");
    test_assert(snobol_array_size(array) == 2, "size is 2");

    bool deleted = snobol_array_delete(array, 1);
    test_assert(deleted == true, "snobol_array_delete returns true for existing key");
    test_assert(snobol_array_size(array) == 1, "size is 1 after delete");

    const char *value = snobol_array_get(array, 1);
    test_assert(value == NULL, "snobol_array_get returns NULL for deleted key");

    snobol_array_release(array);
}

static void test_array_delete_nonexistent(void) {
    test_suite("Array: delete non-existent key");

    snobol_array_t *array = snobol_array_create(0);

    bool deleted = snobol_array_delete(array, 99);
    test_assert(deleted == false, "delete non-existent returns false");

    snobol_array_release(array);
}

static void test_array_clear(void) {
    test_suite("Array: clear");

    snobol_array_t *array = snobol_array_create(0);

    (void)snobol_array_set(array, 1, "a");
    (void)snobol_array_set(array, 2, "b");
    (void)snobol_array_set(array, 3, "c");
    test_assert(snobol_array_size(array) == 3, "size is 3");

    snobol_array_clear(array);
    test_assert(snobol_array_size(array) == 0, "size is 0 after clear");

    snobol_array_release(array);
}

static void test_array_sparse_access(void) {
    test_suite("Array: sparse access");

    snobol_array_t *array = snobol_array_create(0);

    (void)snobol_array_set(array, 1, "first");
    (void)snobol_array_set(array, 100, "hundredth");
    (void)snobol_array_set(array, 1000, "thousandth");

    test_assert(snobol_array_size(array) == 3, "size is 3 for sparse entries");

    const char *v1 = snobol_array_get(array, 1);
    const char *v100 = snobol_array_get(array, 100);
    const char *v1000 = snobol_array_get(array, 1000);

    test_assert(strcmp(v1, "first") == 0, "index 1 has 'first'");
    test_assert(strcmp(v100, "hundredth") == 0, "index 100 has 'hundredth'");
    test_assert(strcmp(v1000, "thousandth") == 0, "index 1000 has 'thousandth'");

    test_assert(snobol_array_get(array, 50) == NULL, "unset index 50 returns NULL");

    snobol_array_release(array);
}

static void test_array_has(void) {
    test_suite("Array: has");

    snobol_array_t *array = snobol_array_create(0);
    test_assert(snobol_array_has(array, 1) == false, "unset key has returns false");

    (void)snobol_array_set(array, 1, "hello");
    test_assert(snobol_array_has(array, 1) == true, "set key has returns true");

    snobol_array_release(array);
}

static void test_array_retain_release(void) {
    test_suite("Array: retain and release");

    snobol_array_t *array = snobol_array_create(0);

    (void)snobol_array_retain(array);
    test_assert(true, "retain succeeds");

    snobol_array_release(array);
    snobol_array_release(array);
    test_assert(true, "double release succeeds (freed on second)");
}

static void test_array_keys_values(void) {
    test_suite("Array: keys and values");

    snobol_array_t *array = snobol_array_create(0);

    (void)snobol_array_set(array, 3, "three");
    (void)snobol_array_set(array, 1, "one");
    (void)snobol_array_set(array, 2, "two");

    size_t kcount;
    int32_t *keys = snobol_array_keys(array, &kcount);
    test_assert(kcount == 3, "keys count is 3");
    if (keys) {
        snobol_free(keys);
    }

    size_t vcount;
    char **values = snobol_array_values(array, &vcount);
    test_assert(vcount == 3, "values count is 3");
    if (values) {
        for (size_t i = 0; i < vcount; i++) {
            if (values[i]) snobol_free(values[i]);
        }
        snobol_free(values);
    }

    snobol_array_release(array);
}

void test_array_suite(void) {
    test_array_create_free();
    test_array_create_no_hint();
    test_array_set_get();
    test_array_get_unset();
    test_array_update();
    test_array_delete();
    test_array_delete_nonexistent();
    test_array_clear();
    test_array_sparse_access();
    test_array_has();
    test_array_retain_release();
    test_array_keys_values();
}
