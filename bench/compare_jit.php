<?php
/**
 * Compare benchmark results with JIT OFF vs JIT ON.
 *
 * This script assumes each JSON file contains rows like:
 *   { scenario, impl: "snobol"|"pcre", opsPerSec: float|null, ... }
 *
 * It prints two kinds of comparisons:
 *  1) SNOBOL speedup from JIT:  snobol_on / snobol_off
 *  2) PCRE vs SNOBOL factor (for each mode):  pcre / snobol
 *
 * Usage:
 *   php bench/compare_jit.php \
 *     --off bench/results_tokenize.json --on bench/results_tokenize.json
 *
 * You can pass multiple suites by repeating --off/--on pairs.
 */

declare(strict_types=1);

function usage(): never
{
    fwrite(STDERR, "Usage:\n");
    fwrite(STDERR,
        "  php bench/compare_jit.php --off <off.json> --on <on.json> [--off <off2.json> --on <on2.json> ...]\n");
    exit(2);
}

/** @return array<int, array<string, mixed>> */
function load_results(string $path): array
{
    if (!is_file($path)) {
        throw new RuntimeException("File not found: {$path}");
    }
    $raw = file_get_contents($path);
    if ($raw === false) {
        throw new RuntimeException("Failed to read: {$path}");
    }
    $data = json_decode($raw, true);
    if (!is_array($data)) {
        throw new RuntimeException("Invalid JSON in: {$path}");
    }
    return $data;
}

/** @param  array<int, array<string, mixed>>  $rows */
function index_by_scenario(array $rows): array
{
    $idx = [];
    foreach ($rows as $r) {
        if (!isset($r['scenario'], $r['impl'])) {
            continue;
        }
        $scenario = (string) $r['scenario'];
        $impl = (string) $r['impl'];
        $idx[$scenario][$impl] = $r;
    }
    ksort($idx);
    return $idx;
}

function fnum(?float $x, int $dec = 2): string
{
    if ($x === null) {
        return 'N/A';
    }
    return number_format($x, $dec);
}

function fx(?float $x, int $dec = 2): string
{
    if ($x === null) {
        return 'N/A';
    }
    return number_format($x, $dec)."×";
}

function ratio(?float $a, ?float $b): ?float
{
    if ($a === null || $b === null || $b <= 0) {
        return null;
    }
    return $a / $b;
}

/**
 * Signed speedup formatter:
 * - If speedup >= 1: show +{speedup}× (faster)
 * - If speedup <  1: show -{1/speedup}× (slower)
 */
function signed_speedup(?float $on, ?float $off, int $dec = 2): string
{
    $r = ratio($on, $off);
    if ($r === null) {
        return 'N/A';
    }
    if ($r >= 1.0) {
        return number_format($r, $dec)."×";
    }
    // slower => invert and prefix '-'
    return '-'.number_format(1.0 / $r, $dec)."×";
}

// Parse args as pairs.
$args = $argv;
array_shift($args);
if (count($args) < 4) {
    usage();
}

$pairs = [];
$argc_local = count($args);
for ($i = 0; $i < $argc_local; $i++) {
    if ($args[$i] === '--off' && isset($args[$i + 1]) && isset($args[$i + 2]) && $args[$i + 2] === '--on' && isset($args[$i + 3])) {
        $pairs[] = ['off' => $args[$i + 1], 'on' => $args[$i + 3]];
        $i += 3;
        continue;
    }
    usage();
}

foreach ($pairs as $pair) {
    $offFile = $pair['off'];
    $onFile = $pair['on'];

    $off = index_by_scenario(load_results($offFile));
    $on = index_by_scenario(load_results($onFile));

    $title = basename($onFile);
    echo "\n== {$title} ==\n";
    echo "OFF: {$offFile}\n";
    echo " ON: {$onFile}\n\n";

    printf(
        "%-30s %14s %14s %10s %12s %12s\n",
        'scenario',
        'sn_off',
        'sn_on',
        'jit',
        'pcre_off',
        'pcre_on'
    );
    echo str_repeat('-', 96)."\n";

    $scenarios = array_unique(array_merge(array_keys($off), array_keys($on)));
    sort($scenarios);

    foreach ($scenarios as $scenario) {
        $snOff = isset($off[$scenario]['snobol']['opsPerSec']) && is_numeric($off[$scenario]['snobol']['opsPerSec'])
            ? (float) $off[$scenario]['snobol']['opsPerSec']
            : null;
        $snOn = isset($on[$scenario]['snobol']['opsPerSec']) && is_numeric($on[$scenario]['snobol']['opsPerSec'])
            ? (float) $on[$scenario]['snobol']['opsPerSec']
            : null;

        $pcOff = isset($off[$scenario]['pcre']['opsPerSec']) && is_numeric($off[$scenario]['pcre']['opsPerSec'])
            ? (float) $off[$scenario]['pcre']['opsPerSec']
            : null;
        $pcOn = isset($on[$scenario]['pcre']['opsPerSec']) && is_numeric($on[$scenario]['pcre']['opsPerSec'])
            ? (float) $on[$scenario]['pcre']['opsPerSec']
            : null;

        $jitSpeedup = ratio($snOn, $snOff);

        // PCRE vs SNOBOL factors for each mode.
        $pcreFasterOff = ratio($pcOff, $snOff);
        $pcreFasterOn = ratio($pcOn, $snOn);

        // Useful if you want a single number in the last columns:
        // show PCRE faster factors; if missing, show N/A.
        printf(
            "%-30s %14s %14s %10s %12s %12s\n",
            $scenario,
            fnum($snOff),
            fnum($snOn),
            signed_speedup($snOn, $snOff),
            fx($pcreFasterOff),
            fx($pcreFasterOn)
        );
    }

    echo "\nLegend:\n";
    echo "  sn_off/sn_on: SNOBOL ops/sec with JIT disabled/enabled\n";
    echo "  jit:          SNOBOL speedup from JIT (sn_on / sn_off; negative means slower)\n";
    echo "  pcre_off/on:  how many times faster PCRE is vs SNOBOL (pcre / snobol)\n";
}
