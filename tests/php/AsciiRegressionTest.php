<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Builder;
use Snobol\Pattern;

class AsciiRegressionTest extends TestCase
{
    /**
     * Test 6.5: Regression tests for ASCII compatibility.
     * Ensure the new range-based implementation behaves exactly like the old bitmaps
     * for standard ASCII operations.
     */

    public function testStandardAsciiClasses(): void
    {
        // SPAN matches longest run
        $p = Pattern::compileFromAst(Builder::span("0123456789"));
        $res = $p->match("12345abc");
        $this->assertIsArray($res);
        $this->assertEquals("12345", substr("12345abc", 0, $res['_match_len']));

        // BREAK scans until char
        $p = Pattern::compileFromAst(Builder::brk(" "));
        $res = $p->match("hello world");
        $this->assertIsArray($res);
        $this->assertEquals("hello", substr("hello world", 0, $res['_match_len']));
    }

    public function testSparseAscii(): void
    {
        // "ace" - previously a bitmap, now 3 separate ranges [97,97], [99,99], [101,101]
        $p = Pattern::compileFromAst(Builder::span("ace"));
        $res = $p->match("aceace_b");
        $this->assertEquals(6, $res['_match_len']);
    }

    public function testControlCharacters(): void
    {
        // Test that low ASCII chars work in ranges
        $p = Pattern::compileFromAst(Builder::any("\t\n\r"));
        $res = $p->match("\t");
        $this->assertIsArray($res);
    }

    public function testHighAsciiBoundary(): void
    {
        // DEL (127)
        $p = Pattern::compileFromAst(Builder::any(chr(127)));
        $res = $p->match(chr(127));
        $this->assertIsArray($res);
    }
}
