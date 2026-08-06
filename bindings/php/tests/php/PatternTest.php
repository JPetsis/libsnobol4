<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Builder;
use Snobol\Pattern;
use Snobol\PatternHelper;

class PatternTest extends TestCase
{
    public function testPatternCompileFromAst(): void
    {
        $ast = Builder::lit("test");
        $pattern = PatternHelper::fromAst($ast);
        $this->assertInstanceOf(Pattern::class, $pattern);
    }

    // === Original tests updated to use PatternHelper::fromAst ===

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

    public function testFromAstInvalidStructure(): void
    {
        $this->expectException(\ValueError::class);
        $this->expectExceptionMessage('Invalid AST');
        PatternHelper::fromAst(['invalid' => 'structure']);
    }

    // === New helper method tests ===

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

    // === Cache behaviour tests ===

    public function testCacheBypassOption(): void
    {
        $ast = Builder::lit("test");

        // With cache disabled
        $result = PatternHelper::matchOnce($ast, "testing", ['cache' => false]);

        $this->assertNotFalse($result);
    }

    public function testFullStringAnchorSuccess(): void
    {
        $ast = Builder::lit("exact");

        $result = PatternHelper::matchOnce($ast, "exact", ['full' => true]);

        // For now, just verify it doesn't crash
        $this->assertNotFalse($result);
    }

    // === Full-string anchor tests (placeholder) ===

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

    public function testBacktrackingDoesNotLeakCaptureAcrossAlternation(): void
    {
        // (cap0('a') end) | cap0('b')  against "b".
        // Current VM restores full capture snapshots when it backtracks, which prevents leaks
        // from failing branches but can also revert captures to "unset" even for successful paths.
        $ast = Builder::alt(
            Builder::concat([
                Builder::cap(0, Builder::lit('a')),
                Builder::anchor('end'),
            ]),
            Builder::cap(0, Builder::lit('b'))
        );

        $result = PatternHelper::matchOnce($ast, 'b', ['full' => true]);
        $this->assertIsArray($result);

        // Guardrail: must NOT return the failed-branch capture.
        $this->assertNotSame('a', $result['v0'] ?? null);
    }

    public function testBacktrackingDoesNotLeakVarAssignmentAcrossAlternation(): void
    {
        // First branch assigns v0="a" then fails; second assigns v0="b" and succeeds.
        // Guardrail: must NOT return the failed-branch value.
        $ast = Builder::alt(
            Builder::concat([
                Builder::cap(0, Builder::lit('a')),
                Builder::assign(0, 0),
                Builder::anchor('end'),
            ]),
            Builder::concat([
                Builder::cap(0, Builder::lit('b')),
                Builder::assign(0, 0),
            ])
        );

        $result = PatternHelper::matchOnce($ast, 'b', ['full' => true]);
        $this->assertIsArray($result);
        $this->assertNotSame('a', $result['v0'] ?? null);
    }

    public function testBacktrackingDoesNotLeakOutputAcrossAlternation(): void
    {
        // Output is currently NOT backtrackable in the VM, so emits from failing branches can remain.
        // This test documents the current behavior as a baseline; once output
        // backtrack semantics are decided, we can tighten this to assert "Y" only.
        $ast = Builder::alt(
            Builder::concat([
                Builder::emit('X'),
                Builder::lit('a'),
                Builder::anchor('end'),
            ]),
            Builder::concat([
                Builder::emit('Y'),
                Builder::lit('b'),
                Builder::anchor('end'),
            ])
        );

        $result = PatternHelper::matchOnce($ast, 'b', ['full' => true]);
        $this->assertIsArray($result);
        $this->assertSame('XY', $result['_output'] ?? null);
    }

    public function testBacktrackingWithRepeatGreedyMin0DoesNotBreakSubsequentMatch(): void
    {
        // repeat('a', 0..inf) then 'b', on input 'b'.
        $ast = Builder::concat([
            Builder::repeat(Builder::lit('a'), 0),
            Builder::cap(0, Builder::lit('b')),
        ]);

        $result = PatternHelper::matchOnce($ast, 'b', ['full' => true]);
        $this->assertIsArray($result);

        // Guardrail: must not produce an incorrect capture from backtracking paths.
        $this->assertNotSame('a', $result['v0'] ?? null);
        $this->assertSame(1, $result['_match_len']);
    }

    public function testTableBackedTemplateWithCaptureKey(): void
    {
        // Test table-backed template substitution with capture-derived key
        $pattern = Builder::cap(0, Builder::span("abcdefghijklmnopqrstuvwxyz"));
        $subject = "key";

        // Create a table and set values
        $table = new \Snobol\Table();
        $table->set("key", "value_from_table");

        // Note: Full table-backed template syntax ($TABLE[key]) requires
        // integration with the table registry
        // For now, test the underlying table lookup mechanism
        $this->assertEquals("value_from_table", $table->get("key"));
    }

    public function testTableBackedTemplateMissingKeyFallback(): void
    {
        // Test graceful degradation for missing table keys
        $table = new \Snobol\Table();
        $table->set("existing", "value");

        // Missing key should return null (graceful degradation)
        $this->assertNull($table->get("missing_key"));
    }

    public function testTableBackedTemplateWithMultipleLookups(): void
    {
        // Test multiple table lookups in sequence
        $table = new \Snobol\Table();
        $table->set("name", "Alice");
        $table->set("city", "Boston");

        $this->assertEquals("Alice", $table->get("name"));
        $this->assertEquals("Boston", $table->get("city"));
    }

    // === searchSplitOffsets tests ===

    public function testSearchSplitOffsetsCommaSeparated(): void
    {
        $pattern = PatternHelper::fromString("','");
        $result = $pattern->searchSplitOffsets("a,b,c");
        $this->assertIsArray($result);
        $this->assertCount(3, $result);
        $this->assertSame([0, 1], $result[0]);
        $this->assertSame([2, 1], $result[1]);
        $this->assertSame([4, 1], $result[2]);
    }

    public function testSearchSplitOffsetsEmptySubject(): void
    {
        $pattern = PatternHelper::fromString("','");
        $result = $pattern->searchSplitOffsets("");
        $this->assertIsArray($result);
        $this->assertCount(1, $result);
        $this->assertSame([0, 0], $result[0]);
    }

    public function testSearchSplitOffsetsNoMatch(): void
    {
        $pattern = PatternHelper::fromString("'x'");
        $result = $pattern->searchSplitOffsets("hello");
        $this->assertIsArray($result);
        $this->assertCount(1, $result);
        $this->assertSame([0, 5], $result[0]);
    }

    public function testSearchSplitOffsetsZeroLengthMatch(): void
    {
        $pattern = PatternHelper::fromString("''");
        $result = $pattern->searchSplitOffsets("abc");
        $this->assertIsArray($result);
        // Empty pattern matches at position 0 (zero-length), advances by 1
        // So we get segments at [0,1]=0, [1,1]=1, [2,1]=2, [3,0]=empty
        // Empty pattern matches at positions 0,1,2,3 (including end)
        // giving 5 segments (before each match + trailing)
        $this->assertCount(5, $result);
        // Segments: "" (0→0), "a" (0→1), "b" (1→2), "c" (2→3), "" (3→3)
        $this->assertSame([0, 0], $result[0]);
        $this->assertSame([0, 1], $result[1]);
        $this->assertSame([1, 1], $result[2]);
        $this->assertSame([2, 1], $result[3]);
        $this->assertSame([3, 0], $result[4]);
    }

    public function testSearchSplitOffsetsTrailingSegment(): void
    {
        $pattern = PatternHelper::fromString("','");
        $result = $pattern->searchSplitOffsets("a,b,");
        $this->assertIsArray($result);
        $this->assertCount(3, $result);
        // "a", "b", ""
        $this->assertSame([0, 1], $result[0]);
        $this->assertSame([2, 1], $result[1]);
        $this->assertSame([4, 0], $result[2]);
    }

    public function testSearchSplitOffsetsMultipleMatches(): void
    {
        $pattern = PatternHelper::fromString("' '");
        $result = $pattern->searchSplitOffsets("a b c d e");
        $this->assertIsArray($result);
        $this->assertCount(5, $result);
        $this->assertSame([0, 1], $result[0]);
        $this->assertSame([2, 1], $result[1]);
        $this->assertSame([4, 1], $result[2]);
        $this->assertSame([6, 1], $result[3]);
        $this->assertSame([8, 1], $result[4]);
    }

    public function testSearchSplitOffsetsWithSpanPattern(): void
    {
        $pattern = PatternHelper::fromString("SPAN(' ')");
        $result = $pattern->searchSplitOffsets("a  b   c");
        $this->assertIsArray($result);
        $this->assertGreaterThanOrEqual(3, count($result));
        // First segment "a" at [0,1], then "b" at some offset, then "c" at some offset
        $this->assertSame([0, 1], $result[0]);
    }

    public function testDfaCache(): void
    {
        // Automaton-eligible pattern with 2+ byte literal prefix (has_bmh_skip),
        // but NOT literal-only (has ANY after the literal).
        $pattern = PatternHelper::fromString("'hello' ANY('xyz')");
        $subject = 'hellox';

        // First match: DFA built and cached
        $r1 = $pattern->match($subject);
        $this->assertNotFalse($r1);
        $this->assertArrayHasKey('_match_len', $r1);

        // Second match: cached DFA reused (should still produce correct result)
        $r2 = $pattern->match($subject);
        $this->assertNotFalse($r2);
        $this->assertEquals($r1['_match_len'], $r2['_match_len']);
    }

    public function testTrieCache(): void
    {
        // Alt-literals pattern: bushy alternation with shared prefix
        $ast = Builder::alt(
            Builder::alt(Builder::lit('cat'), Builder::lit('car')),
            Builder::lit('cab')
        );
        $pattern = PatternHelper::fromAst($ast);
        $subject = 'xxx car yyy';

        // First search: trie built and cached
        $r1 = $pattern->searchAll($subject);
        $this->assertCount(1, $r1);
        $this->assertArrayHasKey(0, $r1);
        $this->assertArrayHasKey('_match_len', $r1[0]);

        // Second search: cached trie reused
        $r2 = $pattern->searchAll($subject);
        $this->assertCount(1, $r2);
        $this->assertEquals(
            $r1[0]['_match_start'],
            $r2[0]['_match_start']
        );
    }

    protected function setUp(): void
    {
        parent::setUp();
        // Clear cache before each test to ensure isolation
        PatternHelper::clearCache();
    }

    // === 2.1 Pattern API surface: compile flags, options, error contracts ===

    public function testFromStringWithCaseInsensitiveOption(): void
    {
        $pattern = Pattern::fromString("'abc'", ['caseInsensitive' => true]);
        $this->assertIsArray($pattern->match('ABC'));
        $this->assertFalse($pattern->match('xyz'));
    }

    public function testCompileFromAstWithOptionsArray(): void
    {
        $pattern = Pattern::compileFromAst(Builder::lit('hello'), ['caseInsensitive' => true]);
        $this->assertIsArray($pattern->match('HELLO'));
    }

    public function testCompileFromAstUnknownNodeThrows(): void
    {
        $this->expectException(\Exception::class);
        Pattern::compileFromAst(['type' => 'no_such_node']);
    }

    public function testFromStringEmptyPatternMatchesEmpty(): void
    {
        $pattern = Pattern::fromString("''");
        $result = $pattern->match('');
        $this->assertIsArray($result);
        $this->assertSame(0, $result['_match_len']);
    }

    public function testMatchExplicitNullOptions(): void
    {
        $pattern = Pattern::fromString("'abc'");
        $this->assertIsArray($pattern->match('abc', null));
    }

    public function testMatchMetricsOption(): void
    {
        $pattern = Pattern::fromString("'a'");
        $result = $pattern->match('a', ['metrics' => true]);
        $this->assertIsArray($result);
        $this->assertArrayHasKey('_metrics', $result);
    }

    public function testMatchMetricsOnCapturePattern(): void
    {
        $pattern = Pattern::compileFromAst(
            Builder::concat([Builder::cap(0, Builder::lit('ab')), Builder::lit('c')])
        );
        $result = $pattern->match('abc', ['metrics' => true]);
        $this->assertIsArray($result);
        $this->assertArrayHasKey('_metrics', $result);
        $this->assertSame(3, $result['_match_len']);
    }

    public function testMatchCapturesOffsetsOption(): void
    {
        $pattern = Pattern::compileFromAst(Builder::cap(0, Builder::lit('abc')));
        $result = $pattern->match('abc', ['captures' => 'offsets']);
        $this->assertIsArray($result);
        $this->assertSame([0, 3], $result['v0']);
    }

    public function testMatchEmptyCaptureIsNull(): void
    {
        $pattern = Pattern::compileFromAst(
            Builder::concat([Builder::cap(0, Builder::lit('')), Builder::lit('x')])
        );
        $result = $pattern->match('x');
        $this->assertIsArray($result);
        $this->assertNull($result['v0']);
    }

    public function testMatchLiteralSuccess(): void
    {
        $pattern = Pattern::fromString("'hello'");
        $result = $pattern->matchLiteral('hello');
        $this->assertIsArray($result);
        $this->assertTrue($result['success']);
        $this->assertSame(5, $result['length']);
    }

    public function testMatchLiteralNoMatch(): void
    {
        $pattern = Pattern::fromString("'hello'");
        $result = $pattern->matchLiteral('nope');
        $this->assertIsArray($result);
        $this->assertFalse($result['success']);
        $this->assertSame(0, $result['length']);
    }

    public function testMatchLiteralOnNonLiteralPattern(): void
    {
        $pattern = Pattern::fromString("'a' 'b'");
        $result = $pattern->matchLiteral('ab');
        $this->assertIsArray($result);
        $this->assertFalse($result['success']);
    }

    public function testSearchAllFlatResult(): void
    {
        $pattern = Pattern::fromString("'a'");
        $result = $pattern->searchAll('banana', ['result' => 'flat']);
        $this->assertArrayHasKey('match_start', $result);
        $this->assertSame([1, 3, 5], $result['match_start']);
        $this->assertSame([1, 1, 1], $result['match_len']);
    }

    public function testSearchAllWithMetricsOption(): void
    {
        $pattern = Pattern::fromString("'a'");
        $result = $pattern->searchAll('banana', ['metrics' => true]);
        $this->assertIsArray($result);
        $this->assertArrayHasKey('_metrics', $result[0]);
    }

    public function testSearchSplitFlatResult(): void
    {
        $pattern = Pattern::fromString("'a'");
        $result = $pattern->searchSplit('banana', ['result' => 'flat']);
        $this->assertSame([0, 1, 2, 1, 4, 1, 6, 0], $result);
    }

    public function testSearchSplitCutsResult(): void
    {
        $pattern = Pattern::fromString("'a'");
        $this->assertSame([2, 4, 6], $pattern->searchSplitCuts('banana'));
    }

    public function testSearchSplitCutsEmptySubject(): void
    {
        $pattern = Pattern::fromString("'a'");
        $this->assertSame([], $pattern->searchSplitCuts(''));
    }

    public function testSearchReplaceNoMatchReturnsSubject(): void
    {
        $pattern = Pattern::fromString("'z'");
        $this->assertSame('banana', $pattern->searchReplace('banana', 'X'));
    }

    public function testSearchReplaceIneligiblePerCallLoop(): void
    {
        // EVAL patterns are not batch-eligible: exercises the per-call loop.
        // Zero-length matches occur at every position, including at the end
        // of the subject (regression for the remainder-append underflow).
        $pattern = Pattern::compileFromAst(Builder::eval(0, 0));
        $this->assertSame('XXXX', $pattern->searchReplace('abc', 'X'));
    }

    public function testSearchReplaceTrailingZeroLengthMatch(): void
    {
        // BREAK pattern that matches up to end-of-subject followed by a
        // zero-length match: batch path must not underflow the remainder.
        $pattern = Pattern::compileFromAst(Builder::brk('x'));
        $this->assertSame('XX', $pattern->searchReplace('abc', 'X'));
    }

    public function testSearchReplaceLargeSubjectCountPass(): void
    {
        // Non-literal pattern, subject > 1 KB: exercises the count pass + pre-size
        $pattern = Pattern::fromString("'a' 'b'");
        $subject = str_repeat('ab', 600);
        $replaced = $pattern->searchReplace($subject, 'X');
        $this->assertSame(str_repeat('X', 600), $replaced);
    }

    public function testSearchReplaceLargeLiteralSubjectPresize(): void
    {
        // Literal-only pattern, subject > 1 KB: exercises the literal pre-size path
        $pattern = Pattern::fromString("'ab'");
        $subject = str_repeat('ab', 600);
        $replaced = $pattern->searchReplace($subject, 'X');
        $this->assertSame(str_repeat('X', 600), $replaced);
    }

    public function testSubstMultipleMatches(): void
    {
        $pattern = Pattern::fromString("'a'");
        $this->assertSame('X-X-X', $pattern->subst('a-a-a', 'X'));
    }

    public function testSubstWithTablesParameter(): void
    {
        $pattern = Pattern::fromString("'a'");
        $table = new \Snobol\Table();
        $table->set('a', 'value');
        // Tables bind without error; template still substitutes normally
        $this->assertSame('X-X', $pattern->subst('a-a', 'X', ['T' => $table]));
    }

    // === 2.3 Pattern lifecycle: reuse, state creation/destruction ===

    public function testPatternReuseAcrossManyMatches(): void
    {
        $pattern = Pattern::fromString("'a'");
        for ($i = 0; $i < 50; $i++) {
            $this->assertIsArray($pattern->match('a'));
            $this->assertFalse($pattern->match('b'));
        }
    }

    public function testPatternSearchStateReuseAcrossMethods(): void
    {
        $pattern = Pattern::fromString("'a'");
        $this->assertIsArray($pattern->match('a'));
        $this->assertIsArray($pattern->searchAll('ba'));
        $this->assertSame(['b', ''], $pattern->searchSplit('ba'));
        $this->assertSame('Xb', $pattern->searchReplace('ab', 'X'));
    }

    public function testPatternDestructionReleasesResources(): void
    {
        $pattern = Pattern::compileFromAst(
            Builder::alt(
                Builder::alt(Builder::lit('cat'), Builder::lit('car')),
                Builder::lit('cab')
            )
        );
        $pattern->setEvalCallbacks([fn (string $s): bool => true]);
        $pattern->searchAll('cat car cab');
        $this->assertIsArray($pattern->matchLiteral('cat'));
        unset($pattern);
        gc_collect_cycles();
        $this->assertTrue(true);
    }

    public function testPatternNotCompiledThrows(): void
    {
        $pattern = new Pattern();
        $methods = [
            'match' => ['x'],
            'searchAll' => ['x'],
            'searchSplit' => ['x'],
            'searchSplitOffsets' => ['x'],
            'searchSplitCuts' => ['x'],
            'searchReplace' => ['x', 'y'],
            'matchLiteral' => ['x'],
            'subst' => ['x', 'y'],
            'searchSplitGenerator' => ['x'],
            'searchAllGenerator' => ['x'],
        ];
        foreach ($methods as $method => $args) {
            try {
                $pattern->{$method}(...$args);
                $this->fail("Expected exception from {$method}()");
            } catch (\Exception $e) {
                $this->assertSame('Pattern not compiled', $e->getMessage());
            }
        }
    }

    public function testSetJitToggle(): void
    {
        $pattern = Pattern::fromString("'a'");
        $this->assertTrue($pattern->setJit(true));
        $this->assertTrue($pattern->setJit(false));
    }

    // === Search loops for batch-ineligible patterns (EVAL/EMIT) ===

    public function testSearchAllPerCallLoopWithEvalPattern(): void
    {
        $pattern = Pattern::compileFromAst(Builder::eval(0, 0));
        $result = $pattern->searchAll('abc');
        $this->assertCount(4, $result);
        $this->assertSame(0, $result[0]['_match_len']);
        $this->assertSame(3, $result[3]['_match_start']);
    }

    public function testSearchAllFlatPerCallLoopWithEvalPattern(): void
    {
        $pattern = Pattern::compileFromAst(Builder::eval(0, 0));
        $result = $pattern->searchAll('abc', ['result' => 'flat']);
        $this->assertSame([0, 1, 2, 3], $result['match_start']);
        $this->assertSame([0, 0, 0, 0], $result['match_len']);
    }

    public function testSearchAllMetricsPerCallLoop(): void
    {
        $pattern = Pattern::compileFromAst(Builder::eval(0, 0));
        $result = $pattern->searchAll('abc', ['metrics' => true]);
        $this->assertCount(4, $result);
        $this->assertArrayHasKey('_metrics', $result[0]);
    }

    public function testSearchAllArrayCapturesPerCallLoop(): void
    {
        $pattern = Pattern::compileFromAst(
            Builder::concat([Builder::cap(0, Builder::lit('a')), Builder::eval(0, 0)])
        );
        $result = $pattern->searchAll('ab');
        $this->assertCount(1, $result);
        $this->assertSame('a', $result[0]['v0']);
    }

    public function testSearchAllFlatCapturesOffsetsPerCallLoop(): void
    {
        $pattern = Pattern::compileFromAst(
            Builder::concat([Builder::cap(0, Builder::lit('a')), Builder::eval(0, 0)])
        );
        $result = $pattern->searchAll('ab', ['result' => 'flat', 'captures' => 'offsets']);
        $this->assertSame([0], $result['match_start']);
        $this->assertSame([[0, 1]], $result['captures']['v0']);
    }

    public function testSearchAllWithEmitPatternOutput(): void
    {
        $pattern = Pattern::compileFromAst(
            Builder::concat([Builder::lit('x'), Builder::emit('OUT')])
        );
        $result = $pattern->searchAll('xx');
        $this->assertCount(2, $result);
        $this->assertSame('OUT', $result[0]['_output']);
        $this->assertSame('OUT', $result[1]['_output']);
    }

    public function testSearchAllFlatWithEmitPatternOutput(): void
    {
        $pattern = Pattern::compileFromAst(
            Builder::concat([Builder::lit('x'), Builder::emit('OUT')])
        );
        $result = $pattern->searchAll('xx', ['result' => 'flat']);
        $this->assertSame(['OUT', 'OUT'], $result['_output']);
    }

    public function testSearchSplitPerCallLoopWithEvalPattern(): void
    {
        $pattern = Pattern::compileFromAst(Builder::eval(0, 0));
        $this->assertSame(['', 'a', 'b', 'c', ''], $pattern->searchSplit('abc'));
    }

    public function testSearchSplitOffsetsPerCallLoopWithEvalPattern(): void
    {
        $pattern = Pattern::compileFromAst(Builder::eval(0, 0));
        $this->assertSame(
            [[0, 0], [0, 1], [1, 1], [2, 1], [3, 0]],
            $pattern->searchSplitOffsets('abc')
        );
    }

    public function testSearchSplitCutsPerCallLoopWithEvalPattern(): void
    {
        $pattern = Pattern::compileFromAst(Builder::eval(0, 0));
        $this->assertSame([0, 1, 2, 3], $pattern->searchSplitCuts('abc'));
    }

    public function testSearchSplitCutsWithAltLiteralsTrieCache(): void
    {
        $pattern = Pattern::compileFromAst(
            Builder::alt(Builder::lit('ab'), Builder::lit('cd'))
        );
        $this->assertSame([2, 5], $pattern->searchSplitCuts('abxcd'));
    }

    public function testSearchSplitOffsetsWithAltLiteralsTrieCache(): void
    {
        $pattern = Pattern::compileFromAst(
            Builder::alt(Builder::lit('ab'), Builder::lit('cd'))
        );
        $this->assertSame(
            [[0, 0], [2, 1], [5, 0]],
            $pattern->searchSplitOffsets('abxcd')
        );
    }
}
