# Benchmark Suite — Interpretation Guide

## Running the benchmarks

### C microbenchmarks (native, no PHP needed)

```bash
make bench-c            # build (if needed) and run snobol4 vs PCRE2
```

This builds the C benchmark suite (`snobol4_bench`) and runs scenarios against
both libsnobol4 and PCRE2, printing a comparison table.

### PHP benchmarks (require the snobol extension loaded)

```bash
make bench              # run PHP benchmarks (tokenize, replace, dates, backtracking)
php bench/compare_pcre2.php   # snobol4 vs PCRE2 comparison via PHP API
```

## JIT timing counters

After a JIT-enabled run, `snobol_get_jit_stats()` returns:

| Counter                      | Unit  | Meaning                             |
|------------------------------|-------|-------------------------------------|
| `jit_method_attempts_total`  | count | Method JIT compilation attempts     |
| `jit_method_successes_total` | count | Successful method JIT compilations  |
| `jit_method_fallbacks_total` | count | Patterns too complex for method JIT |
| `jit_method_evictions_total` | count | Method JIT cache evictions          |

## Diagnostic Probe

A standalone C tool (`bench/c/bench_probe.c`) that measures per-scenario
timing and JIT stat deltas without modifying `core/`. Use it to attribute
per-iteration cost between the interpreter and JIT-compiled paths.

### Building and running

```bash
cmake -B build -DBUILD_BENCH_C=ON
cmake --build build --target snobol4_probe
./build/bench/c/snobol4_probe
```

Override iteration count:

```bash
PROBE_ITERS=1000000 ./build/bench/c/snobol4_probe
```

### Scenarios

| Scenario       | Pattern              | Subject              | API                           |
|----------------|----------------------|----------------------|-------------------------------|
| `literal_fail` | `'pqr'`              | 1KB no `pqr`         | `snobol_pattern_match`        |
| `literal_ok`   | `'pqr'`              | 1KB with `pqr`       | `snobol_pattern_match`        |
| `span_comma`   | `SPAN(',')`          | 1KB CSV              | `snobol_pattern_match`        |
| `span_search`  | `SPAN(',')`          | 1KB CSV              | `snobol_pattern_search`       |
| `alternation`  | `'a' \| 'b' \| 'c'`  | mixed                | `snobol_pattern_match`        |
| `alt_search`   | `'a' \| 'b' \| 'c'`  | mixed                | `snobol_pattern_search`       |
| `tokenize`     | `' '`                | whitespace stream    | `snobol_pattern_search` loop  |

The `tokenize` scenario mimics the inner loop of `Pattern::searchSplit` —
it advances one byte at a time through the subject, calling
`snobol_pattern_search` at each position.

### Output columns

| Column         | Source                                    | Meaning                             |
|----------------|-------------------------------------------|-------------------------------------|
| `ns/iter`      | `clock_gettime` (or `mach_absolute_time`) | Wall time per match attempt         |
| `iters`        | iteration counter                         | Match attempts executed             |
| `jit_attempts` | `jit_method_attempts_total` delta         | Method-JIT compile attempts         |
| `jit_ok`       | `jit_method_successes_total` delta        | Successful method-JIT compilations  |
| `jit_fb`       | `jit_method_fallbacks_total` delta        | Patterns too complex for method JIT |

When `SNOBOL_JIT` is not compiled in, all JIT columns read 0 and `ns/iter`
measures pure interpreter cost.

### Interpreting results

- **`jit_attempts > 0`, `jit_ok > 0`**: pattern was compiled once and cached;
  subsequent calls run native code.
- **`jit_fb > 0`**: pattern contains non-compilable opcodes (SPAN, BREAK,
  SPLIT, ASSIGN, etc.) — runs via VM interpreter. Each call wastes a failed
  compilation attempt; non-compilable patterns are not cached.
- **High `ns/iter` + `jit_attempts == 0`** (match API): interpreter path,
  no attempt is made (method JIT only triggers in search-mode or PHP match()).

## PHP probe

`bench/php/probe.php` mirrors the C probe's scenarios through the public
PHP API. Use it to attribute cost between the C engine and the PHP
binding layer (`memset(VM,0)`, `add_next_index_stringl`, PHP↔C crossing).

```bash
# In ddev:
ddev build-c-probe           # one-time: builds the C probe
php /var/www/bench/php/probe.php
```

The PHP probe emits a JSON block on stderr that the coupling test parses.

## C/PHP coupling test

`bindings/php/tests/php/JitCPhpCouplingTest.php` runs both probes and
asserts they move together. A regression guard: if a JIT change improves
the C path but the PHP path stays the same, this test fails.

```bash
# In ddev:
ddev test --filter JitCPhpCouplingTest
```

The test is intentionally loose (PHP/C ratio ≤ 500x for the `tokenize`
scenario) so it doesn't fail on legitimate architectural differences.
The goal is to catch the case where a JIT optimization is implemented
in the C engine but the PHP binding doesn't see it.
