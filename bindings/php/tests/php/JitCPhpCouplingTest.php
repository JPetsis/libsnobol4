<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;

/**
 * Regression guard: the C probe and the PHP probe must move together.
 *
 * The diagnostic-probe change established two complementary attribution
 * tools — a C-side probe that measures the engine's per-iteration cost
 * (bench/c/bench_probe.c) and a PHP-side probe that measures the full
 * user-facing cost (bench/php/probe.php). The PHP probe includes the
 * binding's overhead (memset(VM,0), add_next_index_stringl, PHP↔C
 * crossing, etc.).
 *
 * This test runs both probes and asserts:
 *
 *   1. Both probes produce non-zero iterations for every scenario.
 *   2. Search-mode scenarios in the PHP probe show jit_entries > 0
 *      (proves the PHP binding exercises the same JIT machinery).
 *   3. The PHP cost for the tokenize scenario is at most 500x the C
 *      cost. This is a loose guard: today's ratio is around 100-300x
 *      on ddev. If the C path improves 5x and the PHP path stays the
 *      same, the ratio jumps 5x — the test catches that.
 *
 * The 500x threshold is intentionally generous. Tighten only after
 * collecting multiple data points; aggressive thresholds make the test
 * flaky on shared CI runners where the PHP extension has variable
 * warmup cost.
 */
class JitCPhpCouplingTest extends TestCase
{
    private const ITER_ENV = 'PROBE_ITERS';
    private const ITER_DEFAULT = 10000;   // smaller than probe default for fast test
    private const PHP_C_RATIO_MAX = 500.0; // loose upper bound

    /** @return array<int, array<string, mixed>>|null */
    private function runPhpProbe(): ?array
    {
        $probe = $this->resolvePhpProbe();
        if ($probe === null) {
            $this->markTestSkipped('PHP probe not found');
            return null;
        }

        $cmd = sprintf(
            '%s %s %s=%d 2>&1',
            escapeshellarg(PHP_BINARY),
            escapeshellarg($probe),
            self::ITER_ENV,
            self::ITER_DEFAULT
        );
        $out = shell_exec($cmd);
        if ($out === null) {
            $this->markTestSkipped('php probe execution failed');
            return null;
        }

        // The probe prints the table on stdout and JSON on stderr
        // (or vice-versa depending on PHP redirect handling). Find
        // the JSON block regardless of channel.
        $results = $this->extractJsonResults($out);
        if ($results === null) {
            $this->fail("PHP probe output did not contain a recognisable table or JSON.\n"
                . "First 500 chars of output:\n" . substr((string)$out, 0, 500));
        }
        return $results;
    }

    /** @return array<int, array<string, mixed>>|null */
    private function runCProbe(): ?array
    {
        $probe = $this->resolveCProbe();
        if ($probe === null) {
            $this->markTestSkipped('C probe not built (run `cmake -B build -DBUILD_BENCH_C=ON && cmake --build build --target snobol4_probe`)');
            return null;
        }

        $cmd = sprintf(
            '%s %s=%d 2>&1',
            escapeshellarg($probe),
            self::ITER_ENV,
            self::ITER_DEFAULT
        );
        $out = shell_exec($cmd);
        if ($out === null) {
            $this->markTestSkipped('c probe execution failed');
            return null;
        }

        return $this->parseCProbeTable((string)$out);
    }

    public function testBothProbesProduceValidOutput(): void
    {
        $php = $this->runPhpProbe();
        $c = $this->runCProbe();
        if ($php === null || $c === null) {
            return; // skipped
        }

        $this->assertNotEmpty($php, 'PHP probe produced no rows');
        $this->assertNotEmpty($c, 'C probe produced no rows');

        foreach ($php as $row) {
            $this->assertGreaterThan(0, $row['iters'],
                "PHP probe scenario '{$row['name']}' produced zero iters");
        }
        foreach ($c as $row) {
            $this->assertGreaterThan(0, $row['iters'],
                "C probe scenario '{$row['name']}' produced zero iters");
        }
    }

    public function testPhpSearchModeFiresJit(): void
    {
        $php = $this->runPhpProbe();
        if ($php === null) return;

        // The tokenize_php scenario uses searchSplit which exercises
        // the search-mode JIT path.
        $tokenize = null;
        foreach ($php as $row) {
            if ($row['name'] === 'tokenize_php') {
                $tokenize = $row;
                break;
            }
        }
        $this->assertNotNull($tokenize, 'PHP probe missing tokenize_php scenario');
        $this->assertGreaterThan(0, $tokenize['jit_entries'],
            'PHP searchSplit should fire the JIT — got 0 entries. '
            . 'Did the binding fail to enable search-mode JIT?');
    }

    public function testPhpC_ratio_within_bounds(): void
    {
        $php = $this->runPhpProbe();
        $c = $this->runCProbe();
        if ($php === null || $c === null) return;

        // Match scenarios by name. We compare the tokenize-like
        // scenarios: tokenize (C) and tokenize_php (PHP).
        $c_token = $this->findRow($c, 'tokenize');
        $php_token = $this->findRow($php, 'tokenize_php');
        if ($c_token === null || $php_token === null) {
            $this->markTestSkipped('tokenize scenario missing in one probe');
            return;
        }

        // Both probes report ns/iter. The PHP probe's `iters` is the
        // number of searchSplit calls (one per outer iter). The C
        // probe's `iters` is the total inner search calls. So we
        // compare total wall time for an equal number of outer iters
        // (which is the per-user-call cost).
        $c_ns = (int)$c_token['total_ns'];
        $php_ns = (int)$php_token['total_ns'];
        $this->assertGreaterThan(0, $c_ns, 'C probe tokenize total_ns is zero');
        $this->assertGreaterThan(0, $php_ns, 'PHP probe tokenize_php total_ns is zero');

        $ratio = $php_ns / $c_ns;
        $this->assertLessThanOrEqual(
            self::PHP_C_RATIO_MAX,
            $ratio,
            sprintf(
                "PHP/C tokenize ratio %.1fx exceeds guard %.1fx\n"
                . "  C total_ns:   %d (per %d iters, ns/iter=%d)\n"
                . "  PHP total_ns: %d (per %d iters, ns/iter=%d)\n"
                . "The binding is dominating. Either the C path regressed,"
                . " or the binding is silently broken.",
                $ratio, self::PHP_C_RATIO_MAX,
                $c_ns, (int)$c_token['iters'], (int)$c_token['ns_per_iter'],
                $php_ns, (int)$php_token['iters'], (int)$php_token['ns_per_iter']
            )
        );
    }

    public function testJitImprovesBothPaths(): void
    {
        // When SNOBOL_JIT=1 (the default in ddev), the search-mode
        // scenarios should show jit_entries > 0 in BOTH the C probe
        // and the PHP probe. This proves the binding is not silently
        // disabling JIT for search-mode work.
        $php = $this->runPhpProbe();
        $c = $this->runCProbe();
        if ($php === null || $c === null) return;

        $c_search = $this->findRow($c, 'tokenize');
        $php_search = $this->findRow($php, 'tokenize_php');
        if ($c_search === null || $php_search === null) {
            $this->markTestSkipped('tokenize scenario missing');
            return;
        }

        $this->assertGreaterThan(0, $c_search['jit_entries'],
            'C probe tokenize did not fire JIT — check SNOBOL_JIT=1');
        $this->assertGreaterThan(0, $php_search['jit_entries'],
            'PHP probe tokenize_php did not fire JIT — '
            . 'the binding may have disabled search-mode JIT');
    }

    // -----------------------------------------------------------------------
    // helpers
    // -----------------------------------------------------------------------

    /** @param array<int, array<string, mixed>> $rows */
    private function findRow(array $rows, string $name): ?array
    {
        foreach ($rows as $r) {
            if ($r['name'] === $name) return $r;
        }
        return null;
    }

    private function resolvePhpProbe(): ?string
    {
        $candidates = [
            __DIR__ . '/../../../bench/php/probe.php',
            '/var/www/bench/php/probe.php', // ddev
        ];
        foreach ($candidates as $p) {
            if (is_file($p)) return $p;
        }
        return null;
    }

    private function resolveCProbe(): ?string
    {
        $candidates = [
            __DIR__ . '/../../../build/bench/c/snobol4_probe',
            '/var/www/build/bench/c/snobol4_probe', // ddev default location
            __DIR__ . '/../../../../build/bench/c/snobol4_probe',
            '/usr/local/bin/snobol4_probe',         // ddev installed via build-c-probe
        ];
        foreach ($candidates as $p) {
            if (is_file($p) && is_executable($p)) return $p;
        }
        return null;
    }

    /**
     * The PHP probe writes the table to stdout and the JSON to stderr.
     * When `2>&1` merges them, we get one big blob. The JSON is the
     * last block (preceded by the table). Find it.
     *
     * @return array<int, array<string, mixed>>|null
     */
    private function extractJsonResults(string $out): ?array
    {
        $jsonStart = strrpos($out, '[');
        if ($jsonStart === false) return null;
        $json = substr($out, $jsonStart);
        $decoded = json_decode($json, true);
        return is_array($decoded) ? $decoded : null;
    }

    /**
     * Parse the C probe's table output. The C probe prints a table
     * with columns: scenario, ns/iter, iters, jit_ent, jit_bail,
     * s_entries, choice_p, choice_pop, exec_ns, interp_ns.
     *
     * @return array<int, array<string, mixed>>
     */
    private function parseCProbeTable(string $out): array
    {
        $rows = [];
        $in_table = false;
        foreach (preg_split('/\r?\n/', $out) as $line) {
            // The data lines start with a scenario name. The header
            // line starts with "scenario" and the separator with "----".
            if (preg_match('/^scenario\s+/', $line)) {
                $in_table = true;
                continue;
            }
            if (!$in_table) continue;
            if (preg_match('/^----+/', $line)) continue;
            if (preg_match('/^Legend:/', $line)) break;
            if (trim($line) === '') continue;

            // Format: name ns/iter iters jit_ent jit_bail s_entries
            //         choice_p choice_pop exec_ns interp_ns
            $cols = preg_split('/\s+/', trim($line));
            if (count($cols) < 10) continue;
            $rows[] = [
                'name'           => $cols[0],
                'ns_per_iter'    => (int)$cols[1],
                'iters'          => (int)$cols[2],
                'jit_entries'    => (int)$cols[3],
                'jit_bailouts'   => (int)$cols[4],
                'jit_search_entries' => (int)$cols[5],
                'jit_choice_push'=> (int)$cols[6],
                'jit_choice_pop' => (int)$cols[7],
                'total_ns'       => (int)$cols[1] * (int)$cols[2],
                'jit_exec_ns'    => (int)$cols[8],
                'jit_interp_ns'  => (int)$cols[9],
            ];
        }
        return $rows;
    }
}
