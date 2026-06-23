# libsnobol4 PHP Binding

SNOBOL4-style pattern matching for PHP. This binding provides a high-performance PHP extension that implements SNOBOL4
pattern matching capabilities.

## Requirements

- PHP 8.0+ (developed with PHP 8.5)
- CMake 3.16+
- C compiler (GCC, Clang, or MSVC)

## Quick Start with DDEV (Recommended)

The easiest way to get started is using [DDEV](https://ddev.com/):

```bash
cd bindings/php
ddev start
```

This will:

1. Start a PHP 8.5 container
2. Build the libsnobol4 extension from the `core/` directory
3. Enable the extension automatically

> **Tip:** After initial setup, you can rebuild the extension without restarting DDEV by running:
> ```bash
> ddev build-snobol-extension
> ```
> This command reliably regenerates the amalgam, cleans stale artifacts, and reloads PHP-FPM.

Verify installation:

```bash
ddev exec php -m | grep snobol
# Output: snobol
```

## Native Build (Without DDEV)

### Prerequisites

- PHP development headers (`php-dev` or `php-devel` package)
- CMake 3.16+
- C compiler

### Build Steps

```bash
# From project root
cd bindings/php

# Configure with CMake
cmake -B build -DBUILD_PHP=ON

# Build
cmake --build build

# Install (may require sudo)
cmake --install build
```

### Enable Extension

Add to your `php.ini`:

```ini
extension = snobol
```

Or create a dedicated ini file:

```bash
echo "extension=snobol" | sudo tee /etc/php/8.5/mods-available/snobol.ini
sudo phpenmod snobol
```

## Usage

### Basic Pattern Matching

```php
<?php
use Snobol\PatternHelper;

// Match "hello" in a string
$result = PatternHelper::matchOnce("'hello'", "hello world");
// Returns: ['_match_len' => 5, '_output' => '']

// Using the Builder API
use Snobol\Builder;

$pattern = Builder::lit("hello");
$result = PatternHelper::matchOnce($pattern, "hello world");
```

### Character Classes

```php
<?php
use Snobol\Builder;
use Snobol\PatternHelper;

// Match digits
$pattern = Builder::span("0123456789");
$result = PatternHelper::matchOnce($pattern, "123abc");

// Match letters
$pattern = Builder::span("abcdefghijklmnopqrstuvwxyz");
$result = PatternHelper::matchOnce($pattern, "abc123");
```

### Captures and Assignments

`Builder::cap(reg, sub)` captures the text matched by `sub` into capture
register `reg` (0–255).  Capture register `reg` is also exposed in the
match result as `v<reg>`, so you don't need a separate `assign()` call:

```php
<?php
use Snobol\Builder;
use Snobol\PatternHelper;

// Capture digits after "id:" — register 0 is exposed as 'v0' automatically
$pattern = Builder::concat([
    Builder::lit("id:"),
    Builder::cap(0, Builder::span("0123456789")),
]);

$result = PatternHelper::matchOnce($pattern, "id:12345");
// Returns: ['v0' => '12345', '_match_len' => 8, '_output' => '']
```

Multiple captures:

```php
$ast = Builder::concat([
    Builder::cap(0, Builder::span("a-z")),  // captures "foo"
    Builder::lit("@"),
    Builder::cap(1, Builder::span("a-z")),  // captures "example"
    Builder::lit("."),
    Builder::cap(2, Builder::span("a-z")),  // captures "com"
]);
$result = PatternHelper::matchOnce($ast, "foo@example.com");
// Returns: ['v0' => 'foo', 'v1' => 'example', 'v2' => 'com', '_match_len' => 13, ...]
```

> **Why the implicit `v<reg>` exposure?**
> Before this feature, `Builder::cap(reg, sub)` required a separate
> `Builder::assign(var, reg)` call to make captures visible in the
> match result.  The `OP_CAP_END` VM opcode now also writes
> `vm.var_start[reg]` / `vm.var_end[reg]` and bumps `vm.var_count`, so
> the `v<reg>` key is populated automatically.  This makes
> `Builder::cap(reg, sub)` a self-contained capture primitive.

### Pattern Replacement

```php
<?php
use Snobol\Builder;
use Snobol\PatternHelper;

// Replace digits with bracketed version
$pattern = Builder::cap(0, Builder::span("0123456789"));
$result = PatternHelper::replace($pattern, "[$v0]", "id:123 code:456");
// Returns: "id:[123] code:[456]"

// With transformations
$pattern = Builder::concat([
    Builder::lit("name:"),
    Builder::cap(1, Builder::span("abcdefghijklmnopqrstuvwxyz"))
]);
$result = PatternHelper::replace($pattern, "NAME:\${v1.upper()}", "name:alice");
// Returns: "NAME:ALICE"
```

### Labelled Control Flow

Labels and forward gotos allow non-backtracking control-flow inside a pattern:

```php
<?php
use Snobol\Builder;
use Snobol\PatternHelper;

// Label wrapping a sub-pattern
$ast = Builder::label('start', Builder::span('A-Za-z'));
$result = PatternHelper::matchOnce($ast, 'hello');
// Returns: ['_match_len' => 5, '_output' => '']

// Forward goto — jumps past a sub-pattern to a later label
// Pattern: lit(">>") :(content) content: SPAN('A-Za-z')
$ast = Builder::concat([
    Builder::lit('>>'),
    Builder::goto('content'),
    Builder::label('content', Builder::span('A-Za-z')),
]);
$result = PatternHelper::matchOnce($ast, '>>hello');
// Returns: ['_match_len' => 7, '_output' => '']

// Compile-time validation: duplicate labels or undefined gotos are
// rejected before execution; no runtime failure possible.
```

> **Note:** Duplicate label names or a goto referencing an undefined label
> are caught at compile time, not at runtime.

### Using Tables

```php
<?php
use Snobol\Table;
use Snobol\PatternHelper;

// Create a table
$table = new Table("mappings");
$table->set("hello", "greeting");
$table->set("world", "planet");

// Use in template substitution
$pattern = Builder::cap(0, Builder::span("abcdefghijklmnopqrstuvwxyz"));
// Template with table lookup: $TABLE['key']
```

## API Reference

### PatternHelper

High-level helper methods for common pattern operations:

| Method                                         | Description                      |
|------------------------------------------------|----------------------------------|
| `matchOnce($pattern, $subject, $options = [])` | Find first match                 |
| `matchAll($pattern, $subject, $options = [])`  | Find all matches                 |
| `split($pattern, $subject)`                    | Split string by pattern          |
| `replace($pattern, $replacement, $subject)`    | Replace matches                  |
| `fromString($source)`                          | Compile pattern from string      |
| `fromAst($ast)`                                | Compile pattern from Builder AST |

### Builder

Fluent API for constructing patterns:

| Method                                | Description                               |
|---------------------------------------|-------------------------------------------|
| `lit($text)`                          | Literal string match                      |
| `span($set)`                          | Match run of characters in set            |
| `brk($set)`                           | Match until character in set              |
| `any($set)`                           | Match any single character                |
| `notany($set)`                        | Match any character NOT in set            |
| `len($n)`                             | Match exactly n characters                |
| `arbno($sub)`                         | Zero or more repetitions                  |
| `repeat($sub, $min, $max)`            | Bounded repetition                        |
| `cap($reg, $sub)`                     | Capture match into register               |
| `assign($var, $reg)`                  | Assign register to variable               |
| `concat($parts)`                      | Concatenate patterns                      |
| `alt($left, $right)`                  | Alternation (OR)                          |
| `emit($text)`                         | Emit literal to output                    |
| `emitRef($reg)`                       | Emit capture to output                    |
| `anchor($type)`                       | Start or end anchor                       |
| `label($name, $sub)`                  | Named label wrapping a pattern            |
| `goto($label)`                        | Unconditional goto label                  |
| `dynamicEval($expr)`                  | Dynamic pattern evaluation                |
| `tableAccess($table, $key)`           | Table lookup                              |
| `tableUpdate($table, $key, $value)`   | Table update                              |
| `arrayAccess($array, $index)`         | Array indexed lookup                      |
| `arrayUpdate($array, $index, $value)` | Array indexed update                      |
| `arrayCreate($array, $size)`          | Array creation declaration                |
| `breakx($set)`                        | Break with O(n) pre-scan optimisation     |
| `bal($open, $close)`                  | Balanced delimiter matching               |
| `fence()`                             | Backtracking cut                          |
| `rem()`                               | Match remainder of subject                |
| `rpos($n)`                            | Succeed at n codepoints from end          |
| `rtab($n)`                            | Advance to n codepoints from end          |
| `arb()`                               | Match arbitrary characters (0 or more)    |
| `pos($n)`                             | Succeed at n codepoints from start        |
| `tab($n)`                             | Advance cursor to n codepoints from start |
| `abort()`                             | Terminate entire match as failure         |
| `fail()`                              | Force failure / trigger backtracking      |
| `succeed()`                           | Force immediate success                   |

### Pattern

Low-level pattern object:

```php
$pattern = Pattern::compileFromAst($ast);
$result = $pattern->match($subject);
$output = $pattern->subst($subject, $template);
```

### Table

Associative table for runtime lookups:

```php
$table = new Table("name");
$table->set($key, $value);
$value = $table->get($key);
```

### Array_

Indexed sparse array with integer keys:

```php
$arr = new Array_(10);  // Optional size hint
$arr->set(1, "hello");
$value = $arr->get(1);       // "hello"
$arr->has(1);                // true
$arr->delete(1);             // Remove entry
$arr->size();                // Number of populated entries
$arr->keys();                // Array of integer keys
$arr->values();              // Array of values
$arr->clear();               // Remove all entries
```

### PatternCache

LRU cache for compiled patterns, keyed by a user-supplied string.  Use when the
key is in the application and the pattern body must be built from a closure:

```php
use Snobol\PatternCache;
use Snobol\PatternHelper;

$cache = new PatternCache(128);  // optional capacity, default 128
$ast = \Snobol\Builder::lit("hello");
$pattern = $cache->get('hello-key', fn() => PatternHelper::fromAst($ast));
// Second call with the same key returns the same Pattern instance.
$same = $cache->get('hello-key', fn() => PatternHelper::fromAst($ast));
```

When the cache is full, the least-recently-used entry is evicted.

### DynamicPatternCache

Caches patterns by their **source text** (the string passed to `Pattern::fromString`).
Use when the pattern is already in source form:

```php
use Snobol\DynamicPatternCache;

$cache = new DynamicPatternCache();

// Compile once, reuse for many evaluations.
$result = $cache->evaluate("'world'", "hello world");
// $result = ['cached' => false, 'evaluated' => true, 'matches' => [...]]

$result = $cache->evaluate("'world'", "world");
// $result = ['cached' => true,  'evaluated' => true, 'matches' => [...]]

$stats = $cache->stats();
// $stats = ['size' => 1, 'max_size' => 128, 'compile_count' => 1, 'evaluate_count' => 2]
```

`compile()` returns `['cached' => bool, 'compiled' => bool, 'pattern' => string]`.
`evaluate()` returns `['cached' => bool, 'evaluated' => bool, 'matches' => array|null]`.
Invalid patterns return `compiled => false` or `evaluated => false` without throwing.

## Template Syntax

Templates support variable references and transformations:

| Syntax            | Description                                          |
|-------------------|------------------------------------------------------|
| `$v0`, `$v1`, ... | Variable reference                                   |
| `${v0}`           | Braced variable reference                            |
| `${v0.upper()}`   | Unicode uppercase (Latin-1 + Latin Extended-A, ß→SS) |
| `${v0.lower()}`   | Unicode lowercase (Latin-1 + Latin Extended-A)       |
| `${v0.length()}`  | Length of captured value                             |
| `$TABLE['key']`   | Table lookup with literal key                        |
| `$TABLE[$v0]`     | Table lookup with variable key                       |

## Case-Insensitive Pattern Matching

`Pattern::fromString()` accepts a `caseInsensitive` option that compiles the
pattern with case-folded character classes:

```php
<?php
use Snobol\Pattern;

$pat = Pattern::fromString("'hello'", ['caseInsensitive' => true]);
$res = $pat->match('HELLO world');
// matches — $res['_match_len'] == 5

$pat2 = Pattern::fromString("'café'", ['caseInsensitive' => true]);
$res2 = $pat2->match("CAF\xC3\x89 dessert");
// matches CAFÉ case-insensitively
```

Coverage: ASCII, Latin-1 Supplement (U+00C0–U+00FF), Latin Extended-A (U+0100–U+017F).
Captured text always preserves the original case of the subject.

## Extension Load-Time Version Check

The `snobol` PHP extension verifies at load time that the linked `libsnobol4`
shared library has the same **major version** as the extension was compiled
against. If a mismatch is detected (e.g., you upgraded the C library without
rebuilding the extension), a `RuntimeException` is thrown and the extension
returns `FAILURE`. You can query the version from PHP:

```php
$v = snobol_get_api_version();
$major = ($v >> 16) & 0xFF;  // 0
$minor = ($v >> 8) & 0xFF;   // 7
$patch = $v & 0xFF;          // 0
```

For v0.11.0 this returns `0x00000B00` (2816 in decimal).

## Running Tests

This project has two `composer.json` files:

- `bindings/php/composer.json` — used inside DDEV (`/var/www/html`)
- `composer.json` (root) — for native (non-DDEV) development and IDE integration

### With DDEV

```bash
# Install/update dependencies
ddev composer install

# Run tests
ddev exec vendor/bin/phpunit tests/php

# Run with coverage
ddev exec vendor/bin/phpunit tests/php --coverage-html /tmp/coverage
```

### Without DDEV

```bash
# From the repository root
composer install

# Run tests
vendor/bin/phpunit bindings/php/tests/php
```

## Troubleshooting

### Extension Not Loading

```bash
# Check if extension is installed
php -m | grep snobol

# Check php.ini location
php --ini

# Check extension directory
php-config --extension-dir
```

### Build Errors

```bash
# Clean and rebuild
rm -rf build/
cmake -B build -DBUILD_PHP=ON
cmake --build build

# Check PHP development headers are installed
php-config --includes
```

### DDEV Issues

```bash
# Restart DDEV
ddev restart

# Check DDEV status
ddev status

# View build logs
ddev logs
```

## Performance Tips

1. **Use compiled patterns**: Patterns are cached by default. Use `['cache' => false]` to disable.
2. **Prefer Builder API**: The Builder API produces optimized AST structures.
3. **Use JIT for hot patterns**: Call `$pattern->setJit(true)` for frequently-used patterns.
4. **Minimize captures**: Only capture what you need; captures have overhead.

## License

Apache License, Version 2.0
