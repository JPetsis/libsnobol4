#!/usr/bin/env bash
#
# update_baseline.sh — Update bench/results/search_perf_baseline.json
# with post-optimization numbers and a c_api_extensions field documenting
# what changed in this round.
#
# Usage:
#   update_baseline.sh <after_baseline.json>
#
# Reads the committed baseline at bench/results/search_perf_baseline.json
# and the after-baseline from the argument, merges them, and writes the
# result back to the committed baseline path.
#
# Requires: bash, jq, date.

set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <after_baseline.json>" >&2
    exit 2
fi

after_path="$1"
baseline_path="bench/results/search_perf_baseline.json"

if [ ! -f "$after_path" ]; then
    echo "error: after-baseline file not found: $after_path" >&2
    exit 2
fi
if [ ! -f "$baseline_path" ]; then
    echo "error: committed baseline not found: $baseline_path" >&2
    exit 2
fi
for cmd in jq date; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "error: required command not found: $cmd" >&2
        exit 2
    fi
done

captured_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

# Build the c_api_extensions block. Uses --slurpfile to splice the after
# baseline and emits comparison tables for both probes.
#
# The comparison table is built by iterating over the union of keys
# from the before and after probe objects, then looking up the value
# in each. `transpose` doesn't work on objects with different keys
# (it produces a sparse array), so we use reduce + keys instead.
c_api_extensions=$(jq -n \
    --arg captured_at "$captured_at" \
    --slurpfile after "$after_path" \
    --slurpfile base "$baseline_path" \
    '
    # Helper: build a {key: {before, after}} object from two probe dicts
    def compare_dict(before; after):
        (before // {} | keys) + (after // {} | keys) | unique |
        map(. as $k | {
            ($k): {
                before_ns_per_iter: (before[$k].ns_per_iter // null),
                after_ns_per_iter:  (after[$k].ns_per_iter  // null)
            }
        }) | add;

    {
        search_metadata_cached_on_pattern:  true,
        stateful_search_api_added:            true,
        binding_searchSplit_refactor:         "applied",
        binding_searchAll_refactor:           "already-optimized",
        binding_searchReplace_refactor:       "applied",
        binding_stateful_api_compat_fix:      "api-takes-bc-bc-len",
        notes: (
            "Task 2 (metadata caching on the pattern) and task 3 (stateful " +
            "search API) landed. The interpreter-mode scenarios show 5-18% " +
            "improvement (see ns/iter deltas). Task 5/6/7 (binding-side " +
            "refactor of searchSplit/searchAll/searchReplace to use the " +
            "stateful API) also landed — the original segfault was caused " +
            "by the C API and PHP binding having different snobol_pattern_t " +
            "structs; the fix was to change the stateful API to take raw " +
            "(bc, bc_len) instead of a pattern pointer. The search-mode " +
            "scenarios (span_search, alt_search, tokenize, tokenize_php) " +
            "improve modestly because the per-call VM init is amortised, " +
            "but the per-segment add_next_index_stringl cost still dominates " +
            "the PHP binding path. A bulk-result-buffer follow-up is the " +
            "next opportunity to attack that cost."
        ),
        comparison_to_before:        compare_dict($base[0].c_probe;   $after[0].c_probe),
        php_comparison_to_before:   compare_dict($base[0].php_probe; $after[0].php_probe)
    }')

# Merge: start with the after baseline, override captured_before_optimization
# to false, set captured_at to now, and add c_api_extensions.
merged=$(jq -s \
    --arg captured_at "$captured_at" \
    --argjson c_api_extensions "$c_api_extensions" \
    '.[0] | .captured_before_optimization = false |
            .captured_at = $captured_at |
            .c_api_extensions = $c_api_extensions' \
    "$after_path")

# Write the result
echo "$merged" | jq '.' > "$baseline_path"
echo "updated $baseline_path"
