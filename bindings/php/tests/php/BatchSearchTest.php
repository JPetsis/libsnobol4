<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Builder;
use Snobol\Pattern;
use Snobol\PatternHelper;

/**
 * Tests for batch-search API integration in PHP binding.
 *
 * Verifies that searchAll, searchSplit, and searchReplace produce identical
 * results whether the pattern is eligible (batch path) or not (per-call fallback).
 */
class BatchSearchTest extends TestCase
{
    // === searchAll ===

    public function testSearchAllLiteralPattern(): void
    {
        $p = Pattern::fromString("'abc'");
        $results = $p->searchAll('abcabcabc');
        $this->assertCount(3, $results);
        foreach ($results as $r) {
            $this->assertSame(3, $r['_match_len']);
        }
    }

    public function testSearchAllSpanPattern(): void
    {
        $p = Pattern::fromString("SPAN('0-9')");
        $results = $p->searchAll('abc123def456ghi');
        $this->assertCount(2, $results);
        $this->assertSame(3, $results[0]['_match_len']);
        $this->assertSame(3, $results[1]['_match_len']);
    }

    public function testSearchAllAltLitPattern(): void
    {
        $p = Pattern::fromString("'cat' | 'dog' | 'fox'");
        $results = $p->searchAll('the cat went dog walking fox');
        $this->assertCount(3, $results);
    }

    public function testSearchAllZeroMatches(): void
    {
        $p = Pattern::fromString("'xyz'");
        $results = $p->searchAll('abc');
        $this->assertCount(0, $results);
    }

    public function testSearchAllWithFlatResult(): void
    {
        $p = Pattern::fromString("'a'");
        $results = $p->searchAll('aXaXa', ['result' => 'flat']);
        $this->assertArrayHasKey('match_start', $results);
        $this->assertArrayHasKey('match_len', $results);
        $this->assertCount(3, $results['match_len']);
    }

    // === searchSplit ===

    public function testSearchSplitLiteralPattern(): void
    {
        $p = Pattern::fromString("','");
        $parts = $p->searchSplit('a,b,c');
        $this->assertCount(3, $parts);
        $this->assertSame('a', $parts[0]);
        $this->assertSame('b', $parts[1]);
        $this->assertSame('c', $parts[2]);
    }

    public function testSearchSplitNoMatch(): void
    {
        $p = Pattern::fromString("'xyz'");
        $parts = $p->searchSplit('abc');
        $this->assertCount(1, $parts);
        $this->assertSame('abc', $parts[0]);
    }

    public function testSearchSplitOffsets(): void
    {
        $p = Pattern::fromString("','");
        $parts = $p->searchSplitOffsets('a,b,c');
        $this->assertCount(3, $parts);
        $this->assertSame([0, 1], $parts[0]); // "a"
        $this->assertSame([2, 1], $parts[1]); // "b"
        $this->assertSame([4, 1], $parts[2]); // "c"
    }

    public function testSearchSplitFlatResult(): void
    {
        $p = Pattern::fromString("','");
        $parts = $p->searchSplit('a,b,c', ['result' => 'flat']);
        $this->assertCount(6, $parts); // flat: [start, len, start, len, start, len]
        $this->assertSame(0, $parts[0]);
        $this->assertSame(1, $parts[1]);
        $this->assertSame(2, $parts[2]);
        $this->assertSame(1, $parts[3]);
        $this->assertSame(4, $parts[4]);
        $this->assertSame(1, $parts[5]);
    }

    // === searchReplace ===

    public function testSearchReplaceLiteral(): void
    {
        $p = Pattern::fromString("'world'");
        $result = $p->searchReplace('hello world', 'there');
        $this->assertSame('hello there', $result);
    }

    public function testSearchReplaceMultiple(): void
    {
        $p = Pattern::fromString("'a'");
        $result = $p->searchReplace('abcabc', 'X');
        $this->assertSame('XbcXbc', $result);
    }

    public function testSearchReplaceNoMatch(): void
    {
        $p = Pattern::fromString("'xyz'");
        $result = $p->searchReplace('abc', 'X');
        $this->assertSame('abc', $result);
    }

    // === Captures with batch path ===

    public function testSearchAllWithCaptures(): void
    {
        $ast = Builder::concat([
            Builder::cap(0, Builder::span('0123456789')),
            Builder::lit('-'),
        ]);
        $p = Pattern::compileFromAst($ast);
        $results = PatternHelper::matchAll($p, '123-456-');
        $this->assertCount(2, $results);
        $this->assertSame('123', $results[0]['v0']);
        $this->assertSame('456', $results[1]['v0']);
    }

    public function testSearchAllWithCaptureOffsets(): void
    {
        $ast = Builder::concat([
            Builder::cap(0, Builder::span('0123456789')),
            Builder::lit('-'),
        ]);
        $p = Pattern::compileFromAst($ast);
        $results = $p->searchAll('123-456-', ['captures' => 'offsets']);
        $this->assertCount(2, $results);
        $this->assertArrayHasKey('v0', $results[0]);
        $this->assertCount(2, $results[0]['v0']);
    }

    // === searchSplitCuts ===

    public function testSearchSplitCuts(): void
    {
        $p = Pattern::fromString("' '");
        $cuts = $p->searchSplitCuts('a b c');
        $this->assertCount(2, $cuts);
        $this->assertSame(2, $cuts[0]); // end of first delimiter
        $this->assertSame(4, $cuts[1]); // end of second delimiter
    }

    // === Edge cases ===

    public function testEmptySubject(): void
    {
        $p = Pattern::fromString("'a'");
        $results = $p->searchAll('');
        $this->assertCount(0, $results);
    }

    public function testSearchReplaceEmptyReplacement(): void
    {
        $p = Pattern::fromString("'a'");
        $result = $p->searchReplace('aaa', '');
        $this->assertSame('', $result);
    }
}
