#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "snobol/snobol_attrs.h"

#define ARRAY_INITIAL_CAPACITY 16

typedef struct array_entry {
    int32_t key;
    char *value;
    uint32_t hash;
    bool active;
} array_entry_t;

typedef struct snobol_array {
    array_entry_t *entries;
    size_t capacity;
    size_t size;
    size_t tombstones;
    uint32_t refcount;
    int32_t bounds_hint;
} snobol_array_t;

SNOBOL_NODISCARD snobol_array_t *snobol_array_create(int32_t bounds_hint);

SNOBOL_NODISCARD snobol_array_t *snobol_array_retain(snobol_array_t *array);

void snobol_array_release(snobol_array_t *array);

SNOBOL_NODISCARD bool snobol_array_set(snobol_array_t *array, int32_t key, const char *value);

const char *snobol_array_get(const snobol_array_t *array, int32_t key);

bool snobol_array_has(const snobol_array_t *array, int32_t key);

SNOBOL_NODISCARD bool snobol_array_delete(snobol_array_t *array, int32_t key);

void snobol_array_clear(snobol_array_t *array);

size_t snobol_array_size(const snobol_array_t *array);

int32_t *snobol_array_keys(const snobol_array_t *array, size_t *out_count);

char **snobol_array_values(const snobol_array_t *array, size_t *out_count);
