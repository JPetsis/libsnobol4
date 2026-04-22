<?php
require __DIR__.'/vendor/autoload.php';

use Snobol\Builder;
use Snobol\Pattern;

echo "=== libsnobol4 Compact Choice Stack Memory Observer ===\n\n";

/**
 * Helper: compile, match, and print metrics.
 */
function runTest(string $label, $ast, string $subject, bool $jit = false): void
{
    $pattern = Pattern::compileFromAst($ast);
    $pattern->setJit($jit);
    $result = $pattern->match($subject);

    echo "Test: $label\n";
    echo "  Subject: ".json_encode($subject)." (".strlen($subject)." chars)\n";
    echo "  JIT:     ".($jit ? 'on' : 'off')."\n";
    echo "  Result:  ".($result === false ? 'NO MATCH' : 'MATCH (len='.$result['_match_len'].')')."\n";

    if ($result !== false && isset($result['_metrics'])) {
        $m = $result['_metrics'];
        printf("  choice_push_count : %d\n", $m['choice_push_count']);
        printf("  choice_allocated  : %s bytes (%.2f KB)\n",
            number_format($m['choice_allocated']),
            $m['choice_allocated'] / 1024
        );
        printf("  choice_peak_depth : %d\n", $m['choice_peak_depth']);
        printf("  choice_peak_memory: %s bytes (%.2f KB)\n",
            number_format($m['choice_peak_memory']),
            $m['choice_peak_memory'] / 1024
        );
    } else {
        echo "  (no match → metrics not available)\n";
    }
    echo "\n";
}

// ── Test 1: multi-char alternation (can't be fused into OP_ANY)
// alt("abc", "x1x", "x2x", ..., "!!!") — tries each multi-char literal before matching '!!!'
$depth = 50;
$alt = Builder::lit('abc');
for ($i = 1; $i < $depth; $i++) {
    $alt = Builder::alt($alt, Builder::lit('x'.$i.'x'));
}
$alt = Builder::alt($alt, Builder::lit('!!!'));
runTest("Deep multi-char alternation ($depth branches, last wins)", $alt, '!!!');

// ── Test 2: greedy star backtracking
// `a*` followed by literal `!`
// Subject "aaaa!" — star greedily consumes 'aaaa', then backtracks to match '!'
$star = Builder::concat([Builder::repeat(Builder::lit('a'), 0), Builder::lit('!')]);
runTest("Greedy a* then ! (backtracking needed)", $star, 'aaaa!');

// ── Test 3: large alternation depth comparison
$deepAlt = Builder::lit('ab');
for ($i = 1; $i < 100; $i++) {
    $deepAlt = Builder::alt($deepAlt, Builder::lit('x'.str_pad($i, 3, '0', STR_PAD_LEFT)));
}
$deepAlt = Builder::alt($deepAlt, Builder::lit('!!'));
runTest("100-branch multi-char alternation (last wins)", $deepAlt, '!!');
