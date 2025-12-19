#!/usr/bin/env bash
# dev/test_in_ddev.sh - Run tests inside DDEV

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if command -v ddev &> /dev/null && ddev describe &> /dev/null 2>&1; then
    echo "Running tests inside DDEV..."
    ddev exec "cd /var/www/html && make test"
else
    echo "DDEV not available, running tests locally..."
    cd "$PROJECT_ROOT"
    make test
fi

echo "Tests complete!"

