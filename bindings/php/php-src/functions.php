<?php

/**
 * SNOBOL4 global functions (Native C Extension stubs).
 *
 * This file serves as a type hint and documentation stub for IDEs and static
 * analysis tools. The actual implementations are provided by the native C
 * extension (snobol4-php/php_snobol.c) and are only available when the
 * extension is built with SNOBOL_JIT support.
 */

/**
 * Retrieve current JIT statistics counters.
 *
 * Returns an associative array of cumulative counters collected by the
 * micro-JIT engine since the last call to snobol_reset_jit_stats() (or
 * since the extension was loaded).
 *
 * Available keys:
 *   - jit_compilations_total       Number of bytecode→native compilations
 *   - jit_cache_hits_total         Number of times a cached trace was reused
 *   - jit_entries_total            Number of times JIT-compiled code was entered
 *   - jit_exits_total              Number of times execution left JIT code
 *   - jit_bailouts_total           Total bailouts (all reasons combined)
 *   - jit_time_ns_total            Total nanoseconds spent in JIT code
 *   - choice_push_total            Number of choice-point pushes (SPLIT ops)
 *   - choice_pop_total             Number of choice-point pops (backtrack steps)
 *   - choice_bytes_total           Total bytes allocated for choice points
 *   - jit_compile_time_ns_total    Nanoseconds spent compiling traces
 *   - jit_exec_time_ns_total       Nanoseconds spent executing JIT traces
 *   - jit_interp_time_ns_total     Nanoseconds spent in the interpreter fallback
 *   - jit_skipped_cold_total       Compilations skipped (pattern not hot enough)
 *   - jit_skipped_exit_rate_total  Compilations skipped (exit rate too high)
 *   - jit_skipped_budget_total     Compilations skipped (budget exhausted)
 *   - jit_bailout_match_fail_total Bailouts caused by a match failure
 *   - jit_bailout_partial_total    Bailouts caused by a partial/incomplete match
 *
 * @return array<string, int> Associative array of JIT counter values
 */
function snobol_get_jit_stats(): array
{
    // Native implementation in C extension
    return [];
}

/**
 * Reset all JIT statistics counters to zero.
 *
 * Resets every counter returned by snobol_get_jit_stats() back to 0.
 * Useful for isolating the statistics of a specific code section.
 *
 * @return void
 */
function snobol_reset_jit_stats(): void
{
    // Native implementation in C extension
}

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

/**
 * Set a key-value pair in a table.
 *
 * @param  resource  $table  Table resource from snobol_table_create()
 * @param  string    $key
 * @param  string    $value
 * @return bool Success
 */
function snobol_table_set($table, string $key, string $value): bool
{
    // Native implementation in C extension
    return false;
}

/**
 * Get a value by key from a table.
 *
 * @param  resource  $table  Table resource from snobol_table_create()
 * @param  string    $key
 * @return string|null Value or null if not found
 */
function snobol_table_get($table, string $key): ?string
{
    // Native implementation in C extension
    return null;
}

/**
 * Check if a key exists in a table.
 *
 * @param  resource  $table  Table resource from snobol_table_create()
 * @param  string    $key
 * @return bool
 */
function snobol_table_has($table, string $key): bool
{
    // Native implementation in C extension
    return false;
}

/**
 * Delete a key from a table.
 *
 * @param  resource  $table  Table resource from snobol_table_create()
 * @param  string    $key
 * @return bool Success
 */
function snobol_table_delete($table, string $key): bool
{
    // Native implementation in C extension
    return false;
}

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
