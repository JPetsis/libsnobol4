<?php

namespace Snobol;

/**
 * SNOBOL4 Text utility class.
 *
 * Provides static methods wrapping the libsnobol4 C built-in string
 * transformation and comparison functions.  All string operations are
 * Unicode codepoint-aware, with ASCII fast paths for common cases.
 *
 * v1.0: UPPER/LOWER are ASCII-only. Full Unicode case folding is planned for v2.0.
 *
 * @see https://github.com/libsnobol4/libsnobol4
 */
class Text
{
    /**
     * SIZE: count Unicode codepoints (not bytes).
     *
     * ASCII fast path: if the string is pure ASCII, this equals strlen().
     *
     * @param  string  $str  Input string (UTF-8)
     * @return int         Number of Unicode codepoints
     */
    public static function size(string $str): int
    {
        // Native implementation provided by C extension (snobol_size)
        return mb_strlen($str, 'UTF-8');
    }

    /**
     * TRIM: remove trailing whitespace (space, tab, CR, LF).
     *
     * @param  string  $str  Input string
     * @return string      String with trailing whitespace removed
     */
    public static function trim(string $str): string
    {
        // Native: snobol_trim – trims trailing whitespace only (not leading)
        return rtrim($str, " \t\r\n");
    }

    /**
     * DUPL: duplicate a string n times.
     *
     * @param  string  $str  Input string
     * @param  int  $n  Number of copies (0 = empty string)
     * @return string      String repeated n times
     */
    public static function dupl(string $str, int $n): string
    {
        // Native: snobol_dupl
        if ($n <= 0) {
            return '';
        }
        return str_repeat($str, $n);
    }

    /**
     * REVERSE: reverse a string by Unicode codepoints.
     *
     * Unlike PHP's strrev(), this operates on codepoints, not bytes,
     * so multi-byte characters are handled correctly.
     *
     * @param  string  $str  Input string (UTF-8)
     * @return string      String with codepoints in reverse order
     */
    public static function reverse(string $str): string
    {
        // Native: snobol_reverse
        if ($str === '') {
            return '';
        }
        // Collect codepoints and reverse
        $codepoints = preg_split('//u', $str, -1, PREG_SPLIT_NO_EMPTY);
        if ($codepoints === false) {
            return strrev($str); // fallback
        }
        return implode('', array_reverse($codepoints));
    }

    /**
     * SUBSTR: extract a substring by 1-based codepoint position and length.
     *
     * @param  string  $str  Input string (UTF-8)
     * @param  int  $pos  1-based codepoint position (1 = first character)
     * @param  int  $len  Maximum number of codepoints to extract
     * @return string|false   Substring, or false on invalid position
     */
    public static function substr(string $str, int $pos, int $len): string|false
    {
        // Native: snobol_substr (1-based)
        if ($pos < 1) {
            return false;
        }
        return mb_substr($str, $pos - 1, $len, 'UTF-8');
    }

    /**
     * REPLACE: replace all occurrences of $from with $to.
     *
     * @param  string  $str  Input string
     * @param  string  $from  Pattern to find
     * @param  string  $to  Replacement string
     * @return string       String with all occurrences replaced
     */
    public static function replace(string $str, string $from, string $to): string
    {
        // Native: snobol_replace
        if ($from === '') {
            return $str;
        }
        return str_replace($from, $to, $str);
    }

    /**
     * REPLACE_CHAR: character-by-character translation (like PHP strtr or POSIX tr).
     *
     * Each character in $from is replaced by the corresponding character in $to.
     * Characters without a mapping are preserved unchanged.
     *
     * @param  string  $str  Input string
     * @param  string  $from  Characters to translate (each char maps to corresp. in $to)
     * @param  string  $to  Replacement characters
     * @return string       Translated string
     */
    public static function replaceChar(string $str, string $from, string $to): string
    {
        // Native: snobol_replace_char
        $fromChars = str_split($from);
        $toChars = str_split($to);
        $map = [];
        $len = min(count($fromChars), count($toChars));
        for ($i = 0; $i < $len; $i++) {
            $map[$fromChars[$i]] = $toChars[$i];
        }
        return strtr($str, $map);
    }

    /**
     * LPAD: left-pad a string to the specified codepoint width.
     *
     * @param  string  $str  Input string
     * @param  int  $width  Target width in codepoints
     * @param  string  $pad  Pad character (default: space)
     * @return string         Left-padded string
     */
    public static function lpad(string $str, int $width, string $pad = ' '): string
    {
        // Native: snobol_lpad
        $cur = mb_strlen($str, 'UTF-8');
        if ($cur >= $width) {
            return $str;
        }
        $padChar = mb_substr($pad, 0, 1, 'UTF-8');
        if ($padChar === '') {
            $padChar = ' ';
        }
        return str_repeat($padChar, $width - $cur).$str;
    }

    /**
     * RPAD: right-pad a string to the specified codepoint width.
     *
     * @param  string  $str  Input string
     * @param  int  $width  Target width in codepoints
     * @param  string  $pad  Pad character (default: space)
     * @return string         Right-padded string
     */
    public static function rpad(string $str, int $width, string $pad = ' '): string
    {
        // Native: snobol_rpad
        $cur = mb_strlen($str, 'UTF-8');
        if ($cur >= $width) {
            return $str;
        }
        $padChar = mb_substr($pad, 0, 1, 'UTF-8');
        if ($padChar === '') {
            $padChar = ' ';
        }
        return $str.str_repeat($padChar, $width - $cur);
    }

    /**
     * CHAR: convert a Unicode codepoint to a UTF-8 string.
     *
     * @param  int  $codepoint  Unicode codepoint (0 to 0x10FFFF)
     * @return string|false           Single-character string, or false if invalid
     */
    public static function char(int $codepoint): string|false
    {
        // Native: snobol_char_fn
        if ($codepoint < 0 || $codepoint > 0x10FFFF) {
            return false;
        }
        if ($codepoint >= 0xD800 && $codepoint <= 0xDFFF) {
            return false; // Surrogate range
        }
        return mb_chr($codepoint, 'UTF-8');
    }

    /**
     * ORD: get the Unicode codepoint of the first character of a string.
     *
     * @param  string  $str  Input string (UTF-8)
     * @return int|false     Codepoint value, or false on empty/invalid input
     */
    public static function ord(string $str): int|false
    {
        // Native: snobol_ord
        if ($str === '') {
            return false;
        }
        return mb_ord($str, 'UTF-8');
    }

    /**
     * UPPER: convert string to uppercase.
     *
     * v1.0 note: ASCII-only (a-z → A-Z). Non-ASCII characters are unchanged.
     * v2.0 TODO: full Unicode case folding.
     *
     * @param  string  $str  Input string
     * @return string      Uppercase string
     */
    public static function upper(string $str): string
    {
        // Native: snobol_upper (ASCII fast path; v2 will use full Unicode)
        return strtoupper($str);
    }

    /**
     * LOWER: convert string to lowercase.
     *
     * v1.0 note: ASCII-only (A-Z → a-z). Non-ASCII characters are unchanged.
     * v2.0 TODO: full Unicode case folding.
     *
     * @param  string  $str  Input string
     * @return string      Lowercase string
     */
    public static function lower(string $str): string
    {
        // Native: snobol_lower (ASCII fast path; v2 will use full Unicode)
        return strtolower($str);
    }

    /**
     * IDENT: identity predicate – returns true if two strings are identical.
     *
     * @param  string  $a  First string
     * @param  string  $b  Second string
     * @return bool      true if strings are identical
     */
    public static function ident(string $a, string $b): bool
    {
        // Native: snobol_ident
        return $a === $b;
    }

    /**
     * DIFFER: difference predicate – returns true if two strings differ.
     *
     * @param  string  $a  First string
     * @param  string  $b  Second string
     * @return bool      true if strings differ
     */
    public static function differ(string $a, string $b): bool
    {
        // Native: snobol_differ
        return $a !== $b;
    }

    /**
     * LEXEQ: lexical equality – returns true if a == b lexicographically.
     *
     * @param  string  $a  First string
     * @param  string  $b  Second string
     * @return bool      true if a == b
     */
    public static function lexeq(string $a, string $b): bool
    {
        // Native: snobol_lexeq
        return strcmp($a, $b) === 0;
    }

    /**
     * LEXLT: lexical less-than – returns true if a < b lexicographically.
     *
     * @param  string  $a  First string
     * @param  string  $b  Second string
     * @return bool      true if a < b
     */
    public static function lexlt(string $a, string $b): bool
    {
        // Native: snobol_lexlt
        return strcmp($a, $b) < 0;
    }

    /**
     * LEXGT: lexical greater-than – returns true if a > b lexicographically.
     *
     * @param  string  $a  First string
     * @param  string  $b  Second string
     * @return bool      true if a > b
     */
    public static function lexgt(string $a, string $b): bool
    {
        // Native: snobol_lexgt
        return strcmp($a, $b) > 0;
    }

    /**
     * EQ: numeric equality – returns true if a and b represent the same
     * numeric value (e.g. eq("5","5.0") is true).
     *
     * @param  string  $a  First string
     * @param  string  $b  Second string
     * @return bool      true if numerically equal
     */
    public static function eq(string $a, string $b): bool
    {
        // Native: snobol_eq
        return (float)$a === (float)$b;
    }

    /**
     * NE: numeric not-equal – returns true if a and b represent different
     * numeric values.
     *
     * @param  string  $a  First string
     * @param  string  $b  Second string
     * @return bool      true if numerically not-equal
     */
    public static function ne(string $a, string $b): bool
    {
        // Native: snobol_ne
        return (float)$a !== (float)$b;
    }

    /**
     * LT: numeric less-than – returns true if a < b numerically.
     *
     * @param  string  $a  First string
     * @param  string  $b  Second string
     * @return bool      true if a < b
     */
    public static function lt(string $a, string $b): bool
    {
        // Native: snobol_lt
        return (float)$a < (float)$b;
    }

    /**
     * GT: numeric greater-than – returns true if a > b numerically.
     *
     * @param  string  $a  First string
     * @param  string  $b  Second string
     * @return bool      true if a > b
     */
    public static function gt(string $a, string $b): bool
    {
        // Native: snobol_gt
        return (float)$a > (float)$b;
    }

    /**
     * LE: numeric less-than-or-equal – returns true if a <= b numerically.
     *
     * @param  string  $a  First string
     * @param  string  $b  Second string
     * @return bool      true if a <= b
     */
    public static function le(string $a, string $b): bool
    {
        // Native: snobol_le
        return (float)$a <= (float)$b;
    }

    /**
     * GE: numeric greater-than-or-equal – returns true if a >= b numerically.
     *
     * @param  string  $a  First string
     * @param  string  $b  Second string
     * @return bool      true if a >= b
     */
    public static function ge(string $a, string $b): bool
    {
        // Native: snobol_ge
        return (float)$a >= (float)$b;
    }

    /**
     * NUMERIC: union type predicate – true if INTEGER or REAL.
     *
     * @param  string  $str  Input string
     * @return bool        true if string represents any numeric value
     */
    public static function numeric(string $str): bool
    {
        // Native: snobol_numeric
        return static::integer($str) || static::real($str);
    }

    /**
     * INTEGER: type predicate – returns true if the string represents an integer.
     *
     * Valid format: optional +/- sign followed by one or more decimal digits.
     *
     * @param  string  $str  Input string
     * @return bool        true if string represents an integer
     */
    public static function integer(string $str): bool
    {
        // Native: snobol_integer
        return (bool) preg_match('/^[+-]?\d+$/', $str);
    }

    /**
     * REAL: type predicate – returns true if the string represents a real number.
     *
     * @param  string  $str  Input string
     * @return bool        true if string represents a real number
     */
    public static function real(string $str): bool
    {
        // Native: snobol_real
        return (bool) preg_match('/^[+-]?\d+(\.\d*)?([eE][+-]?\d+)?$/', $str);
    }
}



