<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Builder;
use Snobol\Pattern;
use Snobol\PatternHelper;

/**
 * Tests for capture-aware Tier-6 search-VM.
 *
 * Verifies that patterns with captures (CAP_START/CAP_END/ASSIGN/BREAKX)
 * produce identical results whether routed through the lightweight search-VM
 * (Tier 6) or the full VM (Tier 8).
 */
class SearchVmCaptureTest extends TestCase
{
    /**
     * Helper: build a pattern from AST, match once, return result.
     */
    private static function matchAst(array $ast, string $subject): ?array
    {
        $pattern = Pattern::compileFromAst(Builder::concat($ast));
        return PatternHelper::matchOnce($pattern, $subject);
    }

    // === Simple capture ===

    public function testSimpleCapture(): void
    {
        $result = self::matchAst(
            [Builder::cap(0, Builder::lit('hello'))],
            'hello'
        );
        $this->assertNotNull($result);
        $this->assertSame('hello', $result['v0']);
    }

    // === Capture with prefix ===

    public function testCaptureWithPrefix(): void
    {
        $result = self::matchAst(
            [
                Builder::lit('id:'),
                Builder::cap(0, Builder::span('0123456789')),
            ],
            'id:12345'
        );
        $this->assertNotNull($result);
        $this->assertSame('12345', $result['v0']);
    }

    // === Multiple captures ===

    public function testMultipleCaptures(): void
    {
        $result = self::matchAst(
            [
                Builder::cap(0, Builder::lit('ab')),
                Builder::cap(1, Builder::lit('cd')),
            ],
            'abcd'
        );
        $this->assertNotNull($result);
        $this->assertSame('ab', $result['v0']);
        $this->assertSame('cd', $result['v1']);
    }

    // === Capture in alternation ===

    public function testCaptureInAlternation(): void
    {
        $result = self::matchAst(
            [Builder::alt(
                Builder::concat([Builder::cap(0, Builder::lit('a'))]),
                Builder::concat([Builder::cap(0, Builder::lit('b'))])
            )],
            'b'
        );
        $this->assertNotNull($result);
        $this->assertSame('b', $result['v0']);
    }

    // === Capture after failed alternation (backtrack) ===

    public function testCaptureAfterFailedAlt(): void
    {
        $result = self::matchAst(
            [Builder::alt(
                Builder::concat([Builder::lit('aaa')]),
                Builder::concat([Builder::cap(0, Builder::lit('bbb'))])
            )],
            'bbb'
        );
        $this->assertNotNull($result);
        $this->assertSame('bbb', $result['v0']);
    }

    // === matchAll with captures ===

    public function testMatchAllCaptures(): void
    {
        $ast = Builder::concat([
            Builder::cap(0, Builder::span('0123456789')),
            Builder::lit('-'),
        ]);
        $pattern = Pattern::compileFromAst($ast);
        $results = PatternHelper::matchAll($pattern, '123-456-');
        $this->assertCount(2, $results, 'matchAll should find 2 captures');
        $this->assertSame('123', $results[0]['v0']);
        $this->assertSame('456', $results[1]['v0']);
    }
}
