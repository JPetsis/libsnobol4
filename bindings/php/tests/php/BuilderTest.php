<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Builder;

class BuilderTest extends TestCase
{
    public function testLiteralPattern(): void
    {
        $ast = Builder::lit("hello");
        $this->assertIsArray($ast);
        $this->assertEquals('lit', $ast['type']);
        $this->assertEquals('hello', $ast['text']);
    }

    public function testConcatPattern(): void
    {
        $ast = Builder::concat([
            Builder::lit("hello"),
            Builder::lit("world")
        ]);
        $this->assertIsArray($ast);
        $this->assertEquals('concat', $ast['type']);
        $this->assertCount(2, $ast['parts']);
    }

    public function testAlternationPattern(): void
    {
        $ast = Builder::alt(
            Builder::lit("foo"),
            Builder::lit("bar")
        );
        $this->assertIsArray($ast);
        $this->assertEquals('alt', $ast['type']);
    }

    public function testSpanPattern(): void
    {
        $ast = Builder::span("0123456789");
        $this->assertIsArray($ast);
        $this->assertEquals('span', $ast['type']);
        $this->assertEquals("0123456789", $ast['set']);
    }

    public function testLabelNode(): void
    {
        $ast = Builder::label('start', Builder::span('A-Za-z'));
        $this->assertIsArray($ast);
        $this->assertEquals('label', $ast['type']);
        $this->assertEquals('start', $ast['name']);
        $this->assertIsArray($ast['target']);
        $this->assertEquals('span', $ast['target']['type']);
    }

    public function testGotoNode(): void
    {
        $ast = Builder::goto('start');
        $this->assertIsArray($ast);
        $this->assertEquals('goto', $ast['type']);
        $this->assertEquals('start', $ast['label']);
    }

    /* -------- POS / TAB -------- */

    public function testPosNode(): void
    {
        $ast = Builder::pos(3);
        $this->assertIsArray($ast);
        $this->assertEquals('pos', $ast['type']);
        $this->assertEquals(3, $ast['n']);
    }

    public function testPosZero(): void
    {
        $ast = Builder::pos(0);
        $this->assertEquals(0, $ast['n']);
    }

    public function testTabNode(): void
    {
        $ast = Builder::tab(5);
        $this->assertIsArray($ast);
        $this->assertEquals('tab', $ast['type']);
        $this->assertEquals(5, $ast['n']);
    }

    public function testTabZero(): void
    {
        $ast = Builder::tab(0);
        $this->assertEquals(0, $ast['n']);
    }

    /* -------- ABORT / FAIL / SUCCEED -------- */

    public function testAbortNode(): void
    {
        $ast = Builder::abort();
        $this->assertIsArray($ast);
        $this->assertEquals('abort', $ast['type']);
    }

    public function testFailNode(): void
    {
        $ast = Builder::fail();
        $this->assertIsArray($ast);
        $this->assertEquals('fail', $ast['type']);
    }

    public function testSucceedNode(): void
    {
        $ast = Builder::succeed();
        $this->assertIsArray($ast);
        $this->assertEquals('succeed', $ast['type']);
    }

    /* ============================================================
     *  Range syntax — C compiler expands X-Y notation
     * ============================================================ */

    public function testSpanSingleRange(): void
    {
        $p = \Snobol\Pattern::compileFromAst(Builder::span("a-z"));
        $r = $p->match("hello");
        $this->assertIsArray($r);
        $this->assertEquals(5, $r['_match_len']);
        $this->assertFalse($p->match("HELLO"));
    }

    public function testSpanMultipleRanges(): void
    {
        $p = \Snobol\Pattern::compileFromAst(Builder::span("a-z0-9"));
        $r1 = $p->match("abc123");
        $this->assertIsArray($r1);
        $this->assertEquals(6, $r1['_match_len']);
        $r2 = $p->match("___");
        $this->assertFalse($r2);
    }

    public function testBrkSingleRange(): void
    {
        $p = \Snobol\Pattern::compileFromAst(Builder::brk("0-9"));
        $r = $p->match("hello123");
        $this->assertIsArray($r);
        $this->assertEquals(5, $r['_match_len']);
    }

    public function testAnySingleRange(): void
    {
        $p = \Snobol\Pattern::compileFromAst(Builder::any("a-m"));
        $this->assertIsArray($p->match("hello"));
        $this->assertFalse($p->match("zoo"));
    }

    public function testNotanySingleRange(): void
    {
        $p = \Snobol\Pattern::compileFromAst(Builder::notany("a-z"));
        $this->assertIsArray($p->match("123"));
        $this->assertFalse($p->match("abc"));
    }

    public function testHyphenAtStartIsLiteral(): void
    {
        $p = \Snobol\Pattern::compileFromAst(Builder::span("-a"));
        $r = $p->match("-aaa");
        $this->assertIsArray($r);
        $this->assertEquals(4, $r['_match_len']);
    }

    public function testHyphenAtEndIsLiteral(): void
    {
        $p = \Snobol\Pattern::compileFromAst(Builder::span("a-"));
        $r = $p->match("aaa-");
        $this->assertIsArray($r);
        $this->assertEquals(4, $r['_match_len']);
    }

    public function testRangeWithUpperCase(): void
    {
        $p = \Snobol\Pattern::compileFromAst(Builder::span("A-Z"));
        $this->assertIsArray($p->match("HELLO"));
        $this->assertFalse($p->match("hello"));
    }
}

