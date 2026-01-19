<?php
/**
 * Compare benchmark JSONs and print SNOBOL/PCRE ratios.
 *
 * Usage:
 *   php bench/compare.php bench/results_tokenize.json [bench/results_replace.json ...]
 */

declare(strict_types=1);

function usage(): never
{
    fwrite(STDERR, "Usage: php bench/compare.php <results_*.json> [more...]}\n");
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

if ($argc < 2) {
    usage();
}

$files = array_slice($argv, 1);

foreach ($files as $file) {
    $rows = load_results($file);
    $idx = index_by_scenario($rows);

    echo "\n== ".$file." ==\n";
    printf("%-30s %14s %14s %12s\n", "scenario", "snobol ops/s", "pcre ops/s", "ratio");
    echo str_repeat('-', 75)."\n";

    foreach ($idx as $scenario => $impls) {
        $sn = $impls['snobol']['opsPerSec'] ?? null;
        $pc = $impls['pcre']['opsPerSec'] ?? null;

        $snDisp = is_numeric($sn) ? number_format((float) $sn, 2) : 'N/A';
        $pcDisp = is_numeric($pc) ? number_format((float) $pc, 2) : 'N/A';

        $ratio = (is_numeric($sn) && is_numeric($pc) && (float) $pc > 0)
            ? ((float) $sn / (float) $pc)
            : null;

        $ratioDisp = $ratio !== null ? number_format($ratio, 4) : 'N/A';

        printf("%-30s %14s %14s %12s\n", $scenario, $snDisp, $pcDisp, $ratioDisp);
    }
}
