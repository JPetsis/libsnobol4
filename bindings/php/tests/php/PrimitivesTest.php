<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Builder as B;
use Snobol\PatternHelper as PH;

/**
 * Tests for Builder primitive wrappers: BREAKX, BAL, FENCE, REM, RPOS, RTAB, ARB.
 */
class PrimitivesTest extends TestCase
{
    /* -------- FENCE -------- */

    public function testFenceAllowsForwardMatch(): void
    {
        // span("a") + FENCE + lit("b") should match "aaab"
        $ast = B::concat([B::span('a'), B::fence(), B::lit('b')]);
        $result = PH::matchOnce($ast, 'aaab');
        $this->assertNotFalse($result);
    }

    /* -------- REM -------- */

    public function testRemConsumesRemainder(): void
    {
        $ast = B::concat([B::lit('hello'), B::rem()]);
        $result = PH::matchOnce($ast, 'hello world');
        $this->assertNotFalse($result);
    }

    /* -------- RPOS -------- */

    public function testRposZeroAtEnd(): void
    {
        // RPOS(0) should succeed when cursor is at the very end.
        // matchOnce is anchored at position 0, so use a subject that starts with 'world'.
        $ast = B::concat([B::lit('world'), B::rpos(0)]);
        $result = PH::matchOnce($ast, 'world');
        $this->assertNotFalse($result);
    }

    public function testRposNonZeroFails(): void
    {
        // RPOS(3) requires cursor to be 3 chars from end – won't match here
        $ast = B::concat([B::lit('hello'), B::rpos(3)]);
        $result = PH::matchOnce($ast, 'hello');
        $this->assertFalse($result);
    }

    /* -------- RTAB -------- */

    public function testRtabConsumesToEnd(): void
    {
        $ast = B::concat([B::lit('hello'), B::rtab(0)]);
        $result = PH::matchOnce($ast, 'hello world');
        $this->assertNotFalse($result);
    }

    public function testRtabConsumesPartial(): void
    {
        // RTAB(5) leaves 5 chars from end; after "he" we are at pos 2, len="hello"=5
        // "he" + RTAB(3) consumes to within 3 of end of "hello" → pos 2
        $ast = B::concat([B::lit('he'), B::rtab(3)]);
        $result = PH::matchOnce($ast, 'hello');
        $this->assertNotFalse($result);
    }

    /* -------- BREAKX -------- */

    public function testBreakxCaptures(): void
    {
        $ast = B::concat([B::cap(0, B::breakx(':')), B::assign(0, 0)]);
        $result = PH::matchOnce($ast, 'key:value');
        $this->assertNotFalse($result);
        $this->assertSame('key', $result['v0']);
    }

    /* -------- BAL -------- */

    public function testBalSimple(): void
    {
        $ast = B::concat([B::cap(0, B::bal('(', ')')), B::assign(0, 0)]);
        $result = PH::matchOnce($ast, '(hello)');
        $this->assertNotFalse($result);
        $this->assertSame('(hello)', $result['v0']);
    }

    public function testBalNested(): void
    {
        $ast = B::concat([B::cap(0, B::bal('(', ')')), B::assign(0, 0)]);
        $result = PH::matchOnce($ast, '(hi (there))');
        $this->assertNotFalse($result);
        $this->assertSame('(hi (there))', $result['v0']);
    }

    public function testBalNoMatch(): void
    {
        $ast = B::bal('(', ')');
        $result = PH::matchOnce($ast, 'no parens here');
        $this->assertFalse($result);
    }

    /* -------- ARB -------- */

    public function testArbMatchesZero(): void
    {
        $ast = B::concat([B::arb(), B::lit('hello')]);
        $result = PH::matchOnce($ast, 'hello');
        $this->assertNotFalse($result);
    }

    public function testArbMatchesMany(): void
    {
        $ast = B::concat([B::arb(), B::lit('world')]);
        $result = PH::matchOnce($ast, 'hello world');
        $this->assertNotFalse($result);
    }

    /* -------- POS -------- */

    public function testPosZeroAtStart(): void
    {
        // POS(0) succeeds when cursor is at position 0 (start)
        $ast = B::concat([B::pos(0), B::lit('hello')]);
        $result = PH::matchOnce($ast, 'hello');
        $this->assertNotFalse($result);
    }

    public function testPosNonZeroMatches(): void
    {
        // lit('he') advances to pos 2, then POS(2) should succeed
        $ast = B::concat([B::lit('he'), B::pos(2), B::lit('llo')]);
        $result = PH::matchOnce($ast, 'hello');
        $this->assertNotFalse($result);
    }

    public function testPosFailsOnPositionMismatch(): void
    {
        // After 'he' we're at pos 2, but POS(3) requires pos 3
        $ast = B::concat([B::lit('he'), B::pos(3)]);
        $result = PH::matchOnce($ast, 'hello');
        $this->assertFalse($result);
    }

    /* -------- TAB -------- */

    public function testTabAdvancesCursor(): void
    {
        // TAB(4) advances from pos 0 to pos 4, then lit('o') matches
        $ast = B::concat([B::tab(4), B::lit('o')]);
        $result = PH::matchOnce($ast, 'hello');
        $this->assertNotFalse($result);
    }

    public function testTabZeroNoop(): void
    {
        // TAB(0) is a no-op, then lit('hello') matches
        $ast = B::concat([B::tab(0), B::lit('hello')]);
        $result = PH::matchOnce($ast, 'hello');
        $this->assertNotFalse($result);
    }

    public function testTabFailsOutOfRange(): void
    {
        // TAB(99) beyond subject length should fail
        $ast = B::tab(99);
        $result = PH::matchOnce($ast, 'hello');
        $this->assertFalse($result);
    }

    /* -------- ABORT -------- */

    public function testAbortCausesFailure(): void
    {
        // Even though 'hello' would match, ABORT immediately fails the whole match
        $ast = B::concat([B::lit('hello'), B::abort()]);
        $result = PH::matchOnce($ast, 'hello');
        $this->assertFalse($result);
    }

    /* -------- FAIL -------- */

    public function testFailAloneAlwaysFails(): void
    {
        // FAIL alone has no alternatives to backtrack to, so the match fails
        $ast = B::fail();
        $result = PH::matchOnce($ast, 'anything');
        $this->assertFalse($result);
    }

    public function testFailCausesFailureEvenAfterMatch(): void
    {
        // Even after 'hello' matches, FAIL forces the match to fail
        $ast = B::concat([B::lit('hello'), B::fail()]);
        $result = PH::matchOnce($ast, 'hello');
        $this->assertFalse($result);
    }

    /* -------- SUCCEED -------- */

    public function testSucceedShortCircuits(): void
    {
        // SUCCEED forces immediate success
        $ast = B::succeed();
        $result = PH::matchOnce($ast, 'anything');
        $this->assertNotFalse($result);
    }
}






