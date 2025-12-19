#!/usr/bin/env bash
# dev/trace_vm.sh - Enable/disable VM tracing for debug builds

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

show_help() {
    cat << EOF
Usage: $0 [on|off|status]

Enable or disable VM tracing for the SNOBOL4 extension.

Commands:
  on      - Enable tracing (sets SNOBOL_TRACE=1)
  off     - Disable tracing (unsets SNOBOL_TRACE)
  status  - Show current tracing status

When tracing is enabled, rebuild the extension with 'make clean && make build'
to compile with trace instrumentation.
EOF
}

case "${1:-status}" in
    on)
        export SNOBOL_TRACE=1
        echo "VM tracing ENABLED"
        echo "Run 'make clean && make build' to rebuild with tracing"
        echo ""
        echo "To persist this setting, add to your shell profile:"
        echo "  export SNOBOL_TRACE=1"
        ;;
    off)
        unset SNOBOL_TRACE
        echo "VM tracing DISABLED"
        echo "Run 'make clean && make build' to rebuild without tracing"
        ;;
    status)
        if [ -n "${SNOBOL_TRACE}" ]; then
            echo "VM tracing is ENABLED (SNOBOL_TRACE=${SNOBOL_TRACE})"
        else
            echo "VM tracing is DISABLED"
        fi
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        echo "Error: Unknown command '$1'" >&2
        echo ""
        show_help
        exit 1
        ;;
esac

