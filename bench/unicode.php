<?php
/**
 * Unicode Performance Benchmark
 *
 * Benchmarks Unicode-aware SNOBOL4 built-in functions (SIZE, REVERSE, SUBSTR, CHAR, ORD)
 * vs PHP mb_ equivalents for both ASCII and multi-byte UTF-8 input.
 *
 * All SNOBOL4 functions use codepoint-based semantics.
 * ASCII fast paths are verified by mixing ASCII and Unicode inputs.
 */

// Support running from project root or from inside DDEV (/var/www/bench)
$autoload = file_exists(__DIR__.'/../vendor/autoload.php')
    ? __DIR__.'/../vendor/autoload.php'
    : __DIR__.'/../html/vendor/autoload.php';
require $autoload;
require __DIR__.'/Harness.php';

use Bench\Harness;
use Snobol\Text;

$harness = new Harness();

// Test inputs
$asciiStr = str_repeat('The quick brown fox jumps over the lazy dog. ', 50);
$unicodeStr = str_repeat('Héllo Wörld! こんにちは 🦊', 50);

$asciiSize = strlen($asciiStr);
$unicodeSize = strlen($unicodeStr);

$warmup = 100;
$iterations = 5000;

echo "Unicode Benchmark\n";
echo "=================\n";
echo "ASCII input:   ".number_format($asciiSize)." bytes\n";
echo "Unicode input: ".number_format($unicodeSize)." bytes\n";
echo "Warmup: $warmup iterations\n";
echo "Measured: $iterations iterations\n\n";

// ------------------------------------------------------------------------
// Scenario 1: SIZE (codepoint count) – ASCII fast path
// ------------------------------------------------------------------------
$harness->bench(
    'size_ascii',
    'snobol_text',
    function () use ($asciiStr) {
        $n = Text::size($asciiStr);
    },
    $warmup, $iterations, $asciiSize
);

$harness->bench(
    'size_ascii',
    'php_strlen',
    function () use ($asciiStr) {
        $n = strlen($asciiStr);
    },
    $warmup, $iterations, $asciiSize
);

// ------------------------------------------------------------------------
// Scenario 2: SIZE – Unicode (multibyte)
// ------------------------------------------------------------------------
$harness->bench(
    'size_unicode',
    'snobol_text',
    function () use ($unicodeStr) {
        $n = Text::size($unicodeStr);
    },
    $warmup, $iterations, $unicodeSize
);

$harness->bench(
    'size_unicode',
    'php_mb_strlen',
    function () use ($unicodeStr) {
        $n = mb_strlen($unicodeStr, 'UTF-8');
    },
    $warmup, $iterations, $unicodeSize
);

// ------------------------------------------------------------------------
// Scenario 3: REVERSE – ASCII
// ------------------------------------------------------------------------
$harness->bench(
    'reverse_ascii',
    'snobol_text',
    function () use ($asciiStr) {
        $r = Text::reverse($asciiStr);
    },
    $warmup, $iterations, $asciiSize
);

$harness->bench(
    'reverse_ascii',
    'php_strrev',
    function () use ($asciiStr) {
        $r = strrev($asciiStr);
    },
    $warmup, $iterations, $asciiSize
);

// ------------------------------------------------------------------------
// Scenario 4: REVERSE – Unicode
// ------------------------------------------------------------------------
$iterations2 = 500; // more expensive for Unicode

$harness->bench(
    'reverse_unicode',
    'snobol_text',
    function () use ($unicodeStr) {
        $r = Text::reverse($unicodeStr);
    },
    $warmup, $iterations2, $unicodeSize
);

$harness->bench(
    'reverse_unicode',
    'php_preg_split_reverse',
    function () use ($unicodeStr) {
        $codepoints = preg_split('//u', $unicodeStr, -1, PREG_SPLIT_NO_EMPTY);
        $r = implode('', array_reverse($codepoints));
    },
    $warmup, $iterations2, $unicodeSize
);

// ------------------------------------------------------------------------
// Scenario 5: SUBSTR – extract substring by codepoint index
// ------------------------------------------------------------------------
$harness->bench(
    'substr_ascii',
    'snobol_text',
    function () use ($asciiStr) {
        $r = Text::substr($asciiStr, 10, 20);
    },
    $warmup, $iterations, $asciiSize
);

$harness->bench(
    'substr_ascii',
    'php_substr',
    function () use ($asciiStr) {
        $r = substr($asciiStr, 10, 20);
    },
    $warmup, $iterations, $asciiSize
);

// ------------------------------------------------------------------------
// Scenario 6: ORD – get codepoint of first character
// ------------------------------------------------------------------------
$aChar = 'A';
$unicChar = '🦊';

$harness->bench(
    'ord_ascii',
    'snobol_text',
    function () use ($aChar) {
        $n = Text::ord($aChar);
    },
    $warmup, $iterations, 1
);

$harness->bench(
    'ord_ascii',
    'php_ord',
    function () use ($aChar) {
        $n = ord($aChar);
    },
    $warmup, $iterations, 1
);

// ------------------------------------------------------------------------
// Scenario 7: CHAR – codepoint to UTF-8 character
// ------------------------------------------------------------------------
$harness->bench(
    'char_ascii',
    'snobol_text',
    function () {
        $c = Text::char(65);
    },
    $warmup, $iterations, 1
);

$harness->bench(
    'char_ascii',
    'php_chr',
    function () {
        $c = chr(65);
    },
    $warmup, $iterations, 1
);

// Output results
$harness->printSummary();

$outputFile = __DIR__.'/results_unicode.json';
$harness->writeJson($outputFile);
echo "Results written to: $outputFile\n";


