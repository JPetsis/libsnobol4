<?php
/**
 * libsnobol4 Benchmark Suite
 *
 * Run from project root: php bench/run_all.php
 */

$benchmarks = [
    'tokenize.php' => 'Lexer tokenization',
    'replace.php' => 'Pattern substitution',
    'dates.php' => 'Date pattern matching',
    'backtracking.php' => 'Backtracking performance',
];

echo "libsnobol4 Benchmark Suite\n";
echo "==========================\n\n";

// Check if extension is loaded
if (!extension_loaded('snobol')) {
    echo "ERROR: snobol extension not loaded!\n";
    echo "Make sure to run: ddev start (in bindings/php/)\n";
    exit(1);
}

echo "Extension loaded: snobol v".phpversion('snobol')."\n\n";

$results = [];

foreach ($benchmarks as $file => $description) {
    if (!file_exists(__DIR__.'/'.$file)) {
        echo "SKIP: $file not found\n";
        continue;
    }

    echo "Running: $description ($file)...\n";

    $start = microtime(true);
    include __DIR__.'/'.$file;
    $end = microtime(true);

    $time = round(($end - $start) * 1000, 2);
    $results[$file] = $time;

    echo "  Time: {$time}ms\n\n";
}

echo "==========================\n";
echo "Summary:\n";
foreach ($results as $file => $time) {
    printf("  %-20s %8.2fms\n", $file, $time);
}
echo "==========================\n";

// Save results to JSON
$output = [
    'date' => date('Y-m-d H:i:s'),
    'version' => phpversion('snobol'),
    'results' => $results,
];

$jsonFile = __DIR__.'/results_'.date('Ymd_His').'.json';
file_put_contents($jsonFile, json_encode($output, JSON_PRETTY_PRINT));
echo "\nResults saved to: $jsonFile\n";
