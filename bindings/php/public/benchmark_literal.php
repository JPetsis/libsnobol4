<?php
// public/benchmark_literal.php
// Compares Pattern::match() vs Pattern::matchLiteral() for literal-only patterns.

require __DIR__ . '/../vendor/autoload.php';

use Snobol\Builder;
use Snobol\Pattern;

$iterations = 100000;
if (isset($argv[1]) && is_numeric($argv[1])) {
    $iterations = (int)$argv[1];
} elseif (getenv('BENCH_ITERS') !== false) {
    $iterations = (int)getenv('BENCH_ITERS');
}

// Subjects of varying sizes
$subj_short = "hello world this is a test";
$subj_1k    = str_repeat("abcdefghijklmnopqrstuvwxyz", 40); // 1040 bytes
$subj_long  = str_repeat("abcdefghijklmnopqrstuvwxyz", 400); // 10400 bytes

// Patterns
$p_lit      = Pattern::fromString("'hello'");        // literal-only
$p_lit_long = Pattern::fromString("'abcdefghij'");    // longer literal
$p_nolit    = Pattern::fromString("SPAN('abc')");     // non-literal
$p_missing  = Pattern::fromString("'xyz'");            // won't match

echo "PHP Literal-match Benchmark ($iterations iterations each)\n";
echo "=========================================================\n\n";

function bench($label, $fn, $iters)
{
    $start = microtime(true);
    for ($i = 0; $i < $iters; $i++) {
        $fn();
    }
    $end = microtime(true);
    $dur = $end - $start;
    $ns_per = ($dur / $iters) * 1e9;
    printf("%-32s %8.2f ms  %8.0f ns/iter  %10.0f ops/sec\n",
        $label, $dur * 1000, $ns_per, $iters / max(0.0000001, $dur));
}

// --- Scenario 1: Short subject, literal at position 0 ---
$pat = $p_lit;
$sub = $subj_short;
bench("match() short literal",     fn() => $pat->match($sub),           $iterations);
bench("matchLiteral() short lit",  fn() => $pat->matchLiteral($sub),    $iterations);
echo "\n";

// --- Scenario 2: 1KB subject, literal at position 0 ---
$pat = $p_lit_long;
$sub = $subj_1k;
bench("match() 1KB literal",       fn() => $pat->match($sub),           $iterations);
bench("matchLiteral() 1KB lit",    fn() => $pat->matchLiteral($sub),    $iterations);
echo "\n";

// --- Scenario 3: Long subject, literal at position 0 ---
$sub = $subj_long;
bench("match() long literal",      fn() => $pat->match($sub),           $iterations);
bench("matchLiteral() long lit",   fn() => $pat->matchLiteral($sub),    $iterations);
echo "\n";

// --- Scenario 4: No match (subject doesn't contain pattern) ---
$pat = $p_missing;
$sub = $subj_short;
bench("match() no match",          fn() => $pat->match($sub),           $iterations);
bench("matchLiteral() no match",   fn() => $pat->matchLiteral($sub),    $iterations);
echo "\n";

// --- Scenario 5: Non-literal pattern ---
$pat = $p_nolit;
$sub = $subj_short;
bench("match() non-literal",       fn() => $pat->match($sub),           $iterations);
bench("matchLiteral() non-lit",    fn() => $pat->matchLiteral($sub),    $iterations);
echo "\n";

// --- Scenario 6: matchLiteral panic test (POS constrained) ---
$ast = Builder::concat([Builder::pos(5), Builder::lit("world")]);
$pat_pos = Pattern::compileFromAst($ast);
$sub = $subj_short;
bench("match() POS-constrained",   fn() => $pat_pos->match($sub),       $iterations);
bench("matchLiteral() POS-const",  fn() => $pat_pos->matchLiteral($sub),$iterations);
echo "\n";

echo "Memory Usage: " . memory_get_peak_usage(true) . " bytes\n";
