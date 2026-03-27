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

    public function testLabelledPatterns(): void
    {
        // Label prefix: LABEL: pattern
        $p = new Parser("LOOP: 'A'*");
        $ast = $p->parse();
        $this->assertEquals('label', $ast['type']);
        $this->assertEquals('LOOP', $ast['name']);
        $this->assertEquals('arbno', $ast['target']['type']);
        $this->assertEquals('lit', $ast['target']['sub']['type']);

        // Nested label
        $p = new Parser("START: 'A' :(END)");
        $ast = $p->parse();
        $this->assertEquals('label', $ast['type']);
        $this->assertEquals('START', $ast['name']);
        $this->assertEquals('concat', $ast['target']['type']);
    }

    public function testGotoLikeControlFlow(): void
    {
        // Goto syntax: :(LABEL)
        $p = new Parser(":(LOOP)");
        $ast = $p->parse();
        $this->assertEquals('goto', $ast['type']);
        $this->assertEquals('LOOP', $ast['label']);

        // Goto after pattern
        $p = new Parser("'A' :(END)");
        $ast = $p->parse();
        $this->assertEquals('concat', $ast['type']);
        $this->assertEquals('lit', $ast['parts'][0]['type']);
        $this->assertEquals('goto', $ast['parts'][1]['type']);
        $this->assertEquals('END', $ast['parts'][1]['label']);
    }

    public function testDynamicEvaluation(): void
    {
        // EVAL with string pattern
        $p = new Parser("EVAL('A' | 'B')");
        $ast = $p->parse();
        $this->assertEquals('dynamic_eval', $ast['type']);
        // The expr inside EVAL is parsed as alt('A', 'B')
        $this->assertEquals('alt', $ast['expr']['type']);
        $this->assertEquals('lit', $ast['expr']['left']['type']);
        $this->assertEquals('A', $ast['expr']['left']['text']);
        $this->assertEquals('lit', $ast['expr']['right']['type']);
        $this->assertEquals('B', $ast['expr']['right']['text']);
    }

    public function testTableAccess(): void
    {
        // Table lookup: TABLE[key]
        $p = new Parser("T['key']");
        $ast = $p->parse();
        $this->assertEquals('table_access', $ast['type']);
        $this->assertEquals('T', $ast['table']);
        $this->assertEquals('lit', $ast['key']['type']);
        $this->assertEquals('key', $ast['key']['text']);

        // Table with variable key (uppercase for table variable)
        $p = new Parser("T[Key]");
        $ast = $p->parse();
        $this->assertEquals('table_access', $ast['type']);
        $this->assertEquals('T', $ast['table']);
        $this->assertEquals('lit', $ast['key']['type']);
        $this->assertEquals('Key', $ast['key']['text']);
    }

    public function testTableUpdate(): void
    {
        // Table assignment: TABLE[key] = value
        $p = new Parser("T['key'] = 'value'");
        $ast = $p->parse();
        $this->assertEquals('table_update', $ast['type']);
        $this->assertEquals('T', $ast['table']);
        $this->assertEquals('lit', $ast['key']['type']);
        $this->assertEquals('key', $ast['key']['text']);
        $this->assertEquals('lit', $ast['value']['type']);
        $this->assertEquals('value', $ast['value']['text']);
    }

    public function testComplexLabelledPatternWithGoto(): void
    {
        // Complete example: LOOP: 'A'* :(END)
        $p = new Parser("LOOP: 'A'* :(END)");
        $ast = $p->parse();
        $this->assertEquals('label', $ast['type']);
        $this->assertEquals('LOOP', $ast['name']);
        $this->assertEquals('concat', $ast['target']['type']);
        $this->assertEquals('arbno', $ast['target']['parts'][0]['type']);
        $this->assertEquals('goto', $ast['target']['parts'][1]['type']);
        $this->assertEquals('END', $ast['target']['parts'][1]['label']);
    }

    public function testDuplicateLabelDetection(): void
    {
        $this->expectException(\Exception::class);
        $this->expectExceptionMessage("Duplicate label 'LOOP'");

        // Two labels with same name in sequence
        $p = new Parser("LOOP: LOOP: 'A'");
        $p->parse();
    }

    public function testValidLabelAndGoto(): void
    {
        // This should NOT throw - label is defined, goto references it
        $p = new Parser("END: 'A' :(END)");
        $ast = $p->parse();
        $this->assertEquals('label', $ast['type']);
        $this->assertEquals('END', $ast['name']);
    }

    public function testGotoForwardReference(): void
    {
        // Forward references are allowed - standalone goto
        $p = new Parser(":(END)");
        $ast = $p->parse();
        $this->assertEquals('goto', $ast['type']);
        $this->assertEquals('END', $ast['label']);
    }

    public function testMalformedDynamicEval(): void
    {
        $this->expectException(\Exception::class);
        $this->expectExceptionMessage("EVAL requires a pattern expression argument");

        $p = new Parser("EVAL()");
        $p->parse();
    }

    public function testEvalErrorHandlingWithNestedEval(): void
    {
        // Test that EVAL with nested EVAL parses correctly
        $p = new Parser("EVAL(EVAL('A'))");
        $ast = $p->parse();
        $this->assertEquals('dynamic_eval', $ast['type']);
        $this->assertEquals('dynamic_eval', $ast['expr']['type']);
        $this->assertEquals('lit', $ast['expr']['expr']['type']);
    }

    public function testEvalErrorHandlingWithComplexExpression(): void
    {
        // Test that EVAL with complex expression parses correctly
        $p = new Parser("EVAL('A' 'B' | 'C')");
        $ast = $p->parse();
        $this->assertEquals('dynamic_eval', $ast['type']);
        $this->assertEquals('alt', $ast['expr']['type']);
    }

    public function testCommaSeparatedArgumentsGenericParsing(): void
    {
        // Test that parser generically accepts comma-separated arguments
        // The parser consumes all arguments; arity validation happens in semantic validation
        // Parser successfully parses the argument list, then buildFunction validates arity
        $this->expectException(\Exception::class);
        $this->expectExceptionMessage("ANY requires exactly one argument, 2 given");

        $p = new Parser("ANY('a', 'b')");
        $p->parse();
    }

    public function testCommaSeparatedArgumentsWithLiterals(): void
    {
        // Test that comma-separated literals are parsed correctly
        // Parser consumes both arguments, semantic validation rejects
        $this->expectException(\Exception::class);
        $this->expectExceptionMessage("SPAN requires exactly one argument, 2 given");
        
        $p = new Parser("SPAN('0-9', 'a-z')");
        $p->parse();
    }

    public function testSurplusArgumentRejectionAtSemanticValidation(): void
    {
        // Test that single-argument builtins reject surplus arguments at semantic validation time
        // Parser accepts the comma-separated list, but buildFunction validates arity

        // SPAN with 2 arguments - should fail semantic validation
        $this->expectException(\Exception::class);
        $this->expectExceptionMessage("SPAN requires exactly one argument, 2 given");

        $p = new Parser("SPAN('0-9', 'a-z')");
        $p->parse();
    }

    public function testSurplusArgumentRejectionForAny(): void
    {
        // ANY with 2 arguments - should fail semantic validation
        $this->expectException(\Exception::class);
        $this->expectExceptionMessage("ANY requires exactly one argument, 2 given");

        $p = new Parser("ANY('a', 'b')");
        $p->parse();
    }

    public function testSurplusArgumentRejectionForLen(): void
    {
        // LEN with 3 arguments - should fail semantic validation
        $this->expectException(\Exception::class);
        $this->expectExceptionMessage("LEN requires exactly one argument, 3 given");

        $p = new Parser("LEN(1, 2, 3)");
        $p->parse();
    }

    public function testSurplusArgumentRejectionForBreak(): void
    {
        // BREAK with 2 arguments - should fail semantic validation
        $this->expectException(\Exception::class);
        $this->expectExceptionMessage("BREAK requires exactly one argument, 2 given");

        $p = new Parser("BREAK('a', 'b')");
        $p->parse();
    }

    public function testSurplusArgumentRejectionForNotany(): void
    {
        // NOTANY with 2 arguments - should fail semantic validation
        $this->expectException(\Exception::class);
        $this->expectExceptionMessage("NOTANY requires exactly one argument, 2 given");

        $p = new Parser("NOTANY('a', 'b')");
        $p->parse();
    }

    public function testMalformedTrailingComma(): void
    {
        $this->expectException(\Exception::class);
        $this->expectExceptionMessage("Unexpected ')' after comma in argument list");

        $p = new Parser("SPAN('a',)");
        $p->parse();
    }

    public function testMalformedMissingArgAfterComma(): void
    {
        $this->expectException(\Exception::class);

        // Comma followed by closing paren
        $p = new Parser("ANY(,)");
        $p->parse();
    }
}
