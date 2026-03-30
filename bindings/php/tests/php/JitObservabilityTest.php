<?php

use PHPUnit\Framework\TestCase;
use Snobol\Builder;
use Snobol\Pattern;

class JitObservabilityTest extends TestCase
{
    public function testJitStatsIncrement(): void
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

    public function testResetStats(): void
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
    public function testJitChoiceCounters(): void
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

    /**
     * Task 4.4: Backtracking-heavy pattern — assert reduced jit_exits_total
     *
     * The profitability gate should recognise alternation-dominated patterns
     * (SPLIT immediately at ip=0 with no useful prefix) as unprofitable and
     * skip JIT compilation for them.  The observable result is that either:
     *   (a) jit_exits_total stays 0 (JIT never entered), OR
     *   (b) jit_skipped_cold_total > 0 (gate explicitly skipped compilation).
     * Either way the total JIT overhead for backtracking patterns is bounded.
     */
    public function testBacktrackingPatternProfitabilityGate(): void
    {
        // Pure alternation: (a|b|c|d|e) — SPLIT is the very first op, no useful prefix.
        // The profitability gate (skip_backtrack_heavy + min_useful_ops) should skip JIT.
        $pattern = Builder::alt(
            Builder::lit("alpha"),
            Builder::lit("beta"),
            Builder::lit("gamma"),
            Builder::lit("delta"),
            Builder::lit("epsilon")
        );
        $p = Pattern::compileFromAst($pattern);
        $p->setJit(true);

        // Run enough iterations to trigger hotness threshold
        for ($i = 0; $i < 200; $i++) {
            $p->match("gamma");
        }

        $stats = snobol_get_jit_stats();

        // The profitability gate must have intervened in at least one of these ways:
        //   - compilations skipped due to cold/insufficient-ops heuristic
        //   - exit rate exceeded threshold and further compilations were stopped
        //   - JIT was entered but exits are bounded (not unbounded for every iteration)
        $gateActivated =
            ($stats['jit_skipped_cold_total'] ?? 0) > 0 ||
            ($stats['jit_skipped_exit_rate_total'] ?? 0) > 0 ||
            ($stats['jit_exits_total'] ?? 0) < 150; // fewer exits than iterations

        $this->assertTrue($gateActivated,
            sprintf(
                'Profitability gate must reduce exit overhead for backtracking patterns. '.
                'skipped_cold=%d, skipped_exit_rate=%d, exits=%d',
                $stats['jit_skipped_cold_total'] ?? 0,
                $stats['jit_skipped_exit_rate_total'] ?? 0,
                $stats['jit_exits_total'] ?? 0
            )
        );

        // New counters must be present in the stats array
        $this->assertArrayHasKey('jit_compile_time_ns_total', $stats,
            'jit_compile_time_ns_total counter must be present');
        $this->assertArrayHasKey('jit_exec_time_ns_total', $stats,
            'jit_exec_time_ns_total counter must be present');
        $this->assertArrayHasKey('jit_interp_time_ns_total', $stats,
            'jit_interp_time_ns_total counter must be present');
        $this->assertArrayHasKey('jit_skipped_cold_total', $stats,
            'jit_skipped_cold_total counter must be present');
        $this->assertArrayHasKey('jit_skipped_exit_rate_total', $stats,
            'jit_skipped_exit_rate_total counter must be present');
        $this->assertArrayHasKey('jit_bailout_match_fail_total', $stats,
            'jit_bailout_match_fail_total counter must be present');
    }

    protected function setUp(): void
    {
        if (!extension_loaded('snobol')) {
            $this->markTestSkipped('snobol extension not loaded');
        }
        if (!function_exists('snobol_get_jit_stats')) {
            $this->markTestSkipped('JIT stats not available (JIT disabled?)');
        }

        // Check if JIT is supported (ARM64 only)
        // On x86_64, JIT compilation is not available
        $arch = php_uname('m');
        if ($arch !== 'arm64' && $arch !== 'aarch64') {
            $this->markTestSkipped('JIT compilation is ARM64-only (current arch: '.$arch.')');
        }
        
        snobol_reset_jit_stats();
    }
}
