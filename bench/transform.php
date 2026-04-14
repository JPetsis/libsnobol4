<?php
/**
 * Transform Benchmark
 *
 * Benchmarks atomic string transformation using SNOBOL4 built-in functions
 * (UPPER, LOWER, TRIM, REPLACE, DUPL) vs native PHP equivalents.
 *
 * Demonstrates that the SNOBOL4 C built-ins provide competitive performance
 * with minimal PHP overhead for common string operations.
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

// Test data
$text = str_repeat('The Quick Brown Fox Jumps Over The Lazy Dog. ', 200);
$textSize = strlen($text);
$csvRow = str_repeat('field1,field2,field3 , field4 , field5 ,', 100);
$csvRowSize = strlen($csvRow);

// Benchmark parameters (fewer iterations since some ops are fast PHP wrappers)
$warmup = 50;
$iterations = 2000;

echo "Transform Benchmark\n";
echo "===================\n";
echo "Text size: ".number_format($textSize)." bytes\n";
echo "CSV size:  ".number_format($csvRowSize)." bytes\n";
echo "Warmup: $warmup iterations\n";
echo "Measured: $iterations iterations\n\n";

// ------------------------------------------------------------------------
// Scenario 1: UPPER – convert to uppercase
// ------------------------------------------------------------------------
$harness->bench(
    'upper_case',
    'snobol_text',
    function () use ($text) {
        $r = Text::upper($text);
    },
    $warmup, $iterations, $textSize
);

$harness->bench(
    'upper_case',
    'php_strtoupper',
    function () use ($text) {
        $r = strtoupper($text);
    },
    $warmup, $iterations, $textSize
);

// ------------------------------------------------------------------------
// Scenario 2: LOWER – convert to lowercase
// ------------------------------------------------------------------------
$harness->bench(
    'lower_case',
    'snobol_text',
    function () use ($text) {
        $r = Text::lower($text);
    },
    $warmup, $iterations, $textSize
);

$harness->bench(
    'lower_case',
    'php_strtolower',
    function () use ($text) {
        $r = strtolower($text);
    },
    $warmup, $iterations, $textSize
);

// ------------------------------------------------------------------------
// Scenario 3: TRIM – remove trailing whitespace
// ------------------------------------------------------------------------
$paddedText = $text."   \t  \n";
$paddedSize = strlen($paddedText);

$harness->bench(
    'trim_trailing',
    'snobol_text',
    function () use ($paddedText) {
        $r = Text::trim($paddedText);
    },
    $warmup, $iterations, $paddedSize
);

$harness->bench(
    'trim_trailing',
    'php_rtrim',
    function () use ($paddedText) {
        $r = rtrim($paddedText);
    },
    $warmup, $iterations, $paddedSize
);

// ------------------------------------------------------------------------
// Scenario 4: REPLACE – substitute all occurrences of substring
// ------------------------------------------------------------------------
$harness->bench(
    'replace_word',
    'snobol_text',
    function () use ($text) {
        $r = Text::replace($text, 'Fox', 'CAT');
    },
    $warmup, $iterations, $textSize
);

$harness->bench(
    'replace_word',
    'php_str_replace',
    function () use ($text) {
        $r = str_replace('Fox', 'CAT', $text);
    },
    $warmup, $iterations, $textSize
);

// ------------------------------------------------------------------------
// Scenario 5: DUPL – duplicate a string N times
// ------------------------------------------------------------------------
$shortStr = 'abc';
$harness->bench(
    'dupl_100x',
    'snobol_text',
    function () use ($shortStr) {
        $r = Text::dupl($shortStr, 100);
    },
    $warmup, $iterations, strlen($shortStr)
);

$harness->bench(
    'dupl_100x',
    'php_str_repeat',
    function () use ($shortStr) {
        $r = str_repeat($shortStr, 100);
    },
    $warmup, $iterations, strlen($shortStr)
);

// ------------------------------------------------------------------------
// Scenario 6: REPLACE_CHAR – character-by-character translation
// ------------------------------------------------------------------------
$harness->bench(
    'replace_char',
    'snobol_text',
    function () use ($text) {
        $r = Text::replaceChar($text, 'aeiou', 'AEIOU');
    },
    $warmup, $iterations, $textSize
);

$harness->bench(
    'replace_char',
    'php_strtr',
    function () use ($text) {
        $r = strtr($text, 'aeiou', 'AEIOU');
    },
    $warmup, $iterations, $textSize
);

// Output results
$harness->printSummary();

$outputFile = __DIR__.'/results_transform.json';
$harness->writeJson($outputFile);
echo "Results written to: $outputFile\n";


