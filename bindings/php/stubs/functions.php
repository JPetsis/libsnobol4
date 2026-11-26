<?php

/**
 * SNOBOL4 global function stubs (native C extension).
 *
 * Type and documentation stubs for IDEs and static analysis. The real
 * implementations are registered by the extension
 * (bindings/php/src/php_snobol.c); this file documents exactly the functions
 * it registers, and nothing else. Each declaration is guarded with
 * function_exists() so loading it with the extension active does not
 * redeclare anything. In a process without the extension a call throws
 * through the shared guard instead of returning a neutral value.
 */

require_once __DIR__ . '/_extension_guard.php';

if (!function_exists('snobol_get_api_version')) {
    /**
     * Return the libsnobol4 API version as a packed integer
     * ((MAJOR << 16) | (MINOR << 8) | PATCH).
     *
     * @return int Packed version integer
     */
    function snobol_get_api_version(): int
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_get_abi_version')) {
    /**
     * Return the libsnobol4 ABI version, bumped whenever the public ABI
     * changes in a breaking way.
     *
     * @return int ABI version
     */
    function snobol_get_abi_version(): int
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_get_choice_stats')) {
    /**
     * Retrieve choice-point statistics from the VM.
     *
     * Keys: choice_push_count, choice_allocated, choice_stack_depth,
     * choice_stack_memory_usage. Global counters are best-effort; per-match
     * metrics are in the `_metrics` key of a match result.
     *
     * @return array<string, int>
     */
    function snobol_get_choice_stats(): array
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_char')) {
    /**
     * Character corresponding to a Unicode codepoint (SNOBOL CHAR).
     *
     * @param int $cp Codepoint
     * @return string
     */
    function snobol_text_char(int $cp)
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_differ')) {
    /**
     * True when the two strings differ.
     *
     * @param string $a @param string $b
     * @return bool
     */
    function snobol_text_differ(string $a, string $b): bool
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_dupl')) {
    /**
     * Repeat a string N times (SNOBOL DUPL).
     *
     * @param string $s @param int $n
     * @return string
     */
    function snobol_text_dupl(string $s, int $n): string
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_eq')) {
    /**
     * Equal comparison.
     *
     * @param string $a @param string $b
     * @return bool
     */
    function snobol_text_eq(string $a, string $b): bool
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_ge')) {
    /**
     * Greater-or-equal comparison.
     *
     * @param string $a @param string $b
     * @return bool
     */
    function snobol_text_ge(string $a, string $b): bool
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_gt')) {
    /**
     * Greater-than comparison.
     *
     * @param string $a @param string $b
     * @return bool
     */
    function snobol_text_gt(string $a, string $b): bool
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_ident')) {
    /**
     * Identical comparison (SNOBOL IDENT).
     *
     * @param string $a @param string $b
     * @return bool
     */
    function snobol_text_ident(string $a, string $b): bool
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_integer')) {
    /**
     * True when the string is an integer (SNOBOL INTEGER).
     *
     * @param string $s
     * @return bool
     */
    function snobol_text_integer(string $s): bool
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_le')) {
    /**
     * Less-or-equal comparison.
     *
     * @param string $a @param string $b
     * @return bool
     */
    function snobol_text_le(string $a, string $b): bool
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_lexeq')) {
    /**
     * Lexically equal comparison.
     *
     * @param string $a @param string $b
     * @return bool
     */
    function snobol_text_lexeq(string $a, string $b): bool
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_lexgt')) {
    /**
     * Lexically greater-than comparison.
     *
     * @param string $a @param string $b
     * @return bool
     */
    function snobol_text_lexgt(string $a, string $b): bool
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_lexlt')) {
    /**
     * Lexically less-than comparison.
     *
     * @param string $a @param string $b
     * @return bool
     */
    function snobol_text_lexlt(string $a, string $b): bool
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_lower')) {
    /**
     * Lowercase a string.
     *
     * @param string $s
     * @return string
     */
    function snobol_text_lower(string $s): string
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_lpad')) {
    /**
     * Left-pad to a width (SNOBOL LPAD).
     *
     * @param string $s @param int $width @param string $pad Defaults to a single space
     * @return string
     */
    function snobol_text_lpad(string $s, int $width, string $pad = ' '): string
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_lt')) {
    /**
     * Less-than comparison.
     *
     * @param string $a @param string $b
     * @return bool
     */
    function snobol_text_lt(string $a, string $b): bool
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_ne')) {
    /**
     * Not-equal comparison.
     *
     * @param string $a @param string $b
     * @return bool
     */
    function snobol_text_ne(string $a, string $b): bool
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_numeric')) {
    /**
     * True when the string is numeric (SNOBOL NUMERIC).
     *
     * @param string $s
     * @return bool
     */
    function snobol_text_numeric(string $s): bool
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_ord')) {
    /**
     * Codepoint of the first character (SNOBOL ORD).
     *
     * @param string $s
     * @return int
     */
    function snobol_text_ord(string $s)
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_real')) {
    /**
     * True when the string is a real number (SNOBOL REAL).
     *
     * @param string $s
     * @return bool
     */
    function snobol_text_real(string $s): bool
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_replace')) {
    /**
     * Replace every occurrence of a substring.
     *
     * @param string $s @param string $from @param string $to
     * @return string
     */
    function snobol_text_replace(string $s, string $from, string $to): string
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_replace_char')) {
    /**
     * Replace every occurrence of a single character.
     *
     * @param string $s @param string $from @param string $to
     * @return string
     */
    function snobol_text_replace_char(string $s, string $from, string $to): string
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_reverse')) {
    /**
     * Reverse a string.
     *
     * @param string $s
     * @return string
     */
    function snobol_text_reverse(string $s): string
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_rpad')) {
    /**
     * Right-pad to a width (SNOBOL RPAD).
     *
     * @param string $s @param int $width @param string $pad Defaults to a single space
     * @return string
     */
    function snobol_text_rpad(string $s, int $width, string $pad = ' '): string
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_size')) {
    /**
     * Byte length of a string (SNOBOL SIZE).
     *
     * @param string $s
     * @return int
     */
    function snobol_text_size(string $s): int
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_substr')) {
    /**
     * Substring by position and length (SNOBOL SUBSTR).
     *
     * @param string $s @param int $pos Zero-based byte offset @param int $len Byte length
     * @return string
     */
    function snobol_text_substr(string $s, int $pos, int $len)
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_trim')) {
    /**
     * Trim whitespace from both ends (SNOBOL TRIM).
     *
     * @param string $s
     * @return string
     */
    function snobol_text_trim(string $s): string
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}

if (!function_exists('snobol_text_upper')) {
    /**
     * Uppercase a string.
     *
     * @param string $s
     * @return string
     */
    function snobol_text_upper(string $s): string
    {
        \Snobol\stub_extension_required(__FUNCTION__);
    }
}
