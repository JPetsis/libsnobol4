#!/bin/bash
# Run PHPUnit under Valgrind

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

if ! command -v valgrind &> /dev/null; then
    # If valgrind is missing, check if we are in a DDEV project and can run it there
    if command -v ddev &> /dev/null && [ -d .ddev ]; then
        echo "valgrind not found locally. Running inside DDEV..."
        # Execute the same script inside the container
        # We assume the script path relative to project root is the same
        RELATIVE_SCRIPT_PATH="${BASH_SOURCE[0]}"
        # If the script path is absolute, try to make it relative (simple attempt) or fallback to known path
        if [[ "$RELATIVE_SCRIPT_PATH" == /* ]]; then
             RELATIVE_SCRIPT_PATH="dev/valgrind_phpunit.sh"
        fi
        
        ddev exec "./$RELATIVE_SCRIPT_PATH" "$@"
        exit $?
    fi

    echo "Error: valgrind not found. Please install it."
    exit 1
fi

echo "Running PHPUnit under Valgrind..."
export USE_ZEND_ALLOC=0
export ZEND_DONT_UNLOAD_MODULES=1

valgrind --tool=memcheck \
         --error-exitcode=1 \
         --leak-check=full \
         --show-leak-kinds=definite,indirect \
         --suppressions="$SCRIPT_DIR/valgrind.supp" \
         --num-callers=30 \
         php vendor/bin/phpunit

