<?php
// Test file for SNOBOL4 PHP extension
require_once __DIR__.'/../vendor/autoload.php';

use Snobol\Builder;
use Snobol\Pattern;

echo "Testing SNOBOL4 PHP Extension\n";
echo "==============================\n\n";

// Build pattern:
// ( "id:" SPAN("0-9") @r0 ) -> capture digits after "id:" into register 0, assign to variable 0
$patAst = Builder::concat([
    Builder::lit("id:"),
    Builder::cap(0, Builder::span("0123456789")),
    Builder::assign(0, 0)
]);

echo "Pattern AST:\n";
print_r($patAst);
echo "\n";

// compile
try {
    $pat = Pattern::compileFromAst($patAst);
    echo "✓ Pattern compiled successfully\n\n";
} catch (Exception $e) {
    echo "✗ Compilation failed: " . $e->getMessage() . "\n";
    exit(1);
}

// Test 1: Match with valid input
echo "Test 1: Matching 'id:12345 rest of string'\n";
$input = "id:12345 rest of string";
$res = $pat->match($input);
echo "Result:\n";
var_dump($res);
echo "\n";

// Test 2: Match with no match
echo "Test 2: Matching 'no match here'\n";
$input2 = "no match here";
$res2 = $pat->match($input2);
echo "Result:\n";
var_dump($res2);
echo "\n";

// Test 3: Simple literal pattern
echo "Test 3: Simple literal pattern\n";
$simplePat = Builder::lit("hello");
$pat3 = Pattern::compileFromAst($simplePat);
$res3 = $pat3->match("hello world");
echo "Result for 'hello world':\n";
var_dump($res3);
echo "\n";

echo "All tests completed!\n";
