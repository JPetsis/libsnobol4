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
