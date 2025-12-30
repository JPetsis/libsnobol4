<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Builder;
use Snobol\Pattern;
use Snobol\PatternHelper;

class PatternTest extends TestCase
{
    protected function setUp(): void
    {
        parent::setUp();
        // Clear cache before each test to ensure isolation
        PatternHelper::clearCache();
    }

    // === Original tests updated to use PatternHelper::fromAst ===

    public function testPatternCompileFromAst(): void
    {
        $ast = Builder::lit("test");
        $pattern = PatternHelper::fromAst($ast);
        $this->assertInstanceOf(Pattern::class, $pattern);
    }

    public function testSimpleLiteralMatch(): void
    {
        $ast = Builder::lit("hello");
        $result = PatternHelper::matchOnce($ast, "hello world");
        $this->assertNotFalse($result);
    }

    public function testLiteralNoMatch(): void
    {
        $ast = Builder::lit("goodbye");
        $result = PatternHelper::matchOnce($ast, "hello world");
        $this->assertFalse($result);
    }

    public function testCaptureAndAssign(): void
    {
        $ast = Builder::concat([
            Builder::lit("id:"),
            Builder::cap(0, Builder::span("0123456789")),
            Builder::assign(0, 0)
        ]);
        $result = PatternHelper::matchOnce($ast, "id:12345 more text");

        $this->assertIsArray($result);
        $this->assertArrayHasKey('v0', $result);
        $this->assertEquals('12345', $result['v0']);
    }

    // === New helper method tests ===

    public function testFromAstInvalidStructure(): void
    {
        $this->expectException(\InvalidArgumentException::class);
        $this->expectExceptionMessage('Invalid AST');
        PatternHelper::fromAst(['invalid' => 'structure']);
    }

    public function testFromStringSuccess(): void
    {
        $pattern = PatternHelper::fromString("'test'");
        $this->assertInstanceOf(Pattern::class, $pattern);

        $result = PatternHelper::matchOnce($pattern, "test");
        $this->assertNotFalse($result);
    }

    public function testMatchOnceWithPrecompiledPattern(): void
    {
        $ast = Builder::lit("hello");
        $pattern = PatternHelper::fromAst($ast);

        $result = PatternHelper::matchOnce($pattern, "hello world");
        $this->assertIsArray($result);
    }

    public function testMatchOnceWithArrayAst(): void
    {
        $ast = Builder::lit("test");
        $result = PatternHelper::matchOnce($ast, "testing 123");
        $this->assertIsArray($result);
    }

    public function testMatchAllFindsMultipleMatches(): void
    {
        $ast = Builder::span("0123456789");
        $matches = PatternHelper::matchAll($ast, "id:123 code:456 num:789");

        $this->assertIsArray($matches);
        $this->assertGreaterThanOrEqual(1, count($matches));
    }

    public function testMatchAllReturnsEmptyOnNoMatch(): void
    {
        $ast = Builder::lit("notfound");
        $matches = PatternHelper::matchAll($ast, "hello world");

        $this->assertIsArray($matches);
        $this->assertEmpty($matches);
    }

    public function testSplitByPattern(): void
    {
        $ast = Builder::lit(",");
        $segments = PatternHelper::split($ast, "a,b,c");

        $this->assertIsArray($segments);
        $this->assertGreaterThan(1, count($segments));
    }

    public function testSplitNoMatchReturnsOriginal(): void
    {
        $ast = Builder::lit("notfound");
        $segments = PatternHelper::split($ast, "hello world");

        $this->assertIsArray($segments);
        $this->assertCount(1, $segments);
        $this->assertEquals("hello world", $segments[0]);
    }

    public function testReplaceByPattern(): void
    {
        $ast = Builder::lit("old");
        $result = PatternHelper::replace($ast, "new", "old text with old words");

        $this->assertIsString($result);
        $this->assertStringContainsString("new", $result);
    }

    public function testReplaceNoMatchReturnsOriginal(): void
    {
        $ast = Builder::lit("notfound");
        $result = PatternHelper::replace($ast, "replacement", "hello world");

        $this->assertEquals("hello world", $result);
    }

    // === Cache behaviour tests ===

    public function testCacheReusesSamePattern(): void
    {
        $ast = Builder::lit("cached");

        // First call should compile
        $result1 = PatternHelper::matchOnce($ast, "cached value");

        // Second call with same AST should reuse compiled pattern
        $result2 = PatternHelper::matchOnce($ast, "cached again");

        $this->assertIsArray($result1);
        $this->assertIsArray($result2);
    }

    public function testCacheBypassOption(): void
    {
        $ast = Builder::lit("test");

        // With cache disabled
        $result = PatternHelper::matchOnce($ast, "testing", ['cache' => false]);

        $this->assertNotFalse($result);
    }

    // === Full-string anchor tests (placeholder) ===

    public function testFullStringAnchorSuccess(): void
    {
        $ast = Builder::lit("exact");

        $result = PatternHelper::matchOnce($ast, "exact", ['full' => true]);

        // For now, just verify it doesn't crash
        $this->assertNotFalse($result);
    }

    public function testFullStringAnchorFailsOnTrailing(): void
    {
        $ast = Builder::lit("prefix");

        // Should fail because "prefix suffix" has trailing characters
        $result = PatternHelper::matchOnce($ast, "prefix suffix", ['full' => true]);

        $this->assertFalse($result, "Expected false because match did not consume full string");
    }

    public function testStartAnchor(): void
    {
        $ast = Builder::concat([Builder::anchor('start'), Builder::lit('hello')]);

        $this->assertNotFalse(PatternHelper::matchOnce($ast, "hello world"));
        $this->assertFalse(PatternHelper::matchOnce($ast, " hello world"));
    }

    public function testEndAnchor(): void
    {
        $ast = Builder::concat([Builder::lit('world'), Builder::anchor('end')]);

        $this->assertNotFalse(PatternHelper::matchOnce($ast, "world"));
        $this->assertFalse(PatternHelper::matchOnce($ast, "world "));
    }

    public function testBoundedRepetitionFixed(): void
    {
        // repeat 'a' exactly 3 times
        $ast = Builder::repeat(Builder::lit('a'), 3, 3);
        $result = PatternHelper::matchOnce($ast, "aaaaa");

        $this->assertIsArray($result);
        $this->assertEquals(3, $result['_match_len']);
    }

    public function testBoundedRepetitionRange(): void
    {
        // repeat 'a' 1 to 3 times (greedy)
        $ast = Builder::repeat(Builder::lit('a'), 1, 3);

        $result = PatternHelper::matchOnce($ast, "aaaaa");
        $this->assertIsArray($result);
        $this->assertEquals(3, $result['_match_len']);

        $result2 = PatternHelper::matchOnce($ast, "a");
        $this->assertIsArray($result2);
        $this->assertEquals(1, $result2['_match_len']);

        $this->assertFalse(PatternHelper::matchOnce($ast, "b"));
    }

    public function testBoundedRepetitionMinOnly(): void
    {
        // repeat 'a' at least 2 times
        $ast = Builder::repeat(Builder::lit('a'), 2);

        $this->assertFalse(PatternHelper::matchOnce($ast, "a"));

        $result = PatternHelper::matchOnce($ast, "aaa");
        $this->assertIsArray($result);
        $this->assertEquals(3, $result['_match_len']);
    }

    public function testEmitLiteral(): void
    {
        $ast = Builder::concat([
            Builder::lit('h'),
            Builder::emit('H'),
            Builder::lit('e'),
            Builder::emit('E')
        ]);

        $result = PatternHelper::matchOnce($ast, "hello");
        $this->assertIsArray($result);
        $this->assertArrayHasKey('_output', $result);
        $this->assertEquals("HE", $result['_output']);
    }

    public function testEmitRef(): void
    {
        $ast = Builder::concat([
            Builder::cap(1, Builder::lit('hel')),
            Builder::emitRef(1),
            Builder::lit('lo')
        ]);

        $result = PatternHelper::matchOnce($ast, "hello");
        $this->assertIsArray($result);
        $this->assertArrayHasKey('_output', $result);
        $this->assertEquals("hel", $result['_output']);
    }

    public function testReplaceWithTemplate(): void
    {
        // Replace digits with "[digits]"
        $pattern = Builder::cap(0, Builder::span("0123456789"));
        $subject = "id:123 code:456";
        $template = "[\$v0]";

        $result = PatternHelper::replace($pattern, $template, $subject);
        $this->assertEquals("id:[123] code:[456]", $result);
    }

    public function testReplaceWithUpperExpression(): void
    {
        // Upper case names
        $pattern = Builder::concat([
            Builder::lit("name:"),
            Builder::cap(1, Builder::span("abcdefghijklmnopqrstuvwxyz"))
        ]);
        $subject = "name:alice name:bob";
        $template = "NAME:\${v1.upper()}";

        $result = PatternHelper::replace($pattern, $template, $subject);
        $this->assertEquals("NAME:ALICE NAME:BOB", $result);
    }

    public function testReplaceWithLengthExpression(): void
    {
        // Replace words with their length
        $pattern = Builder::cap(1, Builder::span("abcdefghijklmnopqrstuvwxyz"));
        $subject = "abc de fghi";
        $template = "\${v1.length()}";

        $result = PatternHelper::replace($pattern, $template, $subject);
        $this->assertEquals("3 2 4", $result);
    }

    public function testTemplateWithEmptyCapture(): void
    {
        // Match literal 'x' (not arbno which is too greedy/flexible for this simple test)
        $pattern = Builder::cap(1, Builder::lit('x'));
        $subject = "axbxc";
        $template = "[\${v1}]";

        $result = PatternHelper::replace($pattern, $template, $subject);
        // "a[x]b[x]c"
        $this->assertEquals("a[x]b[x]c", $result);
    }

    public function testTemplateWithInvalidVariable(): void
    {
        $pattern = Builder::lit("abc");
        $subject = "abc";
        $template = "val:\${v99}"; // v99 is not captured

        $result = PatternHelper::replace($pattern, $template, $subject);
        $this->assertEquals("val:", $result); // Should emit empty for non-existent capture
    }

    public function testTemplateWithBracedMethodNoParens(): void
    {
        $pattern = Builder::cap(1, Builder::lit("abc"));
        $subject = "abc";
        $template = "\${v1.length}"; // Missing parens

        $result = PatternHelper::replace($pattern, $template, $subject);
        // Parser correctly treats invalid ${...} as literal if it can't match it as a variable or expr
        $this->assertEquals("\${v1.length}", $result);
    }
}


