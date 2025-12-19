<?php
// Simple smoke test for PatternHelper and PatternCache
require_once __DIR__.'/../vendor/autoload.php';

use Snobol\Builder;
use Snobol\PatternCache;
use Snobol\PatternHelper;

echo "=== PatternHelper Smoke Test ===\n\n";

// Test 1: fromAst
echo "Test 1: PatternHelper::fromAst()\n";
try {
    $ast = Builder::lit("test");
    $pattern = PatternHelper::fromAst($ast);
    echo "✓ Pattern compiled from AST\n";
} catch (\Exception $e) {
    echo "✗ Failed: ".$e->getMessage()."\n";
    exit(1);
}

// Test 2: matchOnce
echo "\nTest 2: PatternHelper::matchOnce()\n";
try {
    $ast = Builder::lit("hello");
    $result = PatternHelper::matchOnce($ast, "hello world");
    if ($result !== false) {
        echo "✓ matchOnce returned: ".print_r($result, true)."\n";
    } else {
        echo "✗ matchOnce returned false\n";
    }
} catch (\Exception $e) {
    echo "✗ Failed: ".$e->getMessage()."\n";
}

// Test 3: PatternCache
echo "\nTest 3: PatternCache\n";
try {
    $cache = new PatternCache(5);
    echo "✓ PatternCache created with capacity 5\n";
    echo "  Size: ".$cache->size()."\n";
} catch (\Exception $e) {
    echo "✗ Failed: ".$e->getMessage()."\n";
}

echo "\n=== Smoke Test Complete ===\n";

