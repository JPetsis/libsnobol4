<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\PatternHelper as PH;

/**
 * PHP-side coverage for BREAK / BREAKX.
 *
 * The C grammar test (test_break_grammar.c) verifies routing to TIER_BREAK_SCAN
 * and correct delimited-field matching. This test exercises the same patterns
 * through the PHP binding's string-compile path (PatternHelper::fromString),
 * which feeds the same C parser, and asserts match/search behaviour.
 */
class BreakTest extends TestCase
{
    public function testBreakCompilesAndMatchesField(): void
    {
        $pattern = PH::fromString("BREAK(',')");
        $result = PH::matchOnce($pattern, 'field1,field2');

        $this->assertIsArray($result);
        $this->assertArrayHasKey('_match_len', $result);
        // BREAK(',') on 'field1,field2' matches 'field1' (6 chars, pos 0).
        $this->assertSame(6, $result['_match_len']);
    }

    public function testBreakMatchesToEndWhenNoDelimiter(): void
    {
        $pattern = PH::fromString("BREAK(',')");
        $result = PH::matchOnce($pattern, 'nofield');

        $this->assertIsArray($result);
        $this->assertArrayHasKey('_match_len', $result);
        // No delimiter present: BREAK consumes the whole subject.
        $this->assertSame(7, $result['_match_len']);
    }

    public function testBreakxCompilesAndMatchesField(): void
    {
        $pattern = PH::fromString("BREAKX(',')");
        $result = PH::matchOnce($pattern, 'field1,field2');

        $this->assertIsArray($result);
        $this->assertArrayHasKey('_match_len', $result);
        $this->assertSame(6, $result['_match_len']);
    }

    public function testBreakNoArgumentIsRejected(): void
    {
        $this->expectException(\Throwable::class);
        PH::fromString("BREAK()");
    }
}
