<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Text;

/**
 * Tests for Snobol\Text built-in string and comparison functions.
 */
class TextTest extends TestCase
{
    /* -------- SIZE -------- */

    public function testSizeAscii(): void
    {
        $this->assertSame(5, Text::size('hello'));
    }

    public function testSizeUnicode(): void
    {
        // café = 4 codepoints, 5 bytes (é is 2 bytes)
        $this->assertSame(4, Text::size("caf\xC3\xA9"));
    }

    public function testSizeEmpty(): void
    {
        $this->assertSame(0, Text::size(''));
    }

    /* -------- TRIM / DUPL -------- */

    public function testTrimTrailingSpaces(): void
    {
        $this->assertSame('hello', Text::trim('hello  '));
    }

    public function testTrimNoOp(): void
    {
        $this->assertSame('hello', Text::trim('hello'));
    }

    public function testTrimAllWhitespace(): void
    {
        $this->assertSame('', Text::trim("   \t\n"));
    }

    public function testDuplBasic(): void
    {
        $this->assertSame('ababab', Text::dupl('ab', 3));
    }

    public function testDuplZero(): void
    {
        $this->assertSame('', Text::dupl('ab', 0));
    }

    public function testDuplUnicode(): void
    {
        $alpha_beta = "\xCE\xB1\xCE\xB2"; // αβ
        $this->assertSame($alpha_beta.$alpha_beta, Text::dupl($alpha_beta, 2));
    }

    /* -------- UPPER / LOWER -------- */

    public function testUpper(): void
    {
        $this->assertSame('HELLO', Text::upper('hello'));
    }

    public function testLower(): void
    {
        $this->assertSame('hello', Text::lower('HELLO'));
    }

    public function testUpperMixed(): void
    {
        $this->assertSame('HELLO WORLD!', Text::upper('Hello World!'));
    }

    public function testLowerMixed(): void
    {
        $this->assertSame('hello world!', Text::lower('Hello World!'));
    }

    /* -------- REPLACE / REPLACE_CHAR -------- */

    public function testReplace(): void
    {
        $this->assertSame('hexxo hexxo', Text::replace('hello hello', 'll', 'xx'));
    }

    public function testReplaceNoMatch(): void
    {
        $this->assertSame('hello', Text::replace('hello', 'xyz', 'abc'));
    }

    public function testReplaceCharBasic(): void
    {
        $this->assertSame('hxyyo', Text::replaceChar('hello', 'el', 'xy'));
    }

    public function testReplaceCharRot13(): void
    {
        $this->assertSame(
            'uryyb',
            Text::replaceChar(
                'hello',
                'abcdefghijklmnopqrstuvwxyz',
                'nopqrstuvwxyzabcdefghijklm'
            )
        );
    }

    /* -------- SUBSTR / REVERSE -------- */

    public function testSubstrBasic(): void
    {
        $this->assertSame('world', Text::substr('hello world', 7, 5));
    }

    public function testSubstrUnicode(): void
    {
        $greek = "\xCE\xB1\xCE\xB2\xCE\xB3\xCE\xB4\xCE\xB5"; // αβγδε
        $this->assertSame("\xCE\xB2\xCE\xB3\xCE\xB4", Text::substr($greek, 2, 3));
    }

    public function testSubstrInvalidPos(): void
    {
        $this->assertFalse(Text::substr('hello', 0, 3));
    }

    public function testReverseAscii(): void
    {
        $this->assertSame('olleh', Text::reverse('hello'));
    }

    public function testReverseUnicode(): void
    {
        $abc = "\xCE\xB1\xCE\xB2\xCE\xB3"; // αβγ
        $cba = "\xCE\xB3\xCE\xB2\xCE\xB1"; // γβα
        $this->assertSame($cba, Text::reverse($abc));
    }

    /* -------- CHAR / ORD -------- */

    public function testCharAscii(): void
    {
        $this->assertSame('A', Text::char(65));
    }

    public function testCharUnicode(): void
    {
        $this->assertSame("\xCE\xB1", Text::char(0x03B1)); // α
    }

    public function testCharInvalid(): void
    {
        $this->assertFalse(Text::char(0x110000));
    }

    public function testOrdAscii(): void
    {
        $this->assertSame(65, Text::ord('A'));
    }

    public function testOrdUnicode(): void
    {
        $this->assertSame(0x03B1, Text::ord("\xCE\xB1")); // α
    }

    public function testOrdEmpty(): void
    {
        $this->assertFalse(Text::ord(''));
    }

    /* -------- IDENT / DIFFER -------- */

    public function testIdentTrue(): void
    {
        $this->assertTrue(Text::ident('hello', 'hello'));
    }

    public function testIdentFalse(): void
    {
        $this->assertFalse(Text::ident('hello', 'world'));
    }

    public function testDifferTrue(): void
    {
        $this->assertTrue(Text::differ('hello', 'world'));
    }

    public function testDifferFalse(): void
    {
        $this->assertFalse(Text::differ('hello', 'hello'));
    }

    /* -------- LEXLT / LEXGT -------- */

    public function testLexlt(): void
    {
        $this->assertTrue(Text::lexlt('apple', 'banana'));
        $this->assertFalse(Text::lexlt('banana', 'apple'));
    }

    public function testLexgt(): void
    {
        $this->assertTrue(Text::lexgt('banana', 'apple'));
        $this->assertFalse(Text::lexgt('apple', 'banana'));
    }

    public function testLexeq(): void
    {
        $this->assertTrue(Text::lexeq('hello', 'hello'));
        $this->assertFalse(Text::lexeq('hello', 'world'));
    }

    /* -------- INTEGER / REAL -------- */

    public function testIntegerTrue(): void
    {
        $this->assertTrue(Text::integer('123'));
        $this->assertTrue(Text::integer('-456'));
        $this->assertTrue(Text::integer('+789'));
    }

    public function testIntegerFalse(): void
    {
        $this->assertFalse(Text::integer('12.34'));
        $this->assertFalse(Text::integer('abc'));
        $this->assertFalse(Text::integer(''));
    }

    public function testRealTrue(): void
    {
        $this->assertTrue(Text::real('12.34'));
        $this->assertTrue(Text::real('1.23e-4'));
        $this->assertTrue(Text::real('123'));
    }

    public function testRealFalse(): void
    {
        $this->assertFalse(Text::real('hello'));
        $this->assertFalse(Text::real(''));
    }

    public function testNumericTrue(): void
    {
        $this->assertTrue(Text::numeric('123'));
        $this->assertTrue(Text::numeric('12.34'));
    }

    public function testNumericFalse(): void
    {
        $this->assertFalse(Text::numeric('hello'));
    }

    /* -------- LPAD / RPAD -------- */

    public function testLpad(): void
    {
        $this->assertSame('005', Text::lpad('5', 3, '0'));
        $this->assertSame('hello', Text::lpad('hello', 3, ' ')); // already wide enough
    }

    public function testRpad(): void
    {
        $this->assertSame('hi   ', Text::rpad('hi', 5, ' '));
        $this->assertSame('hello', Text::rpad('hello', 3, ' ')); // already wide enough
    }
}

