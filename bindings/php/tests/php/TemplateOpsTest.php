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

    public function testSubstDocumentedTableSyntaxLiteralKey(): void
    {
        $table = new Table();
        $table->set('sky', 'blue');

        $pattern = $this->makeCap0Pattern();
        // Documented bare form $TABLE[key]: identifier directly followed
        // by '[' (docs/php-manual.md) — must compile to OP_EMIT_TABLE
        $result = $pattern->subst('anything', "\$colors['sky']", ['colors' => $table]);
        $this->assertSame('blue', $result);
    }

    public function testSubstDocumentedTableSyntaxCaptureKey(): void
    {
        $table = new Table('STATE');
        $table->set('CA', 'California');
        $table->set('NY', 'New York');

        $pattern = $this->makeCap0Pattern();
        // Documented bare form $TABLE[$v0] with a capture-derived key
        $result = $pattern->subst('CA', "\$STATE[\$v0]", ['STATE' => $table]);
        $this->assertSame('California', $result);
    }

    public function testSubstTableNameTooLongThrows(): void
    {
        $this->expectException(\Exception::class);
        $this->expectExceptionMessage('Failed to compile template');

        $pattern = $this->makeCap0Pattern();
        // Table names are limited to 255 bytes (name_len field is 1 byte);
        // the compiler must fail loudly, not emit a literal '$'
        $tpl = '$' . str_repeat('x', 256) . "['k']";
        $pattern->subst('anything', $tpl, []);
    }

    public function testSubstLongTableKeyIsNotExecuted(): void
    {
        $table = new Table();
        $table->set('sky', 'blue');

        // A literal key of 1024+ bytes is instruction payload, not code: the
        // VM skipped it only for keys shorter than 1024, which left the
        // instruction pointer on the first key byte and decoded the key as
        // opcodes.  The key starts with OP_EMIT_CAPTURE (18) + register 0,
        // which would emit the capture into the output if decoded.
        $key = \chr(18) . \chr(0) . str_repeat(\chr(41), 2048 - 2);

        $pattern = $this->makeCap0Pattern();
        $result = $pattern->subst('anything', "\$v0[colors['" . $key . "']]", ['colors' => $table]);
        $this->assertSame('', $result);
    }

    public function testSubstTableKeyTooLongToEncodeThrows(): void
    {
        $this->expectException(\Exception::class);
        $this->expectExceptionMessage('Failed to compile template');

        $pattern = $this->makeCap0Pattern();
        // key_len is a 16-bit field; a 65536-byte key cannot be encoded, and
        // a truncated length would desynchronise the VM instruction pointer
        $tpl = "\$v0[colors['" . str_repeat('k', 65536) . "']]";
        $pattern->subst('anything', $tpl, []);
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

