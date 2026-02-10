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
