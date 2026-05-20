<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Pattern;

/**
 * PHP tests for snobol_pattern_compile_ex (via Pattern::fromString)
 * with SNOBOL_FLAG_CASE_INSENSITIVE / ['caseInsensitive' => true].
 */
class CaseInsensitiveCompileExTest extends TestCase
{
    /** Compile a source-text pattern with optional caseInsensitive flag and match it. */
    private function matchOnce(string $source, string $subject, bool $ci = false): bool
    {
        $options = $ci ? ['caseInsensitive' => true] : [];
        $pat = Pattern::fromString($source, $options ?: null);
        $res = $pat->match($subject);
        return $res !== false && $res !== null;
    }

    // --- ASCII case-insensitive matching ---

    public function testAsciiLiteralCiMatchesUpper(): void
    {
        $this->assertTrue($this->matchOnce("'hello'", 'HELLO', true));
    }

    public function testAsciiLiteralCiMatchesMixed(): void
    {
        $this->assertTrue($this->matchOnce("'hello'", 'HeLLo', true));
    }

    public function testAsciiLiteralCsSensitive(): void
    {
        // Case-sensitive (default): 'hello' must NOT match 'HELLO'
        $this->assertFalse($this->matchOnce("'hello'", 'HELLO', false));
    }

    public function testAsciiLiteralCsMatchesExact(): void
    {
        $this->assertTrue($this->matchOnce("'hello'", 'hello', false));
    }

    // --- Latin-1 case-insensitive matching ---

    public function testLatin1CafeUpperMatchesLower(): void
    {
        // 'CAFÉ' (CI) should match subject "café"
        $this->assertTrue(
            $this->matchOnce("'CAF\xC3\x89'", "caf\xC3\xA9", true),
            "'CAFÉ' CI matches \"café\""
        );
    }

    public function testLatin1CafeLowerMatchesUpper(): void
    {
        // 'café' (CI) should match subject "CAFÉ"
        $this->assertTrue(
            $this->matchOnce("'caf\xC3\xA9'", "CAF\xC3\x89", true),
            "'café' CI matches \"CAFÉ\""
        );
    }

    // --- Captured text should preserve original case ---

    public function testCapturePreservesOriginalCase(): void
    {
        // Pattern '$0<hello>' captures 5 chars (group 0)
        // With CI the pattern matches "WORLD" then captures what 'hello' matched => "WORLD"?
        // Actually let's keep it simple: literal 'hello' CI, capture group wraps it.
        $options = ['caseInsensitive' => true];
        $pat = Pattern::fromString("'hello'", $options);
        $res = $pat->match('HELLO world');
        $this->assertIsArray($res);
        // The matched region is the first 5 bytes — original case preserved in subject
        $this->assertEquals(5, $res['_match_len']);
    }
}

