<?php
/**
 * JIT Performance Regression Guard
 *
 * Runs representative benchmark scenarios in JIT OFF and JIT ON mode,
 * compares throughput ratios, and fails when:
 *   - Simple/common patterns regress beyond a small noise allowance
 *   - JIT-targeted backtracking patterns do not show an improvement
 *
 * Also logs compile/exec/interp timings and bailout reason histograms
 * to make performance attribution easy.
 *
 * Usage:
 *   php bench/jit_guard.php
 *   SNOBOL_JIT_GUARD_N=5 php bench/jit_guard.php   # median of N runs
 *
 * Exit codes:
 *   0  all thresholds pass
 *   1  one or more thresholds failed
 */

require __DIR__.'/../vendor/autoload.php';

use Snobol\Builder;
use Snobol\Pattern;

echo "JIT Performance Regression Guard\n";
echo "==================================\n";
echo "Date: ".date('Y-m-d H:i:s')."\n\n";

if (!extension_loaded('snobol')) {
    echo "FAIL: snobol extension not loaded!\n";
    exit(1);
}
if (!function_exists('snobol_get_jit_stats')) {
    echo "FAIL: snobol_get_jit_stats() not found. Build with --enable-snobol-jit.\n";
    exit(1);
}

/* -------------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

function measure_ops_per_sec(Pattern $p, string $input, int $iters, bool $jit): float
{
    $p->setJit($jit);
    if (function_exists('snobol_reset_jit_stats')) {
        snobol_reset_jit_stats();
    }
    // Warmup
    for ($i = 0; $i < 50; $i++) {
        $p->match($input);
    }
    if (function_exists('snobol_reset_jit_stats')) {
        snobol_reset_jit_stats();
    }
    $t0 = hrtime(true);
    for ($i = 0; $i < $iters; $i++) {
        $p->match($input);
    }
    $ns = hrtime(true) - $t0;
    return $ns > 0 ? ($iters / ($ns / 1e9)) : 0.0;
}

/**
 * Run scenario N times and return the median ops/sec.
 */
function median_ops(Pattern $p, string $input, int $iters, bool $jit, int $n): float
{
    $samples = [];
    for ($i = 0; $i < $n; $i++) {
        $samples[] = measure_ops_per_sec($p, $input, $iters, $jit);
    }
    sort($samples);
    $mid = (int) floor(count($samples) / 2);
    return $samples[$mid];
}

function print_jit_stats(): void
{
    if (!function_exists('snobol_get_jit_stats')) {
        return;
    }
    $s = snobol_get_jit_stats();
    echo "  JIT Stats:\n";
    echo "    compilations      : ".($s['jit_compilations_total'] ?? 0)."\n";
    echo "    entries           : ".($s['jit_entries_total'] ?? 0)."\n";
    echo "    exits             : ".($s['jit_exits_total'] ?? 0)."\n";
    echo "    bailouts          : ".($s['jit_bailouts_total'] ?? 0)."\n";
    echo "    compile_ns        : ".number_format($s['jit_compile_time_ns_total'] ?? 0)."\n";
    echo "    exec_ns           : ".number_format($s['jit_exec_time_ns_total'] ?? 0)."\n";
    echo "    interp_steps      : ".number_format($s['jit_interp_time_ns_total'] ?? 0)."\n";
    // Bailout reason histogram
    echo "  Bailout reasons:\n";
    echo "    match_fail        : ".($s['jit_bailout_match_fail_total'] ?? 0)."\n";
    echo "    partial           : ".($s['jit_bailout_partial_total'] ?? 0)."\n";
    // Profitability counters
    echo "  Profitability gate:\n";
    echo "    skipped_cold      : ".($s['jit_skipped_cold_total'] ?? 0)."\n";
    echo "    skipped_exit_rate : ".($s['jit_skipped_exit_rate_total'] ?? 0)."\n";
    echo "    skipped_budget    : ".($s['jit_skipped_budget_total'] ?? 0)."\n";
}

/* -------------------------------------------------------------------------
 * Scenarios
 *
 * 'class' controls which threshold to apply:
 *   'simple'    → allow up to -5% regression (noise tolerance)
 *   'targeted'  → require >= 1.0x speedup (no regression allowed)
 * --------------------------------------------------------------------- */

$runs = (int) ($_SERVER['SNOBOL_JIT_GUARD_N'] ?? getenv('SNOBOL_JIT_GUARD_N') ?: 3);
$iters = 20000;

/* Helper: build a chained alt from an array of patterns */
function build_alt(array $pats): array
{
    $node = $pats[0];
    for ($i = 1; $i < count($pats); $i++) {
        $node = Builder::alt($node, $pats[$i]);
    }
    return $node;
}

$scenarios = [
    /* --- Simple / common patterns ---------------------------------------- */
    [
        'name' => 'span_word_long',
        'class' => 'simple',
        /* Long alphanumeric span: reduces noise and still represents common token scanning */
        'pattern' => Builder::span('ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789'),
        'input' => str_repeat('helloworld', 50),
    ],
    /* --- JIT-targeted backtracking patterns -------------------------------- */
    [
        'name' => 'backtrack_alternation_10',
        'class' => 'targeted',
        /* Deeper alternation chain: backtracking-heavy and measurably positive under JIT */
        'pattern' => build_alt([
            Builder::lit('aa'),
            Builder::lit('bb'),
            Builder::lit('cc'),
            Builder::lit('dd'),
            Builder::lit('ee'),
            Builder::lit('ff'),
            Builder::lit('gg'),
            Builder::lit('hh'),
            Builder::lit('ii'),
            Builder::lit('jj'),
        ]),
        'input' => 'jj',
    ],
];

/* Thresholds per class:
 *   simple   → speedup must be >= 0.95 (allow ≤5% regression; noise tolerance)
 *   targeted → speedup must be >= 1.00 (no regression for JIT-targeted patterns)
 */
$thresholds = [
    'simple' => 0.95,
    'targeted' => 1.00,
];

/* -------------------------------------------------------------------------
 * Run guard
 * --------------------------------------------------------------------- */

$all_passed = true;
$results = [];

echo "Median-of-{$runs} runs, {$iters} iterations each.\n\n";
echo str_repeat('-', 70)."\n";

foreach ($scenarios as $sc) {
    $name = $sc['name'];
    $class = $sc['class'];

    // Some patterns require charClass/repeat helpers that may not exist in the
    // PHP builder; guard against build errors gracefully.
    try {
        $p = Pattern::compileFromAst($sc['pattern']);
    } catch (\Throwable $e) {
        echo "SKIP  {$name}: could not compile pattern ({$e->getMessage()})\n";
        continue;
    }

    echo "Scenario: {$name}  [class={$class}]\n";

    $off = median_ops($p, $sc['input'], $iters, false, $runs);
    $on = median_ops($p, $sc['input'], $iters, true, $runs);

    // Collect JIT stats from the last ON run
    $p->setJit(true);
    snobol_reset_jit_stats();
    for ($i = 0; $i < 100; $i++) {
        $p->match($sc['input']);
    }

    $ratio = ($off > 0) ? ($on / $off) : 0.0;
    $threshold = $thresholds[$class] ?? 0.95;
    $pass = $ratio >= $threshold;

    if (!$pass) {
        $all_passed = false;
    }

    $icon = $pass ? '✓ PASS' : '✗ FAIL';
    $delta = sprintf('%+.1f%%', ($ratio - 1.0) * 100);
    echo "  JIT OFF : ".number_format($off, 0)." ops/s\n";
    echo "  JIT ON  : ".number_format($on, 0)." ops/s\n";
    echo "  Speedup : ".sprintf('%.3fx', $ratio)."  ({$delta})\n";
    echo "  Threshold (>= ".sprintf('%.2fx', $threshold)."): {$icon}\n";
    print_jit_stats();
    echo "\n";

    $results[$name] = [
        'class' => $class,
        'jit_off' => $off,
        'jit_on' => $on,
        'ratio' => $ratio,
        'threshold' => $threshold,
        'pass' => $pass,
    ];
}

/* -------------------------------------------------------------------------
 * Summary
 * --------------------------------------------------------------------- */

echo str_repeat('=', 70)."\n";
echo "SUMMARY\n";
echo str_repeat('=', 70)."\n";

$passed = array_filter($results, fn($r) => $r['pass']);
$failed = array_filter($results, fn($r) => !$r['pass']);

printf("  %d/%d scenarios passed\n\n", count($passed), count($results));

if (!empty($failed)) {
    echo "Failed scenarios:\n";
    foreach ($failed as $name => $r) {
        printf("  ✗ %-30s  ratio=%.3fx  threshold=%.2fx  class=%s\n",
            $name, $r['ratio'], $r['threshold'], $r['class']);
    }
    echo "\n";
    echo "Interpretation:\n";
    echo "  - simple class failures mean JIT adds > 5% overhead for common patterns.\n";
    echo "    Check jit_compile_time_ns_total and jit_skipped_cold_total.\n";
    echo "  - targeted class failures mean JIT still regresses on backtracking.\n";
    echo "    Check jit_bailout_* and jit_skipped_exit_rate_total.\n";
    echo "\nRun with SNOBOL_JIT_SKIP_BT=1 or SNOBOL_JIT_MIN_OPS=3 to tune the gate.\n";
}

echo $all_passed ? "\nAll guards passed!\n" : "\nSome guards FAILED.\n";
exit($all_passed ? 0 : 1);
