<?php
/**
 * Benchmark Harness
 *
 * Provides utilities for timing, warmup, iteration control, and result recording.
 */

namespace Bench;

class Harness
{
    private array $results = [];
    private array $metadata = [];

    public function __construct()
    {
        $this->metadata = [
            'phpVersion' => PHP_VERSION,
            'os' => PHP_OS,
            'timestamp' => date('c'),
            'extensionVersion' => $this->getExtensionVersion(),
        ];
    }

    private function getExtensionVersion(): ?string
    {
        if (extension_loaded('snobol')) {
            $version = phpversion('snobol');
            return $version !== false ? $version : 'unknown';
        }
        return null;
    }

    /**
     * Run a benchmark scenario with timeout protection
     *
     * @param  string  $scenario  Stable scenario ID
     * @param  string  $impl  Implementation name ('snobol' or 'pcre')
     * @param  callable  $callable  The code to benchmark
     * @param  int  $warmupIterations  Number of warmup iterations
     * @param  int  $iterations  Number of measured iterations
     * @param  int|null  $inputSize  Optional input size in bytes/chars
     * @param  int  $timeoutSeconds  Maximum seconds per iteration (0 = no timeout)
     */
    public function bench(
        string $scenario,
        string $impl,
        callable $callable,
        int $warmupIterations = 100,
        int $iterations = 1000,
        ?int $inputSize = null,
        int $timeoutSeconds = 30
    ): void {
        echo "Running: $scenario ($impl) - $iterations iterations...\n";

        // Warmup with timeout check
        $warmupStart = time();
        for ($i = 0; $i < $warmupIterations; $i++) {
            if ($timeoutSeconds > 0 && (time() - $warmupStart) > $timeoutSeconds) {
                echo "⚠ TIMEOUT during warmup for $scenario ($impl) after ".(time() - $warmupStart)."s\n";
                $this->recordTimeout($scenario, $impl, $warmupIterations, $iterations, $inputSize);
                return;
            }
            $callable();
        }

        // Measured run with timeout check
        $startTime = hrtime(true);
        $wallStart = time();
        for ($i = 0; $i < $iterations; $i++) {
            if ($timeoutSeconds > 0 && (time() - $wallStart) > $timeoutSeconds) {
                echo "⚠ TIMEOUT during measurement for $scenario ($impl) after ".(time() - $wallStart)."s (completed $i/$iterations iterations)\n";
                $this->recordTimeout($scenario, $impl, $warmupIterations, $i, $inputSize);
                return;
            }
            $callable();
        }
        $endTime = hrtime(true);

        // Fetch JIT stats if available
        $jitStats = [];
        if (function_exists('snobol_get_jit_stats')) {
            $jitStats = snobol_get_jit_stats();
        }

        $durationNs = $endTime - $startTime;
        $durationMs = $durationNs / 1_000_000;
        $opsPerSec = $iterations / ($durationNs / 1_000_000_000);

        $result = [
            'scenario' => $scenario,
            'impl' => $impl,
            'warmupIterations' => $warmupIterations,
            'iterations' => $iterations,
            'durationNs' => $durationNs,
            'durationMs' => $durationMs,
            'opsPerSec' => $opsPerSec,
            'jitStats' => $jitStats,
        ];

        if ($inputSize !== null) {
            $result['inputBytes'] = $inputSize;
        }

        $this->results[] = array_merge($result, $this->metadata);
        echo "✓ Completed: $scenario ($impl) - ".number_format($opsPerSec, 2)." ops/sec\n";
    }

    /**
     * Record a timeout event
     */
    private function recordTimeout(
        string $scenario,
        string $impl,
        int $warmup,
        int $completedIters,
        ?int $inputSize
    ): void {
        $result = [
            'scenario' => $scenario,
            'impl' => $impl,
            'warmupIterations' => $warmup,
            'iterations' => $completedIters,
            'durationNs' => null,
            'durationMs' => null,
            'opsPerSec' => null,
            'timeout' => true,
        ];

        if ($inputSize !== null) {
            $result['inputBytes'] = $inputSize;
        }

        $this->results[] = array_merge($result, $this->metadata);
    }

    /**
     * Get all benchmark results
     */
    public function getResults(): array
    {
        return $this->results;
    }

    /**
     * Write results to JSON file
     */
    public function writeJson(string $filepath): void
    {
        $json = json_encode($this->results, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
        file_put_contents($filepath, $json);
    }

    /**
     * Print a summary table
     */
    public function printSummary(): void
    {
        if (empty($this->results)) {
            echo "No results to display.\n";
            return;
        }

        echo "\n".str_repeat('=', 95)."\n";
        echo "BENCHMARK RESULTS\n";
        echo str_repeat('=', 95)."\n";
        printf("%-30s %-10s %12s %15s %12s\n", "Scenario", "Impl", "Iterations", "Ops/Sec", "JIT Entries");
        echo str_repeat('-', 95)."\n";

        foreach ($this->results as $result) {
            $opsDisplay = isset($result['timeout']) && $result['timeout']
                ? 'TIMEOUT'
                : number_format($result['opsPerSec'], 2);

            $jitEntries = isset($result['jitStats']['jit_entries_total'])
                ? $result['jitStats']['jit_entries_total']
                : '-';

            printf(
                "%-30s %-10s %12d %15s %12s\n",
                $result['scenario'],
                $result['impl'],
                $result['iterations'],
                $opsDisplay,
                $jitEntries
            );
        }

        echo str_repeat('=', 95)."\n";
        echo "Peak memory: ".number_format(memory_get_peak_usage(true) / 1024 / 1024, 2)." MB\n";
        echo str_repeat('=', 95)."\n\n";
    }
}
