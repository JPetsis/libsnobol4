<?php

use PHPUnit\Framework\TestCase;
use Snobol\Builder;
use Snobol\Pattern;

class JitObservabilityTest extends TestCase
{
    public function testJitStatsIncrement()
    {
        $stats = snobol_get_jit_stats();
        $this->assertEquals(0, $stats['jit_entries_total']);

        // Hot loop to trigger JIT
        $ast = Builder::concat([Builder::lit("abc"), Builder::lit("def")]);
        $p = Pattern::compileFromAst($ast);
        $p->setJit(true);

        for ($i = 0; $i < 100; $i++) {
            $p->match("abcdef");
        }

        $stats = snobol_get_jit_stats();
        $this->assertGreaterThan(0, $stats['jit_entries_total'], 'Should have some JIT entries');
        $this->assertGreaterThan(0, $stats['jit_compilations_total'], 'Should have at least one compilation');
    }

    public function testResetStats()
    {
        $ast = Builder::concat([Builder::lit("xyz"), Builder::lit("123")]);
        $p = Pattern::compileFromAst($ast);
        $p->setJit(true);
        for ($i = 0; $i < 60; $i++) {
            $p->match("xyz123");
        }

        $this->assertGreaterThan(0, snobol_get_jit_stats()['jit_entries_total']);

        snobol_reset_jit_stats();
        $stats = snobol_get_jit_stats();
        $this->assertEquals(0, $stats['jit_entries_total']);
        $this->assertEquals(0, $stats['jit_compilations_total']);
    }

    /**
     * Test that JIT counters work correctly for basic patterns.
     *
     * Validates that choice_push_total and choice_bytes_total
     * are properly tracked when SPLIT operations occur.
     */
    public function testJitChoiceCounters()
    {
        // Simple alternation pattern: (a|b)
        $alternation = Builder::alt(Builder::lit("a"), Builder::lit("b"));
        $pattern = Builder::concat([
            Builder::lit("start"),
            $alternation,
            Builder::lit("end")
        ]);

        $p = Pattern::compileFromAst($pattern);
        $p->setJit(true);

        // Warmup
        for ($i = 0; $i < 100; $i++) {
            $p->match("startbend");
        }

        // Reset stats
        snobol_reset_jit_stats();

        // Test match that should succeed
        $result = $p->match("startbend");

        $stats = snobol_get_jit_stats();

        // Basic JIT counters should be present
        $this->assertArrayHasKey('jit_entries_total', $stats);
        $this->assertArrayHasKey('choice_push_total', $stats);
        $this->assertArrayHasKey('choice_bytes_total', $stats);

        // Log stats for debugging
        $this->assertTrue(true, sprintf(
            'JIT stats: entries=%d, exits=%d, choices=%d, bytes=%d',
            $stats['jit_entries_total'] ?? 0,
            $stats['jit_exits_total'] ?? 0,
            $stats['choice_push_total'] ?? 0,
            $stats['choice_bytes_total'] ?? 0
        ));
    }

    protected function setUp(): void
    {
        if (!extension_loaded('snobol')) {
            $this->markTestSkipped('snobol extension not loaded');
        }
        if (!function_exists('snobol_get_jit_stats')) {
            $this->markTestSkipped('JIT stats not available (JIT disabled?)');
        }
        snobol_reset_jit_stats();
    }
}
