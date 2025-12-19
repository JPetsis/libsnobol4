<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Builder;
use Snobol\Pattern;

class PatternTest extends TestCase
{
    public function testPatternCompileFromAst(): void
    {
        $ast = Builder::lit("test");
        $pattern = Pattern::compileFromAst($ast);
        $this->assertInstanceOf(Pattern::class, $pattern);
    }

    public function testSimpleLiteralMatch(): void
    {
        $ast = Builder::lit("hello");
        $pattern = Pattern::compileFromAst($ast);
        $result = $pattern->match("hello world");
        $this->assertNotFalse($result);
    }

    public function testLiteralNoMatch(): void
    {
        $ast = Builder::lit("goodbye");
        $pattern = Pattern::compileFromAst($ast);
        $result = $pattern->match("hello world");
        $this->assertFalse($result);
    }

    public function testCaptureAndAssign(): void
    {
        $ast = Builder::concat([
            Builder::lit("id:"),
            Builder::cap(0, Builder::span("0123456789")),
            Builder::assign(0, 0)
        ]);
        $pattern = Pattern::compileFromAst($ast);
        $result = $pattern->match("id:12345 more text");

        $this->assertIsArray($result);
        $this->assertArrayHasKey('v0', $result);
        $this->assertEquals('12345', $result['v0']);
    }
}

