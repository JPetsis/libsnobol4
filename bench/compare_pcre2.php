<?php
/**
 * Head-to-head benchmark: libsnobol4 vs PCRE2
 *
 * Runs representative workloads on both engines and records performance.
 * Results are saved to results_pcre2_comparison.json.
 *
 * Usage:
 *   php bench/compare_pcre2.php
 *
 * Requires: PHP with snobol extension loaded
 */

require_once __DIR__ . '/Harness.php';

use Bench\Harness;
use Snobol\Builder as B;
use Snobol\PatternHelper as PH;

$harness = new Harness();

/* ── Literal matching ──────────────────────────────────────────────────── */

$iterations = 10000;

$harness->bench(
    'literal_short',
    'snobol',
    function () {
        PH::matchOnce("'hello'", "hello world");
    },
    100, $iterations
);

$harness->bench(
    'literal_short',
    'pcre',
    function () {
        preg_match('/hello/', "hello world");
    },
    100, $iterations
);

/* ── Alternation / delimiter search ───────────────────────────────────── */

$altSubject = "apple,banana,cherry,date,elderberry";
$altAst = B::brk(',');
$altPatternPcre = '/[^,]+/';

$harness->bench(
    'alternation_delimiter',
    'snobol',
    function () use ($altAst, $altSubject) {
        PH::matchOnce($altAst, $altSubject);
    },
    100, $iterations
);

$harness->bench(
    'alternation_delimiter',
    'pcre',
    function () use ($altPatternPcre, $altSubject) {
        preg_match_all($altPatternPcre, $altSubject);
    },
    100, $iterations
);

/* ── Tokenization ──────────────────────────────────────────────────────── */

$tokenSubject = str_repeat("token1,token2,token3,token4,token5,", 200);
$tokenAst = B::brk(',');
$tokenPatternPcre = '/[^,]+/';

$harness->bench(
    'tokenization',
    'snobol',
    function () use ($tokenAst, $tokenSubject) {
        PH::matchAll($tokenAst, $tokenSubject);
    },
    50, 500, strlen($tokenSubject)
);

$harness->bench(
    'tokenization',
    'pcre',
    function () use ($tokenPatternPcre, $tokenSubject) {
        preg_match_all($tokenPatternPcre, $tokenSubject);
    },
    50, 500, strlen($tokenSubject)
);

/* ── Substitution ──────────────────────────────────────────────────────── */

$subSubject = str_repeat("The quick brown fox jumps over the lazy dog. ", 50);
$subPatternSnobol = "'fox'";
$subPatternPcre = '/fox/';

$harness->bench(
    'substitution',
    'snobol',
    function () use ($subPatternSnobol, $subSubject) {
        PH::matchOnce($subPatternSnobol, $subSubject);
    },
    50, 500, strlen($subSubject)
);

$harness->bench(
    'substitution',
    'pcre',
    function () use ($subPatternPcre, $subSubject) {
        preg_match($subPatternPcre, $subSubject);
    },
    50, 500, strlen($subSubject)
);

/* ── Complex pattern ───────────────────────────────────────────────────── */

$complexSubject = "GET /api/users HTTP/1.1\r\nHost: example.com\r\nAccept: */*";
$complexAst = B::concat([
    B::span('ABCDEFGHIJKLMNOPQRSTUVWXYZ'),
    B::span(' '),
    B::brk(' '),
    B::span(' '),
    B::brk("\r"),
]);
$complexPatternPcre = '/^([A-Z]+)\s+([^\s]+)\s+(.+)$/m';

$harness->bench(
    'complex_http',
    'snobol',
    function () use ($complexAst, $complexSubject) {
        PH::matchOnce($complexAst, $complexSubject);
    },
    100, $iterations
);

$harness->bench(
    'complex_http',
    'pcre',
    function () use ($complexPatternPcre, $complexSubject) {
        preg_match($complexPatternPcre, $complexSubject);
    },
    100, $iterations
);

/* ── Output ────────────────────────────────────────────────────────────── */

$harness->printSummary();
$harness->writeJson(__DIR__ . '/results_pcre2_comparison.json');
$harness->writeMarkdown(__DIR__ . '/BENCHMARKS.md');

echo "\nResults saved to bench/results_pcre2_comparison.json\n";
