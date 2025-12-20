#!/bin/bash
# Test runner script with output capture
cd /var/www/html
echo "Building extension..."
make build
echo ""
echo "Running verification test..."
php dev/verify_fixes.php
echo ""
echo "Exit code: $?"

