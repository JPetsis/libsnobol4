<?php
/**
 * Worst-case Backtracking Benchmark
 *
 * Tests pathological backtracking scenarios.
 * Compares SNOBOL VM vs PCRE performance.
 */

require __DIR__.'/../vendor/autoload.php';
require __DIR__.'/Harness.php';

use Bench\Harness;
use Snobol\Builder;
use Snobol\Pattern;
use Snobol\PatternHelper;

$harness = new Harness();

// Benchmark parameters
$warmup = 5;
$iterations = 100;

echo "Worst-case Backtracking Benchmark\n";
echo "==================================\n";
echo "Warmup: $warmup iterations\n";
echo "Measured: $iterations iterations\n\n";

// ------------------------------------------------------------------------
// Scenario 1: Deep alternation chain (SNOBOL)
// Pattern: 'a' | 'b' | ... | 'z' | '!'
// Subject: '!' forces backtracking through all alternatives
// ------------------------------------------------------------------------
$altDepth = 100;
$alt = Builder::lit('a');
for ($i = 1; $i < $altDepth; $i++) {
    $ch = chr(ord('a') + ($i % 26));
    $alt = Builder::alt($alt, Builder::lit($ch));
}
$alt = Builder::alt($alt, Builder::lit('!'));
$patternAlt = Pattern::compileFromAst($alt);
$subjectAlt = '!';

$harness->bench(
    'backtrack_alternation',
    'snobol',
    function () use ($patternAlt, $subjectAlt) {
        $result = $patternAlt->match($subjectAlt);
    },
    $warmup,
    $iterations,
    strlen($subjectAlt)
);

// ------------------------------------------------------------------------
// Scenario 1: Deep alternation chain (PCRE)
// ------------------------------------------------------------------------
$pcreAltPattern = '/^(';
for ($i = 0; $i < $altDepth; $i++) {
    if ($i > 0) {
        $pcreAltPattern .= '|';
    }
    $ch = chr(ord('a') + ($i % 26));
    $pcreAltPattern .= $ch;
}
$pcreAltPattern .= '|!)$/';

$harness->bench(
    'backtrack_alternation',
    'pcre',
    function () use ($pcreAltPattern, $subjectAlt) {
        $result = preg_match($pcreAltPattern, $subjectAlt);
    },
    $warmup,
    $iterations,
    strlen($subjectAlt)
);

// ------------------------------------------------------------------------
// Scenario 2: Greedy repetition with backtracking (SNOBOL)
// Pattern: repeat('a', 0, 100) 'b'
// Subject: 100 'a's then 'b' - greedy match backtracks
// ------------------------------------------------------------------------
$repeatMax = 100;
$rep = Builder::concat([
    Builder::repeat(Builder::lit('a'), 0, $repeatMax),
    Builder::lit('b'),
]);
$patternRep = Pattern::compileFromAst($rep);
$subjectRep = str_repeat('a', $repeatMax).'b';

$harness->bench(
    'backtrack_repetition',
    'snobol',
    function () use ($patternRep, $subjectRep) {
        $result = $patternRep->match($subjectRep);
    },
    $warmup,
    $iterations,
    strlen($subjectRep)
);

// ------------------------------------------------------------------------
// Scenario 2: Greedy repetition with backtracking (PCRE)
// ------------------------------------------------------------------------
$pcreRepPattern = '/^a{0,'.$repeatMax.'}b$/';

$harness->bench(
    'backtrack_repetition',
    'pcre',
    function () use ($pcreRepPattern, $subjectRep) {
        $result = preg_match($pcreRepPattern, $subjectRep);
    },
    $warmup,
    $iterations,
    strlen($subjectRep)
);

// ------------------------------------------------------------------------
// Scenario 3: Nested repetition (SNOBOL)
// Pattern: (('a'*)*) 'b'
// Subject: Many 'a's with no 'b' - causes exponential backtracking
// Note: SEVERELY reduced to avoid timeout (exponential complexity)
// ------------------------------------------------------------------------
$nestedSize = 20; // Reduced from 20 to avoid timeout
$nested = Builder::concat([
    Builder::arbno(Builder::arbno(Builder::lit('a'))),
    Builder::lit('b'),
]);
$patternNested = Pattern::compileFromAst($nested);
$subjectNested = str_repeat('a', $nestedSize); // No 'b' - forces full backtrack

$harness->bench(
    'backtrack_nested',
    'snobol',
    function () use ($patternNested, $subjectNested) {
        $result = $patternNested->match($subjectNested);
    },
    $warmup,
    50, // Reduced from 50 due to exponential cost
    strlen($subjectNested)
);

// ------------------------------------------------------------------------
// Scenario 3: Nested repetition (PCRE)
// ------------------------------------------------------------------------
$pcreNestedPattern = '/^(a*)*b$/';

$harness->bench(
    'backtrack_nested',
    'pcre',
    function () use ($pcreNestedPattern, $subjectNested) {
        $result = @preg_match($pcreNestedPattern, $subjectNested); // @ to suppress backtrack limit warnings
    },
    $warmup,
    50, // Reduced to match SNOBOL iterations
    strlen($subjectNested)
);

// ------------------------------------------------------------------------
// Scenario 4: Alternation with overlapping prefixes (SNOBOL)
// Pattern: 'aaaa' | 'aaab' | 'aaac' | 'aaad'
// Subject: 'aaae' - forces trying all alternatives
// ------------------------------------------------------------------------
$overlapPattern = PatternHelper::fromString("'aaaa' | 'aaab' | 'aaac' | 'aaad' | 'aaae'");
$subjectOverlap = 'aaae';

$harness->bench(
    'backtrack_overlap',
    'snobol',
    function () use ($overlapPattern, $subjectOverlap) {
        $result = PatternHelper::matchOnce($overlapPattern, $subjectOverlap);
    },
    $warmup,
    $iterations,
    strlen($subjectOverlap)
);

// ------------------------------------------------------------------------
// Scenario 4: Alternation with overlapping prefixes (PCRE)
// ------------------------------------------------------------------------
$pcreOverlapPattern = '/^(aaaa|aaab|aaac|aaad|aaae)$/';

$harness->bench(
    'backtrack_overlap',
    'pcre',
    function () use ($pcreOverlapPattern, $subjectOverlap) {
        $result = preg_match($pcreOverlapPattern, $subjectOverlap);
    },
    $warmup,
    $iterations,
    strlen($subjectOverlap)
);

// Output results
$harness->printSummary();

$outputFile = __DIR__.'/results_backtracking.json';
$harness->writeJson($outputFile);
echo "Results written to: $outputFile\n";
