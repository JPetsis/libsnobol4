<?php
/**
 * Replace/Removal Benchmark
 *
 * Tests many small character replacements and removals.
 * Compares SNOBOL VM vs PCRE performance.
 */

require __DIR__.'/../vendor/autoload.php';
require __DIR__.'/Harness.php';

use Bench\Harness;
use Snobol\PatternHelper;

$harness = new Harness();

// Generate test data: text with many replaceable characters
$text = str_repeat('The quick brown fox jumps over the lazy dog. ', 100);
$textSize = strlen($text);

// Benchmark parameters
$warmup = 10;
$iterations = 200;

echo "Replace/Removal Benchmark\n";
echo "=========================\n";
echo "Text size: ".number_format($textSize)." bytes\n";
echo "Warmup: $warmup iterations\n";
echo "Measured: $iterations iterations\n\n";

// ------------------------------------------------------------------------
// Scenario 1: Remove all spaces (SNOBOL)
// ------------------------------------------------------------------------
$harness->bench(
    'remove_spaces',
    'snobol',
    function () use ($text) {
        $result = PatternHelper::replace("' '", '', $text);
    },
    $warmup,
    $iterations,
    $textSize
);

// ------------------------------------------------------------------------
// Scenario 1: Remove all spaces (PCRE)
// ------------------------------------------------------------------------
$harness->bench(
    'remove_spaces',
    'pcre',
    function () use ($text) {
        $result = preg_replace('/\s/', '', $text);
    },
    $warmup,
    $iterations,
    $textSize
);

// ------------------------------------------------------------------------
// Scenario 2: Replace vowels with asterisks (SNOBOL)
// ------------------------------------------------------------------------
$harness->bench(
    'replace_vowels',
    'snobol',
    function () use ($text) {
        $result = PatternHelper::replace("[aeiouAEIOU]", '*', $text);
    },
    $warmup,
    $iterations,
    $textSize
);

// ------------------------------------------------------------------------
// Scenario 2: Replace vowels with asterisks (PCRE)
// ------------------------------------------------------------------------
$harness->bench(
    'replace_vowels',
    'pcre',
    function () use ($text) {
        $result = preg_replace('/[aeiouAEIOU]/', '*', $text);
    },
    $warmup,
    $iterations,
    $textSize
);

// ------------------------------------------------------------------------
// Scenario 3: Replace multiple patterns (SNOBOL)
// Replace 'the' -> 'THE', 'fox' -> 'FOX', 'dog' -> 'DOG'
// ------------------------------------------------------------------------
$harness->bench(
    'replace_words',
    'snobol',
    function () use ($text) {
        $temp = PatternHelper::replace("'the'", 'THE', $text);
        $temp = PatternHelper::replace("'fox'", 'FOX', $temp);
        $result = PatternHelper::replace("'dog'", 'DOG', $temp);
    },
    $warmup,
    $iterations,
    $textSize
);

// ------------------------------------------------------------------------
// Scenario 3: Replace multiple patterns (PCRE)
// ------------------------------------------------------------------------
$harness->bench(
    'replace_words',
    'pcre',
    function () use ($text) {
        $temp = preg_replace('/the/', 'THE', $text);
        $temp = preg_replace('/fox/', 'FOX', $temp);
        $result = preg_replace('/dog/', 'DOG', $temp);
    },
    $warmup,
    $iterations,
    $textSize
);

// ------------------------------------------------------------------------
// Scenario 4: Remove punctuation (SNOBOL)
// ------------------------------------------------------------------------
$harness->bench(
    'remove_punctuation',
    'snobol',
    function () use ($text) {
        $result = PatternHelper::replace("[.,!?;:]", '', $text);
    },
    $warmup,
    $iterations,
    $textSize
);

// ------------------------------------------------------------------------
// Scenario 4: Remove punctuation (PCRE)
// ------------------------------------------------------------------------
$harness->bench(
    'remove_punctuation',
    'pcre',
    function () use ($text) {
        $result = preg_replace('/[.,!?;:]/', '', $text);
    },
    $warmup,
    $iterations,
    $textSize
);

// Output results
$harness->printSummary();

$outputFile = __DIR__.'/results_replace.json';
$harness->writeJson($outputFile);
echo "Results written to: $outputFile\n";
