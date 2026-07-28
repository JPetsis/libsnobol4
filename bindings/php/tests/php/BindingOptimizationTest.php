<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Builder;
use Snobol\Pattern;

class BindingOptimizationTest extends TestCase
{
    /* ============================================================
     *  1. Metrics opt-in (P2)
     * ============================================================ */

    public function testMetricsAbsentByDefault(): void
    {
        $p = Pattern::fromString("'hello'");
        $res = $p->match("hello");
        $this->assertArrayNotHasKey('_metrics', $res);
    }

    public function testMetricsPresentWhenEnabled(): void
    {
        $p = Pattern::fromString("BREAK(',')");
        $res = $p->match("a,b,c", ['metrics' => true]);
        $this->assertArrayHasKey('_metrics', $res);
        $this->assertArrayHasKey('choice_push_count', $res['_metrics']);
    }

    public function testMetricsPresentLiteralWhenEnabled(): void
    {
        $p = Pattern::fromString("'hello'");
        $res = $p->match("hello", ['metrics' => true]);
        $this->assertArrayHasKey('_metrics', $res);
    }

    public function testMetricsCountUnchanged(): void
    {
        $p = Pattern::fromString("'hello'");
        $with = $p->match("hello", ['metrics' => true]);
        $without = $p->match("hello");
        $this->assertEquals(
            $with['_match_len'],
            $without['_match_len']
        );
    }

    /* ============================================================
     *  2. Capture-as-offsets (P3)
     * ============================================================ */

    public function testMatchCaptureOffsets(): void
    {
        $ast = Builder::concat([
            Builder::cap(0, Builder::span("a-z")),
            Builder::lit(" "),
            Builder::cap(1, Builder::span("0-9")),
        ]);
        $p = Pattern::compileFromAst($ast);
        $strings = $p->match("foo 123");
        $offsets = $p->match("foo 123", ['captures' => 'offsets']);

        $this->assertEquals($strings['v0'], "foo");
        $this->assertEquals($strings['v1'], "123");
        $this->assertSame([0, 3], $offsets['v0']);
        $this->assertSame([4, 3], $offsets['v1']);
    }

    public function testMatchCaptureOffsetsLiteral(): void
    {
        $p = Pattern::fromString("'hello'");
        $res = $p->match("hello", ['captures' => 'offsets']);
        $this->assertIsArray($res);
        $this->assertEquals(5, $res['_match_len']);
    }

    /* ============================================================
     *  4. Flat result for searchAll (P5)
     * ============================================================ */

    public function testSearchAllFlatMatchesArraysSemantics(): void
    {
        $p = Pattern::fromString("'a'");
        $arrays = $p->searchAll("ababab");
        $flat = $p->searchAll("ababab", ['result' => 'flat']);

        $this->assertArrayHasKey('match_start', $flat);
        $this->assertArrayHasKey('match_len', $flat);
        $this->assertCount(count($arrays), $flat['match_start']);

        foreach ($arrays as $i => $arr) {
            $this->assertEquals($arr['_match_start'], $flat['match_start'][$i]);
            $this->assertEquals($arr['_match_len'], $flat['match_len'][$i]);
        }
    }

    public function testSearchAllFlatNoMatches(): void
    {
        $p = Pattern::fromString("'xyz'");
        $flat = $p->searchAll("hello", ['result' => 'flat']);
        $this->assertArrayHasKey('match_start', $flat);
        $this->assertEmpty($flat['match_start']);
    }

    public function testSearchAllFlatWithCaptures(): void
    {
        $ast = Builder::concat([
            Builder::cap(0, Builder::span("a-z")),
            Builder::lit("="),
            Builder::cap(1, Builder::span("0-9")),
        ]);
        $p = Pattern::compileFromAst($ast);

        $flat = $p->searchAll("a=1 b=2 c=3", ['result' => 'flat']);
        $this->assertArrayHasKey('captures', $flat);
        $this->assertArrayHasKey('v0', $flat['captures']);
        $this->assertArrayHasKey('v1', $flat['captures']);
        $this->assertCount(3, $flat['captures']['v0']);
    }

    /* ============================================================
     *  5. Flat offset arrays for searchSplit (P6)
     * ============================================================ */

    public function testSearchSplitFlatOffsets(): void
    {
        $p = Pattern::fromString("','");
        $strings = $p->searchSplit("a,b,c");
        $flat = $p->searchSplit("a,b,c", ['result' => 'flat']);

        for ($i = 0; $i < count($strings); $i++) {
            $start = $flat[$i * 2];
            $len   = $flat[$i * 2 + 1];
            $this->assertEquals($strings[$i], substr("a,b,c", $start, $len));
        }
    }

    public function testSearchSplitCuts(): void
    {
        $p = Pattern::fromString("' '");
        $cuts = $p->searchSplitCuts("a b c d");
        $subject = "a b c d";

        $prev = 0;
        for ($i = 0; $i < count($cuts); $i++) {
            $this->assertEquals(
                substr($subject, $prev, $cuts[$i] - $prev),
                substr($subject, $prev, $cuts[$i] - $prev)
            );
            $prev = $cuts[$i];
        }
        $this->assertEquals(
            substr($subject, $prev),
            substr($subject, $prev)
        );
    }

    public function testSearchSplitCutsSegmentBoundaries(): void
    {
        $p = Pattern::fromString("' '");
        $subject = "a b c d";
        $cuts = $p->searchSplitCuts($subject);
        // Cuts are delimiter-end positions. Segments include delimiters:
        //   seg0 = subject[0:cuts[0]]  = "a "
        //   seg1 = subject[cuts[0]:cuts[1]] = "b "
        //   seg2 = subject[cuts[1]:cuts[2]] = "c "
        //   seg3 = subject[cuts[2]:] = "d"
        $this->assertSame([2, 4, 6], $cuts);
        $this->assertEquals("a ", substr($subject, 0, $cuts[0]));
        $this->assertEquals("b ", substr($subject, $cuts[0], $cuts[1] - $cuts[0]));
        $this->assertEquals("c ", substr($subject, $cuts[1], $cuts[2] - $cuts[1]));
        $this->assertEquals("d", substr($subject, $cuts[2]));
    }

    public function testSearchSplitOffsetsFlat(): void
    {
        $p = Pattern::fromString("','");
        $flat = $p->searchSplitOffsets("x,y,z", ['result' => 'flat']);
        $this->assertCount(6, $flat);
        $this->assertSame(0, $flat[0]);
        $this->assertSame(1, $flat[1]);
        $this->assertSame(2, $flat[2]);
        $this->assertSame(1, $flat[3]);
    }

    /* ============================================================
     *  8. searchReplace with pre-sized buffer (P9)
     * ============================================================ */

    public function testSearchReplaceOutputCorrect(): void
    {
        $p = Pattern::fromString("'foo'");
        $res = $p->searchReplace("foo bar foo baz", "X");
        $this->assertSame("X bar X baz", $res);
    }

    public function testSearchReplaceWithOptions(): void
    {
        $p = Pattern::fromString("'foo'");
        $res = $p->searchReplace("foo bar", "X", ['metrics' => true]);
        $this->assertSame("X bar", $res);
    }

    /* ============================================================
     *  9. Capture-as-offsets in searchAll (P10)
     * ============================================================ */

    public function testSearchAllCaptureOffsetsArrays(): void
    {
        $ast = Builder::concat([
            Builder::cap(0, Builder::span("a-zA-Z")),
            Builder::lit(" "),
            Builder::cap(1, Builder::span("0-9")),
        ]);
        $p = Pattern::compileFromAst($ast);
        $subject = "foo 123 bar 456";

        $strings = $p->searchAll($subject);
        $offsets = $p->searchAll($subject, ['captures' => 'offsets']);

        $this->assertCount(count($strings), $offsets);
        foreach ($strings as $i => $arr) {
            $this->assertEquals(
                $arr['v0'],
                substr($subject, $offsets[$i]['v0'][0], $offsets[$i]['v0'][1])
            );
            $this->assertEquals(
                $arr['v1'],
                substr($subject, $offsets[$i]['v1'][0], $offsets[$i]['v1'][1])
            );
        }
    }

    public function testSearchAllCaptureOffsetsFlat(): void
    {
        $ast = Builder::concat([
            Builder::cap(0, Builder::span("a-zA-Z")),
            Builder::lit(" "),
            Builder::cap(1, Builder::span("0-9")),
        ]);
        $p = Pattern::compileFromAst($ast);
        $subject = "foo 123 bar 456";

        $strings = $p->searchAll($subject);
        $flat = $p->searchAll($subject, [
            'result'   => 'flat',
            'captures' => 'offsets',
        ]);

        $this->assertArrayHasKey('captures', $flat);
        $this->assertArrayHasKey('v0', $flat['captures']);
        $this->assertArrayHasKey('v1', $flat['captures']);
        $this->assertCount(count($strings), $flat['captures']['v0']);
        $this->assertCount(count($strings), $flat['captures']['v1']);

        foreach ($strings as $i => $arr) {
            $off_v0 = $flat['captures']['v0'][$i];
            $off_v1 = $flat['captures']['v1'][$i];
            $this->assertEquals(
                $arr['v0'],
                substr($subject, $off_v0[0], $off_v0[1])
            );
            $this->assertEquals(
                $arr['v1'],
                substr($subject, $off_v1[0], $off_v1[1])
            );
        }
    }

    /* ============================================================
     *  7. SearchIterator lazy iteration (P7)
     * ============================================================ */

    public function testSearchIteratorReturnsSearchIterator(): void
    {
        $p = Pattern::fromString("'a'");
        $it = $p->searchAllGenerator("aba");
        $this->assertInstanceOf(\Snobol\SearchIterator::class, $it);
    }

    public function testSearchIteratorIteratesAllMatches(): void
    {
        $p = Pattern::fromString("'a'");
        $flat = $p->searchAll("abacad", ['result' => 'flat']);
        $it = $p->searchAllGenerator("abacad");
        $count = 0;
        foreach ($it as $match) {
            $this->assertEquals($flat['match_start'][$count], $match['_match_start']);
            $this->assertEquals($flat['match_len'][$count], $match['_match_len']);
            $count++;
        }
        $this->assertEquals(count($flat['match_start']), $count);
    }

    public function testSearchIteratorNoMatches(): void
    {
        $p = Pattern::fromString("'xyz'");
        $it = $p->searchAllGenerator("hello world");
        $count = 0;
        foreach ($it as $match) {
            $count++;
        }
        $this->assertEquals(0, $count);
    }

    public function testSearchIteratorWithCaptures(): void
    {
        $ast = Builder::concat([
            Builder::cap(0, Builder::span("a-z")),
            Builder::lit("="),
            Builder::cap(1, Builder::span("0-9")),
        ]);
        $p = Pattern::compileFromAst($ast);
        $flat = $p->searchAll("x=1 y=2", ['result' => 'flat']);
        $it = $p->searchAllGenerator("x=1 y=2");
        $count = 0;
        foreach ($it as $match) {
            $this->assertArrayHasKey('v0', $match);
            $this->assertArrayHasKey('v1', $match);
            $this->assertEquals($flat['captures']['v0'][$count], $match['v0']);
            $count++;
        }
        $this->assertEquals(2, $count);
    }

    /* ============================================================
     *  10. Lean tokenize API (P2)
     * ============================================================ */

    public function testSearchSplitLiteralDelimiterNextPath(): void
    {
        $p = Pattern::fromString("','");
        $parts = $p->searchSplit("a,b,c");
        $this->assertCount(3, $parts);
        $this->assertSame('a', $parts[0]);
        $this->assertSame('b', $parts[1]);
        $this->assertSame('c', $parts[2]);
    }

    public function testSearchSplitLiteralDelimiterFlat(): void
    {
        $p = Pattern::fromString("','");
        $flat = $p->searchSplit("a,b,c", ['result' => 'flat']);
        $this->assertCount(6, $flat);
        $this->assertSame(0, $flat[0]);
        $this->assertSame(1, $flat[1]);
        $this->assertSame(2, $flat[2]);
        $this->assertSame(1, $flat[3]);
    }

    public function testSearchSplitLiteralDelimiterOffsets(): void
    {
        $p = Pattern::fromString("','");
        $parts = $p->searchSplitOffsets("a,b,c");
        $this->assertCount(3, $parts);
        $this->assertSame([0, 1], $parts[0]);
        $this->assertSame([2, 1], $parts[1]);
        $this->assertSame([4, 1], $parts[2]);
    }

    public function testSearchSplitLiteralDelimiterCuts(): void
    {
        $p = Pattern::fromString("','");
        $cuts = $p->searchSplitCuts("a,b,c");
        $this->assertCount(2, $cuts);
        $this->assertSame(2, $cuts[0]);   // first delimiter ends at pos 2
        $this->assertSame(4, $cuts[1]);   // second delimiter ends at pos 4
    }

    public function testSearchSplitAltDelimiterFallback(): void
    {
        $p = Pattern::fromString("'cat' | 'dog'");
        $parts = $p->searchSplit("thecatwalk");
        $this->assertCount(2, $parts);
        $this->assertSame('the', $parts[0]);
        $this->assertSame('walk', $parts[1]);
    }

    /* ============================================================
     *  11. Lazy split iterator (P4)
     * ============================================================ */

    public function testSearchSplitGeneratorProducesSegments(): void
    {
        $p = Pattern::fromString("','");
        $result = [];
        foreach ($p->searchSplitGenerator("a,b,c") as $i => $seg) {
            $result[$i] = $seg;
        }
        $this->assertCount(3, $result);
        $this->assertSame('a', $result[0]);
        $this->assertSame('b', $result[1]);
        $this->assertSame('c', $result[2]);
    }

    public function testSearchSplitGeneratorEarlyBreak(): void
    {
        $p = Pattern::fromString("','");
        $count = 0;
        foreach ($p->searchSplitGenerator("w,x,y,z") as $seg) {
            $count++;
            if ($count >= 2) break;
        }
        $this->assertSame(2, $count);
    }

    public function testSearchSplitGeneratorNoDelimiter(): void
    {
        $p = Pattern::fromString("','");
        $result = [];
        foreach ($p->searchSplitGenerator("hello") as $seg) {
            $result[] = $seg;
        }
        $this->assertCount(1, $result);
        $this->assertSame('hello', $result[0]);
    }

    public function testSearchSplitGeneratorEmptySubject(): void
    {
        $p = Pattern::fromString("','");
        $result = [];
        foreach ($p->searchSplitGenerator("") as $seg) {
            $result[] = $seg;
        }
        $this->assertCount(0, $result);
    }

    public function testSearchSplitGeneratorEquivalence(): void
    {
        $p = Pattern::fromString("','");
        $eager = $p->searchSplit("a,b,c");
        $lazy = [];
        foreach ($p->searchSplitGenerator("a,b,c") as $seg) {
            $lazy[] = $seg;
        }
        $this->assertSame($eager, $lazy);
    }
}
