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

    /* ============================================================
     *  Remaining node constructors (coverage)
     * ============================================================ */

    public function testLenNode(): void
    {
        $ast = Builder::len(3);
        $this->assertSame('len', $ast['type']);
        $this->assertSame(3, $ast['n']);
        $p = \Snobol\Pattern::compileFromAst($ast);
        $this->assertIsArray($p->match('abc'));
        $this->assertFalse($p->match('ab'));
    }

    public function testArbnoNode(): void
    {
        $ast = Builder::arbno(Builder::lit('ab'));
        $this->assertSame('arbno', $ast['type']);
        $this->assertSame('lit', $ast['sub']['type']);
        $p = \Snobol\Pattern::compileFromAst($ast);
        $this->assertIsArray($p->match('ababab'));
    }

    public function testAltWithStringOperands(): void
    {
        $ast = Builder::alt('foo', 'bar');
        $this->assertSame('alt', $ast['type']);
        $this->assertSame('lit', $ast['left']['type']);
        $this->assertSame('foo', $ast['left']['text']);
        $this->assertSame('bar', $ast['right']['text']);
    }

    public function testEvalNode(): void
    {
        $ast = Builder::eval(3, 1);
        $this->assertSame('eval', $ast['type']);
        $this->assertSame(3, $ast['fn']);
        $this->assertSame(1, $ast['reg']);
    }

    public function testDynamicEvalNode(): void
    {
        $ast = Builder::dynamicEval(Builder::lit('AB'));
        $this->assertSame('dynamic_eval', $ast['type']);
        $this->assertSame('lit', $ast['expr']['type']);
    }

    public function testTableAccessNode(): void
    {
        $ast = Builder::tableAccess('T', Builder::lit('k'));
        $this->assertSame('table_access', $ast['type']);
        $this->assertSame('T', $ast['table']);
        $this->assertSame('lit', $ast['key']['type']);
    }

    public function testTableUpdateNode(): void
    {
        $ast = Builder::tableUpdate('T', Builder::lit('k'), Builder::lit('v'));
        $this->assertSame('table_update', $ast['type']);
        $this->assertSame('T', $ast['table']);
        $this->assertSame('lit', $ast['value']['type']);
    }

    public function testArrayAccessNode(): void
    {
        $ast = Builder::arrayAccess('A', Builder::lit('i'));
        $this->assertSame('array_access', $ast['type']);
        $this->assertSame('A', $ast['array']);
        $this->assertSame('lit', $ast['index']['type']);
    }

    public function testArrayUpdateNode(): void
    {
        $ast = Builder::arrayUpdate('A', Builder::lit('i'), Builder::lit('v'));
        $this->assertSame('array_update', $ast['type']);
        $this->assertSame('A', $ast['array']);
        $this->assertSame('lit', $ast['index']['type']);
        $this->assertSame('lit', $ast['value']['type']);
    }

    public function testArrayCreateNode(): void
    {
        $ast = Builder::arrayCreate('A');
        $this->assertSame('array_create', $ast['type']);
        $this->assertSame('A', $ast['array']);

        $ast2 = Builder::arrayCreate('A', 16);
        $this->assertSame('A', $ast2['array']);
    }

    public function testAnchorNode(): void
    {
        $ast = Builder::anchor('start');
        $this->assertSame('anchor', $ast['type']);
        $this->assertSame('start', $ast['atype']);

        $ast2 = Builder::anchor('end');
        $this->assertSame('end', $ast2['atype']);
    }

    public function testRepeatNodeWithMax(): void
    {
        $ast = Builder::repeat(Builder::lit('a'), 1, 3);
        $this->assertSame('repeat', $ast['type']);
        $this->assertSame(1, $ast['min']);
        $this->assertSame(3, $ast['max']);
    }

    public function testEmitNode(): void
    {
        $ast = Builder::emit('out');
        $this->assertSame('emit', $ast['type']);
        $this->assertSame('out', $ast['text']);
    }

    public function testEmitRefNode(): void
    {
        $ast = Builder::emitRef(2);
        $this->assertSame('emit', $ast['type']);
        $this->assertSame(2, $ast['reg']);
    }

    public function testBreakxNode(): void
    {
        $ast = Builder::breakx('ab');
        $this->assertSame('breakx', $ast['type']);
        $this->assertSame('ab', $ast['set']);
    }

    public function testBalNode(): void
    {
        $ast = Builder::bal('(', ')');
        $this->assertSame('bal', $ast['type']);
        $this->assertSame('(', $ast['open']);
        $this->assertSame(')', $ast['close']);
    }

    public function testFenceNode(): void
    {
        $ast = Builder::fence();
        $this->assertSame('fence', $ast['type']);
    }

    public function testRemNode(): void
    {
        $ast = Builder::rem();
        $this->assertSame('rem', $ast['type']);
    }

    public function testRposNode(): void
    {
        $ast = Builder::rpos(2);
        $this->assertSame('rpos', $ast['type']);
        $this->assertSame(2, $ast['n']);
    }

    public function testRtabNode(): void
    {
        $ast = Builder::rtab(2);
        $this->assertSame('rtab', $ast['type']);
        $this->assertSame(2, $ast['n']);
    }

    public function testArbNode(): void
    {
        $ast = Builder::arb();
        $this->assertSame('arbno', $ast['type']);
    }

    public function testAssignNode(): void
    {
        $ast = Builder::assign(1, 2);
        $this->assertSame('assign', $ast['type']);
        $this->assertSame(1, $ast['var']);
        $this->assertSame(2, $ast['reg']);
    }

    public function testCapNode(): void
    {
        $ast = Builder::cap(0, Builder::lit('x'));
        $this->assertSame('cap', $ast['type']);
        $this->assertSame(0, $ast['reg']);
        $this->assertSame('lit', $ast['sub']['type']);
    }

    public function testAnyWithoutArgument(): void
    {
        $ast = Builder::any();
        $this->assertSame('any', $ast['type']);
        $this->assertArrayNotHasKey('set', $ast);
    }

    public function testAnyWithNullArgument(): void
    {
        $ast = Builder::any(null);
        $this->assertSame('any', $ast['type']);
        $this->assertArrayNotHasKey('set', $ast);
    }

    /* ============================================================
     *  Invalid-argument rejections
     * ============================================================ */

    public function testInvalidArgumentsThrowTypeError(): void
    {
        // Array/object arguments are never coercible to string/long,
        // so these always raise TypeError regardless of weak typing.
        $cases = [
            fn () => Builder::lit(['x']),
            fn () => Builder::span(['x']),
            fn () => Builder::brk(['x']),
            fn () => Builder::notany(['x']),
            fn () => Builder::len('x'),
            fn () => Builder::arbno('not-an-array'),
            fn () => Builder::concat('not-an-array'),
            fn () => Builder::eval('x', 1),
            fn () => Builder::anchor(['x']),
            fn () => Builder::emit(['x']),
            fn () => Builder::emitRef('x'),
            fn () => Builder::goto(['x']),
            fn () => Builder::dynamicEval('not-an-array'),
            fn () => Builder::tableAccess(['x'], Builder::lit('k')),
            fn () => Builder::tableUpdate('T', 'not-an-array', Builder::lit('v')),
            fn () => Builder::arrayAccess('A', 'not-an-array'),
            fn () => Builder::arrayCreate(['x']),
            fn () => Builder::breakx(['x']),
            fn () => Builder::pos('x'),
            fn () => Builder::tab('x'),
            fn () => Builder::rpos('x'),
            fn () => Builder::rtab('x'),
        ];
        foreach ($cases as $case) {
            try {
                $case();
                $this->fail('Expected TypeError');
            } catch (\TypeError $e) {
                $this->assertTrue(true);
            }
        }
    }

    public function testWrongArityThrowsArgumentCountError(): void
    {
        $cases = [
            fn () => Builder::lit(),
            fn () => Builder::span(),
            fn () => Builder::cap(1),
            fn () => Builder::assign(1),
            fn () => Builder::concat(),
            fn () => Builder::alt(Builder::lit('a')),
            fn () => Builder::eval(1),
            fn () => Builder::repeat(Builder::lit('a')),
        ];
        foreach ($cases as $case) {
            try {
                $case();
                $this->fail('Expected ArgumentCountError');
            } catch (\ArgumentCountError $e) {
                $this->assertTrue(true);
            }
        }
    }

    public function testFullPatternCompileAndMatch(): void
    {
        $ast = Builder::concat([
            Builder::cap(0, Builder::lit('hel')),
            Builder::lit('lo'),
        ]);
        $p = \Snobol\Pattern::compileFromAst($ast);
        $result = $p->match('hello world');
        $this->assertIsArray($result);
        $this->assertSame(5, $result['_match_len']);
        $this->assertSame('hel', $result['v0']);
    }
}

