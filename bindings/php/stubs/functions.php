<?php

/**
 * SNOBOL4 global functions (Native C Extension stubs).
 *
 * This file serves as a type hint and documentation stub for IDEs and static
 * analysis tools. The actual implementations are provided by the native C
 * extension (bindings/php/src/php_snobol.c).
 *
 * Each declaration is guarded with function_exists() so that loading this
 * file when the C extension is already active does not cause redeclaration
 * errors.
 */
}

if (!function_exists('snobol_get_api_version')) {
    /**
     * Return the libsnobol4 API version as a packed integer.
     *
     * Encoding: (MAJOR << 16) | (MINOR << 8) | PATCH
     *
     * For v0.11.0 this returns 0x00000B00 (2816).
     * Extract components:
     *   $major = ($v >> 16) & 0xFF;
     *   $minor = ($v >>  8) & 0xFF;
     *   $patch = $v & 0xFF;
     *
     * @return int Packed version integer
     */
    function snobol_get_api_version(): int
    {
        // Native implementation in C extension
        return 0;
    }
}

if (!function_exists('snobol_get_abi_version')) {
    /**
     * Return the libsnobol4 ABI version as an integer.
     *
     * The ABI version is a monotonically-increasing integer that is bumped
     * whenever the public ABI changes in a breaking way.  The initial value
     * is 1.
     *
     * @return int ABI version
     */
    function snobol_get_abi_version(): int
    {
        // Native implementation in C extension
        return 0;
    }
}

if (!function_exists('snobol_get_choice_stats')) {
    /**
     * Retrieve choice-point statistics from the VM.
     *
     * Returns an associative array of metrics related to backtracking and
     * memory usage of the choice-stack.
     * Note: Global counters are currently best-effort; per-match metrics
     * are available in the `_metrics` key of any match result.
     *
     * Available keys:
     *   - choice_push_count         Total choice points pushed
     *   - choice_allocated         Total choice points allocated
     *   - choice_stack_depth       Current/maximum stack depth
     *   - choice_stack_memory_usage Bytes currently used by the stack
     *
     * @return array<string, int>
     */
    function snobol_get_choice_stats(): array
    {
        // Native implementation in C extension
        return [];
    }
}


if (!function_exists('snobol_table_create')) {
    /**
     * Create a new SNOBOL4 table resource.
     *
     * @param  string  $name  Optional table name for debugging
     * @return resource Table resource handle
     */
    function snobol_table_create(string $name = '')
    {
        // Native implementation in C extension
    }
}

if (!function_exists('snobol_table_set')) {
    /**
     * Set a key-value pair in a table.
     *
     * @param  resource  $table  Table resource from snobol_table_create()
     * @param  string  $key
     * @param  string  $value
     * @return bool Success
     */
    function snobol_table_set($table, string $key, string $value): bool
    {
        // Native implementation in C extension
        return false;
    }
}

if (!function_exists('snobol_table_get')) {
    /**
     * Get a value by key from a table.
     *
     * @param  resource  $table  Table resource from snobol_table_create()
     * @param  string  $key
     * @return string|null Value or null if not found
     */
    function snobol_table_get($table, string $key): ?string
    {
        // Native implementation in C extension
        return null;
    }
}

if (!function_exists('snobol_table_has')) {
    /**
     * Check if a key exists in a table.
     *
     * @param  resource  $table  Table resource from snobol_table_create()
     * @param  string  $key
     * @return bool
     */
    function snobol_table_has($table, string $key): bool
    {
        // Native implementation in C extension
        return false;
    }
}

if (!function_exists('snobol_table_delete')) {
    /**
     * Delete a key from a table.
     *
     * @param  resource  $table  Table resource from snobol_table_create()
     * @param  string  $key
     * @return bool Success
     */
    function snobol_table_delete($table, string $key): bool
    {
        // Native implementation in C extension
        return false;
    }
}

if (!function_exists('snobol_table_size')) {
    /**
     * Get the number of entries in a table.
     *
     * @param  resource  $table  Table resource from snobol_table_create()
     * @return int Number of entries
     */
    function snobol_table_size($table): int
    {
        // Native implementation in C extension
        return 0;
    }
}

if (!function_exists('snobol_table_name')) {
    /**
     * Get the name of a table.
     *
     * @param  resource  $table  Table resource from snobol_table_create()
     * @return string Table name
     */
    function snobol_table_name($table): string
    {
        // Native implementation in C extension
        return '';
    }
}

if (!function_exists('snobol_table_clear')) {
    /**
     * Clear all entries from a table.
     *
     * @param  resource  $table  Table resource from snobol_table_create()
     * @return void
     */
    function snobol_table_clear($table): void
    {
        // Native implementation in C extension
    }
}

if (!function_exists('snobol_table_keys')) {
    /**
     * Get all keys from a table.
     *
     * @param  resource  $table  Table resource from snobol_table_create()
     * @return array List of keys
     */
    function snobol_table_keys($table): array
    {
        // Native implementation in C extension
        return [];
    }
}

if (!function_exists('snobol_table_values')) {
    /**
     * Get all values from a table.
     *
     * @param  resource  $table  Table resource from snobol_table_create()
     * @return array List of values
     */
    function snobol_table_values($table): array
    {
        // Native implementation in C extension
        return [];
    }
}

if (!function_exists('snobol_table_to_array')) {
    /**
     * Convert a table to an associative PHP array.
     *
     * @param  resource  $table  Table resource from snobol_table_create()
     * @return array Associative array of key-value pairs
     */
    function snobol_table_to_array($table): array
    {
        // Native implementation in C extension
        return [];
    }
}


