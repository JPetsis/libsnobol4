<?php

/**
 * AST Tree Visualizer
 *
 * This script parses SNOBOL pattern strings and displays their AST (Abstract Syntax Tree)
 * in a visual tree format to help understand the pattern structure.
 */

require_once __DIR__.'/../php-src/Lexer.php';
require_once __DIR__.'/../php-src/Parser.php';
require_once __DIR__.'/../php-src/Builder.php';

use Snobol\Parser;

/**
 * Print AST as a visual tree structure
 *
 * @param  array  $node  The AST node to print
 * @param  string  $prefix  Current line prefix for indentation
 * @param  bool  $isLast  Whether this is the last child node
 * @param  int  $depth  Current depth in the tree
 */
function printAstTree(array $node, string $prefix = '', bool $isLast = true, int $depth = 0): void
{
    // Color codes for terminal output
    $colors = [
        'type' => "\033[1;36m",      // Cyan (bold) for node types
        'key' => "\033[0;33m",       // Yellow for keys
        'value' => "\033[0;32m",     // Green for values
        'string' => "\033[0;35m",    // Magenta for string literals
        'number' => "\033[0;34m",    // Blue for numbers
        'reset' => "\033[0m",        // Reset color
        'branch' => "\033[0;37m",    // White for tree branches
    ];

    // Tree drawing characters
    $branch = $colors['branch'].($isLast ? '└── ' : '├── ').$colors['reset'];
    $vertical = $colors['branch'].'│   '.$colors['reset'];
    $space = '    ';

    // Print current node
    if ($depth === 0) {
        echo $colors['type']."ROOT".$colors['reset']."\n";
    }

    // Get node type and other properties
    $type = $node['type'] ?? 'unknown';
    $nodeStr = $prefix.$branch.$colors['type'].$type.$colors['reset'];

    // Add relevant info based on type
    switch ($type) {
        case 'lit':
            $text = $node['text'] ?? '';
            $nodeStr .= " ".$colors['string']."'".addcslashes($text, "\n\r\t")."'".$colors['reset'];
            break;

        case 'span':
        case 'break':
        case 'any':
        case 'notany':
            $set = $node['set'] ?? '';
            $displaySet = strlen($set) > 20 ? substr($set, 0, 20).'...' : $set;
            $nodeStr .= " ".$colors['string']."[".addcslashes($displaySet, "\n\r\t")."]".$colors['reset'];
            break;

        case 'len':
            $n = $node['n'] ?? 0;
            $nodeStr .= " ".$colors['number'].$n.$colors['reset'];
            break;

        case 'cap':
            $reg = $node['reg'] ?? 0;
            $nodeStr .= " ".$colors['key']."r".$reg.$colors['reset'];
            break;

        case 'assign':
            $var = $node['var'] ?? 0;
            $reg = $node['reg'] ?? 0;
            $nodeStr .= " ".$colors['key']."v".$var." ← r".$reg.$colors['reset'];
            break;

        case 'repeat':
            $min = $node['min'] ?? 0;
            $max = $node['max'] ?? -1;
            $maxStr = $max === -1 ? '∞' : $max;
            $nodeStr .= " ".$colors['number']."{".$min.",".$maxStr."}".$colors['reset'];
            break;

        case 'anchor':
            $atype = $node['atype'] ?? '';
            $nodeStr .= " ".$colors['value'].$atype.$colors['reset'];
            break;
    }

    echo $nodeStr."\n";

    // Prepare prefix for children
    $childPrefix = $prefix.($isLast ? $space : $vertical);

    // Recursively print child nodes
    $childNodes = [];

    // Collect child nodes based on structure
    if (isset($node['parts']) && is_array($node['parts'])) {
        // concat node
        $childNodes = array_map(fn($part) => ['label' => 'part', 'node' => $part], $node['parts']);
    } elseif (isset($node['left']) && isset($node['right'])) {
        // alt node
        $childNodes = [
            ['label' => 'left', 'node' => $node['left']],
            ['label' => 'right', 'node' => $node['right']],
        ];
    } elseif (isset($node['sub']) && is_array($node['sub'])) {
        // arbno, repeat, cap nodes
        $childNodes = [['label' => 'sub', 'node' => $node['sub']]];
    }

    // Print children
    $childCount = count($childNodes);
    foreach ($childNodes as $idx => $child) {
        $isLastChild = ($idx === $childCount - 1);
        printAstTree($child['node'], $childPrefix, $isLastChild, $depth + 1);
    }
}

/**
 * Parse and display a pattern's AST
 */
function displayPattern(string $pattern, string $description = ''): void
{
    $colors = [
        'header' => "\033[1;37m",    // White (bold)
        'pattern' => "\033[0;36m",   // Cyan
        'reset' => "\033[0m",
    ];

    echo "\n".str_repeat('=', 80)."\n";
    if ($description) {
        echo $colors['header'].$description.$colors['reset']."\n";
    }
    echo $colors['header']."Pattern: ".$colors['pattern'].$pattern.$colors['reset']."\n";
    echo str_repeat('=', 80)."\n";

    try {
        $parser = new Parser($pattern);
        $ast = $parser->parse();

        printAstTree($ast);
    } catch (Exception $e) {
        echo "\033[0;31mError: ".$e->getMessage()."\033[0m\n";
    }
}

// ============================================================================
// Example Patterns
// ============================================================================

echo "\n";
echo "\033[1;35m╔════════════════════════════════════════════════════════════════════════════╗\033[0m\n";
echo "\033[1;35m║                     SNOBOL4 Pattern AST Visualizer                        ║\033[0m\n";
echo "\033[1;35m╚════════════════════════════════════════════════════════════════════════════╝\033[0m\n";

// Simple literal
displayPattern("'hello'", "Simple Literal");

// Concatenation
displayPattern("'hello' ' ' 'world'", "Concatenation");

// Alternation
displayPattern("'cat' | 'dog'", "Alternation (OR)");

// Character class
displayPattern("[a-z]", "Character Class");

// Negated character class
displayPattern("[^0-9]", "Negated Character Class");

// Repetition operators
displayPattern("'a'*", "Arbno (Zero or More)");
displayPattern("'a'+", "One or More");
displayPattern("'a'?", "Optional (Zero or One)");

// Built-in functions
displayPattern("SPAN('0-9')", "SPAN - Match run of digits");
displayPattern("BREAK(' ')", "BREAK - Match until space");
displayPattern("LEN(5)", "LEN - Match exactly 5 characters");

// Complex: email-like pattern
displayPattern("SPAN('a-z') '@' SPAN('a-z') '.' SPAN('a-z')", "Complex - Email-like Pattern");

// Grouping and alternation
displayPattern("('cat' | 'dog') 's'", "Grouping with Alternation");

// Nested repetition
displayPattern("('ab')*", "Nested Repetition");

// Capture
displayPattern("'id:' @r1(SPAN('0-9'))", "Capture - Extract digits into register r1");

// Complex nested pattern
displayPattern("'[' (SPAN('a-z') (',' SPAN('a-z'))*)? ']'", "Complex - List Pattern with Optional Elements");

// Mixed operators
displayPattern("'^' SPAN('A-Z') SPAN('a-z')* '$'", "Mixed - Anchored capitalized word");

// Multiple alternations
displayPattern("'red' | 'green' | 'blue'", "Multiple Alternations");

echo "\n\033[1;32m✓ AST visualization complete!\033[0m\n\n";
echo "Legend:\n";
echo "  \033[1;36mNode Types\033[0m - The type of operation (lit, concat, alt, etc.)\n";
echo "  \033[0;35mString Values\033[0m - Literal text or character sets\n";
echo "  \033[0;34mNumbers\033[0m - Numeric parameters (length, repetition counts)\n";
echo "  \033[0;33mReferences\033[0m - Register/variable references\n";
echo "\n";