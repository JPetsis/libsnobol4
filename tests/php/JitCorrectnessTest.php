<?php

use PHPUnit\Framework\TestCase;
use Snobol\Builder;
use Snobol\Pattern;

class JitCorrectnessTest extends TestCase
{
    public function testLiterals()
    {
        $ast = Builder::concat([
            Builder::lit("abc"),
            Builder::lit("def")
        ]);
        $this->assertJitMatch($ast, "abcdefgh", 6);
        $this->assertJitMatch($ast, "abcdeX", false);
    }

    private function assertJitMatch($ast, $input, $expectedMatchLen, $expectedVars = [])
    {
        $p = Pattern::compileFromAst($ast);

        // Test with JIT OFF
        $p->setJit(false);
        $resOff = $p->match($input);

        // Test with JIT ON (trigger compilation first)
        $p->setJit(true);
        for ($i = 0; $i < 100; $i++) {
            $resOn = $p->match($input);
        }

        if ($expectedMatchLen === false) {
            $this->assertFalse($resOff, "JIT OFF should fail for input '$input'");
            $this->assertFalse($resOn, "JIT ON should fail for input '$input'");
        } else {
            $this->assertNotFalse($resOff, "JIT OFF should match for input '$input'");
            $this->assertNotFalse($resOn, "JIT ON should match for input '$input'");
            $this->assertEquals($expectedMatchLen, $resOff['_match_len'],
                "JIT OFF match len mismatch for input '$input'");
            $this->assertEquals($expectedMatchLen, $resOn['_match_len'],
                "JIT ON match len mismatch for input '$input'");

            foreach ($expectedVars as $var => $val) {
                $this->assertEquals($val, $resOff[$var], "JIT OFF var $var mismatch for input '$input'");
                $this->assertEquals($val, $resOn[$var], "JIT ON var $var mismatch for input '$input'");
            }
        }
    }

    public function testSpan()
    {
        $ast = Builder::span("0123456789");
        $this->assertJitMatch($ast, "12345abc", 5);
        $this->assertJitMatch($ast, "abc12345", false);
    }

    public function testAny()
    {
        $ast = Builder::any("xyz");
        $this->assertJitMatch($ast, "x...", 1);
        $this->assertJitMatch($ast, "y...", 1);
        $this->assertJitMatch($ast, "z...", 1);
        $this->assertJitMatch($ast, "a...", false);
    }

    public function testNotAny()
    {
        $ast = Builder::notany("abc");
        $this->assertJitMatch($ast, "x...", 1);
        $this->assertJitMatch($ast, "a...", false);

        $ast2 = Builder::notany("123");
        $this->assertJitMatch($ast2, "9...", 1);
        $this->assertJitMatch($ast2, "1...", false);
    }

    public function testLen()
    {
        $ast = Builder::len(3);
        $this->assertJitMatch($ast, "abcde", 3);
        $this->assertJitMatch($ast, "ab", false);
    }

    public function testAnchors()
    {
        $ast = Builder::concat([
            Builder::anchor("start"),
            Builder::lit("abc")
        ]);
        $this->assertJitMatch($ast, "abc", 3);
    }

    public function testMixedTrace()
    {
        // abc + SPAN(digits) + xyz
        $ast = Builder::concat([
            Builder::lit("abc"),
            Builder::span("0123456789"),
            Builder::lit("xyz")
        ]);
        $this->assertJitMatch($ast, "abc123xyz...", 9);
        $this->assertJitMatch($ast, "abc123Xyz...", false);
    }

    public function testCaptures()
    {
        $ast = Builder::concat([
            Builder::cap(1, Builder::concat([
                Builder::lit("abc"),
                Builder::span("def")
            ])),
            Builder::assign(1, 1)
        ]);
        $this->assertJitMatch($ast, "abcddeffg", 8, ['v1' => 'abcddeff']);
    }
}