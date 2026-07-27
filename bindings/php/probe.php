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

$SUBJECT_PQR_AT_0 =
    "pqr" . str_repeat("z", 2077);

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

$SUBJECT_AUTOMATON =
    "xyzabcdxyzabcdxyzabcdxyzabcdxyzabcdxyzabcdxyzabcdxyzabcd"
    . "xyzabcdxyzabcdxyzabcdxyzabcdxyzabcdxyzabcdxyzabcdxyzabcd"
    . "xyzabcdxyzabcdxyzabcdxyzabcdxyzabcdxyzabcdxyzabcdxyzabcd"
    . "xyzabcdxyzabcdxyzabcdxyzabcdxyzabcdxyzabcdxyzabcdxyzabcd";

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
    // Anchored literal SUCCESS: subject starts with "pqr" (offset 0).
    $p = PatternHelper::fromString("'pqr'");
    for ($i = 0; $i < 100; $i++) { $p->match($GLOBALS['SUBJECT_PQR_AT_0']); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->match($GLOBALS['SUBJECT_PQR_AT_0']);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

/** @return array{iters:int,total_ns:int} */
function run_literal_late(int $iters): array
{
    // Anchored literal REJECTION: "pqr" is at offset 16, anchored at 0.
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
function run_break(int $iters): array
{
    // Exercises the TIER_BREAK_SCAN path (T0) through the binding.
    $p = PatternHelper::fromString("BREAK(',')");
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
    // FIRST match (pairs C alt_literals_search, unit=match).
    $p = PatternHelper::fromString("'cat' | 'dog' | 'fox'");
    for ($i = 0; $i < 100; $i++) { $p->match($GLOBALS['SUBJECT_ALTLIT']); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->match($GLOBALS['SUBJECT_ALTLIT']);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

/** @return array{iters:int,total_ns:int} */
function run_alt_literals_search_all(int $iters): array
{
    // ALL matches per iteration (pairs C alt_literals_search_all, unit=pass).
    $p = PatternHelper::fromString("'cat' | 'dog' | 'fox'");
    for ($i = 0; $i < 100; $i++) { $p->searchAll($GLOBALS['SUBJECT_ALTLIT']); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->searchAll($GLOBALS['SUBJECT_ALTLIT']);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

/** @return array{iters:int,total_ns:int} */
function run_alt_literals_search_flat(int $iters): array
{
    $p = PatternHelper::fromString("'cat' | 'dog' | 'fox'");
    for ($i = 0; $i < 100; $i++) { $p->searchAll($GLOBALS['SUBJECT_ALTLIT'], ['result' => 'flat']); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->searchAll($GLOBALS['SUBJECT_ALTLIT'], ['result' => 'flat']);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

/** @return array{iters:int,total_ns:int} */
function run_automatons(int $iters): array
{
    $p = PatternHelper::fromString("SPAN('abc') 'd'");
    for ($i = 0; $i < 100; $i++) { $p->match($GLOBALS['SUBJECT_AUTOMATON']); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->match($GLOBALS['SUBJECT_AUTOMATON']);
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

/** @return array{iters:int,total_ns:int} */
function run_tokenize_php_flat(int $outer_iters): array
{
    $p = PatternHelper::fromString("' '");
    for ($i = 0; $i < 10; $i++) { $p->searchSplit($GLOBALS['SUBJECT_WHITESPACE'], ['result' => 'flat']); }

    $total_search_calls = 0;
    $start = now_ns();
    for ($i = 0; $i < $outer_iters; $i++) {
        $tokens = $p->searchSplit($GLOBALS['SUBJECT_WHITESPACE'], ['result' => 'flat']);
        $total_search_calls += 1;
    }
    return [
        'iters' => $total_search_calls,
        'total_ns' => now_ns() - $start,
    ];
}

/** @return array{iters:int,total_ns:int} */
function run_tokenize_php_offsets(int $outer_iters): array
{
    $p = PatternHelper::fromString("' '");
    for ($i = 0; $i < 10; $i++) { $p->searchSplitOffsets($GLOBALS['SUBJECT_WHITESPACE']); }

    $total_search_calls = 0;
    $start = now_ns();
    for ($i = 0; $i < $outer_iters; $i++) {
        $offsets = $p->searchSplitOffsets($GLOBALS['SUBJECT_WHITESPACE']);
        $total_search_calls += 1;
    }
    return [
        'iters' => $total_search_calls,
        'total_ns' => now_ns() - $start,
    ];
}

/** @return array{iters:int,total_ns:int} */
function run_tokenize_php_offsets_flat(int $outer_iters): array
{
    $p = PatternHelper::fromString("' '");
    for ($i = 0; $i < 10; $i++) { $p->searchSplitOffsets($GLOBALS['SUBJECT_WHITESPACE'], ['result' => 'flat']); }

    $total_search_calls = 0;
    $start = now_ns();
    for ($i = 0; $i < $outer_iters; $i++) {
        $offsets = $p->searchSplitOffsets($GLOBALS['SUBJECT_WHITESPACE'], ['result' => 'flat']);
        $total_search_calls += 1;
    }
    return [
        'iters' => $total_search_calls,
        'total_ns' => now_ns() - $start,
    ];
}

// C-core residue/Vm scenarios
$SUBJECT_RESIDUE = str_repeat("a", 128);
$SUBJECT_CAT = str_repeat("a", 10);

function run_span_simd(int $iters): array
{
    $p = PatternHelper::fromString("SPAN('0-9')");
    $subj = str_repeat("0", 1023) . "x";
    for ($i = 0; $i < 100; $i++) { $p->match($subj); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->match($subj);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

function run_span_simd_miss(int $iters): array
{
    $p = PatternHelper::fromString("SPAN('0-9')");
    for ($i = 0; $i < 100; $i++) { $p->match($GLOBALS['SUBJECT_NO_PQR']); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->match($GLOBALS['SUBJECT_NO_PQR']);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

function run_notany_simd_miss(int $iters): array
{
    $p = PatternHelper::fromString("NOTANY('0')");
    $subj = str_repeat("0", 1024);
    for ($i = 0; $i < 100; $i++) { $p->match($subj); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->match($subj);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

/* All residue/pike/prefilter/zero_progress scenarios use the EXACT C probe
 * pattern sources via PatternHelper::fromString (no Builder-AST divergence),
 * and are split into a first-match (unit=match) and an all-matches
 * (unit=pass) variant so they pair 1:1 with the C probe's rows. */

function run_residue_repeat(int $iters): array
{
    $p = PatternHelper::fromString("@r('a'*) 'b'");
    for ($i = 0; $i < 100; $i++) { $p->match($GLOBALS['SUBJECT_RESIDUE']); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->match($GLOBALS['SUBJECT_RESIDUE']);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

function run_residue_repeat_all(int $iters): array
{
    $p = PatternHelper::fromString("@r('a'*) 'b'");
    for ($i = 0; $i < 100; $i++) { $p->searchAll($GLOBALS['SUBJECT_RESIDUE']); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->searchAll($GLOBALS['SUBJECT_RESIDUE']);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

function run_residue_zero_width(int $iters): array
{
    $p = PatternHelper::fromString("(''*) 'b'");
    for ($i = 0; $i < 100; $i++) { $p->match($GLOBALS['SUBJECT_RESIDUE']); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->match($GLOBALS['SUBJECT_RESIDUE']);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

function run_residue_zero_width_all(int $iters): array
{
    $p = PatternHelper::fromString("(''*) 'b'");
    for ($i = 0; $i < 100; $i++) { $p->searchAll($GLOBALS['SUBJECT_RESIDUE']); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->searchAll($GLOBALS['SUBJECT_RESIDUE']);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

function run_residue_catastrophic(int $iters): array
{
    $p = PatternHelper::fromString("('a'+)+ 'b'");
    $subj = str_repeat("a", 10);
    for ($i = 0; $i < 10; $i++) { $p->match($subj); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->match($subj);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

function run_pike_overflow(int $iters): array
{
    $p = PatternHelper::fromString("BREAKX(' ')");
    $subj = str_repeat("x", 900) . " ";
    for ($i = 0; $i < 100; $i++) { $p->match($subj); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->match($subj);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

function run_pike_overflow_all(int $iters): array
{
    $p = PatternHelper::fromString("BREAKX(' ')");
    $subj = str_repeat("x", 900) . " ";
    for ($i = 0; $i < 100; $i++) { $p->searchAll($subj); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->searchAll($subj);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

function run_prefilter_miss(int $iters): array
{
    $p = PatternHelper::fromString("('a'+)+ 'b'");
    $subj = str_repeat("a", 10);
    for ($i = 0; $i < 100; $i++) { $p->match($subj); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->match($subj);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

function run_prefilter_miss_all(int $iters): array
{
    $p = PatternHelper::fromString("('a'+)+ 'b'");
    $subj = str_repeat("a", 10);
    for ($i = 0; $i < 100; $i++) { $p->searchAll($subj); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->searchAll($subj);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

function run_zero_progress(int $iters): array
{
    $p = PatternHelper::fromString("('a'*) 'b'");
    $subj = str_repeat("a", 64);
    for ($i = 0; $i < 100; $i++) { $p->match($subj); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->match($subj);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
}

function run_zero_progress_all(int $iters): array
{
    $p = PatternHelper::fromString("('a'*) 'b'");
    $subj = str_repeat("a", 64);
    for ($i = 0; $i < 100; $i++) { $p->searchAll($subj); }
    $start = now_ns();
    for ($i = 0; $i < $iters; $i++) {
        $p->searchAll($subj);
    }
    return ['iters' => $iters, 'total_ns' => now_ns() - $start];
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
$heavy_iters = max(1, (int)($iters / 100));  // for slow scenarios

echo "\n";
echo "libsnobol4 diagnostic probe (PHP binding)\n";
echo "==========================================\n";
echo "Iterations per scenario: $iters (override with PROBE_ITERS)\n";
echo "Tokenize uses $tokenize_iters outer iters (one searchSplit call each).\n\n";

$scenarios = [
    /* Anchored first-match scenarios (unit=match) */
    ['name' => 'literal_fail',           'run' => 'run_literal_fail',           'iter' => $iters, 'unit' => 'match'],
    ['name' => 'literal_ok',             'run' => 'run_literal_ok',             'iter' => $iters, 'unit' => 'match'],
    ['name' => 'literal_late',           'run' => 'run_literal_late',           'iter' => $iters, 'unit' => 'match'],
    ['name' => 'span_comma',             'run' => 'run_span_comma',             'iter' => $iters, 'unit' => 'match'],
    ['name' => 'break_comma',            'run' => 'run_break',                  'iter' => $iters, 'unit' => 'match'],
    ['name' => 'alternation',            'run' => 'run_alternation',            'iter' => $iters, 'unit' => 'match'],
    ['name' => 'alt_literals',           'run' => 'run_alt_literals',           'iter' => $iters, 'unit' => 'match'],
    ['name' => 'alt_literals_search',    'run' => 'run_alt_literals_search',    'iter' => $iters, 'unit' => 'match'],
    ['name' => 'automaton',              'run' => 'run_automatons',             'iter' => $iters, 'unit' => 'match'],
    ['name' => 'span_simd',              'run' => 'run_span_simd',              'iter' => $iters, 'unit' => 'match'],
    ['name' => 'span_simd_miss',         'run' => 'run_span_simd_miss',         'iter' => $iters, 'unit' => 'match'],
    ['name' => 'notany_simd_miss',       'run' => 'run_notany_simd_miss',       'iter' => $iters, 'unit' => 'match'],
    /* All-matches scenarios (unit=pass) — pair with C *_all rows */
    ['name' => 'alt_literals_search_all', 'run' => 'run_alt_literals_search_all', 'iter' => $iters, 'unit' => 'pass'],
    ['name' => 'residue_repeat_all',     'run' => 'run_residue_repeat_all',     'iter' => $heavy_iters, 'unit' => 'pass'],
    ['name' => 'residue_zero_width_all', 'run' => 'run_residue_zero_width_all', 'iter' => $heavy_iters, 'unit' => 'pass'],
    ['name' => 'pike_overflow_all',      'run' => 'run_pike_overflow_all',      'iter' => $heavy_iters, 'unit' => 'pass'],
    ['name' => 'prefilter_miss_all',     'run' => 'run_prefilter_miss_all',     'iter' => $heavy_iters, 'unit' => 'pass'],
    ['name' => 'zero_progress_all',      'run' => 'run_zero_progress_all',      'iter' => $heavy_iters, 'unit' => 'pass'],
    /* First-match residue/pike/prefilter/zero_progress (unit=match) */
    ['name' => 'residue_repeat',         'run' => 'run_residue_repeat',         'iter' => $heavy_iters, 'unit' => 'match'],
    ['name' => 'residue_zero_width',     'run' => 'run_residue_zero_width',     'iter' => $heavy_iters, 'unit' => 'match'],
    ['name' => 'residue_catastrophic',   'run' => 'run_residue_catastrophic',   'iter' => 1000,         'unit' => 'match'],
    ['name' => 'pike_overflow',          'run' => 'run_pike_overflow',          'iter' => $heavy_iters, 'unit' => 'match'],
    ['name' => 'prefilter_miss',         'run' => 'run_prefilter_miss',         'iter' => $heavy_iters, 'unit' => 'match'],
    ['name' => 'zero_progress',          'run' => 'run_zero_progress',          'iter' => $heavy_iters, 'unit' => 'match'],
    /* Tokenize: per full pass (pairs C tokenize_reuse, unit=pass) */
    ['name' => 'tokenize_reuse',         'run' => 'run_tokenize_php',           'iter' => $tokenize_iters, 'unit' => 'pass'],
    /* PHP-specific binding variants (no C counterpart) */
    ['name' => 'alt_literals_search_flat','run' => 'run_alt_literals_search_flat','iter' => $iters, 'unit' => 'pass'],
    ['name' => 'tokenize_php_flat',      'run' => 'run_tokenize_php_flat',      'iter' => $tokenize_iters, 'unit' => 'pass'],
    ['name' => 'tokenize_php_offsets',   'run' => 'run_tokenize_php_offsets',   'iter' => $tokenize_iters, 'unit' => 'pass'],
    ['name' => 'tokenize_php_offsets_flat','run' => 'run_tokenize_php_offsets_flat','iter' => $tokenize_iters, 'unit' => 'pass'],
];

$results = [];
foreach ($scenarios as $s) {
    $timing = $s['run']($s['iter']);
    $results[] = [
        'name'           => $s['name'],
        'iters'          => $timing['iters'],
        'total_ns'       => $timing['total_ns'],
        'unit'           => $s['unit'],
        'ns_per_iter'    => $timing['iters'] > 0 ? (int)($timing['total_ns'] / $timing['iters']) : 0,
    ];
}

printf("%-24s %10s %10s %-5s\n",
    "scenario", "ns/iter", "iters", "unit");
printf("%-24s %10s %10s %-5s\n",
    "-------", "-------", "-----", "----");

foreach ($results as $r) {
    printf("%-24s %10d %10d %-5s\n",
        $r['name'],
        $r['ns_per_iter'],
        $r['iters'],
        $r['unit']);
}

echo "\n";
echo "Legend: see bench/c/bench_probe.c for column definitions.\n";
echo "unit = match (one match attempt) | pass (one full all-match/split pass).\n";
echo "PHP probe measures the full user-facing path (binding cost included).\n";
echo "Run bench/c/bench_probe.c to compare against the pure C path.\n";
echo "Only rows with the same unit are comparable across the two probes.\n";
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
