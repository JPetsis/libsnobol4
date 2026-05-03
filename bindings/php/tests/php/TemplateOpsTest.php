<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Builder;
use Snobol\Pattern;
use Snobol\Table;

/**
 * Integration tests for template expression evaluation through the C VM.
 *
 * Covers: subst() with .lower(), .lpad(), .rpad(), table-backed templates,
 * unregistered-table error handling, and regression checks for .upper()/.length().
 */
class TemplateOpsTest extends TestCase
{
    /** Build a simple capture-0 pattern that grabs the full alphanumeric match */
    private function makeCap0Pattern(): Pattern
    {
        $ast = Builder::concat([
            Builder::cap(0, Builder::span("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")),
            Builder::assign(0, 0),
        ]);
        return Pattern::compileFromAst($ast);
    }

    public function testSubstLower(): void
    {
        $pattern = $this->makeCap0Pattern();
        $result  = $pattern->subst('HELLO', '${v0.lower()}');
        $this->assertSame('hello', $result);
    }

    public function testSubstLpadWithFillChar(): void
    {
        $ast = Builder::concat([
            Builder::cap(0, Builder::span("0123456789")),
            Builder::assign(0, 0),
        ]);
        $pattern = Pattern::compileFromAst($ast);
        $result  = $pattern->subst('42', '${v0.lpad(5,\'0\')}');
        $this->assertSame('00042', $result);
    }

    public function testSubstRpadWithFillChar(): void
    {
        $pattern = $this->makeCap0Pattern();
        $result  = $pattern->subst('hi', '${v0.rpad(8,\'.\')}');
        $this->assertSame('hi......', $result);
    }

    public function testSubstTableBackedTemplate(): void
    {
        $table = new Table();
        $table->set('sky',  'blue');
        $table->set('sun',  'yellow');

        $pattern = $this->makeCap0Pattern();
        // Template references a table named "colors" with a literal key 'sky'
        $result = $pattern->subst('anything', "\$v0[colors['sky']]", ['colors' => $table]);
        $this->assertSame('blue', $result);
    }

    public function testSubstThrowsForUnregisteredTable(): void
    {
        $this->expectException(\Exception::class);

        $pattern = $this->makeCap0Pattern();
        // "unknownDict" is not in the passed tables array → should throw
        $pattern->subst('word', "\$v0[unknownDict['key']]", []);
    }

    public function testSubstLengthRegression(): void
    {
        $pattern = $this->makeCap0Pattern();
        $result  = $pattern->subst('hello', '${v0.length()}');
        $this->assertSame('5', $result);
    }

    public function testSubstUpperRegression(): void
    {
        $pattern = $this->makeCap0Pattern();
        $result  = $pattern->subst('hello', '${v0.upper()}');
        $this->assertSame('HELLO', $result);
    }

    public function testSubstCaptureRegression(): void
    {
        $pattern = $this->makeCap0Pattern();
        $result  = $pattern->subst('world', '${v0}');
        $this->assertSame('world', $result);
    }

    public function testSubstLiteralRegression(): void
    {
        $pattern = $this->makeCap0Pattern();
        $result  = $pattern->subst('anything', 'literal');
        $this->assertSame('literal', $result);
    }
}

