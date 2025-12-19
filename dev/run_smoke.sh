#!/usr/bin/env bash
# dev/run_smoke.sh - Run smoke tests against public/ endpoints and CLI

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "Running smoke tests..."
echo ""

# Check if we're in DDEV
if command -v ddev &> /dev/null && ddev describe &> /dev/null 2>&1; then
    IN_DDEV=1
    BASE_URL="https://$(ddev describe -j | grep -o '"urls":\s*\[[^]]*\]' | grep -o 'https://[^"]*' | head -n1)"
else
    IN_DDEV=0
    BASE_URL="http://localhost:8080"
fi

# Test 1: CLI extension is loaded
echo "Test 1: Checking if snobol extension is loaded..."
if [ $IN_DDEV -eq 1 ]; then
    if ddev exec php -m | grep -q snobol; then
        echo "✓ Extension loaded in CLI"
    else
        echo "✗ Extension NOT loaded in CLI" >&2
        exit 1
    fi
else
    if php -m | grep -q snobol; then
        echo "✓ Extension loaded in CLI"
    else
        echo "✗ Extension NOT loaded in CLI" >&2
        exit 1
    fi
fi

# Test 2: Basic pattern compile
echo "Test 2: Testing basic pattern compile..."
if [ $IN_DDEV -eq 1 ]; then
    RESULT=$(ddev exec php -r 'require_once "php-src/builder.php"; use Snobol\Builder; use Snobol\Pattern; $ast = Builder::lit("test"); $pat = Pattern::compileFromAst($ast); echo "OK";' 2>&1)
else
    RESULT=$(php -r 'require_once "php-src/builder.php"; use Snobol\Builder; use Snobol\Pattern; $ast = Builder::lit("test"); $pat = Pattern::compileFromAst($ast); echo "OK";' 2>&1)
fi

if echo "$RESULT" | grep -q "OK"; then
    echo "✓ Pattern compile successful"
else
    echo "✗ Pattern compile failed: $RESULT" >&2
    exit 1
fi

# Test 3: Web endpoint (if DDEV or local server)
if [ $IN_DDEV -eq 1 ]; then
    echo "Test 3: Testing web endpoint at $BASE_URL..."
    HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL" || echo "000")
    if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "302" ]; then
        echo "✓ Web endpoint accessible (HTTP $HTTP_CODE)"
    else
        echo "⚠ Web endpoint returned HTTP $HTTP_CODE (might be expected)"
    fi
else
    echo "Test 3: Skipping web endpoint test (not in DDEV)"
fi

echo ""
echo "Smoke tests complete!"

