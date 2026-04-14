<?php
/**
 * @file text_functions.php
 * @brief libsnobol4 PHP built-in text function examples
 *
 * Demonstrates the Snobol\Text class — PHP wrappers for the libsnobol4
 * C built-in string transformation and comparison functions.
 *
 * Run from the bindings/php directory (with DDEV or after installing the
 * extension manually):
 *
 *   ddev exec "php /var/www/html/../../../examples/php/text_functions.php"
 *
 * Or from the project root if the extension is installed system-wide:
 *
 *   php examples/php/text_functions.php
 */

// Try both autoload locations (project root vs DDEV container)
$autoload = file_exists(__DIR__.'/../../vendor/autoload.php')
    ? __DIR__.'/../../vendor/autoload.php'
    : __DIR__.'/../../bindings/php/vendor/autoload.php';

if (file_exists($autoload)) {
    require $autoload;
} else {
    // When running standalone, define minimal stubs so the file still parses
    echo "Note: autoload not found – running in documentation-only mode.\n\n";
}

// Only execute examples when the Snobol extension is loaded
$hasExtension = extension_loaded('snobol');

use Snobol\Builder as B;
use Snobol\PatternHelper as PH;
use Snobol\Text;

echo "libsnobol4 PHP Text Function Examples\n";
echo "======================================\n\n";

// -------------------------------------------------------------------------
// SIZE: Unicode codepoint count
// -------------------------------------------------------------------------
echo "SIZE\n";
if ($hasExtension) {
    echo "  Text::size('hello')    = ".Text::size('hello')."\n";      // 5
    echo "  Text::size('café')     = ".Text::size('café')."\n";       // 4 codepoints
    echo "  Text::size('こんにちは') = ".Text::size('こんにちは')."\n"; // 5
} else {
    echo "  Text::size('hello')    → 5\n";
    echo "  Text::size('café')     → 4  (codepoints, not bytes)\n";
}
echo "\n";

// -------------------------------------------------------------------------
// TRIM: remove trailing whitespace
// -------------------------------------------------------------------------
echo "TRIM\n";
if ($hasExtension) {
    $s = "hello   \t\n";
    echo "  Text::trim('hello   \\t\\n')  = '".Text::trim($s)."'\n";
} else {
    echo "  Text::trim('hello   \\t\\n')  → 'hello'\n";
}
echo "\n";

// -------------------------------------------------------------------------
// DUPL: repeat a string N times
// -------------------------------------------------------------------------
echo "DUPL\n";
if ($hasExtension) {
    echo "  Text::dupl('ab', 4)  = '".Text::dupl('ab', 4)."'\n"; // 'abababab'
    echo "  Text::dupl('x', 0)   = '".Text::dupl('x', 0)."'\n";  // ''
} else {
    echo "  Text::dupl('ab', 4)  → 'abababab'\n";
}
echo "\n";

// -------------------------------------------------------------------------
// REVERSE: reverse by Unicode codepoints
// -------------------------------------------------------------------------
echo "REVERSE\n";
if ($hasExtension) {
    echo "  Text::reverse('hello')  = '".Text::reverse('hello')."'\n"; // 'olleh'
    echo "  Text::reverse('café')   = '".Text::reverse('café')."'\n";  // 'éfac'
} else {
    echo "  Text::reverse('hello')  → 'olleh'\n";
    echo "  Text::reverse('café')   → 'éfac'  (codepoint-safe)\n";
}
echo "\n";

// -------------------------------------------------------------------------
// SUBSTR: 1-based codepoint substring extraction
// -------------------------------------------------------------------------
echo "SUBSTR\n";
if ($hasExtension) {
    echo "  Text::substr('hello world', 7, 5)  = '".Text::substr('hello world', 7, 5)."'\n"; // 'world'
    echo "  Text::substr('café', 3, 2)          = '".Text::substr('café', 3, 2)."'\n";        // 'fé'
} else {
    echo "  Text::substr('hello world', 7, 5)  → 'world'  (1-based, codepoint)\n";
}
echo "\n";

// -------------------------------------------------------------------------
// REPLACE: substitute all occurrences of a substring
// -------------------------------------------------------------------------
echo "REPLACE\n";
if ($hasExtension) {
    echo "  Text::replace('foo bar foo', 'foo', 'qux')  = '"
        .Text::replace('foo bar foo', 'foo', 'qux')."'\n"; // 'qux bar qux'
} else {
    echo "  Text::replace('foo bar foo', 'foo', 'qux')  → 'qux bar qux'\n";
}
echo "\n";

// -------------------------------------------------------------------------
// REPLACE_CHAR: character-by-character translation (like tr)
// -------------------------------------------------------------------------
echo "REPLACE_CHAR\n";
if ($hasExtension) {
    echo "  Text::replaceChar('hello', 'aeiou', 'AEIOU')  = '"
        .Text::replaceChar('hello', 'aeiou', 'AEIOU')."'\n"; // 'hEllO'
} else {
    echo "  Text::replaceChar('hello', 'aeiou', 'AEIOU')  → 'hEllO'\n";
}
echo "\n";

// -------------------------------------------------------------------------
// LPAD / RPAD: pad to a target codepoint width
// -------------------------------------------------------------------------
echo "LPAD / RPAD\n";
if ($hasExtension) {
    echo "  Text::lpad('hi', 10)       = '".Text::lpad('hi', 10)."'\n";
    echo "  Text::rpad('hi', 10, '-')  = '".Text::rpad('hi', 10, '-')."'\n";
} else {
    echo "  Text::lpad('hi', 10)       → '        hi'\n";
    echo "  Text::rpad('hi', 10, '-')  → 'hi--------'\n";
}
echo "\n";

// -------------------------------------------------------------------------
// CHAR / ORD: codepoint ↔ character conversions
// -------------------------------------------------------------------------
echo "CHAR / ORD\n";
if ($hasExtension) {
    echo "  Text::char(65)       = '".Text::char(65)."'\n";       // 'A'
    echo "  Text::char(0x1F98A)  = '".Text::char(0x1F98A)."'\n"; // '🦊'
    echo "  Text::ord('A')       = ".Text::ord('A')."\n";       // 65
    echo "  Text::ord('🦊')      = ".Text::ord('🦊')."\n";      // 129674
} else {
    echo "  Text::char(65)       → 'A'\n";
    echo "  Text::char(0x1F98A)  → '🦊'\n";
    echo "  Text::ord('A')       → 65\n";
    echo "  Text::ord('🦊')      → 129674\n";
}
echo "\n";

// -------------------------------------------------------------------------
// UPPER / LOWER (v1.0: ASCII a-z/A-Z only)
// -------------------------------------------------------------------------
echo "UPPER / LOWER  (v1.0: ASCII fast path)\n";
if ($hasExtension) {
    echo "  Text::upper('Hello World!')  = '".Text::upper('Hello World!')."'\n";
    echo "  Text::lower('Hello World!')  = '".Text::lower('Hello World!')."'\n";
} else {
    echo "  Text::upper('Hello World!')  → 'HELLO WORLD!'\n";
    echo "  Text::lower('Hello World!')  → 'hello world!'\n";
}
echo "\n";

// -------------------------------------------------------------------------
// IDENT / DIFFER: identity predicates
// -------------------------------------------------------------------------
echo "IDENT / DIFFER\n";
if ($hasExtension) {
    var_dump(Text::ident('abc', 'abc'));   // bool(true)
    var_dump(Text::differ('abc', 'xyz')); // bool(true)
} else {
    echo "  Text::ident('abc', 'abc')    → true\n";
    echo "  Text::differ('abc', 'xyz')   → true\n";
}
echo "\n";

// -------------------------------------------------------------------------
// LEXEQ / LEXLT / LEXGT: lexicographic comparison
// -------------------------------------------------------------------------
echo "LEX* comparisons\n";
if ($hasExtension) {
    var_dump(Text::lexlt('abc', 'abd')); // bool(true)
    var_dump(Text::lexgt('xyz', 'abc')); // bool(true)
    var_dump(Text::lexeq('hi', 'hi'));   // bool(true)
} else {
    echo "  Text::lexlt('abc', 'abd')  → true\n";
    echo "  Text::lexgt('xyz', 'abc')  → true\n";
}
echo "\n";

// -------------------------------------------------------------------------
// INTEGER / REAL / NUMERIC: type predicates
// -------------------------------------------------------------------------
echo "Type predicates\n";
if ($hasExtension) {
    var_dump(Text::integer('42'));      // bool(true)
    var_dump(Text::integer('42.5'));    // bool(false)
    var_dump(Text::real('3.14'));       // bool(true)
    var_dump(Text::real('1.2e-3'));     // bool(true)
    var_dump(Text::numeric('100'));     // bool(true)
    var_dump(Text::numeric('hello'));   // bool(false)
} else {
    echo "  Text::integer('42')     → true\n";
    echo "  Text::integer('42.5')   → false\n";
    echo "  Text::real('3.14')      → true\n";
    echo "  Text::numeric('100')    → true\n";
    echo "  Text::numeric('hello')  → false\n";
}
echo "\n";

// -------------------------------------------------------------------------
// Pattern primitives: BREAKX, BAL, FENCE, REM, RPOS, RTAB
// -------------------------------------------------------------------------
echo "Pattern Primitives\n";
if ($hasExtension) {
    // BREAKX: advance to first occurrence of a character set, then retry
    $ast = B::concat([B::cap(0, B::breakx(':')), B::assign(0, 0)]);
    $result = PH::matchOnce($ast, 'key:value');
    echo "  BREAKX — extract key from 'key:value': '".($result['v0'] ?? 'n/a')."'\n"; // 'key'

    // BAL: balanced parentheses
    $ast = B::concat([B::cap(0, B::bal('(', ')')), B::assign(0, 0)]);
    $result = PH::matchOnce($ast, '(hello (world))');
    echo "  BAL    — balanced parens:               '".($result['v0'] ?? 'n/a')."'\n"; // full balanced span

    // REM: match remainder of string
    $ast = B::concat([B::lit('foo'), B::cap(0, B::rem()), B::assign(0, 0)]);
    $result = PH::matchOnce($ast, 'foobar baz');
    echo "  REM    — after 'foo':                   '".($result['v0'] ?? 'n/a')."'\n"; // 'bar baz'

    // RPOS: position from end
    $ast = B::concat([B::cap(0, B::span('a-z')), B::rpos(0), B::assign(0, 0)]);
    $result = PH::matchOnce($ast, 'hello');
    echo "  RPOS(0)— word at end of string:         '".($result['v0'] ?? 'n/a')."'\n"; // 'hello'
} else {
    echo "  BREAKX: break on char set, retry on backtrack\n";
    echo "  BAL:    balanced delimiter matching\n";
    echo "  FENCE:  cut / prevent backtracking\n";
    echo "  REM:    match remainder of subject\n";
    echo "  RPOS:   position relative to end\n";
    echo "  RTAB:   tab to position relative to end\n";
}
echo "\n";

echo "Done.\n";

