/**
 * @file string_fn.h
 * @brief SNOBOL4 built-in string transformation functions
 *
 * All functions operate on UTF-8 encoded strings.
 * Codepoint semantics: positions and lengths are in Unicode codepoints, not bytes.
 * ASCII fast path: if all bytes are < 0x80, faster byte-level operations are used.
 *
 * Output is always written to a snobol_buf (from vm.h).
 * Functions return true on success, false on error/invalid input.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "snobol/vm.h"   /* for snobol_buf */

/**
 * SIZE: count Unicode codepoints (not bytes).
 * ASCII fast path: if all bytes < 0x80, returns byte length directly.
 * @param str  Input string (UTF-8, not required to be null-terminated)
 * @param len  Byte length of str
 * @return     Number of Unicode codepoints
 */
size_t snobol_size(const char *str, size_t len);

/**
 * TRIM: remove trailing whitespace (space, tab, CR, LF).
 * Unicode-aware: stops at first non-whitespace from the right.
 * @param in      Input string
 * @param in_len  Byte length of input
 * @param out     Output buffer (cleared and filled)
 * @return        true always (function cannot fail on valid input)
 */
bool snobol_trim(const char *in, size_t in_len, snobol_buf *out);

/**
 * DUPL: duplicate a string n times.
 * Pre-allocates the output buffer.
 * @param str      Input string
 * @param str_len  Byte length of input
 * @param n        Number of copies
 * @param out      Output buffer
 * @return         true on success
 */
bool snobol_dupl(const char *str, size_t str_len, size_t n, snobol_buf *out);

/**
 * REVERSE: reverse a string by Unicode codepoints (not bytes).
 * Two-pass implementation: first collect codepoint spans, then reverse.
 * @param str      Input string (UTF-8)
 * @param str_len  Byte length
 * @param out      Output buffer
 * @return         true on success
 */
bool snobol_reverse(const char *str, size_t str_len, snobol_buf *out);

/**
 * SUBSTR: extract substring by 1-based codepoint position and codepoint length.
 * @param str      Input string (UTF-8)
 * @param str_len  Byte length
 * @param pos      1-based codepoint position (1 = first character)
 * @param len      Maximum number of codepoints to extract
 * @param out      Output buffer
 * @return         true on success, false if pos is out of range
 */
bool snobol_substr(const char *str, size_t str_len, size_t pos, size_t len,
                   snobol_buf *out);

/**
 * REPLACE: replace all occurrences of [from] with [to].
 * O(n*m) where m is the number of occurrences.
 * @param str       Input string
 * @param str_len   Byte length
 * @param from      Pattern to find
 * @param from_len  Byte length of pattern
 * @param to        Replacement string
 * @param to_len    Byte length of replacement
 * @param out       Output buffer
 * @return          true on success
 */
bool snobol_replace(const char *str, size_t str_len,
                    const char *from, size_t from_len,
                    const char *to, size_t to_len,
                    snobol_buf *out);

/**
 * REPLACE_CHAR: character-by-character translation using a 256-byte lookup table.
 * Equivalent to POSIX tr. Each character in [from] is replaced by the
 * corresponding character in [to]. Characters in [from] beyond the length of
 * [to] are left unchanged.
 * O(n) single-pass implementation.
 * @param str       Input string
 * @param str_len   Byte length
 * @param from      Characters to translate
 * @param from_len  Length of [from]
 * @param to        Replacement characters
 * @param to_len    Length of [to]
 * @param out       Output buffer
 * @return          true on success
 */
bool snobol_replace_char(const char *str, size_t str_len,
                         const char *from, size_t from_len,
                         const char *to, size_t to_len,
                         snobol_buf *out);

/**
 * LPAD: left-pad a string to [width] codepoints using [pad_cp] as fill character.
 * If string is already as wide as or wider than [width], returns the string unchanged.
 * @param str      Input string
 * @param str_len  Byte length
 * @param width    Target width in codepoints
 * @param pad_cp   Pad character as Unicode codepoint
 * @param out      Output buffer
 * @return         true on success
 */
bool snobol_lpad(const char *str, size_t str_len, size_t width, uint32_t pad_cp,
                 snobol_buf *out);

/**
 * RPAD: right-pad a string to [width] codepoints using [pad_cp] as fill character.
 * If string is already as wide as or wider than [width], returns the string unchanged.
 * @param str      Input string
 * @param str_len  Byte length
 * @param width    Target width in codepoints
 * @param pad_cp   Pad character as Unicode codepoint
 * @param out      Output buffer
 * @return         true on success
 */
bool snobol_rpad(const char *str, size_t str_len, size_t width, uint32_t pad_cp,
                 snobol_buf *out);

/**
 * CHAR: convert a Unicode codepoint to a UTF-8 string.
 * @param cp   Unicode codepoint (0 to 0x10FFFF)
 * @param out  Output buffer
 * @return     true on success, false if cp > 0x10FFFF or is a surrogate
 */
bool snobol_char_fn(uint32_t cp, snobol_buf *out);

/**
 * ORD: get the Unicode codepoint of the first character in a string.
 * @param str     Input string (UTF-8)
 * @param str_len Byte length
 * @param out_cp  Output codepoint
 * @return        true on success, false if string is empty or invalid UTF-8
 */
bool snobol_ord(const char *str, size_t str_len, uint32_t *out_cp);

/**
 * UPPER: convert string to uppercase.
 * v1.0: ASCII fast path only (a-z → A-Z). Non-ASCII bytes are preserved.
 * v2.0 TODO: Full Unicode case folding.
 * @param str      Input string
 * @param str_len  Byte length
 * @param out      Output buffer
 * @return         true on success
 */
bool snobol_upper(const char *str, size_t str_len, snobol_buf *out);

/**
 * LOWER: convert string to lowercase.
 * v1.0: ASCII fast path only (A-Z → a-z). Non-ASCII bytes are preserved.
 * v2.0 TODO: Full Unicode case folding.
 * @param str      Input string
 * @param str_len  Byte length
 * @param out      Output buffer
 * @return         true on success
 */
bool snobol_lower(const char *str, size_t str_len, snobol_buf *out);

