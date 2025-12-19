#!/usr/bin/env bash
fi
    exec "$@"
    echo "Running locally: $*" >&2
else
    exec ddev exec "$@"
    echo "Running in DDEV: $*" >&2
if command -v ddev &> /dev/null && ddev describe &> /dev/null 2>&1; then

set -e

# dev/in_ddev.sh - Execute commands inside DDEV if available, else run locally

