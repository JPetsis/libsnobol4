#!/usr/bin/env bash
# Stress runner: executes PHPUnit repeatedly to confirm heap corruption is reproducible

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

RUNS=${1:-20}
echo "=== PHPUnit Stress Runner ==="
echo "Running PHPUnit $RUNS times to detect heap corruption..."
echo ""

for i in $(seq 1 $RUNS); do
    echo "Run $i/$RUNS..."
    if ! vendor/bin/phpunit --no-coverage 2>&1 | tee /tmp/phpunit-run-$i.log; then
        echo ""
        echo "✗ PHPUnit FAILED on run $i"
        echo "Check /tmp/phpunit-run-$i.log for details"
        exit 1
    fi

    # Check for heap corruption in output
    if grep -q "zend_mm_heap corrupted" /tmp/phpunit-run-$i.log; then
        echo ""
        echo "✗ HEAP CORRUPTION detected on run $i"
        echo "See /tmp/phpunit-run-$i.log"
        exit 1
    fi

    echo "  ✓ Run $i passed"
done

echo ""
echo "✓ All $RUNS runs completed successfully without heap corruption"

