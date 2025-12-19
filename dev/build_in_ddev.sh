#!/usr/bin/env bash
# dev/build_in_ddev.sh - Build the extension inside DDEV

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if command -v ddev &> /dev/null && ddev describe &> /dev/null 2>&1; then
    echo "Building extension inside DDEV..."
    ddev exec "cd /var/www/html && make build"
else
    echo "DDEV not available, building locally..."
    cd "$PROJECT_ROOT"
    make build
fi

echo "Build complete!"

