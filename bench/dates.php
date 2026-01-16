<?php
/**
 * Date Matching Benchmark
 *
 * Tests matching date patterns in various formats.
 * Compares SNOBOL VM vs PCRE performance.
 */

require __DIR__.'/../vendor/autoload.php';
require __DIR__.'/Harness.php';

use Bench\Harness;
use Snobol\PatternHelper;

$harness = new Harness();

// Generate test data with dates in various formats
$dates = [];
for ($i = 0; $i < 1000; $i++) {
    $year = 2020 + ($i % 5);
    $month = str_pad(($i % 12) + 1, 2, '0', STR_PAD_LEFT);
    $day = str_pad(($i % 28) + 1, 2, '0', STR_PAD_LEFT);

    // Mix different formats
    if ($i % 3 === 0) {
        $dates[] = "$year-$month-$day"; // ISO format
    } elseif ($i % 3 === 1) {
        $dates[] = "$month/$day/$year"; // US format
    } else {
        $dates[] = "$day.$month.$year"; // European format
    }
}

$text = 'Dates: '.implode(', ', $dates).'.';
$textSize = strlen($text);

// Benchmark parameters
$warmup = 10;
$iterations = 200;

echo "Date Matching Benchmark\n";
echo "=======================\n";
echo "Text size: ".number_format($textSize)." bytes\n";
echo "Date count: ".count($dates)."\n";
echo "Warmup: $warmup iterations\n";
echo "Measured: $iterations iterations\n\n";

// ------------------------------------------------------------------------
// Scenario 1: Match ISO dates (YYYY-MM-DD) (SNOBOL)
// ------------------------------------------------------------------------
$harness->bench(
    'match_iso_dates',
    'snobol',
    function () use ($text) {
        $matches = PatternHelper::matchAll(
            "SPAN('0-9') '-' SPAN('0-9') '-' SPAN('0-9')",
            $text
        );
    },
    $warmup,
    $iterations,
    $textSize
);

// ------------------------------------------------------------------------
// Scenario 1: Match ISO dates (PCRE)
// ------------------------------------------------------------------------
$harness->bench(
    'match_iso_dates',
    'pcre',
    function () use ($text) {
        preg_match_all('/\d{4}-\d{2}-\d{2}/', $text, $matches);
    },
    $warmup,
    $iterations,
    $textSize
);

// ------------------------------------------------------------------------
// Scenario 2: Match US dates (MM/DD/YYYY) (SNOBOL)
// ------------------------------------------------------------------------
$harness->bench(
    'match_us_dates',
    'snobol',
    function () use ($text) {
        $matches = PatternHelper::matchAll(
            "SPAN('0-9') '/' SPAN('0-9') '/' SPAN('0-9')",
            $text
        );
    },
    $warmup,
    $iterations,
    $textSize
);

// ------------------------------------------------------------------------
// Scenario 2: Match US dates (PCRE)
// ------------------------------------------------------------------------
$harness->bench(
    'match_us_dates',
    'pcre',
    function () use ($text) {
        preg_match_all('/\d{2}\/\d{2}\/\d{4}/', $text, $matches);
    },
    $warmup,
    $iterations,
    $textSize
);

// ------------------------------------------------------------------------
// Scenario 3: Match European dates (DD.MM.YYYY) (SNOBOL)
// ------------------------------------------------------------------------
$harness->bench(
    'match_eu_dates',
    'snobol',
    function () use ($text) {
        $matches = PatternHelper::matchAll(
            "SPAN('0-9') '.' SPAN('0-9') '.' SPAN('0-9')",
            $text
        );
    },
    $warmup,
    $iterations,
    $textSize
);

// ------------------------------------------------------------------------
// Scenario 3: Match European dates (PCRE)
// ------------------------------------------------------------------------
$harness->bench(
    'match_eu_dates',
    'pcre',
    function () use ($text) {
        preg_match_all('/\d{2}\.\d{2}\.\d{4}/', $text, $matches);
    },
    $warmup,
    $iterations,
    $textSize
);

// ------------------------------------------------------------------------
// Scenario 4: Match any date format (SNOBOL)
// Using alternation
// ------------------------------------------------------------------------
$harness->bench(
    'match_any_dates',
    'snobol',
    function () use ($text) {
        $matches = PatternHelper::matchAll(
            "(SPAN('0-9') '-' SPAN('0-9') '-' SPAN('0-9')) | ".
            "(SPAN('0-9') '/' SPAN('0-9') '/' SPAN('0-9')) | ".
            "(SPAN('0-9') '.' SPAN('0-9') '.' SPAN('0-9'))",
            $text
        );
    },
    $warmup,
    $iterations,
    $textSize
);

// ------------------------------------------------------------------------
// Scenario 4: Match any date format (PCRE)
// ------------------------------------------------------------------------
$harness->bench(
    'match_any_dates',
    'pcre',
    function () use ($text) {
        preg_match_all('/\d{2,4}[-\/\.]\d{2}[-\/\.]\d{2,4}/', $text, $matches);
    },
    $warmup,
    $iterations,
    $textSize
);

// Output results
$harness->printSummary();

$outputFile = __DIR__.'/results_dates.json';
$harness->writeJson($outputFile);
echo "Results written to: $outputFile\n";
