<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Builder;
use Snobol\Pattern;
use Snobol\PatternHelper;

class UnicodeTest extends TestCase
{
    public function testUnicodeRange(): void
    {
        // Greek alpha to omega range
        // Note: Builder::span takes a string of characters, not a range syntax "a-z"
        // So we must provide all chars if we want them all, OR rely on the fact that we can construct ranges manually?
        // Wait, Builder::span takes a string "set".
        // The compiler iterates the string and adds ranges.
        // If I pass "αω", I get range [α,α] and [ω,ω]. I don't get beta.
        // So to test ranges properly with SPAN, I should provide a string with adjacent chars?
        // Or does Builder::span support ranges? No, implementation treats string as set of chars.
        // But the internal storage IS ranges.
        // So if I provide "abc", I get range 'a'-'c' internally.

        $p = Pattern::compileFromAst(Builder::span("αβγ"));

        $res = $p->match("βγα");
        $this->assertIsArray($res);
        $this->assertEquals(6, $res['_match_len']); // 3 chars * 2 bytes
    }

    public function testCaseInsensitiveAscii(): void
    {
        $p = Pattern::compileFromAst(
            Builder::span("abc"),
            ['caseInsensitive' => true]
        );

        $res = $p->match("ABC");
        $this->assertIsArray($res);
        $this->assertEquals(3, $res['_match_len']);
    }

    public function testCaseInsensitiveLatin1(): void
    {
        // 'à' (U+00E0) matches 'À' (U+00C0)
        $p = Pattern::compileFromAst(
            Builder::any("à"),
            ['caseInsensitive' => true]
        );

        $res = $p->match("À");
        $this->assertIsArray($res);
        $this->assertEquals(2, $res['_match_len']);
    }

    public function testMultiByteSpan(): void
    {
        // Emojis: 😀 (U+1F600), 😁 (U+1F601), 😂 (U+1F602)
        $p = Pattern::compileFromAst(Builder::span("😀😁😂"));

        $res = $p->match("😂😁😀");
        $this->assertIsArray($res);
        $this->assertEquals(12, $res['_match_len']); // 3 * 4 bytes
    }

    public function testMixedByteSizes(): void
    {
        // 'a' (1 byte), 'α' (2 bytes), '€' (3 bytes), '😀' (4 bytes)
        $set = "aα€😀";
        $p = Pattern::compileFromAst(Builder::span($set));

        $res = $p->match("😀€αa");
        $this->assertIsArray($res);
        $this->assertEquals(10, $res['_match_len']); // 4+3+2+1
    }

    public function testHelperWithOptions(): void
    {
        $res = PatternHelper::matchOnce(
            Builder::span("abc"),
            "ABC",
            ['caseInsensitive' => true]
        );
        $this->assertIsArray($res);
        $this->assertEquals(3, $res['_match_len']);
    }
}
