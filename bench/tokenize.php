<?php
/**
 * Tokenization Benchmark
 *
 * Tests splitting strings on whitespace and comma delimiters.
 * Compares SNOBOL VM vs PCRE performance.
 */

require __DIR__.'/../vendor/autoload.php';
require __DIR__.'/Harness.php';

use Bench\Harness;
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

// Output results
$harness->printSummary();

$outputFile = __DIR__.'/results_tokenize.json';
$harness->writeJson($outputFile);
echo "Results written to: $outputFile\n";
