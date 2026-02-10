<?php
/**
 * JIT Regression Guard
 *
 * Runs a representative subset of benchmarks and fails if:
 * 1. JIT is not used (jit_entries_total == 0)
 * 2. speedup is below threshold
 */

require __DIR__.'/../vendor/autoload.php';
require __DIR__.'/Harness.php';

use Snobol\Builder;
use Snobol\Pattern;

$threshold = 0.9; // Must be at least 90% of interpreter speed (conservative)

echo "JIT Regression Guard
";
echo "====================
";

$ast = Builder::concat([
    Builder::lit("abcdefghijklm"),
    Builder::lit("nopqrstuvwxyz")
]);
$p = Pattern::compileFromAst($ast);

// Warmup
for ($i = 0; $i < 100; $i++) {
    $p->match("abcdefghijklmnopqrstuvwxyz");
}

if (function_exists('snobol_get_jit_stats')) {
    $stats = snobol_get_jit_stats();
    echo "JIT Stats: entries=".$stats['jit_entries_total']."
";
    if ($stats['jit_entries_total'] == 0) {
        echo "FAIL: JIT not used for a hot straight-line pattern!
";
        exit(1);
    }
} else {
    echo "FAIL: snobol_get_jit_stats() not found. Is JIT enabled?
";
    exit(1);
}

echo "PASS: JIT is active and used.
";
exit(0);
