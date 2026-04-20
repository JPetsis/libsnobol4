<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\PatternHelper;

/**
 * JIT Branch Completeness — PHP-layer regression tests.
 *
 * Verifies that alternation / multi-delimiter patterns produce correct token
 * output when cold and when the JIT has warmed up, covering the search-oriented
 * delimiter SPLIT path added in the jit-basic-branches change.
 */
class JitBranchesTest extends TestCase
{
    /**
     * A pattern matching '|' or ',' as delimiter.
     * searchSplit should return the segments between delimiters correctly.
     */
    public function testMultiDelimiterSplit(): void
    {
        $segments = PatternHelper::split("'|' | ','", 'a,b|c,d');

        $this->assertCount(4, $segments,
            'Multi-delimiter split on "a,b|c,d" should yield 4 segments');
        $this->assertSame('a', $segments[0]);
        $this->assertSame('b', $segments[1]);
        $this->assertSame('c', $segments[2]);
        $this->assertSame('d', $segments[3]);
    }

    /**
     * Warm the JIT then verify result is still correct.
     * Exercises the search-oriented delimiter SPLIT path in compiled code.
     */
    public function testMultiDelimiterSplitJitWarm(): void
    {
        $pattern = "'|' | ','";
        $subject = 'one,two|three,four|five';

        $lastResult = null;
        for ($i = 0; $i < 150; $i++) {
            $lastResult = PatternHelper::split($pattern, $subject);
        }

        $this->assertNotNull($lastResult);
        $this->assertCount(5, $lastResult,
            'JIT-warm multi-delimiter split should yield 5 segments');
        $this->assertSame('one', $lastResult[0]);
        $this->assertSame('two', $lastResult[1]);
        $this->assertSame('three', $lastResult[2]);
        $this->assertSame('four', $lastResult[3]);
        $this->assertSame('five', $lastResult[4]);
    }

    /**
     * matchAll with alternation pattern — verifies match counts are correct.
     */
    public function testMultiDelimiterMatchAll(): void
    {
        $matches = PatternHelper::matchAll("'|' | ','", 'a,b|c,d|e');

        // 4 delimiters: ',', '|', ',', '|'
        $this->assertCount(4, $matches,
            'matchAll on "a,b|c,d|e" with | or , delimiter should find 4 matches');
    }

    /**
     * Alternation: first branch fails, second branch succeeds.
     * Exercises SPLIT backtracking: choice pushed for second branch,
     * first fails, match falls back to second.
     */
    public function testAlternationFallsBackToSecondBranch(): void
    {
        $matches = PatternHelper::matchAll("'x' | 'a'", 'bab');

        $this->assertCount(1, $matches,
            "Alternation 'x'|'a' on 'bab' should find one 'a'");
    }

    /**
     * Alternation with no match — JIT-warm, result must still be empty.
     */
    public function testAlternationNoMatchJitWarm(): void
    {
        $pattern = "'x' | 'y'";
        $subject = 'abcdef';

        $result = null;
        for ($i = 0; $i < 150; $i++) {
            $result = PatternHelper::matchAll($pattern, $subject);
        }

        $this->assertNotNull($result);
        $this->assertCount(0, $result,
            "JIT-warm alternation: no match in subject without 'x' or 'y'");
    }
}



