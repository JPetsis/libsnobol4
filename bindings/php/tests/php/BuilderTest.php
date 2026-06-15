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
}

