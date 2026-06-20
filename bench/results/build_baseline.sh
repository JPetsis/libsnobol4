#!/usr/bin/env bash
#
# build_baseline.sh — Build bench/results/search_perf_baseline.json
# from raw C and PHP probe output.
#
# Usage:
#   build_baseline.sh <c_probe_raw.txt> <php_probe.json> <output.json> \
#       --captured-before-optimization true
#
# The C probe output is a fixed-width text table (10 columns per data row).
# The PHP probe output is a JSON array (one entry per scenario, on stderr).
#
# Requires: bash, awk, jq.

set -euo pipefail

if [ "$#" -lt 3 ]; then
    echo "Usage: $0 <c_probe_raw.txt> <php_probe.json> <output.json> [--captured-before-optimization true|false]" >&2
    exit 2
fi

c_path="$1"
php_path="$2"
out_path="$3"

# Parse --captured-before-optimization flag (default: false)
before=false
if [ "${4:-}" = "--captured-before-optimization" ] && [ "${5:-}" = "true" ]; then
    before=true
fi

# Validate inputs
for f in "$c_path" "$php_path"; do
    if [ ! -f "$f" ]; then
        echo "error: input file not found: $f" >&2
        exit 2
    fi
done
for cmd in awk jq date; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "error: required command not found: $cmd" >&2
        exit 2
    fi
done

captured_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

# Parse the C probe table. Data rows are lines whose first column
# matches a known scenario name; skip header / separator / legend lines.
# The C probe's table has 10 columns:
#   1 scenario  2 ns_per_iter  3 iters  4 jit_entries  5 jit_bailouts
#   6 jit_search_entries  7 jit_choice_push  8 jit_choice_pop
#   9 jit_exec_ns  10 jit_interp_ns
c_probe_json=$(awk -v OFS='\t' '
    # Match lines that start with a known scenario name (lowercase identifier)
    $1 ~ /^(literal_fail|literal_ok|span_comma|span_search|alternation|alt_search|tokenize)$/ {
        ns_per_iter   = $2 + 0
        iters         = $3 + 0
        jit_entries   = $4 + 0
        jit_bailouts  = $5 + 0
        jit_search    = $6 + 0
        jit_push      = $7 + 0
        jit_pop       = $8 + 0
        jit_exec      = $9 + 0
        jit_interp    = $10 + 0
        total_ns      = ns_per_iter * iters
        # Emit one TSV line per scenario with all fields as columns
        print $1, ns_per_iter, iters, jit_entries, jit_bailouts, jit_search, \
              jit_push, jit_pop, jit_exec, jit_interp, total_ns
    }
' "$c_path")

# Build the c_probe JSON object from the TSV lines
c_probe_obj=$(echo "$c_probe_json" | jq -R -s '
    split("\n")
    | map(select(length > 0))
    | map(split("\t"))
    | map({
        (.[0]): {
            ns_per_iter:         (.[1] | tonumber),
            iters:               (.[2] | tonumber),
            jit_entries:         (.[3] | tonumber),
            jit_bailouts:        (.[4] | tonumber),
            jit_search_entries:  (.[5] | tonumber),
            jit_choice_push:     (.[6] | tonumber),
            jit_choice_pop:      (.[7] | tonumber),
            jit_exec_ns:         (.[8] | tonumber),
            jit_interp_ns:       (.[9] | tonumber),
            total_ns:            (.[10] | tonumber)
        }
      })
    | add // {}
')

# Read the PHP probe JSON (one entry per scenario, each with a "name" field)
php_probe_text=$(cat "$php_path")
php_probe_obj=$(echo "$php_probe_text" | jq '
    # Convert array of {name, ...} to a single object keyed by name,
    # dropping the "name" field from each entry.
    map({ key: .name, value: (del(.name)) }) | from_entries
')

# Build the full baseline document
baseline_json=$(jq -n \
    --argjson before "$before" \
    --arg captured_at "$captured_at" \
    --argjson c_probe "$c_probe_obj" \
    --argjson php_probe "$php_probe_obj" \
    '{
        schema_version: 1,
        captured_at: $captured_at,
        captured_before_optimization: $before,
        probe_iterations: 100000,
        tokenize_outer_iterations: 10000,
        php_probe_iterations: 10000,
        c_probe: $c_probe,
        php_probe: $php_probe
    }')

# Write the output
mkdir -p "$(dirname "$out_path")"
echo "$baseline_json" | jq '.' > "$out_path"

c_count=$(echo "$c_probe_obj" | jq 'length')
php_count=$(echo "$php_probe_obj" | jq 'length')
echo "wrote $out_path ($c_count C scenarios, $php_count PHP scenarios)"
