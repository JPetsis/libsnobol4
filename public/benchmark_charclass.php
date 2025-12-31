<?php
// public/benchmark_charclass.php

require __DIR__.'/../vendor/autoload.php';

use Snobol\Builder;
use Snobol\Pattern;

$iterations = 100000;
$subject = str_repeat("The quick brown fox jumps over the lazy dog. ", 10); // ~430 chars
$subject_unicode = str_repeat("The quick bröwn föx jümps över the lazy dög. αβγde", 10);

echo "Generating patterns...\n";

// 1. Simple ASCII Span (formerly bitmap)
// [a-z]
$p_ascii = Pattern::compileFromAst(Builder::span("abcdefghijklmnopqrstuvwxyz"));

// 2. Unicode Span (ranges)
// [a-z] + greek
$p_unicode = Pattern::compileFromAst(Builder::span("abcdefghijklmnopqrstuvwxyzαβγ"));

// 3. Worst case for ranges (alternating chars to prevent merging)
// "aceg..." vs [a-z]
$p_sparse = Pattern::compileFromAst(Builder::span("acegikmoqsuwy"));

function bench($label, $pattern, $subject, $iters)
{
    $start = microtime(true);
    for ($i = 0; $i < $iters; $i++) {
        $pattern->match($subject);
    }
    $end = microtime(true);
    $dur = $end - $start;
    echo sprintf("% -30s: %.4f sec (%.2f ops/sec)\n", $label, $dur, $iters / $dur);
}

echo "Starting Benchmark ($iterations iterations)...";

bench("ASCII Span (Dense)", $p_ascii, $subject, $iterations);
bench("Unicode Span (Mixed)", $p_unicode, $subject_unicode, $iterations);
bench("ASCII Span (Sparse)", $p_sparse, $subject, $iterations);

echo "\nMemory Usage: ".memory_get_peak_usage(true)." bytes\n";

