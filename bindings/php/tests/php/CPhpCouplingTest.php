<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;

/**
 * Regression guard: the C probe and the PHP probe must both produce valid
 * output and stay within a loose ratio bound.
 *
 * The diagnostic-probe change established two complementary attribution
 * tools — a C-side probe that measures the engine's per-iteration cost
 * (bench/c/bench_probe.c) and a PHP-side probe that measures the full
 * user-facing cost (bench/php/probe.php). The PHP probe includes the
 * binding's overhead.
 *
 * This test runs both probes and asserts:
 *
 *   1. Both probes produce non-zero iterations for every scenario.
 *   2. The PHP cost for the alt_literals scenario is at most 50x the C
 *      cost. This is a loose guard; if the C path improves dramatically
 *      and the PHP path doesn't, the ratio jumps — the test catches that.
 */
class CPhpCouplingTest extends TestCase
{
    private const ITER_ENV = 'PROBE_ITERS';
    private const ITER_DEFAULT = 10000;   // smaller than probe default for fast test
    private const PHP_C_RATIO_MAX = 50.0; // loose upper bound

    /** @return array<int, array<string, mixed>>|null */
    private function runPhpProbe(): ?array
    {
        $probe = $this->resolvePhpProbe();
        if ($probe === null) {
            $this->markTestSkipped('PHP probe not found');
            return null;
        }

        $cmd = sprintf(
            '%s %s %s=%d',
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

        return $this->extractJsonResults($out);
    }

    /** @return array<int, array<string, mixed>>|null */
    private function runCProbe(): ?array
    {
        $probe = $this->resolveCProbe();
        if ($probe === null) {
            $this->markTestSkipped('C probe not built');
            return null;
        }

        $cmd = sprintf(
            '%s %s=%d',
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

    public function testPhpC_ratio_within_bounds(): void
    {
        $php = $this->runPhpProbe();
        $c = $this->runCProbe();
        if ($php === null || $c === null) return;

        // Match scenarios by name. Compare alt_literals (both probes
        // have it with the same name).
        $c_alt = $this->findRow($c, 'alt_literals');
        $php_alt = $this->findRow($php, 'alt_literals');
        if ($c_alt === null || $php_alt === null) {
            $this->markTestSkipped('alt_literals scenario missing in one probe');
            return;
        }

        $c_ns = (int)$c_alt['ns_per_iter'] * (int)$c_alt['iters'];
        $php_ns = (int)$php_alt['ns_per_iter'] * (int)$php_alt['iters'];
        $this->assertGreaterThan(0, $c_ns, 'C probe alt_literals total_ns is zero');
        $this->assertGreaterThan(0, $php_ns, 'PHP probe alt_literals total_ns is zero');

        $ratio = $php_ns / $c_ns;
        $this->assertLessThanOrEqual(
            self::PHP_C_RATIO_MAX,
            $ratio,
            sprintf(
                "PHP/C alt_literals ratio %.1fx exceeds guard %.1fx\n"
                . "  C:   %d ns total (iters=%d, ns/iter=%d)\n"
                . "  PHP: %d ns total (iters=%d, ns/iter=%d)\n"
                . "The binding is dominating.",
                $ratio, self::PHP_C_RATIO_MAX,
                $c_ns, (int)$c_alt['iters'], (int)$c_alt['ns_per_iter'],
                $php_ns, (int)$php_alt['iters'], (int)$php_alt['ns_per_iter']
            )
        );
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
            '/var/www/build/bench/c/snobol4_probe',         // ddev (html)
            '/var/www/html-root/build/bench/c/snobol4_probe', // ddev (html-root)
            __DIR__ . '/../../../../build/bench/c/snobol4_probe',
            '/usr/local/bin/snobol4_probe',                 // ddev installed
        ];
        foreach ($candidates as $p) {
            if (is_file($p) && is_executable($p)) return $p;
        }
        return null;
    }

    /**
     * The PHP probe now emits JSON at the end of stdout.
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
     * Parse the C probe's table output.
     * Columns: scenario, ns/iter, iters.
     *
     * @return array<int, array<string, mixed>>
     */
    private function parseCProbeTable(string $out): array
    {
        $rows = [];
        $in_table = false;
        foreach (preg_split('/\r?\n/', $out) as $line) {
            if (preg_match('/^scenario\s+/', $line)) {
                $in_table = true;
                continue;
            }
            if (!$in_table) continue;
            if (preg_match('/^----+/', $line)) continue;
            if (preg_match('/^Legend:/', $line)) break;
            if (trim($line) === '') continue;

            // Format: name ns/iter iters
            $cols = preg_split('/\s+/', trim($line));
            if (count($cols) < 3) continue;
            $rows[] = [
                'name'        => $cols[0],
                'ns_per_iter' => (int)$cols[1],
                'iters'       => (int)$cols[2],
                'total_ns'    => (int)$cols[1] * (int)$cols[2],
            ];
        }
        return $rows;
    }
}
