# Benchmark Suite

This directory contains a repeatable benchmark suite for the SNOBOL4 PHP extension. The suite quantifies performance
characteristics of the SNOBOL VM and compares them against equivalent PCRE implementations.

## Purpose

The benchmark suite serves to:

- **Quantify performance** of the SNOBOL pattern engine
- **Compare against PCRE** for common workloads
- **Detect regressions** across VM, compiler, and PHP binding changes
- **Inform optimization decisions** with concrete data

### Important Notes

- **PCRE is highly optimized** C code with decades of tuning. The goal is **competitive performance** and **feature
  parity** for parsing-style workloads, not necessarily always beating PCRE.
- Some SNOBOL operations (especially backtracking and captures) won't map 1:1 to PCRE semantics. Comparisons use the "
  closest reasonable" PCRE implementation.
- Benchmarks can be **noisy**. Run multiple times and look for trends, not absolute numbers.
- **Known performance issues**: The SNOBOL VM currently has significant performance gaps vs PCRE (10,000x+ slower on
  some workloads). This is a known limitation being actively investigated.
- **Timeout protection**: Each benchmark has a 30-second timeout. If a scenario times out, it will be marked as "
  TIMEOUT" in results.

## Running Benchmarks

### Prerequisites

- PHP 8.0 or newer
- The `snobol` extension must be loaded (`php -m | grep snobol`)
- Composer dependencies installed (`composer install`)

### Run All Benchmarks

```bash
cd bench/
php tokenize.php
php replace.php
php dates.php
php backtracking.php
```

### Run a Single Benchmark

```bash
php bench/tokenize.php
```

Each benchmark will:

1. Run warmup iterations (default: 5-10)
2. Run measured iterations (default: 100-500, depending on scenario)
3. Print a summary table
4. Write results to a JSON file (e.g., `results_tokenize.json`)

## Benchmark Scenarios

### 1. Tokenization (`tokenize.php`)

Tests splitting strings on delimiters:

- **tokenize_comma**: Split on comma
- **tokenize_whitespace**: Split on whitespace
- **tokenize_mixed**: Split on multiple delimiters (comma + whitespace)

**SNOBOL pattern examples:**

- `"','"` (split on comma)
- `"' '"` (split on space)
- `"' ' | ','"` (split on space or comma)

**PCRE equivalents:**

- `/,/` (split on comma)
- `/\s+/` (split on whitespace)
- `/[\s,]+/` (split on whitespace or comma)

### 2. Replace/Removal (`replace.php`)

Tests character replacement and removal:

- **remove_spaces**: Remove all spaces
- **replace_vowels**: Replace vowels with asterisks
- **replace_words**: Replace multiple words
- **remove_punctuation**: Remove punctuation characters

**SNOBOL pattern examples:**

- `"' '"` (match space)
- `"[aeiouAEIOU]"` (character class)

**PCRE equivalents:**

- `/\s/` (match whitespace)
- `/[aeiouAEIOU]/` (character class)

### 3. Date Matching (`dates.php`)

Tests matching date patterns in various formats:

- **match_iso_dates**: Match YYYY-MM-DD format
- **match_us_dates**: Match MM/DD/YYYY format
- **match_eu_dates**: Match DD.MM.YYYY format
- **match_any_dates**: Match any of the above formats (alternation)

**SNOBOL pattern examples:**

- `"SPAN('0-9') '-' SPAN('0-9') '-' SPAN('0-9')"`
- `"SPAN('0-9') '/' SPAN('0-9') '/' SPAN('0-9')"`

**PCRE equivalents:**

- `/\d{4}-\d{2}-\d{2}/`
- `/\d{2}\/\d{2}\/\d{4}/`

### 4. Worst-case Backtracking (`backtracking.php`)

Tests pathological backtracking scenarios:

- **backtrack_alternation**: Deep alternation chain (forces trying many alternatives)
- **backtrack_repetition**: Greedy repetition with backtracking
- **backtrack_nested**: Nested repetition (exponential backtracking)
- **backtrack_overlap**: Alternation with overlapping prefixes

**Warning:** These scenarios are intentionally pathological and may be slow. Iteration counts are reduced to avoid
timeouts.

## Result Format

Each benchmark writes results to a JSON file with the following schema:

```json
[
  {
    "scenario": "tokenize_comma",
    "impl": "snobol",
    "warmupIterations": 10,
    "iterations": 500,
    "durationNs": 123456789,
    "durationMs": 123.456789,
    "opsPerSec": 4048.58,
    "inputBytes": 5000,
    "phpVersion": "8.3.0",
    "os": "Linux",
    "timestamp": "2026-01-16T12:00:00+00:00",
    "extensionVersion": "0.1.0"
  }
]
```

### Fields

- **scenario**: Stable scenario ID (e.g., `tokenize_comma`, `replace_vowels`)
- **impl**: Implementation (`snobol` or `pcre`)
- **warmupIterations**: Number of warmup iterations
- **iterations**: Number of measured iterations
- **durationNs**: Total duration in nanoseconds
- **durationMs**: Total duration in milliseconds
- **opsPerSec**: Operations per second (iterations / duration)
- **inputBytes**: Optional input size in bytes
- **phpVersion**: PHP version
- **os**: Operating system
- **timestamp**: ISO 8601 timestamp
- **extensionVersion**: SNOBOL extension version (if available)

## Interpreting Results

### Comparing SNOBOL vs PCRE

1. **Look at ops/sec** for each scenario
2. **Compare ratios**, not absolute values (e.g., "SNOBOL is 2x slower" vs "SNOBOL is 100 ops/sec slower")
3. **Consider input size** - some operations scale differently
4. **Watch for outliers** - re-run if results seem suspicious

### Common Pitfalls

- **Warmup matters**: PHP's JIT and CPU caches need time to stabilize
- **Noise is real**: Run multiple times and average results
- **Concurrency**: Don't run benchmarks while heavy processes are running
- **Semantic differences**: PCRE and SNOBOL may not always match identically

### What to Expect

- **Tokenization**: PCRE often wins due to highly optimized C string functions
- **Character classes**: Should be comparable once Unicode range encoding is mature
- **Backtracking**: Results vary; SNOBOL VM is designed for correctness first
- **Complex patterns**: SNOBOL's expressiveness may come with overhead vs hand-tuned PCRE

## CI Integration

Benchmarks can be run in CI as optional jobs (non-gating):

- Results are uploaded as artifacts
- CI does not fail on performance regressions (too noisy)
- Trends are monitored manually or via separate tooling

See `.github/workflows/benchmarks.yml` (if present) for CI configuration.

## Example Results

See `results_example.json` for a sample output shape. (Note: Results will vary by hardware, PHP version, and workload.)
