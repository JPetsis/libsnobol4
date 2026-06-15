<?php

/**
 * SNOBOL4 global functions (Native C Extension stubs).
 *
 * This file serves as a type hint and documentation stub for IDEs and static
 * analysis tools. The actual implementations are provided by the native C
 * extension (snobol4-php/php_snobol.c) and are only available when the
 * extension is built with SNOBOL_JIT support.
 *
 * Each declaration is guarded with function_exists() so that loading this
 * file when the C extension is already active does not cause redeclaration
 * errors.
 */

if (!function_exists('snobol_get_jit_stats')) {
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
}

if (!function_exists('snobol_get_api_version')) {
    /**
     * Return the libsnobol4 API version as a packed integer.
     *
     * Encoding: (MAJOR << 16) | (MINOR << 8) | PATCH
     *
     * For v0.7.0 this returns 0x00000700 (1792).
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

if (!function_exists('snobol_reset_jit_stats')) {
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

if (!function_exists('snobol_text_eq')) {
    /**
     * Compare two strings numerically for equality (SNOBOL4 EQ).
     *
     * Both strings are converted to doubles via strtod(); non-numeric strings
     * become 0.0. Returns true if the values are equal.
     *
     * @param  string  $a  First operand
     * @param  string  $b  Second operand
     * @return bool True if numerically equal
     */
    function snobol_text_eq(string $a, string $b): bool
    {
        return (float)$a === (float)$b;
    }
}

if (!function_exists('snobol_text_ne')) {
    /**
     * Compare two strings numerically for inequality (SNOBOL4 NE).
     *
     * @param  string  $a  First operand
     * @param  string  $b  Second operand
     * @return bool True if numerically not equal
     */
    function snobol_text_ne(string $a, string $b): bool
    {
        return (float)$a !== (float)$b;
    }
}

if (!function_exists('snobol_text_lt')) {
    /**
     * Compare two strings numerically for less-than (SNOBOL4 LT).
     *
     * @param  string  $a  First operand
     * @param  string  $b  Second operand
     * @return bool True if $a < $b numerically
     */
    function snobol_text_lt(string $a, string $b): bool
    {
        return (float)$a < (float)$b;
    }
}

if (!function_exists('snobol_text_gt')) {
    /**
     * Compare two strings numerically for greater-than (SNOBOL4 GT).
     *
     * @param  string  $a  First operand
     * @param  string  $b  Second operand
     * @return bool True if $a > $b numerically
     */
    function snobol_text_gt(string $a, string $b): bool
    {
        return (float)$a > (float)$b;
    }
}

if (!function_exists('snobol_text_le')) {
    /**
     * Compare two strings numerically for less-than-or-equal (SNOBOL4 LE).
     *
     * @param  string  $a  First operand
     * @param  string  $b  Second operand
     * @return bool True if $a <= $b numerically
     */
    function snobol_text_le(string $a, string $b): bool
    {
        return (float)$a <= (float)$b;
    }
}

if (!function_exists('snobol_text_ge')) {
    /**
     * Compare two strings numerically for greater-than-or-equal (SNOBOL4 GE).
     *
     * @param  string  $a  First operand
     * @param  string  $b  Second operand
     * @return bool True if $a >= $b numerically
     */
    function snobol_text_ge(string $a, string $b): bool
    {
        return (float)$a >= (float)$b;
    }
}
