<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
// Text class wrappers available via Snobol\Text; testing \snobol_text_*() directly

/**
 * Tests for Snobol\Text built-in string and comparison functions.
 */
class TextTest extends TestCase
{
    /* -------- SIZE -------- */

    public function testSizeAscii(): void
    {
        $this->assertSame(5, \snobol_text_size('hello'));
    }

    public function testSizeUnicode(): void
    {
        // café = 4 codepoints, 5 bytes (é is 2 bytes)
        $this->assertSame(4, \snobol_text_size("caf\xC3\xA9"));
    }

    public function testSizeEmpty(): void
    {
        $this->assertSame(0, \snobol_text_size(''));
    }

    /* -------- TRIM / DUPL -------- */

    public function testTrimTrailingSpaces(): void
    {
        $this->assertSame('hello', \snobol_text_trim('hello  '));
    }

    public function testTrimNoOp(): void
    {
        $this->assertSame('hello', \snobol_text_trim('hello'));
    }

    public function testTrimAllWhitespace(): void
    {
        $this->assertSame('', \snobol_text_trim("   \t\n"));
    }

    public function testDuplBasic(): void
    {
        $this->assertSame('ababab', \snobol_text_dupl('ab', 3));
    }

    public function testDuplZero(): void
    {
        $this->assertSame('', \snobol_text_dupl('ab', 0));
    }

    public function testDuplUnicode(): void
    {
        $alpha_beta = "\xCE\xB1\xCE\xB2"; // αβ
        $this->assertSame($alpha_beta.$alpha_beta, \snobol_text_dupl($alpha_beta, 2));
    }

    /* -------- UPPER / LOWER -------- */

    public function testUpper(): void
    {
        $this->assertSame('HELLO', \snobol_text_upper('hello'));
    }

    public function testLower(): void
    {
        $this->assertSame('hello', \snobol_text_lower('HELLO'));
    }

    public function testUpperMixed(): void
    {
        $this->assertSame('HELLO WORLD!', \snobol_text_upper('Hello World!'));
    }

    public function testLowerMixed(): void
    {
        $this->assertSame('hello world!', \snobol_text_lower('Hello World!'));
    }

    public function testUpperCyrillic(): void
    {
        $this->assertSame("\xD0\x90\xD0\xAF", \snobol_text_upper("\xD0\xB0\xD1\x8F")); // ая → АЯ
    }

    public function testLowerCyrillic(): void
    {
        $this->assertSame("\xD0\xB0\xD1\x8F", \snobol_text_lower("\xD0\x90\xD0\xAF")); // АЯ → ая
    }

    public function testUpperGreek(): void
    {
        $this->assertSame("\xCE\x91\xCE\xA9", \snobol_text_upper("\xCE\xB1\xCF\x89")); // αω → ΑΩ
    }

    public function testLowerGreek(): void
    {
        $this->assertSame("\xCE\xB1\xCF\x89", \snobol_text_lower("\xCE\x91\xCE\xA9")); // ΑΩ → αω
    }

    public function testUpperLatinExtendedA(): void
    {
        $this->assertSame("\xC4\x80", \snobol_text_upper("\xC4\x81")); // ā → Ā
    }

    public function testLowerLatinExtendedA(): void
    {
        $this->assertSame("\xC4\x81", \snobol_text_lower("\xC4\x80")); // Ā → ā
    }

    public function testUpperIdentityArabic(): void
    {
        // Arabic alef (U+0627) has no case — upper/lower are identity
        $this->assertSame("\xD8\xA7", \snobol_text_upper("\xD8\xA7"));
        $this->assertSame("\xD8\xA7", \snobol_text_lower("\xD8\xA7"));
    }

    public function testUpperIdentityHebrew(): void
    {
        // Hebrew alef (U+05D0) has no case — upper/lower are identity
        $this->assertSame("\xD7\x90", \snobol_text_upper("\xD7\x90"));
        $this->assertSame("\xD7\x90", \snobol_text_lower("\xD7\x90"));
    }

    public function testUpperIdentityCjk(): void
    {
        // CJK ideograph (U+4E2D) has no case — upper/lower are identity
        $this->assertSame("\xE4\xB8\xAD", \snobol_text_upper("\xE4\xB8\xAD"));
        $this->assertSame("\xE4\xB8\xAD", \snobol_text_lower("\xE4\xB8\xAD"));
    }

    /* -------- REPLACE / REPLACE_CHAR -------- */

    public function testReplace(): void
    {
        $this->assertSame('hexxo hexxo', \snobol_text_replace('hello hello', 'll', 'xx'));
    }

    public function testReplaceNoMatch(): void
    {
        $this->assertSame('hello', \snobol_text_replace('hello', 'xyz', 'abc'));
    }

    public function testReplaceCharBasic(): void
    {
        $this->assertSame('hxyyo', \snobol_text_replace_char('hello', 'el', 'xy'));
    }

    public function testReplaceCharRot13(): void
    {
        $this->assertSame(
            'uryyb',
            \snobol_text_replace_char(
                'hello',
                'abcdefghijklmnopqrstuvwxyz',
                'nopqrstuvwxyzabcdefghijklm'
            )
        );
    }

    /* -------- SUBSTR / REVERSE -------- */

    public function testSubstrBasic(): void
    {
        $this->assertSame('world', \snobol_text_substr('hello world', 7, 5));
    }

    public function testSubstrUnicode(): void
    {
        $greek = "\xCE\xB1\xCE\xB2\xCE\xB3\xCE\xB4\xCE\xB5"; // αβγδε
        $this->assertSame("\xCE\xB2\xCE\xB3\xCE\xB4", \snobol_text_substr($greek, 2, 3));
    }

    public function testSubstrInvalidPos(): void
    {
        $this->assertFalse(\snobol_text_substr('hello', 0, 3));
    }

    public function testReverseAscii(): void
    {
        $this->assertSame('olleh', \snobol_text_reverse('hello'));
    }

    public function testReverseUnicode(): void
    {
        $abc = "\xCE\xB1\xCE\xB2\xCE\xB3"; // αβγ
        $cba = "\xCE\xB3\xCE\xB2\xCE\xB1"; // γβα
        $this->assertSame($cba, \snobol_text_reverse($abc));
    }

    /* -------- CHAR / ORD -------- */

    public function testCharAscii(): void
    {
        $this->assertSame('A', \snobol_text_char(65));
    }

    public function testCharUnicode(): void
    {
        $this->assertSame("\xCE\xB1", \snobol_text_char(0x03B1)); // α
    }

    public function testCharInvalid(): void
    {
        $this->assertFalse(\snobol_text_char(0x110000));
    }

    public function testOrdAscii(): void
    {
        $this->assertSame(65, \snobol_text_ord('A'));
    }

    public function testOrdUnicode(): void
    {
        $this->assertSame(0x03B1, \snobol_text_ord("\xCE\xB1")); // α
    }

    public function testOrdEmpty(): void
    {
        $this->assertFalse(\snobol_text_ord(''));
    }

    /* -------- IDENT / DIFFER -------- */

    public function testIdentTrue(): void
    {
        $this->assertTrue(\snobol_text_ident('hello', 'hello'));
    }

    public function testIdentFalse(): void
    {
        $this->assertFalse(\snobol_text_ident('hello', 'world'));
    }

    public function testDifferTrue(): void
    {
        $this->assertTrue(\snobol_text_differ('hello', 'world'));
    }

    public function testDifferFalse(): void
    {
        $this->assertFalse(\snobol_text_differ('hello', 'hello'));
    }

    /* -------- LEXLT / LEXGT -------- */

    public function testLexlt(): void
    {
        $this->assertTrue(\snobol_text_lexlt('apple', 'banana'));
        $this->assertFalse(\snobol_text_lexlt('banana', 'apple'));
    }

    public function testLexgt(): void
    {
        $this->assertTrue(\snobol_text_lexgt('banana', 'apple'));
        $this->assertFalse(\snobol_text_lexgt('apple', 'banana'));
    }

    public function testLexeq(): void
    {
        $this->assertTrue(\snobol_text_lexeq('hello', 'hello'));
        $this->assertFalse(\snobol_text_lexeq('hello', 'world'));
    }

    /* -------- INTEGER / REAL -------- */

    public function testIntegerTrue(): void
    {
        $this->assertTrue(\snobol_text_integer('123'));
        $this->assertTrue(\snobol_text_integer('-456'));
        $this->assertTrue(\snobol_text_integer('+789'));
    }

    public function testIntegerFalse(): void
    {
        $this->assertFalse(\snobol_text_integer('12.34'));
        $this->assertFalse(\snobol_text_integer('abc'));
        $this->assertFalse(\snobol_text_integer(''));
    }

    public function testRealTrue(): void
    {
        $this->assertTrue(\snobol_text_real('12.34'));
        $this->assertTrue(\snobol_text_real('1.23e-4'));
        $this->assertTrue(\snobol_text_real('123'));
    }

    public function testRealFalse(): void
    {
        $this->assertFalse(\snobol_text_real('hello'));
        $this->assertFalse(\snobol_text_real(''));
    }

    public function testNumericTrue(): void
    {
        $this->assertTrue(\snobol_text_numeric('123'));
        $this->assertTrue(\snobol_text_numeric('12.34'));
    }

    public function testNumericFalse(): void
    {
        $this->assertFalse(\snobol_text_numeric('hello'));
    }

    /* -------- LPAD / RPAD -------- */

    public function testLpad(): void
    {
        $this->assertSame('005', \snobol_text_lpad('5', 3, '0'));
        $this->assertSame('hello', \snobol_text_lpad('hello', 3, ' ')); // already wide enough
    }

    public function testRpad(): void
    {
        $this->assertSame('hi   ', \snobol_text_rpad('hi', 5, ' '));
        $this->assertSame('hello', \snobol_text_rpad('hello', 3, ' ')); // already wide enough
    }
}

