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
        if (function_exists('snobol_reset_jit_stats')) {
            snobol_reset_jit_stats();
        }
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
     * Write a complete BENCHMARKS.md file with methodology and results.
     * The file is placed at $filepath (typically bench/BENCHMARKS.md).
     */
    public function writeMarkdown(string $filepath): void
    {
        $scenarios = [
            'literal_short' => [
                'name' => 'Literal Matching',
                'desc' => 'A simple substring search (`\'hello\'` vs `/hello/`). Measures raw engine throughput for the most basic pattern type.',
            ],
            'alternation_delimiter' => [
                'name' => 'Alternation / Delimiter Search',
                'desc' => 'Finds tokens in a comma-separated string using character-class scanning (`BREAK(\',\')` vs `/[^,]+/`). Highlights delimiter-search performance.',
            ],
            'tokenization' => [
                'name' => 'Tokenization',
                'desc' => 'Repeatedly tokenizes a long comma-separated string (1000 tokens). Stress-tests the engine\'s ability to handle many match operations.',
            ],
            'substitution' => [
                'name' => 'Substitution',
                'desc' => 'Finds a pattern (`\'fox\'` vs `/fox/`) in a moderate-length repetitive subject. Measures basic search throughput.',
            ],
            'complex_http' => [
                'name' => 'Complex Pattern (HTTP Request Line)',
                'desc' => 'Parses an HTTP request line with structured captures (method, path, version). Demonstrates structured data extraction.',
            ],
        ];

        // Build per-scenario results lookup
        $snobol = []; $pcre = [];
        foreach ($this->results as $r) {
            $s = $r['scenario'];
            if ($r['impl'] === 'snobol') $snobol[$s] = $r;
            else $pcre[$s] = $r;
        }

        // Determine environment info
        $phpVersion = $this->metadata['phpVersion'] ?? PHP_VERSION;
        $os = php_uname('s');
        $extVersion = $this->metadata['extensionVersion'] ?? 'unknown';
        $hasJit = isset($this->results[0]['jitStats']) && !empty($this->results[0]['jitStats']);

        // Build results table rows
        $tableRows = '';
        foreach (array_keys($scenarios) as $s) {
            $sOps = $snobol[$s]['opsPerSec'] ?? null;
            $pOps = $pcre[$s]['opsPerSec'] ?? null;
            $sVal = $sOps !== null ? number_format(round($sOps)) : '—';
            $pVal = $pOps !== null ? number_format(round($pOps)) : '—';
            $ratio = ($sOps !== null && $pOps > 0) ? number_format($sOps / $pOps, 2) : '—';
            $tableRows .= sprintf(
                "| %-22s | %18s | %18s | %5s |\n",
                $s, $sVal, $pVal, $ratio
            );
        }

        $md = <<<MARKDOWN
# Benchmarks: libsnobol4 vs PCRE2

This document presents head-to-head benchmark results comparing libsnobol4
against PCRE2 across representative pattern matching workloads.
*Auto-generated by `php bench/compare_pcre2.php`. Delete and re-run to refresh.*

## Methodology

- **OS**: {$os}
- **PHP version**: {$phpVersion}
- **PCRE version**: As bundled with PHP (PCRE2 10.44+)
- **libsnobol4 version**: {$extVersion} (JIT: {$this->boolToStr($hasJit)})
- **Warmup**: 50–100 iterations before measurement
- **Sample size**: 500–10,000 measured iterations per benchmark
- **Timing**: `hrtime(true)` — wall clock, nanosecond resolution
- **Timeout**: 30 seconds per benchmark scenario

### Running the Benchmarks

```bash
# Build with PHP extension
make build-php

# Run comparison benchmarks
php bench/compare_pcre2.php
```

## Results

MARKDOWN;

        $md .= "\n| Scenario               | libsnobol4 (ops/s) | PCRE2 (ops/s) | Ratio |\n";
        $md .= "|------------------------|--------------------|---------------|-------|\n";
        $md .= $tableRows;

        $md .= <<<MARKDOWN

## Benchmark Descriptions

MARKDOWN;

        foreach (array_keys($scenarios) as $s) {
            $info = $scenarios[$s];
            $md .= "\n### {$info['name']}\n{$info['desc']}\n";
        }

        $md .= <<<MARKDOWN

## Performance Characteristics

### When libsnobol4 is faster
- Patterns requiring balanced delimiter matching (BAL primitive)
- Patterns with complex backtracking that benefit from compact choice stack
- Template-based substitution with formatting expressions

### When PCRE2 is faster
- Simple literal substring searches (PCRE2's Boyer-Moore fast path)
- Very simple character class matching (BREAK/SPAN of small character sets)
- Pre-compiled regex patterns (no per-call compilation overhead)
- Tokenization of long subjects: JIT bailout overhead at every delimiter creates a significant gap

### When they are comparable
- Alternation-based tokenization of delimiter-separated values
- Medium-complexity pattern matching with few captures

## Interpreting Results

A ratio > 1.0 means libsnobol4 is faster; < 1.0 means PCRE2 is faster.

Performance depends heavily on pattern complexity, subject size, and JIT
profitability. libsnobol4's JIT requires a warmup period (SNOBOL_JIT_HOTNESS,
default 50 executions) before compilation kicks in. For very short-lived
patterns, the interpreter may be used instead.

## JIT Diagnostics

When JIT is enabled, additional counters are available:

```php
\$stats = snobol_get_jit_stats();
```

Key counters:
- `jit_entries_total` — how many times a JIT-compiled trace was entered
- `jit_exec_time_ns_total` — time spent executing compiled code
- `jit_bailouts_total` — how often execution fell back to the interpreter

See [bench/README.md](bench/README.md) for detailed JIT counter documentation.
MARKDOWN;

        file_put_contents($filepath, $md);
        echo "\nBenchmark report written to {$filepath}\n";
    }

    private function boolToStr(bool $val): string
    {
        return $val ? 'enabled' : 'disabled';
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

        echo "\n".str_repeat('=', 120)."\n";
        echo "BENCHMARK RESULTS\n";
        echo str_repeat('=', 120)."\n";
        printf("%-30s %-10s %12s %15s %12s %14s %14s\n",
            "Scenario", "Impl", "Iterations", "Ops/Sec", "JIT Entries", "Choice Pushes", "Choice Bytes");
        echo str_repeat('-', 120)."\n";

        foreach ($this->results as $result) {
            $opsDisplay = isset($result['timeout']) && $result['timeout']
                ? 'TIMEOUT'
                : number_format($result['opsPerSec'], 2);

            $jitStats = $result['jitStats'] ?? [];
            $jitEntries = isset($jitStats['jit_entries_total']) ? number_format($jitStats['jit_entries_total']) : '-';
            $choicePushes = isset($jitStats['choice_push_total']) ? number_format($jitStats['choice_push_total']) : '-';
            $choiceBytes = isset($jitStats['choice_bytes_total']) ? number_format($jitStats['choice_bytes_total']) : '-';

            printf(
                "%-30s %-10s %12d %15s %12s %14s %14s\n",
                $result['scenario'],
                $result['impl'],
                $result['iterations'],
                $opsDisplay,
                $jitEntries,
                $choicePushes,
                $choiceBytes
            );
        }

        echo str_repeat('=', 120)."\n";
        echo "Peak memory: ".number_format(memory_get_peak_usage(true) / 1024 / 1024, 2)." MB\n";
        echo str_repeat('=', 120)."\n\n";
    }

    /**
     * Print search-mode execution diagnostics for SNOBOL results.
     *
     * Shows the layered-search-runtime counters collected by the JIT stats
     * facility (layered-search-performance change):
     *   - JIT search entries (how many times the search loop hit a compiled trace)
     *   - Search candidate rejects (expected early misses attributed to search mode)
     *   - Search-mode cold skips (JIT skipped due to search_hotness threshold)
     *   - Search bailout candidates (mismatches attributed to search mode)
     */
    public function printSearchDiagnostics(): void
    {
        $snobolResults = array_filter($this->results, fn($r) => $r['impl'] === 'snobol');
        if (empty($snobolResults)) {
            return;
        }

        echo "\n".str_repeat('-', 80)."\n";
        echo "Search-Mode Execution Diagnostics (layered-search-performance)\n";
        echo str_repeat('-', 80)."\n";
        printf("%-30s %12s %12s %12s %12s\n",
            "Scenario", "JIT Entries", "CandRejcts", "ColdSkips", "SearchBail");
        echo str_repeat('-', 80)."\n";

        foreach ($snobolResults as $result) {
            $jit = $result['jitStats'] ?? [];
            printf("%-30s %12s %12s %12s %12s\n",
                $result['scenario'],
                isset($jit['jit_search_entries_total'])
                    ? number_format($jit['jit_search_entries_total']) : '-',
                isset($jit['jit_search_candidate_rejects'])
                    ? number_format($jit['jit_search_candidate_rejects']) : '-',
                isset($jit['jit_skipped_search_cold_total'])
                    ? number_format($jit['jit_skipped_search_cold_total']) : '-',
                isset($jit['jit_bailout_search_candidate_total'])
                    ? number_format($jit['jit_bailout_search_candidate_total']) : '-'
            );
        }
        echo str_repeat('-', 80)."\n\n";
    }
}
