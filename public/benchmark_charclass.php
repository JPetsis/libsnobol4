<?php
// public/benchmark_charclass.php

require __DIR__.'/../vendor/autoload.php';

use Snobol\Builder;
use Snobol\Pattern;

$iterations = 100000;
if (isset($argv[1]) && is_numeric($argv[1])) {
    $iterations = (int) $argv[1];
} elseif (getenv('BENCH_ITERS') !== false) {
    $iterations = (int) getenv('BENCH_ITERS');
}

$subject = str_repeat("The quick brown fox jumps over the lazy dog. ", 10); // ~430 chars
$subject_unicode = str_repeat("The quick bröwn föx jümps איבער the lazy dög. αβγde", 10);

// Large subjects for scan-heavy ops
$big_ascii = str_repeat("0123456789 abcdefghijklmnopqrstuvwxyz ", 5000); // ~185k bytes
$big_unicode = str_repeat("0123456789 café naïve über αβγδεζη ", 5000);

echo "Generating patterns...\n";

// 1. Simple ASCII Span (dense) [a-z]
$p_ascii = Pattern::compileFromAst(Builder::span("abcdefghijklmnopqrstuvwxyz"));

// 2. Unicode Span (ranges) [a-z] + greek
$p_unicode = Pattern::compileFromAst(Builder::span("abcdefghijklmnopqrstuvwxyzαβγ"));

// 3. Worst case for ranges (sparse)
$p_sparse = Pattern::compileFromAst(Builder::span("acegikmoqsuwy"));

// 4. BREAK scan (ASCII)
$p_break_ascii = Pattern::compileFromAst(Builder::brk("abcdefghijklmnopqrstuvwxyz"));

// 5. BREAK scan (Unicode/mixed)
$p_break_unicode = Pattern::compileFromAst(Builder::brk("abcdefghijklmnopqrstuvwxyzαβγ"));

function bench($label, $pattern, $subject, $iters)
{
    $start = microtime(true);
    for ($i = 0; $i < $iters; $i++) {
        $pattern->match($subject);
    }
    $end = microtime(true);
    $dur = $end - $start;
    echo sprintf("% -30s: %.4f sec (%.2f ops/sec)\n", $label, $dur, $iters / max(0.0000001, $dur));
}

echo "Starting Benchmark ($iterations iterations)...\n";

bench("ASCII Span (Dense)", $p_ascii, $subject, $iterations);
bench("Unicode Span (Mixed)", $p_unicode, $subject_unicode, $iterations);
bench("ASCII Span (Sparse)", $p_sparse, $subject, $iterations);

// scan-heavy
bench("ASCII BREAK (Big)", $p_break_ascii, $big_ascii, max(1, (int) ($iterations / 50)));
bench("Unicode BREAK (Big)", $p_break_unicode, $big_unicode, max(1, (int) ($iterations / 50)));
bench("ASCII Span (Big)", $p_ascii, $big_ascii, max(1, (int) ($iterations / 50)));
bench("Unicode Span (Big)", $p_unicode, $big_unicode, max(1, (int) ($iterations / 50)));

echo "\nMemory Usage: ".memory_get_peak_usage(true)." bytes\n";

