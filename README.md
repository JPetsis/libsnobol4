# SNOBOL4 for PHP

A high-performance PHP extension that implements [SNOBOL4](https://en.wikipedia.org/wiki/SNOBOL)-style string pattern
matching. This extension brings the powerful, expressive pattern matching capabilities of SNOBOL4 to modern PHP
applications, offering a robust alternative to PCRE (Perl Compatible Regular Expressions) for complex string
manipulation tasks.

## Features

* **Native C Extension:** Built for performance, integrating directly with PHP's core.
* **Expressive Pattern Building:**
    * **Textual Parser:** Write patterns as strings using a familiar SNOBOL-like syntax.
    * **Builder API:** Construct patterns programmatically using a fluent AST-based builder.
* **Rich Pattern Primitives:**
    * **Literals:** Exact string matching (`'text'`).
    * **Concatenation & Alternation:** Combine patterns sequentially (`P1 P2`) or as alternatives (`P1 | P2`).
    * **Span & Break:** Match runs of characters (`SPAN('0-9')`) or scan until a set of characters.
    * **Any & NotAny:** Match single characters based on sets (`[abc]`, `[^abc]`).
    * **Arbno:** Match an arbitrary number of repetitions (`P*`).
    * **Len:** Match specific lengths (`LEN(5)`).
* **Captures & Assignments:**
    * Capture substrings into registers during matching (`@r1(...)`).
    * Assign captured values to variables, returned as an associative array upon successful match.
* **Dynamic Evaluation:** Trigger PHP callbacks during the matching process for complex, logic-driven matching.

## Requirements

* PHP 8.0+ (Developed with PHP 8.4)
* Standard build tools (`build-essential`, `autoconf`, `php-dev`) for manual compilation.
* [DDEV](https://ddev.com/) for the recommended containerised dev environment.
* Composer (for running the PHP test suite via PHPUnit).

## Installation

### Using DDEV (Recommended for Development)

This project includes a fully configured [DDEV](https://ddev.com/) environment that automatically builds and installs
the extension.

1. **Clone the repository:**
   ```bash
   git clone <repository-url>
   cd snobol4-ddev
   ```

2. **Start the environment:**
   ```bash
   ddev start
   ```
   During startup, DDEV will:
    - Build the `snobol` extension in a temporary directory (`/tmp/snobol_build`).
    - Install it into the PHP extension directory inside the container.
    - Enable `snobol.so` for both CLI and FPM.

3. **Verify installation:**
   ```bash
   ddev exec php -m | grep snobol
   ```

### Manual Compilation (outside DDEV)

If you are not using DDEV, you can build the extension manually on any Linux/Unix system.

1. **Navigate to the source directory:**
   ```bash
   cd snobol4-php
   ```

2. **Prepare the build environment:**
   ```bash
   phpize
   ```

3. **Configure and build:**
   ```bash
   ./configure
   make
   sudo make install
   ```

4. **Enable the extension:**
   Add the following line to your `php.ini` file:
   ```ini
   extension=snobol.so
   ```

## Usage

### Textual Pattern Parsing

You can now define patterns using a concise textual syntax, similar to Regex but with SNOBOL semantics.

```php
<?php
use Snobol\PatternHelper;

// Match "id:" followed by digits
$pattern = PatternHelper::fromString("'id:' SPAN('0-9')");

if (PatternHelper::matchOnce($pattern, "id:12345")) {
    echo "Match found!";
}
```

**Supported Grammar:**

- **Literals:** `'text'` or `"text"`
- **Concatenation:** Implicit (space), e.g., `'A' 'B'` matches "AB".
- **Alternation:** `|`, e.g., `'A' | 'B'`.
- **Repetition:** `*` (Arbno), `+` (1 or more), `?` (0 or 1). E.g., `'A'*`.
- **Character Classes:** `[a-z]`, `[^0-9]` (Negation).
- **Built-ins:** `SPAN('...')`, `BREAK('...')`, `LEN(n)`, `ANY('...')`, `NOTANY('...')`.
- **Captures:** `@rN(...)` captures content into register `N`.
- **Grouping:** `(...)` for precedence.

### Capturing Values

You can capture parts of the matched string into "registers" and assign them to variables. The `match()` method returns
an array of these assignments on success.

**Using Textual Syntax:**

```php
<?php
use Snobol\PatternHelper;

// Capture digits into register 1
$pat = PatternHelper::fromString("'id:' @r1(SPAN('0-9'))");
$result = PatternHelper::matchOnce($pat, "id:555");

if ($result) {
    // Note: VM currently maps register N to variable 'vN'
    echo "Found ID: " . $result['v1']; // Outputs 555
}
```

**Using Builder API:**
```php
<?php
use Snobol\Builder;
use Snobol\Pattern;

// Pattern: ( "id:" SPAN("0-9") @reg0 ) -> assign reg0 to var0
$patternAst = Builder::concat([
    Builder::lit("id:"),
    Builder::cap(0,                  // Capture into register 0
        Builder::span("0123456789")
    ),
    Builder::assign(0, 0)            // Assign register 0 to variable 0 (key "v0")
]);

$pat = Pattern::compileFromAst($patternAst);
$result = $pat->match("Item id:555 details...");

if ($result) {
    // $result will contain: ['v0' => '555']
    echo "Found ID: " . $result['v0'];
}
```

### Pattern Helper API (Convenience Methods)

For common use cases, the `Snobol\PatternHelper` class provides high-level convenience methods:

```php
<?php
use Snobol\Builder;
use Snobol\PatternHelper;

// Quick one-liner match using string pattern
$result = PatternHelper::matchOnce("'hello'", "hello world");

// Find all occurrences
$matches = PatternHelper::matchAll("SPAN('0-9')", "id:123 code:456");
// Returns: [['v0' => '123'], ['v0' => '456']] (if captured) or just matches

// Split by pattern
$segments = PatternHelper::split("','", "a,b,c");
// Returns: ['a', 'b', 'c']

// Replace matches
$result = PatternHelper::replace("'old'", "new", "old text with old word");
// Returns: "new text with new word"

// Full string match check
$isFullMatch = PatternHelper::matchOnce("'exact'", "exact", ['full' => true]);
```

**Features:**

- **Textual Parsing:** Automatically parses string patterns using `Snobol\Parser`.
- **Automatic Caching:** Compiles and caches patterns for better performance.
- **Flexible Input:** Accepts string patterns, AST arrays, or precompiled `Pattern` objects.
- **Options:**
    - `['cache' => false]` to bypass cache.
    - `['full' => true]` to enforce full-string matching (fails if trailing characters remain).

### Pattern Builder API

The `Snobol\Builder` class provides static methods to construct pattern nodes:

| Method                           | Description                                                                     |
|:---------------------------------|:--------------------------------------------------------------------------------|
| `lit(string $s)`                 | Matches the literal string `$s`.                                                |
| `concat(array $parts)`           | Matches a sequence of patterns.                                                 |
| `alt(array $left, array $right)` | Matches either `$left` OR `$right`.                                             |
| `span(string $set)`              | Matches a run of characters found in `$set`.                                    |
| `brk(string $set)`               | Matches until a character in `$set` is found.                                   |
| `any(string $set = null)`        | Matches any single character (optionally from `$set`).                          |
| `notany(string $set)`            | Matches any single character NOT in `$set`.                                     |
| `len(int $n)`                    | Matches exactly `$n` characters.                                                |
| `arbno(array $sub)`              | Matches arbitrary repetitions of the `$sub` pattern.                            |
| `cap(int $reg, array $sub)`      | Captures the match of `$sub` into register `$reg`.                              |
| `assign(int $var, int $reg)`     | Assigns the content of register `$reg` to output variable `$var` (key `v$var`). |

## Development

### Quick Start with Makefile

The project includes a `Makefile` to streamline development workflows:

```bash
# Show available targets
make help

# Build the extension (native or inside DDEV)
make build

# Run tests (C and PHP)
make test

# Clean build artifacts
make clean

# Install and enable the extension
make install
```

The Makefile automatically detects whether you're running inside DDEV or in a native environment. When running inside
DDEV it builds the extension from a temporary directory (`/tmp/snobol_build`) to avoid read-only mounts.

### Developer Tools

The `dev/` directory contains helper scripts:

- **`dev/build_in_ddev.sh`** - Build the extension inside DDEV.
- **`dev/test_in_ddev.sh`** - Run tests inside DDEV.
- **`dev/trace_vm.sh`** - Enable/disable VM tracing for debug builds (via `SNOBOL_TRACE=1`).
- **`dev/run_smoke.sh`** - Run smoke tests against the extension (CLI + web endpoint checks).
- **`dev/in_ddev.sh`** - Execute arbitrary commands in DDEV or locally.

Example usage:

```bash
# Enable VM tracing
./dev/trace_vm.sh on

# Rebuild with tracing
dev/build_in_ddev.sh

# Run smoke tests
./dev/run_smoke.sh
```

### Debugging with AddressSanitizer (ASan)

For identifying memory leaks and memory corruption, you can build the extension with ASan:

```bash
# Build with ASan
make build-asan

# Install the extension
make install
```

**Note:** When the extension is built with ASan, it is NOT enabled globally by default to avoid breaking PHP tools like
Composer (which would require `LD_PRELOAD` to run).

To use the ASan-built extension:

- **Testing:** `make test` or `make test-asan` will automatically handle the necessary environment variables (
  `LD_PRELOAD`).
- **Global Toggle:** Use `make enable` to enable it globally (warning: this will require `LD_PRELOAD` for ALL PHP
  commands) and `make disable` to disable it.
- **Composer:** Use `make composer <args>` (e.g., `make composer install`) if you need to run Composer while the ASan
  extension is enabled.

### Testing

The project includes two test suites:

**C Tests** (`tests/c/`)

- Minimal test runner for the VM (`snobol_vm.c`).
- No external dependencies required.
- Run with:

```bash
# From the project root
make test

# Or directly
cd tests/c
make test
```

**PHP Tests** (`tests/php/`)

- PHPUnit-based tests for the `Builder` and `Pattern` API.
- Requires Composer to install dev dependencies:

```bash
ddev composer install
```

- Run with:

```bash
# From the host
make test

# Or inside DDEV
ddev exec vendor/bin/phpunit
```

## Project Structure

* `snobol4-php/` - C source code for the PHP extension.
* `php-src/` - PHP helper classes:
    - `Parser.php` - Recursive-descent parser for SNOBOL-like pattern strings.
    - `Lexer.php` - Tokenizer for the parser.
    - `Builder.php` - Fluent API for constructing pattern ASTs.
    - `Pattern.php` - Type stub for the native Pattern class (implemented in C extension).
    - `PatternHelper.php` - High-level convenience methods (matchOnce, matchAll, split, replace).
    - `PatternCache.php` - LRU cache for compiled patterns.
* `public/` - Example scripts and entry point for the DDEV web container.
* `tests/` - Test suites (C and PHP).
* `dev/` - Developer tools and helper scripts.
* `.ddev/` - Configuration for the development environment (including extension build hook).

## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for details on our code of conduct, and the
process for submitting pull requests.

## License

[Apache License, Version 2.0](LICENSE)