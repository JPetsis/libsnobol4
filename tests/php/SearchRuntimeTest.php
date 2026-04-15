<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Pattern;
use Snobol\PatternHelper;

/**
 * Tests verifying that the core search runtime (searchAll / searchSplit /
 * searchReplace / subst) produces results identical to the legacy helper-loop
 * behaviour for non-overlapping matches, split segments, and replacement results.
 */
class SearchRuntimeTest extends TestCase
{
    // -----------------------------------------------------------------------
    // matchAll / Pattern::searchAll
    // -----------------------------------------------------------------------

    public function testMatchAllSingleMatch(): void
    {
        $results = PatternHelper::matchAll("'fox'", 'the quick brown fox');
        $this->assertCount(1, $results);
        $this->assertSame('', $results[0]['_output']);
    }

    public function testMatchAllMultipleNonOverlapping(): void
    {
        $results = PatternHelper::matchAll("','", 'a,b,c,d');
        // Three commas → three matches
        $this->assertCount(3, $results);
    }

    public function testMatchAllNoMatch(): void
    {
        $results = PatternHelper::matchAll("'z'", 'abcdefg');
        $this->assertCount(0, $results);
    }

    public function testMatchAllCaptures(): void
    {
        $results = PatternHelper::matchAll("SPAN('abc') $ v0", 'aaabbbccc xyzabc');
        $this->assertGreaterThanOrEqual(1, count($results));
        // First capture should be a non-empty span of abc-chars
        $this->assertNotEmpty($results[0]['v0']);
        $this->assertMatchesRegularExpression('/^[abc]+$/', $results[0]['v0']);
    }

    public function testMatchAllEmptySubject(): void
    {
        $results = PatternHelper::matchAll("','", '');
        $this->assertCount(0, $results);
    }

    // -----------------------------------------------------------------------
    // split / Pattern::searchSplit
    // -----------------------------------------------------------------------

    public function testSplitOnComma(): void
    {
        $parts = PatternHelper::split("','", 'a,b,c');
        $this->assertSame(['a', 'b', 'c'], $parts);
    }

    public function testSplitOnWhitespace(): void
    {
        $parts = PatternHelper::split("' '", 'hello world foo');
        $this->assertSame(['hello', 'world', 'foo'], $parts);
    }

    public function testSplitNoDelimiter(): void
    {
        $parts = PatternHelper::split("','", 'nodelmiter');
        $this->assertSame(['nodelmiter'], $parts);
    }

    public function testSplitEmptySubject(): void
    {
        $parts = PatternHelper::split("','", '');
        $this->assertSame([''], $parts);
    }

    public function testSplitDelimiterAtStart(): void
    {
        $parts = PatternHelper::split("','", ',a,b');
        $this->assertSame(['', 'a', 'b'], $parts);
    }

    public function testSplitDelimiterAtEnd(): void
    {
        $parts = PatternHelper::split("','", 'a,b,');
        $this->assertSame(['a', 'b', ''], $parts);
    }

    public function testSplitConsistencyWithNative(): void
    {
        // Verify split result matches explode() for a simple case
        $subject = 'one,two,three,four';
        $snobol = PatternHelper::split("','", $subject);
        $native = explode(',', $subject);
        $this->assertSame($native, $snobol);
    }

    // -----------------------------------------------------------------------
    // replace / Pattern::searchReplace
    // -----------------------------------------------------------------------

    public function testReplaceLiteral(): void
    {
        $result = PatternHelper::replace("' '", '_', 'hello world');
        $this->assertSame('hello_world', $result);
    }

    public function testReplaceAllOccurrences(): void
    {
        $result = PatternHelper::replace("'a'", 'X', 'banana');
        $this->assertSame('bXnXnX', $result);
    }

    public function testReplaceNoMatch(): void
    {
        $result = PatternHelper::replace("'z'", 'X', 'banana');
        $this->assertSame('banana', $result);
    }

    public function testReplaceEmptySubject(): void
    {
        $result = PatternHelper::replace("'a'", 'X', '');
        $this->assertSame('', $result);
    }

    public function testReplaceConsistencyWithStr_replace(): void
    {
        $subject = 'The quick brown fox jumps over the lazy dog.';
        $snobol = PatternHelper::replace("'o'", '0', $subject);
        $native = str_replace('o', '0', $subject);
        $this->assertSame($native, $snobol);
    }

    // -----------------------------------------------------------------------
    // subst / Pattern::subst (template-based substitution)
    // -----------------------------------------------------------------------

    public function testSubstSimpleCapture(): void
    {
        $pattern = Pattern::fromString("ANY('aeiou') $ v0");
        $result = $pattern->subst('hello', '[$v0]');
        // 'e' is the first vowel; only it should be bracketed
        $this->assertStringContainsString('[', $result);
    }

    public function testSubstAllVowels(): void
    {
        $pattern = Pattern::fromString("ANY('aeiou') $ v0");
        $result = $pattern->subst('aeiou', '($v0)');
        $this->assertSame('(a)(e)(i)(o)(u)', $result);
    }

    public function testSubstNoMatch(): void
    {
        $pattern = Pattern::fromString("'z'");
        $result = $pattern->subst('hello', 'X');
        $this->assertSame('hello', $result);
    }

    public function testSubstEmptyReplacement(): void
    {
        $pattern = Pattern::fromString("','");
        $result = $pattern->subst('a,b,c', '');
        $this->assertSame('abc', $result);
    }

    public function testSubstReplaceSpaces(): void
    {
        $pattern = Pattern::fromString("' '");
        $result = $pattern->subst('hello world foo', '_');
        $this->assertSame('hello_world_foo', $result);
    }

    // -----------------------------------------------------------------------
    // Regression: non-overlapping semantics preserved
    // -----------------------------------------------------------------------

    public function testNonOverlappingSemantics(): void
    {
        // Pattern 'aa' in 'aaaa' should match twice non-overlappingly: positions 0-2, 2-4
        $results = PatternHelper::matchAll("'aa'", 'aaaa');
        $this->assertCount(2, $results);
    }

    public function testSplitMultiCharDelimiter(): void
    {
        $parts = PatternHelper::split("'--'", 'a--b--c');
        $this->assertSame(['a', 'b', 'c'], $parts);
    }
}

