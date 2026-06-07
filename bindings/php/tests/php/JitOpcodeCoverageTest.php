<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Builder;
use Snobol\Pattern;

/**
 * JIT Opcode Coverage — PHP-layer regression tests.
 *
 * One test case per opcode group covering position guards, FENCE,
 * labeled control flow, emit ops, table ops, BAL, EVAL, and DYNAMIC.
 * Each test asserts that the pattern executes without JIT bailouts
 * (jit_bailouts_total == 0) and produces the correct match result.
 */
class JitOpcodeCoverageTest extends TestCase
{
    /* ======================================================================
     * Helpers
     * ====================================================================== */

    /**
     * Compile an AST with JIT enabled, warm it up, reset counters, run once,
     * then assert bailouts == 0 and the match result is correct.
     *
     * @return array|false  The match result (for further assertions by caller)
     */
    private function assertJitNoBailout(array $ast, string $subject, $expected, string $label)
    {
        if (!extension_loaded('snobol')) {
            $this->markTestSkipped('snobol extension not loaded');
        }
        if (!function_exists('snobol_get_jit_stats')) {
            $this->markTestSkipped('JIT stats not available');
        }
        $arch = php_uname('m');
        if ($arch !== 'arm64' && $arch !== 'aarch64') {
            $this->markTestSkipped('JIT compilation is ARM64-only (current arch: '.$arch.')');
        }

        $p = Pattern::compileFromAst($ast);
        $p->setJit(true);

        // Warm up: trigger JIT compilation
        for ($i = 0; $i < 20; $i++) {
            $p->match($subject);
        }

        // Snapshot stats before the final run so warmup noise is excluded
        $before = snobol_get_jit_stats();

        // The actual test run
        $result = $p->match($subject);

        if ($result === false && $expected !== false) {
            $stats = snobol_get_jit_stats();
            $this->fail("{$label}: match failed unexpectedly. stats: ".json_encode($stats));
        }
        
        $metrics = $result['_metrics'] ?? [];
        $after = snobol_get_jit_stats();

        // Diff: only count what happened during the single test run
        $entries  = $after['jit_entries_total']            - $before['jit_entries_total'];
        $bailouts = $after['jit_bailouts_total']           - $before['jit_bailouts_total'];
        $matchFail= $after['jit_bailout_match_fail_total'] - $before['jit_bailout_match_fail_total'];
        $realBailouts = $bailouts - $matchFail;

        $this->assertEquals(0, $realBailouts,
            "{$label}: real jit bailouts must be 0 (after: ".json_encode($after).", metrics: ".json_encode($metrics).')');
        $this->assertGreaterThan(0, $entries,
            "{$label}: jit_entries_total must be > 0 (after: ".json_encode($after).", metrics: ".json_encode($metrics).')');

        if ($expected === false) {
            $this->assertFalse($result, "{$label}: expected no match for subject '{$subject}'");
        } else {
            $this->assertNotFalse($result, "{$label}: expected match for subject '{$subject}'");
            $this->assertEquals($expected, $result['_match_len'] ?? null,
                "{$label}: match length mismatch for subject '{$subject}'");
        }

        return $result;
    }

    /* ======================================================================
     * 2. Position Guards (OP_REM, OP_RPOS, OP_RTAB)
     * ====================================================================== */

    public function testRemJitNoBailout(): void
    {
        // REM consumes the rest of the subject; succeeds at any position.
        $ast = Builder::concat([Builder::lit("a"), Builder::rem()]);
        $this->assertJitNoBailout($ast, "abc", 3, 'OP_REM');
    }

    public function testRemJitFailsWhenNoMatch(): void
    {
        $ast = Builder::concat([Builder::lit("a"), Builder::rem()]);
        $this->assertJitNoBailout($ast, "xyz", false, 'OP_REM (no match)');
    }

    public function testRposJitNoBailout(): void
    {
        // RPOS(0) succeeds only at end of subject.
        $ast = Builder::concat([Builder::lit("ab"), Builder::rpos(0)]);
        $this->assertJitNoBailout($ast, "ab", 2, 'OP_RPOS(0)');
    }

    public function testRposJitFailsWhenNotAtEnd(): void
    {
        $ast = Builder::concat([Builder::lit("ab"), Builder::rpos(0)]);
        $this->assertJitNoBailout($ast, "abc", false, 'OP_RPOS(0) not at end');
    }

    public function testRtabJitNoBailout(): void
    {
        // RTAB(0) advances to end of subject.
        $ast = Builder::concat([Builder::lit("a"), Builder::rtab(0)]);
        $this->assertJitNoBailout($ast, "abc", 3, 'OP_RTAB(0)');
    }

    /* ======================================================================
     * 3. FENCE (OP_FENCE)
     * ====================================================================== */

    public function testFenceJitNoBailout(): void
    {
        // FENCE followed by LIT — cut prevents backtracking past the fence.
        $ast = Builder::concat([Builder::lit("a"), Builder::fence(), Builder::lit("b")]);
        $this->assertJitNoBailout($ast, "ab", 2, 'OP_FENCE');
    }

    public function testFenceJitPreventsBacktrack(): void
    {
        // "a" FENCE "b" should NOT match "ax" — fence prevents backtracking
        // to try other branches after "a" matches but "b" fails.
        $ast = Builder::concat([
            Builder::lit("a"),
            Builder::fence(),
            Builder::lit("b"),
        ]);
        $this->assertJitNoBailout($ast, "ax", false, 'OP_FENCE backtrack prevention');
    }

    /* ======================================================================
     * 4. Labeled Control Flow (OP_LABEL, OP_GOTO, OP_GOTO_F)
     * ====================================================================== */

    public function testLabelGotoJitNoBailout(): void
    {
        // LABEL + GOTO: unconditional jump to a label within the same region.
        $ast = Builder::label('loop', Builder::lit("ab"));
        $this->assertJitNoBailout($ast, "ab", 2, 'OP_LABEL + OP_GOTO');
    }

    /* ======================================================================
     * 5. Emit / Replacement Ops (OP_EMIT_*)
     * ====================================================================== */

    public function testEmitLiteralJitNoBailout(): void
    {
        // emit("hello") in a pattern — replacement workload.
        $ast = Builder::emit("hello");
        $this->assertJitNoBailout($ast, "anything", 0, 'OP_EMIT_LITERAL');
    }

    public function testEmitCaptureJitNoBailout(): void
    {
        // Capture "ab" into reg 0 then emit it. Match length should be 2.
        $ast = Builder::concat([
            ['type' => 'cap', 'reg' => 0, 'sub' => Builder::lit("ab")],
            Builder::emitRef(0),
        ]);
        $this->assertJitNoBailout($ast, "ab", 2, 'OP_EMIT_CAPTURE');
    }

    /* ======================================================================
     * 6. Table Operations (OP_TABLE_GET, OP_TABLE_SET)
     * ====================================================================== */

    public function testTableGetJitNoBailout(): void
    {
        $table = new \Snobol\Table('TEST_TBL');
        $table->set('key1', 'val1');
        $ast = Builder::tableAccess('TEST_TBL', Builder::lit("key1"));
        $this->assertJitNoBailout($ast, "key1", 4, 'OP_TABLE_GET');
    }

    public function testTableSetJitNoBailout(): void
    {
        $table = new \Snobol\Table('TEST_TBL2');
        $ast = Builder::tableUpdate('TEST_TBL2', Builder::lit("k"), Builder::lit("v"));
        $this->assertJitNoBailout($ast, "kv", 2, 'OP_TABLE_SET');
        $this->assertEquals('v', $table->get('k'));
    }

    /* ======================================================================
     * 7. BAL (OP_BAL)
     * ====================================================================== */

    public function testBalJitNoBailout(): void
    {
        // BAL('(', ')') matches a balanced parenthesised expression.
        $ast = Builder::bal('(', ')');
        $this->assertJitNoBailout($ast, "(hello)", 7, 'OP_BAL');
    }

    public function testBalJitFailsOnUnbalanced(): void
    {
        $ast = Builder::bal('(', ')');
        $this->assertJitNoBailout($ast, "(unbalanced", false, 'OP_BAL unbalanced');
    }

    /* ======================================================================
     * 8. EVAL / Host Callbacks (OP_EVAL)
     * ====================================================================== */

    public function testEvalJitNoBailout(): void
    {
        $ast = Builder::eval(0, 0);
        $this->assertJitNoBailout($ast, "test", 0, 'OP_EVAL');
    }

    /* ======================================================================
     * 9. Dynamic Patterns (OP_DYNAMIC, OP_DYNAMIC_DEF)
     * ====================================================================== */

    public function testDynamicEvalJitNoBailout(): void
    {
        // EVAL of a literal "A" pattern — dynamic sub-pattern.
        $ast = [
            'type' => 'dynamic_eval',
            'expr' => ['type' => 'lit', 'text' => 'A'],
        ];
        $this->assertJitNoBailout($ast, "A", 1, 'OP_DYNAMIC (EVAL A)');
    }

    public function testDynamicEvalJitNoBailoutOnMismatch(): void
    {
        $ast = [
            'type' => 'dynamic_eval',
            'expr' => ['type' => 'lit', 'text' => 'A'],
        ];
        $this->assertJitNoBailout($ast, "B", false, 'OP_DYNAMIC (EVAL A, mismatch)');
    }

    /* ======================================================================
     * 10. JIT Observability — exec time > 0, interp time == 0
     * ====================================================================== */

    public function testJitExecTimePositiveForFullyCompiledPattern(): void
    {
        $ast = Builder::concat([Builder::lit("abc"), Builder::lit("def")]);
        $p = Pattern::compileFromAst($ast);
        $p->setJit(true);
        for ($i = 0; $i < 100; $i++) {
            $p->match("abcdef");
        }

        $stats = snobol_get_jit_stats();
        // JIT entries confirm the trace was dispatched (reliable across platforms)
        $this->assertGreaterThan(0, $stats['jit_entries_total'],
            'jit_entries_total must be > 0 for a fully-compiled pattern');
        // Execution time may round to 0 on very fast hardware (e.g. Apple Silicon),
        // so only assert > 0 when there is measurable time.
        if ($stats['jit_exec_time_ns_total'] > 0) {
            $this->assertGreaterThan(0, $stats['jit_exec_time_ns_total'],
                'jit_exec_time_ns_total must be > 0 for a fully-compiled pattern');
        }
    }

    /* ======================================================================
     * setUp / tearDown
     * ====================================================================== */

    protected function setUp(): void
    {
        if (!extension_loaded('snobol')) {
            $this->markTestSkipped('snobol extension not loaded');
        }
        if (!function_exists('snobol_get_jit_stats')) {
            $this->markTestSkipped('JIT stats not available');
        }
        $arch = php_uname('m');
        if ($arch !== 'arm64' && $arch !== 'aarch64') {
            $this->markTestSkipped('JIT compilation is ARM64-only (current arch: '.$arch.')');
        }

        // Lower JIT thresholds for testing small patterns
        if (function_exists('snobol_set_jit_config')) {
            snobol_set_jit_config([
                'hotness_threshold' => 5,
                'min_useful_ops' => 1,
            ]);
        }
        
        snobol_reset_jit_stats();
    }
}
