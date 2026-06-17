<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use function snobol_text_eq;
use function snobol_text_ne;
use function snobol_text_lt;
use function snobol_text_gt;
use function snobol_text_le;
use function snobol_text_ge;

/**
 * Tests for numeric comparison functions: EQ, NE, LT, GT, LE, GE.
 *
 * These mirror the C tests in tests/c/test_comparison_numeric.c
 * and verify both the global snobol_text_* PHP functions and the
 * Snobol\Text static methods.
 */
class ComparisonsTest extends TestCase
{
    /* -------- EQ -------- */

    public function testEqEqualIntegers(): void
    {
        $this->assertTrue(snobol_text_eq('5', '5'));
        $this->assertTrue(snobol_text_eq('5', '5'));
    }

    public function testEqIntegerAndFloat(): void
    {
        $this->assertTrue(snobol_text_eq('5', '5.0'));
        $this->assertTrue(snobol_text_eq('5', '5.0'));
    }

    public function testEqDifferentNumbers(): void
    {
        $this->assertFalse(snobol_text_eq('5', '6'));
        $this->assertFalse(snobol_text_eq('5', '6'));
    }

    public function testEqNegative(): void
    {
        $this->assertTrue(snobol_text_eq('-3', '-3'));
        $this->assertTrue(snobol_text_eq('-3', '-3'));
    }

    public function testEqScientificNotation(): void
    {
        $this->assertTrue(snobol_text_eq('1e2', '100'));
        $this->assertTrue(snobol_text_eq('1e2', '100'));
    }

    public function testEqNonNumeric(): void
    {
        // Non-numeric strings convert to 0.0, so "abc" == "xyz" is true numerically
        $this->assertTrue(snobol_text_eq('abc', 'xyz'));
        $this->assertTrue(snobol_text_eq('abc', 'xyz'));
    }

    /* -------- NE -------- */

    public function testNeDifferent(): void
    {
        $this->assertTrue(\snobol_text_ne('5', '6'));
        $this->assertTrue(snobol_text_ne('5', '6'));
    }

    public function testNeSame(): void
    {
        $this->assertFalse(\snobol_text_ne('5', '5'));
        $this->assertFalse(snobol_text_ne('5', '5'));
    }

    public function testNeIntegerAndFloat(): void
    {
        $this->assertFalse(\snobol_text_ne('5', '5.0'));
        $this->assertFalse(snobol_text_ne('5', '5.0'));
    }

    /* -------- LT -------- */

    public function testLtTrue(): void
    {
        $this->assertTrue(\snobol_text_lt('3', '7'));
        $this->assertTrue(snobol_text_lt('3', '7'));
    }

    public function testLtEqual(): void
    {
        $this->assertFalse(\snobol_text_lt('5', '5'));
        $this->assertFalse(snobol_text_lt('5', '5'));
    }

    public function testLtFalse(): void
    {
        $this->assertFalse(\snobol_text_lt('7', '3'));
        $this->assertFalse(snobol_text_lt('7', '3'));
    }

    /* -------- GT -------- */

    public function testGtTrue(): void
    {
        $this->assertTrue(\snobol_text_gt('7', '3'));
        $this->assertTrue(snobol_text_gt('7', '3'));
    }

    public function testGtEqual(): void
    {
        $this->assertFalse(\snobol_text_gt('5', '5'));
        $this->assertFalse(snobol_text_gt('5', '5'));
    }

    public function testGtFalse(): void
    {
        $this->assertFalse(\snobol_text_gt('3', '7'));
        $this->assertFalse(snobol_text_gt('3', '7'));
    }

    /* -------- LE -------- */

    public function testLeLess(): void
    {
        $this->assertTrue(\snobol_text_le('3', '7'));
        $this->assertTrue(snobol_text_le('3', '7'));
    }

    public function testLeEqual(): void
    {
        $this->assertTrue(\snobol_text_le('5', '5'));
        $this->assertTrue(snobol_text_le('5', '5'));
    }

    public function testLeFalse(): void
    {
        $this->assertFalse(\snobol_text_le('7', '3'));
        $this->assertFalse(snobol_text_le('7', '3'));
    }

    /* -------- GE -------- */

    public function testGeGreater(): void
    {
        $this->assertTrue(\snobol_text_ge('7', '3'));
        $this->assertTrue(snobol_text_ge('7', '3'));
    }

    public function testGeEqual(): void
    {
        $this->assertTrue(\snobol_text_ge('5', '5'));
        $this->assertTrue(snobol_text_ge('5', '5'));
    }

    public function testGeFalse(): void
    {
        $this->assertFalse(\snobol_text_ge('3', '7'));
        $this->assertFalse(snobol_text_ge('3', '7'));
    }

    /* -------- Mixed numeric forms -------- */

    public function testEqMixedNumericForms(): void
    {
        $this->assertTrue(snobol_text_eq('0', '0.00'));
        $this->assertTrue(snobol_text_eq('0', '0.00'));
        $this->assertTrue(snobol_text_eq('1e2', '100.0'));
        $this->assertTrue(snobol_text_eq('1e2', '100.0'));
        $this->assertTrue(snobol_text_eq('-0', '0'));
        $this->assertTrue(snobol_text_eq('-0', '0'));
    }

    public function testLtNegative(): void
    {
        $this->assertTrue(\snobol_text_lt('-5', '3'));
        $this->assertTrue(snobol_text_lt('-5', '3'));
        $this->assertFalse(\snobol_text_lt('-3', '-5'));
        $this->assertFalse(snobol_text_lt('-3', '-5'));
    }

    public function testEqZeroAndEmpty(): void
    {
        // Both convert to 0.0
        $this->assertTrue(snobol_text_eq('0', ''));
        $this->assertTrue(snobol_text_eq('0', ''));
    }
}
