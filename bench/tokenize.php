<?php
/**
 * Tokenization Benchmark
 *
 * Tests splitting strings on whitespace and comma delimiters.
 * Compares SNOBOL VM vs PCRE performance.
 *
 * Also compares BREAKX vs ARB for key extraction.
 * BREAKX advances O(n) with a retry choice point; ARB does O(n²) backtracking.
 */

$autoload = file_exists(__DIR__.'/../vendor/autoload.php')
    ? __DIR__.'/../vendor/autoload.php'
    : __DIR__.'/../html/vendor/autoload.php';
require $autoload;
require __DIR__.'/Harness.php';

use Bench\Harness;
use Snobol\Builder as B;
use Snobol\PatternHelper;

$harness = new Harness();

// Generate test data: comma-separated words
$words = [];
for ($i = 0; $i < 1000; $i++) {
    $words[] = 'word'.$i;
}
$csvData = implode(',', $words);
$csvSize = strlen($csvData);

// Generate test data: whitespace-separated words
$wspData = implode(' ', $words);
$wspSize = strlen($wspData);

// Benchmark parameters
$warmup = 10;
$iterations = 500;

echo "Tokenization Benchmark\n";
echo "======================\n";
echo "CSV data: ".number_format($csvSize)." bytes\n";
echo "Whitespace data: ".number_format($wspSize)." bytes\n";
echo "Warmup: $warmup iterations\n";
echo "Measured: $iterations iterations\n\n";

// ------------------------------------------------------------------------
// Scenario 1: Split on comma (SNOBOL)
// ------------------------------------------------------------------------
$harness->bench(
    'tokenize_comma',
    'snobol',
    function () use ($csvData) {
        $tokens = PatternHelper::split("','", $csvData);
    },
    $warmup,
    $iterations,
    $csvSize
);

// ------------------------------------------------------------------------
// Scenario 1: Split on comma (PCRE)
// ------------------------------------------------------------------------
$harness->bench(
    'tokenize_comma',
    'pcre',
    function () use ($csvData) {
        $tokens = preg_split('/,/', $csvData);
    },
    $warmup,
    $iterations,
    $csvSize
);

// ------------------------------------------------------------------------
// Scenario 2: Split on whitespace (SNOBOL)
// ------------------------------------------------------------------------
$harness->bench(
    'tokenize_whitespace',
    'snobol',
    function () use ($wspData) {
        $tokens = PatternHelper::split("' '", $wspData);
    },
    $warmup,
    $iterations,
    $wspSize
);

// ------------------------------------------------------------------------
// Scenario 2: Split on whitespace (PCRE)
// ------------------------------------------------------------------------
$harness->bench(
    'tokenize_whitespace',
    'pcre',
    function () use ($wspData) {
        $tokens = preg_split('/\s+/', $wspData);
    },
    $warmup,
    $iterations,
    $wspSize
);

// ------------------------------------------------------------------------
// Scenario 3: Split on multiple delimiters (SNOBOL)
// Mixed comma and whitespace
// ------------------------------------------------------------------------
$mixedData = str_replace(',', ' , ', $csvData);
$mixedSize = strlen($mixedData);

$harness->bench(
    'tokenize_mixed',
    'snobol',
    function () use ($mixedData) {
        $tokens = PatternHelper::split("' ' | ','", $mixedData);
    },
    $warmup,
    $iterations,
    $mixedSize
);

// ------------------------------------------------------------------------
// Scenario 3: Split on multiple delimiters (PCRE)
// ------------------------------------------------------------------------
$harness->bench(
    'tokenize_mixed',
    'pcre',
    function () use ($mixedData) {
        $tokens = preg_split('/[\s,]+/', $mixedData);
    },
    $warmup,
    $iterations,
    $mixedSize
);

// ========================================================================
// BREAKX vs ARB comparison
//
// BREAKX(set) advances past all non-set chars and pushes a retry
// choice point, giving O(n) tokenization.
//
// The ARB approach (ARBNO(NOTANY(set))) is semantically equivalent but
// may cause more backtracking in the general case.
//
// Both patterns capture the token before the delimiter.
// ========================================================================
echo "\n--- BREAKX vs ARB comparison (Task 8.2) ---\n\n";

$warmupBx = 5;
$itersBx = 200;

// BREAKX-based key extraction: BREAKX captures text up to ':'
$breakxAst = B::concat([
    B::cap(0, B::breakx(':')),
    B::assign(0, 0),
]);

// ARB-based key extraction: ARB + literal ':' to find text before ':'
// Using NOTANY to match any char not in ':' (similar to BREAKX semantics)
$arbAst = B::concat([
    B::cap(0, B::arbno(B::notany(':'))),
    B::assign(0, 0),
]);

// Test data: many key:value pairs on separate lines
$kvData = '';
for ($i = 0; $i < 500; $i++) {
    $kvData .= "key{$i}:value{$i}\n";
}
$kvData = rtrim($kvData);
$kvSize = strlen($kvData);

echo "Key-value data: ".number_format($kvSize)." bytes\n";
echo "Warmup: $warmupBx iterations | Measured: $itersBx iterations\n\n";

// Run BREAKX version
$harness->bench(
    'extract_key_from_kv',
    'breakx',
    function () use ($breakxAst, $kvData) {
        $tokens = PatternHelper::matchAll($breakxAst, $kvData);
    },
    $warmupBx, $itersBx, $kvSize
);

// Run ARB version
$harness->bench(
    'extract_key_from_kv',
    'arb_notany',
    function () use ($arbAst, $kvData) {
        $tokens = PatternHelper::matchAll($arbAst, $kvData);
    },
    $warmupBx, $itersBx, $kvSize
);

// PCRE comparison
$harness->bench(
    'extract_key_from_kv',
    'pcre',
    function () use ($kvData) {
        preg_match_all('/([^:]+):/', $kvData, $m);
    },
    $warmupBx, $itersBx, $kvSize
);

// Output results
$harness->printSummary();

$outputFile = __DIR__.'/results_tokenize.json';
$harness->writeJson($outputFile);
echo "Results written to: $outputFile\n";


$harness = new Harness();

// Generate test data: comma-separated words
$words = [];
for ($i = 0; $i < 1000; $i++) {
    $words[] = 'word'.$i;
}
$csvData = implode(',', $words);
$csvSize = strlen($csvData);

// Generate test data: whitespace-separated words
$wspData = implode(' ', $words);
$wspSize = strlen($wspData);

// Benchmark parameters
$warmup = 10;
$iterations = 500;

echo "Tokenization Benchmark\n";
echo "======================\n";
echo "CSV data: ".number_format($csvSize)." bytes\n";
echo "Whitespace data: ".number_format($wspSize)." bytes\n";
echo "Warmup: $warmup iterations\n";
echo "Measured: $iterations iterations\n\n";

// ------------------------------------------------------------------------
// Scenario 1: Split on comma (SNOBOL)
// ------------------------------------------------------------------------
$harness->bench(
    'tokenize_comma',
    'snobol',
    function () use ($csvData) {
        $tokens = PatternHelper::split("','", $csvData);
    },
    $warmup,
    $iterations,
    $csvSize
);

// ------------------------------------------------------------------------
// Scenario 1: Split on comma (PCRE)
// ------------------------------------------------------------------------
$harness->bench(
    'tokenize_comma',
    'pcre',
    function () use ($csvData) {
        $tokens = preg_split('/,/', $csvData);
    },
    $warmup,
    $iterations,
    $csvSize
);

// ------------------------------------------------------------------------
// Scenario 2: Split on whitespace (SNOBOL)
// ------------------------------------------------------------------------
$harness->bench(
    'tokenize_whitespace',
    'snobol',
    function () use ($wspData) {
        $tokens = PatternHelper::split("' '", $wspData);
    },
    $warmup,
    $iterations,
    $wspSize
);

// ------------------------------------------------------------------------
// Scenario 2: Split on whitespace (PCRE)
// ------------------------------------------------------------------------
$harness->bench(
    'tokenize_whitespace',
    'pcre',
    function () use ($wspData) {
        $tokens = preg_split('/\s+/', $wspData);
    },
    $warmup,
    $iterations,
    $wspSize
);

// ------------------------------------------------------------------------
// Scenario 3: Split on multiple delimiters (SNOBOL)
// Mixed comma and whitespace
// ------------------------------------------------------------------------
$mixedData = str_replace(',', ' , ', $csvData);
$mixedSize = strlen($mixedData);

$harness->bench(
    'tokenize_mixed',
    'snobol',
    function () use ($mixedData) {
        $tokens = PatternHelper::split("' ' | ','", $mixedData);
    },
    $warmup,
    $iterations,
    $mixedSize
);

// ------------------------------------------------------------------------
// Scenario 3: Split on multiple delimiters (PCRE)
// ------------------------------------------------------------------------
$harness->bench(
    'tokenize_mixed',
    'pcre',
    function () use ($mixedData) {
        $tokens = preg_split('/[\s,]+/', $mixedData);
    },
    $warmup,
    $iterations,
    $mixedSize
);

// Output results
$harness->printSummary();

$outputFile = __DIR__.'/results_tokenize.json';
$harness->writeJson($outputFile);
echo "Results written to: $outputFile\n";
