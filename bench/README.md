# Benchmark Suite — Interpretation Guide

## Running the benchmarks

```bash
make bench
make bench-jit-guard
SNOBOL_JIT_GUARD_N=5 php bench/jit_guard.php
```

## JIT timing counters

After a JIT-enabled run, `snobol_get_jit_stats()` returns:

| Counter                     | Unit     | Meaning                                      |
|-----------------------------|----------|----------------------------------------------|
| `jit_compile_time_ns_total` | ns       | Wall time inside the JIT compiler            |
| `jit_exec_time_ns_total`    | ns       | Wall time executing compiled traces          |
| `jit_interp_time_ns_total`  | op steps | Interpreter dispatch steps while JIT enabled |

**Attribution:** if `compile_time` dominates → tune `SNOBOL_JIT_BUDGET_NS` or `SNOBOL_JIT_HOTNESS`.  
If `interp_time_proxy` is high → many bailouts; check `jit_bailout_*` counters.

## Bailout reason counters

| Counter                        | Meaning                                                |
|--------------------------------|--------------------------------------------------------|
| `jit_bailouts_total`           | Total exits where execution did not advance            |
| `jit_bailout_match_fail_total` | ip == entry_ip after trace (first op failed)           |
| `jit_bailout_partial_total`    | ip > entry_ip after trace (partial progress then fail) |

High `bailout_match_fail_total / entries_total` (> 50%) → JIT covering a frequently-failing region.

## Profitability gate counters

| Counter                       | Meaning                                                     |
|-------------------------------|-------------------------------------------------------------|
| `jit_skipped_cold_total`      | Regions skipped: too few useful ops before SPLIT/REPEAT     |
| `jit_skipped_exit_rate_total` | Compilations halted: exit rate exceeded `max_exit_rate_pct` |
| `jit_skipped_budget_total`    | Compilations halted: `compile_budget_ns` exceeded           |

## Environment-variable tuning

| Variable                  | Default  | Effect                                              |
|---------------------------|----------|-----------------------------------------------------|
| `SNOBOL_JIT_HOTNESS`      | `50`     | Executions before a region is compiled              |
| `SNOBOL_JIT_MAX_EXIT_PCT` | `80`     | Exit-rate % that triggers stop_compiling            |
| `SNOBOL_JIT_BUDGET_NS`    | `500000` | Per-context compile budget (ns)                     |
| `SNOBOL_JIT_CACHE_MAX`    | `128`    | Max LRU cache entries                               |
| `SNOBOL_JIT_MIN_OPS`      | `2`      | Minimum useful ops to allow JIT compilation         |
| `SNOBOL_JIT_SKIP_BT`      | `1`      | Set 0 to disable the backtrack-heavy skip heuristic |

```bash
# Profile backtracking without profitability gate
SNOBOL_JIT_SKIP_BT=0 SNOBOL_JIT_MIN_OPS=0 php bench/backtracking.php

# Tighter hotness threshold
SNOBOL_JIT_HOTNESS=10 php bench/tokenize.php
```

## Guard scenario classes

| Class      | Threshold | Rationale                                                    |
|------------|-----------|--------------------------------------------------------------|
| `simple`   | ≥ 0.95×   | ≤5% regression allowed — noise tolerance for common patterns |
| `targeted` | ≥ 1.00×   | No regression for JIT-targeted backtracking patterns         |

A `targeted` failure signals that the profitability gate or in-JIT control-flow improvements
are insufficient for the identified scenario. Inspect `skipped_cold_total` and
`exec_time_ns_total` to determine whether gate or codegen is the bottleneck.

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

| Column        | Source                                      | Meaning                                  |
|---------------|---------------------------------------------|------------------------------------------|
| `ns/iter`     | `clock_gettime` (or `mach_absolute_time`)   | Wall time per match attempt              |
| `iters`       | iteration counter                           | Match attempts executed                  |
| `jit_ent`     | `entries_total` delta                       | JIT-compiled trace entries               |
| `jit_bail`    | `bailouts_total` delta                      | Bailouts from compiled traces            |
| `s_entries`   | `search_entries_total` delta                | Search-mode trace entries                |
| `choice_p`    | `choice_push_total` delta                   | VM choice stack push count               |
| `choice_pop`  | `choice_pop_total` delta                    | VM choice stack pop count                |
| `exec_ns`     | `exec_time_ns_total` delta                  | Time inside compiled traces (ns)         |
| `interp_ns`   | `interp_time_ns_total` delta                | Time in interpreter dispatch (ns)        |

When `SNOBOL_JIT` is not compiled in, all JIT columns read 0 and `ns/iter`
measures pure interpreter cost.

### Interpreting results

- **High `ns/iter` + `jit_ent == 0`**: interpreter is the bottleneck.
  Check whether the pattern meets the `min_useful_ops` threshold
  (`SNOBOL_JIT_MIN_OPS`, default 2). For single-op patterns the search-mode
  threshold (`SNOBOL_JIT_SEARCH_OPS`, default 1) only applies when using
  `snobol_pattern_search`.
- **High `ns/iter` + `jit_ent > 0` + `exec_ns << interp_ns`**: search-loop
  overhead per candidate position dominates. The compiled trace is fast;
  the interpreter frames around it are not.
- **High `choice_pop_total`**: backtracking pressure; SPLIT/REPEAT-heavy
  patterns push choice records on every candidate.

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
