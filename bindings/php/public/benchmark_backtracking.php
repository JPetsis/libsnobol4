<?php
// public/benchmark_backtracking.php
//
// Backtracking-heavy microbenchmarks:
// - deep alternation (ALT chains)
// - heavy (greedy) repetition with backtracking
//
// Configure runtime via env vars:
//   BENCH_ITERS=50000
//   BENCH_ALT_DEPTH=200
//   BENCH_REPEAT_MAX=200

require __DIR__.'/../vendor/autoload.php';

use Snobol\Builder;
use Snobol\Pattern;

$iters = 100000;
$altDepth = 200;
$repeatMax = 200;

if (isset($argv[1]) && is_numeric($argv[1])) {
    $iters = (int) $argv[1];
} elseif (getenv('BENCH_ITERS') !== false) {
    $iters = (int) getenv('BENCH_ITERS');
}

if (isset($argv[2]) && is_numeric($argv[2])) {
    $altDepth = (int) $argv[2];
} elseif (getenv('BENCH_ALT_DEPTH') !== false) {
    $altDepth = (int) getenv('BENCH_ALT_DEPTH');
}

if (isset($argv[3]) && is_numeric($argv[3])) {
    $repeatMax = (int) $argv[3];
} elseif (getenv('BENCH_REPEAT_MAX') !== false) {
    $repeatMax = (int) getenv('BENCH_REPEAT_MAX');
}

function bench(string $label, Pattern $pattern, string $subject, int $iters): void
{
    $start = microtime(true);
    for ($i = 0; $i < $iters; $i++) {
        $res = $pattern->match($subject);
    }
    $dur = microtime(true) - $start;
    printf("%-30s: %.4f sec (%.2f ops/sec)\n", $label, $dur, $iters / max(0.0000001, $dur));
}

echo "Building patterns...\n";

// Deep alternation: 'a' | 'b' | ... | 'z' | '!' (not present)
// Subject is '!' so it backtracks through most branches.
$alt = Builder::lit('a');
for ($i = 1; $i < $altDepth; $i++) {
    // cycle through 'a'..'z' (ensures bytecode isn't identical literals only)
    $ch = chr(ord('a') + ($i % 26));
    $alt = Builder::alt($alt, Builder::lit($ch));
}
$alt = Builder::alt($alt, Builder::lit('!'));

$p_alt = Pattern::compileFromAst($alt);

// Heavy repetition with backtracking:
// repeat('a', 0..repeatMax) then 'b'
// Subject is many 'a's then 'b'. Greedy repeat goes to max then backtracks.
$rep = Builder::concat([
    Builder::repeat(Builder::lit('a'), 0, $repeatMax),
    Builder::lit('b'),
]);
$p_rep = Pattern::compileFromAst($rep);

$subjectAlt = '!';
$subjectRep = str_repeat('a', $repeatMax).'b';

echo "Running ($iters iterations)...\n";
bench("ALT chain (depth=$altDepth)", $p_alt, $subjectAlt, $iters);
bench("Repeat backtrack (max=$repeatMax)", $p_rep, $subjectRep, max(1, (int) ($iters / 20)));

echo "\nMemory Usage: ".memory_get_peak_usage(true)." bytes\n";
