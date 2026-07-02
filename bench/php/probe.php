<?php
/**
 * bench/php/probe.php — Diagnostic probe for the PHP binding
 *
 * Mirrors bench/c/bench_probe.c scenarios through the public PHP API,
 * so we can attribute the per-iteration cost between the C engine
 * and the binding layer (memset(VM,0), add_next_index_stringl,
 * PHP↔C crossing, etc.).
 *
 * Usage:
 *   php bench/php/probe.php
 *   PROBE_ITERS=10000 php bench/php/probe.php
 *
 * Output: a fixed-width ASCII table matching the C probe's format,
 * plus a "vs C" column showing the PHP/C ns/iter ratio.
 *
 * Requires: snobol extension loaded (\Snobol\Pattern, \Snobol\PatternHelper).
 */

declare(strict_types=1);

if (!class_exists('Snobol\\Pattern', true)) {
    fwrite(STDERR, "FATAL: Snobol\\Pattern is not available. "
        . "Build and load the snobol PHP extension first.\n");
    exit(2);
}

// Load the snobol stub classes. The probe uses PatternHelper::fromString
// which is implemented in the snobol extension (not in stubs), so the
// stubs are needed for type-hinting only. If vendor/ isn't present
// (e.g. running the probe from a tarball checkout), skip — the probe
// only needs the snobol_* functions and the \Snobol\Pattern PHP class
// (which is also provided by the extension).
$autoload = __DIR__ . '/../../bindings/php/vendor/autoload.php';
if (is_file($autoload)) {
    require $autoload;
}
$repo_autoload = __DIR__ . '/../../html/vendor/autoload.php';
if (is_file($repo_autoload)) {
    require $repo_autoload;
}

use Snobol\Builder;
use Snobol\Pattern;
use Snobol\PatternHelper;

// ---------------------------------------------------------------------------
// Subjects — must match bench/c/bench_probe.c exactly for the C/PHP ratio
// to be meaningful.
// ---------------------------------------------------------------------------

$SUBJECT_CSV =
    "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status,"
    . "id,name,email,age,status,id,name,email,age,status";

$SUBJECT_WITH_PQR =
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
    . "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";

$SUBJECT_NO_PQR =
    "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz"
    . "abcdefghijklmnorstuvwxyzabcdefghijklmnorstuvwxyz";

$SUBJECT_WHITESPACE =
    "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z "
    . "a b c d e f g h i j k l m n o p q r s t u v w x y z ";

$SUBJECT_MIXED =
    "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c "
    . "the a quick b brown c fox a jumps b over c the a lazy b dog c ";

$SUBJECT_ALTLIT =
    "the cat went dog walking fox jumped cat over dog near fox "
    . "the cat went dog walking fox jumped cat over dog near fox "
    . "the cat went dog walking fox jumped cat over dog near fox "
    . "the cat went dog walking fox jumped cat over dog near fox "
    . "the cat went dog walking fox jumped cat over dog near fox "
    . "the cat went dog walking fox jumped cat over dog near fox "
    . "the cat went dog walking fox jumped cat over dog near fox "
    . "the cat went dog walking fox jumped cat over dog near fox "
    . "the cat went dog walking fox jumped cat over dog near fox "
    . "the cat went dog walking fox jumped cat over dog near fox "
    . "the cat went dog walking fox jumped cat over dog near fox "
    . "the cat went dog walking fox jumped cat over dog near fox ";

// ---------------------------------------------------------------------------
// Timer
// ---------------------------------------------------------------------------

function now_ns(): int
{
    return (int)(microtime(true) * 1_000_000_000);
}

// ---------------------------------------------------------------------------
// Scenario runners
//
// Each runner is a closure that takes (iterations) and returns
// ['iters' => int, 'total_ns' => int].
// ---------------------------------------------------------------------------

/** @return array{iters:int,total_ns:int} */
function run_literal_fail(int $iters): array
{
    $p = PatternHelper::fromString("'pqr'");
    for ($i = 0; $i < 100; $i++) { $p->match($GLOBALS['SUBJECT_NO_PQR']); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->match($GLOBALS['SUBJECT_NO_PQR']);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

/** @return array{iters:int,total_ns:int} */
function run_literal_ok(int $iters): array
{
    $p = PatternHelper::fromString("'pqr'");
    for ($i = 0; $i < 100; $i++) { $p->match($GLOBALS['SUBJECT_WITH_PQR']); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->match($GLOBALS['SUBJECT_WITH_PQR']);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

/** @return array{iters:int,total_ns:int} */
function run_span_comma(int $iters): array
{
    $p = PatternHelper::fromString("SPAN(',')");
    for ($i = 0; $i < 100; $i++) { $p->match($GLOBALS['SUBJECT_CSV']); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->match($GLOBALS['SUBJECT_CSV']);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

/** @return array{iters:int,total_ns:int} */
function run_alternation(int $iters): array
{
    $p = PatternHelper::fromString("'a' | 'b' | 'c'");
    for ($i = 0; $i < 100; $i++) { $p->match($GLOBALS['SUBJECT_MIXED']); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->match($GLOBALS['SUBJECT_MIXED']);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

/** @return array{iters:int,total_ns:int} */
function run_alt_literals(int $iters): array
{
    $p = PatternHelper::fromString("'cat' | 'dog' | 'fox'");
    for ($i = 0; $i < 100; $i++) { $p->match($GLOBALS['SUBJECT_ALTLIT']); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->match($GLOBALS['SUBJECT_ALTLIT']);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

/** @return array{iters:int,total_ns:int} */
function run_alt_literals_search(int $iters): array
{
    $p = PatternHelper::fromString("'cat' | 'dog' | 'fox'");
    for ($i = 0; $i < 100; $i++) { $p->searchAll($GLOBALS['SUBJECT_ALTLIT']); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->searchAll($GLOBALS['SUBJECT_ALTLIT']);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

/** @return array{iters:int,total_ns:int} */
function run_tokenize_php(int $outer_iters): array
{
    $p = PatternHelper::fromString("' '");
    for ($i = 0; $i < 10; $i++) { $p->searchSplit($GLOBALS['SUBJECT_WHITESPACE']); }

    $total_search_calls = 0;
    $start = now_ns();
    for ($i = 0; $i < $outer_iters; $i++) {
        // one full tokenize pass per outer iter; the inner loop is in C
        $tokens = $p->searchSplit($GLOBALS['SUBJECT_WHITESPACE']);
        $total_search_calls += 1; // one searchSplit call per outer iter
    }
    return [
        'iters' => $total_search_calls,
        'total_ns' => now_ns() - $start,
    ];
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

$iters = 100000;
$env_iters = getenv('PROBE_ITERS');
if ($env_iters !== false && $env_iters !== '') {
    $v = (int)$env_iters;
    if ($v > 0) $iters = $v;
}
$tokenize_iters = max(1, (int)($iters / 10));

echo "\n";
echo "libsnobol4 diagnostic probe (PHP binding)\n";
echo "==========================================\n";
echo "Iterations per scenario: $iters (override with PROBE_ITERS)\n";
echo "Tokenize uses $tokenize_iters outer iters (one searchSplit call each).\n\n";

$scenarios = [
    ['name' => 'literal_fail',        'run' => 'run_literal_fail',        'iter' => $iters],
    ['name' => 'literal_ok',          'run' => 'run_literal_ok',          'iter' => $iters],
    ['name' => 'span_comma',          'run' => 'run_span_comma',          'iter' => $iters],
    ['name' => 'alternation',         'run' => 'run_alternation',         'iter' => $iters],
    ['name' => 'alt_literals',        'run' => 'run_alt_literals',        'iter' => $iters],
    ['name' => 'alt_literals_search', 'run' => 'run_alt_literals_search', 'iter' => $iters],
    ['name' => 'tokenize_php',        'run' => 'run_tokenize_php',        'iter' => $tokenize_iters],
];

$results = [];
foreach ($scenarios as $s) {
    $timing = $s['run']($s['iter']);
    $results[] = [
        'name'           => $s['name'],
        'iters'          => $timing['iters'],
        'total_ns'       => $timing['total_ns'],
        'ns_per_iter'    => $timing['iters'] > 0 ? (int)($timing['total_ns'] / $timing['iters']) : 0,
    ];
}

printf("%-20s %10s %10s\n",
    "scenario", "ns/iter", "iters");
printf("%-20s %10s %10s\n",
    "-------", "-------", "-----");

foreach ($results as $r) {
    printf("%-20s %10d %10d\n",
        $r['name'],
        $r['ns_per_iter'],
        $r['iters']);
}

echo "\n";
echo "Legend: see bench/c/bench_probe.c for column definitions.\n";
echo "PHP probe measures the full user-facing path (binding cost included).\n";
echo "Run bench/c/bench_probe.c to compare against the pure C path.\n";
echo "\n";

/* Optional baseline regression guard. Reads the committed JSON baseline
 * and asserts each scenario's ns_per_iter is within the threshold. */
if (getenv('PROBE_BASELINE') === '1') {
    $baseline_path = getenv('PROBE_BASELINE_PATH') ?: __DIR__ . '/../../bench/results/search_perf_baseline.json';
    if (!is_file($baseline_path)) {
        fwrite(STDERR, "PROBE_BASELINE=1 but no baseline file at $baseline_path\n");
        exit(2);
    }
    $baseline_json = file_get_contents($baseline_path);
    $baseline = json_decode($baseline_json, true);
    if (!is_array($baseline) || !isset($baseline['php_probe'])) {
        fwrite(STDERR, "Baseline file missing php_probe section\n");
        exit(2);
    }
    $threshold_pct = 25.0;
    echo "=== Baseline regression guard (PROBE_BASELINE=1) ===\n";
    echo "Baseline file: $baseline_path\n";
    printf("%-20s %12s %12s %12s\n", "scenario", "baseline", "observed", "delta%");
    printf("%-20s %12s %12s %12s\n", "-------", "--------", "--------", "------");
    $regressions = 0;
    $speedups = 0;
    foreach ($results as $r) {
        if (!isset($baseline['php_probe'][$r['name']])) continue;
        $base = $baseline['php_probe'][$r['name']]['ns_per_iter'] ?? 0;
        if ($base <= 0) continue;
        $obs = $r['ns_per_iter'];
        $delta_pct = ($obs - $base) / $base * 100.0;
        $label = '';
        if ($delta_pct > $threshold_pct) { $label = '  REGRESSION'; $regressions++; }
        elseif ($delta_pct < -10.0)     { $label = '  speedup';    $speedups++; }
        else                             { $label = '  ok'; }
        printf("%-20s %12d %12d %+11.1f%%%s\n",
            $r['name'], $base, $obs, $delta_pct, $label);
    }
    echo "\n{$regressions} regressions, {$speedups} speedups, " . count($results) . " scenarios checked\n";
    if ($regressions > 0) {
        echo "FAILED: {$regressions} scenarios regressed by more than {$threshold_pct}%\n";
        exit(1);
    }
    echo "OK: no regressions exceeding {$threshold_pct}% threshold\n";
}

// Emit a machine-readable JSON block on stdout for the coupling test
echo json_encode($results, JSON_PRETTY_PRINT) . "\n";
