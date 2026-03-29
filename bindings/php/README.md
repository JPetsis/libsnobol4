# libsnobol4 PHP Binding

SNOBOL4-style pattern matching for PHP. This binding provides a high-performance PHP extension that implements SNOBOL4
pattern matching capabilities.

## Requirements

- PHP 8.0+ (developed with PHP 8.4)
- CMake 3.16+
- C compiler (GCC, Clang, or MSVC)

## Quick Start with DDEV (Recommended)

The easiest way to get started is using [DDEV](https://ddev.com/):

```bash
cd bindings/php
ddev start
```

This will:

1. Start a PHP 8.4 container
2. Build the libsnobol4 extension from the `core/` directory
3. Enable the extension automatically

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
echo "extension=snobol" | sudo tee /etc/php/8.4/mods-available/snobol.ini
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

```php
<?php
use Snobol\Builder;
use Snobol\PatternHelper;

// Capture digits after "id:"
$pattern = Builder::concat([
    Builder::lit("id:"),
    Builder::cap(0, Builder::span("0123456789")),
    Builder::assign(0, 0)  // Assign capture 0 to variable v0
]);

$result = PatternHelper::matchOnce($pattern, "id:12345");
// Returns: ['v0' => '12345', '_match_len' => 8, '_output' => '']
```

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

| Method                     | Description                    |
|----------------------------|--------------------------------|
| `lit($text)`               | Literal string match           |
| `span($set)`               | Match run of characters in set |
| `brk($set)`                | Match until character in set   |
| `any($set)`                | Match any single character     |
| `notany($set)`             | Match any character NOT in set |
| `len($n)`                  | Match exactly n characters     |
| `arbno($sub)`              | Zero or more repetitions       |
| `repeat($sub, $min, $max)` | Bounded repetition             |
| `cap($reg, $sub)`          | Capture match into register    |
| `assign($var, $reg)`       | Assign register to variable    |
| `concat($parts)`           | Concatenate patterns           |
| `alt($left, $right)`       | Alternation (OR)               |
| `emit($text)`              | Emit literal to output         |
| `emitRef($reg)`            | Emit capture to output         |

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

## Template Syntax

Templates support variable references and transformations:

| Syntax            | Description                    |
|-------------------|--------------------------------|
| `$v0`, `$v1`, ... | Variable reference             |
| `${v0}`           | Braced variable reference      |
| `${v0.upper()}`   | Uppercase transformation       |
| `${v0.length()}`  | Length of captured value       |
| `$TABLE['key']`   | Table lookup with literal key  |
| `$TABLE[$v0]`     | Table lookup with variable key |

## Running Tests

```bash
# Install dependencies
composer install

# Run PHPUnit tests
vendor/bin/phpunit tests/php

# Run with coverage
vendor/bin/phpunit tests/php --coverage-html build/coverage
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
