<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Parser;

class ParserTest extends TestCase
{
    public function testLiterals(): void
    {
        $p = new Parser("'hello'");
        $ast = $p->parse();
        $this->assertEquals(['type' => 'lit', 'text' => 'hello'], $ast);

        $p = new Parser('"hello world"');
        $ast = $p->parse();
        $this->assertEquals(['type' => 'lit', 'text' => 'hello world'], $ast);
    }

    public function testConcatenation(): void
    {
        $p = new Parser("'A' 'B'");
        $ast = $p->parse();
        $this->assertEquals([
            'type' => 'concat',
            'parts' => [
                ['type' => 'lit', 'text' => 'A'],
                ['type' => 'lit', 'text' => 'B']
            ]
        ], $ast);
    }

    public function testAlternation(): void
    {
        $p = new Parser("'A' | 'B'");
        $ast = $p->parse();
        $this->assertEquals([
            'type' => 'alt',
            'left' => ['type' => 'lit', 'text' => 'A'],
            'right' => ['type' => 'lit', 'text' => 'B']
        ], $ast);
    }

    public function testPrecedence(): void
    {
        // 'A' 'B' | 'C' -> (AB) | C
        $p = new Parser("'A' 'B' | 'C'");
        $ast = $p->parse();
        $this->assertEquals('alt', $ast['type']);
        $this->assertEquals('concat', $ast['left']['type']);
        $this->assertEquals('lit', $ast['right']['type']);

        // 'A' ('B' | 'C') -> A (B|C)
        $p = new Parser("'A' ('B' | 'C')");
        $ast = $p->parse();
        $this->assertEquals('concat', $ast['type']);
        $this->assertEquals('lit', $ast['parts'][0]['type']);
        $this->assertEquals('alt', $ast['parts'][1]['type']);
    }

    public function testRepetition(): void
    {
        $p = new Parser("'A'*");
        $ast = $p->parse();
        $this->assertEquals('arbno', $ast['type']);
        $this->assertEquals('lit', $ast['sub']['type']);

        $p = new Parser("'A'+");
        $ast = $p->parse();
        // A arbno(A)
        $this->assertEquals('concat', $ast['type']);
        $this->assertEquals('lit', $ast['parts'][0]['type']);
        $this->assertEquals('arbno', $ast['parts'][1]['type']);

        $p = new Parser("'A'?");
        $ast = $p->parse();
        // A | ""
        $this->assertEquals('alt', $ast['type']);
        $this->assertEquals('lit', $ast['left']['type']);
        $this->assertEquals('lit', $ast['right']['type']);
        $this->assertEquals('', $ast['right']['text']);
    }

    public function testCharClass(): void
    {
        $p = new Parser("[abc]");
        $ast = $p->parse();
        $this->assertEquals('any', $ast['type']);
        $this->assertEquals('abc', $ast['set']);

        // Range
        $p = new Parser("[a-c]");
        $ast = $p->parse();
        $this->assertEquals('any', $ast['type']);
        $this->assertEquals('abc', $ast['set']);

        // Escaped range dash
        $p = new Parser("[a\\-c]");
        $ast = $p->parse();
        $this->assertEquals('any', $ast['type']);
        // a, -, c
        $this->assertTrue(str_contains($ast['set'], 'a'));
        $this->assertTrue(str_contains($ast['set'], '-'));
        $this->assertTrue(str_contains($ast['set'], 'c'));
        $this->assertEquals(3, strlen($ast['set']));

        // Negation
        $p = new Parser("[^abc]");
        $ast = $p->parse();
        $this->assertEquals('notany', $ast['type']);
        $this->assertEquals('abc', $ast['set']);
    }

    public function testBuiltins(): void
    {
        $p = new Parser("SPAN('012')");
        $ast = $p->parse();
        $this->assertEquals('span', $ast['type']);
        $this->assertEquals('012', $ast['set']);

        $p = new Parser("LEN(5)");
        $ast = $p->parse();
        $this->assertEquals('len', $ast['type']);
        $this->assertEquals(5, $ast['n']);
    }

    public function testCapture(): void
    {
        $p = new Parser("@r1('A')");
        $ast = $p->parse();
        $this->assertEquals('cap', $ast['type']);
        $this->assertEquals(1, $ast['reg']);
        $this->assertEquals('lit', $ast['sub']['type']);
    }

    public function testAnchors(): void
    {
        // ^ matches nothing (empty lit)
        $p = new Parser("^ 'A'");
        $ast = $p->parse();
        $this->assertEquals('concat', $ast['type']);
        $this->assertEquals('', $ast['parts'][0]['text']);
    }
}
