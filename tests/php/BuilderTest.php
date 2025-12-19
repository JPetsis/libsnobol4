<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Builder;
use Snobol\Pattern;

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
}

