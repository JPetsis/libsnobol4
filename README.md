# SNOBOL4 for PHP

A high-performance PHP extension that implements [SNOBOL4](https://en.wikipedia.org/wiki/SNOBOL)-style string pattern
matching. This extension brings the powerful, expressive pattern matching capabilities of SNOBOL4 to modern PHP
applications, offering a robust alternative to PCRE (Perl Compatible Regular Expressions) for complex string
manipulation tasks.

## Features

* **Native C Extension:** Built for performance, integrating directly with PHP's core.
* **Robust Backtracking Engine:**
    * **Catastrophic Backtracking Protection:** Detects and prevents infinite loops in nested zero-width matches.
    * **Deep Recursion:** Dynamically growable choice stack handles deeply nested patterns without crashing.
* **Expressive Pattern Building:**
    * **Textual Parser:** Write patterns as strings using a familiar SNOBOL-like syntax.
    * **Builder API:** Construct patterns programmatically using a fluent AST-based builder.
* **Rich Pattern Primitives:**
    * **Literals:** Exact string matching (`'text'`).
    * **Concatenation & Alternation:** Combine patterns sequentially (`P1 P2`) or as alternatives (`P1 | P2`).
    * **Span & Break:** Match runs of characters (`SPAN('0-9')`) or scan until a set of characters.
    * **Any & NotAny:** Match single characters based on sets (`[abc]`, `[^abc]`).
  * **Unicode Support:** Fully supports UTF-8 multi-byte characters and Unicode ranges (e.g. `SPAN('α-ω')`).
  * **Case Insensitivity:** Optional case-insensitive matching for ASCII and Latin-1 characters.
  * **Arbno & Bounded Repetition:** Match arbitrary (`P*`) or fixed/ranged repetitions (`repeat(P, min, max)`).
  * **Anchors:** Start-of-string (`^`) and end-of-string (`$`) anchors.
    * **Len:** Match specific lengths (`LEN(5)`).
* **Captures & Assignments:**
    * Capture substrings into registers during matching (`@r1(...)`).
    * Assign captured values to variables, returned as an associative array upon successful match.
* **Replacement & Emission:**
    * Emit literals or captured references to an output buffer during matching for stream-based replacements.
* **Associative Tables:**
    * Runtime-owned table objects for key-value storage (`Snobol\Table`).
  * Table-backed pattern matching and replacements with literal and capture-derived keys.
* **Dynamic Pattern Evaluation:**
    * Runtime pattern compilation and caching via `EVAL(...)` syntax.
    * Efficient reuse of compiled patterns through core runtime cache.
    * Helper API: `PatternHelper::evalPattern()` for dynamic pattern execution.
* **Formatted Substitutions:**
    * Template-based replacements with formatting options (upper, lower, length).
    * Graceful degradation for missing values.
  * Helper API: `PatternHelper::formattedSubst()` for runtime-backed template execution.
* **Table-Backed Substitutions:**
    * Template syntax: `$TABLE['literal_key']` and `$TABLE[$v0]` for capture-derived lookups.
    * Helper API: `PatternHelper::tableSubst()` for table-backed replacements.
* **Labelled Control Flow:**
    * Labels and goto-like transfers for advanced pattern flow.
    * Explicit control flow distinct from backtracking.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Language Bindings                    │
│  ┌────────────┐    ┌────────────┐    ┌────────────┐     │
│  │    PHP     │    │   Python   │    │    Rust    │     │
│  │  Binding   │    │  Binding   │    │  Binding   │     │
│  └─────┬──────┘    └─────┬──────┘    └─────┬──────┘     │
│        │                 │                 │            │
│        └─────────────────┼─────────────────┘            │
│                          │                              │
│                   C Extension API                       │
└──────────────────────────┼──────────────────────────────┘
                           │
┌──────────────────────────┼──────────────────────────────┐
│              Language-Agnostic Core (C23)               │
│                          │                              │
│  ┌──────────────┐  ┌─────▼──────┐  ┌──────────────┐     │
│  │ Lexer (C)    │─▶│ Parser (C) │─▶│  Compiler    │     │
│  │ UTF-8 aware  │  │ Recursive  │  │  Bytecode    │     │
│  └──────────────┘  │  Descent   │  └──────┬───────┘     │
│                    └────────────┘         │             │
│                          │                │             │
│                    EBNF Grammar      ┌────▼───────┐     │
│                    (snobol.ebnf)     │  Runtime   │     │
│                                      │   Cache    │     │
│                                      └────┬───────┘     │
│                                           │             │
│  ┌──────────────┐  ┌──────────────┐  ┌───▼────────┐     │
│  │      VM      │◀─│   Bytecode   │◀─│   Tables   │     │
│  │  Backtracking│  │   Execution  │  │  (Assoc)   │     │
│  └──────────────┘  └──────────────┘  └────────────┘     │
│                          │                              │
│                    Optional JIT                         │
│                  (Micro-JIT for hot                     │
│                   patterns)                             │
└─────────────────────────────────────────────────────────┘
```

**Key Design Principles:**

1. **Language-Agnostic Core**: The C core (`snobol4-php/`) has no dependencies on PHP or any other host language.
2. **Thin Bindings**: Language bindings (PHP, Python, Rust) are thin wrappers over the C API.
3. **Formal Grammar**: The SNOBOL pattern syntax is defined in `grammar/snobol.ebnf`.
4. **Caching**: Compiled patterns are cached by source text for efficient reuse.
5. **Optional JIT**: Hot patterns can be JIT-compiled for performance.

### C Core Components

| Component            | File                         | Description                              |
|----------------------|------------------------------|------------------------------------------|
| **Lexer**            | `snobol_lexer.h/c`           | UTF-8 aware tokenizer with save/restore  |
| **Parser**           | `snobol_parser.h/c`          | Recursive descent parser producing C AST |
| **AST**              | `snobol_ast.h/c`             | Tagged union AST with memory management  |
| **Compiler**         | `snobol_compiler.c`          | AST → bytecode compiler                  |
| **VM**               | `snobol_vm.c`                | Bytecode interpreter with backtracking   |
| **Tables**           | `snobol_table.h/c`           | Associative table runtime                |
| **Dynamic Patterns** | `snobol_dynamic_pattern.h/c` | EVAL(...) runtime cache                  |
| **JIT**              | `snobol_jit.h/c`             | Micro-JIT for hot patterns               |

### PHP Binding

The PHP binding (`php-src/`) provides:

- `Pattern::fromString($source)` - Parse and compile SNOBOL pattern text
- `Pattern::compileFromAst($ast)` - Compile AST from `Builder` API
- `PatternHelper` - High-level convenience methods
- `DynamicPatternCache` - PHP interface to runtime cache

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
   *Optional:* To enable internal VM profiling (for debugging performance), use:
   ```bash
   ./configure --enable-snobol-profile
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

### Replacement & Emission

You can build an output string during the matching process using `emit` operations. The `match()` method returns this
accumulated text in the `_output` key.

Additionally, `PatternHelper::replace()` now supports **Substitution Templates** with variable references:

```php
<?php
use Snobol\Builder;
use Snobol\PatternHelper;

$pattern = Builder::cap(0, Builder::span("0123456789"));
$subject = "id:123 code:456";

// Reference captured registers with $vN or ${vN}
$result = PatternHelper::replace($pattern, "[\$v0]", $subject);
// Output: "id:[123] code:[456]"

// Use expressions like .upper() or .length()
$names = Builder::concat([
    Builder::lit("name:"),
    Builder::cap(1, Builder::span("abcdefghijklmnopqrstuvwxyz"))
]);
$result = PatternHelper::replace($names, "NAME:\${v1.upper()} (len:\${v1.length()})", "name:alice");
// Output: "NAME:ALICE (len:5)"
```

### Pattern Builder API

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

// Case-insensitive match
$match = PatternHelper::matchOnce("'abc'", "ABC", ['caseInsensitive' => true]);
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

| Method                                        | Description                                                                     |
|:----------------------------------------------|:--------------------------------------------------------------------------------|
| `lit(string $s)`                              | Matches the literal string `$s`.                                                |
| `concat(array $parts)`                        | Matches a sequence of patterns.                                                 |
| `alt(array $left, array $right)`              | Matches either `$left` OR `$right`.                                             |
| `span(string $set)`                           | Matches a run of characters found in `$set`.                                    |
| `brk(string $set)`                            | Matches until a character in `$set` is found.                                   |
| `any(string $set = null)`                     | Matches any single character (optionally from `$set`).                          |
| `notany(string $set)`                         | Matches any single character NOT in `$set`.                                     |
| `len(int $n)`                                 | Matches exactly `$n` characters.                                                |
| `arbno(array $sub)`                           | Matches arbitrary repetitions of the `$sub` pattern.                            |
| `repeat(array $sub, int $min, int $max = -1)` | Matches `$sub` repeated between `$min` and `$max` times.                        |
| `anchor(string $type)`                        | Anchors match to `'start'` or `'end'` of string.                                |
| `cap(int $reg, array $sub)`                   | Captures the match of `$sub` into register `$reg`.                              |
| `assign(int $var, int $reg)`                  | Assigns the content of register `$reg` to output variable `$var` (key `v$var`). |
| `emit(string $text)`                          | Appends literal text to the `_output` buffer on match.                          |

## Performance

The extension is designed for high-performance string processing. Benchmarks are included under `public/`.

**Important:** If you see suspicious results (e.g. `New C subst: 0.0000s` or empty output), it almost always means the
native extension failed to load and you are running against PHP-only stubs. Verify with `php -m | grep snobol` inside
DDEV, and ensure `ddev restart` succeeds after rebuilding.

### Character Class Performance

Character classes are stored as Unicode ranges (`CpRange`) but use an ASCII fast-path (a cached 256-bit bitmap) for
O(1) matching of codepoints `< 256`.

Run:

```bash
ddev exec php public/benchmark_charclass.php 1000000
```

Typical results inside DDEV (PHP 8.4, Apple Silicon host):

- **ASCII SPAN (Dense):** ~145k ops/sec
- **Unicode SPAN (Mixed):** ~115k ops/sec

### Substitution Benchmark

Comparing a PHP-level match-and-concatenate loop with the native streaming C-level substitution:

Run:

```bash
ddev exec php public/benchmark_subst.php
```

Typical results:

- **PHP Loop:** ~0.9s
- **Native `subst()`:** ~0.05s
- **Speedup:** ~19x

The native implementation minimizes data copying between PHP and C and avoids the overhead of returning match result
arrays to PHP for every replacement.

## Micro-JIT (experimental)

The VM includes an **opt-in micro-JIT** (currently targeting **ARM64/aarch64**) that accelerates some hot, ASCII-heavy
straight-line opcode traces (not a general-purpose regex JIT).

### Build and run

Inside DDEV:

```bash
make build-jit
make install
```

You can also toggle JIT at runtime per pattern:

```php
$pattern->setJit(true);  // enable
$pattern->setJit(false); // disable
```

### Benchmarks

- Run benchmarks: `make bench`
- Compare SNOBOL vs PCRE for a single run: `php bench/compare.php ...`
- Compare JIT OFF vs JIT ON snapshots: `php bench/compare_jit.php ...`

Details and reference tables live in:

- `openspec/specs/jit.md`
- `bench/README.md`

## Development workflow

### Cleaning

`make clean` removes build artifacts and also deletes generated benchmark result JSONs under `bench/`.
It keeps only the example file:

- `bench/results_example.json`

If you want to keep a benchmark run, copy/rename it before cleaning (for example to `bench/results_*_jitoff.json` or
`bench/results_*_jiton.json`).

### Benchmarks and JIT comparisons

- Run benchmarks: `make bench`
- Compare SNOBOL vs PCRE for a single run: `php bench/compare.php ...`
- Compare JIT OFF vs JIT ON snapshots: `php bench/compare_jit.php ...`

See `bench/README.md` and `openspec/specs/jit.md` for details.

## Use Cases

- **Data Sanitization:** Masking or transforming sensitive patterns in large logs or datasets.
- **Templating:** Efficiently applying complex pattern-based transformations to text blocks.
- **Protocols:** Parsing and rewriting custom binary or text-based protocols where regular regex falls short.
- **Dynamic Pattern Generation:** Building patterns at runtime from captured input or configuration.
- **Table-Driven Transformations:** Lookup-based replacements for state machines, translators, and data mapping.

## Known Limitations and Future Work

### Dynamic Pattern Evaluation

- **Simple patterns only:** `EVAL(...)` currently supports literal patterns and concatenation. Patterns with
  alternation (`|`) or complex backtracking fall back to PHP-native evaluation.
- **No recursive EVAL:** Nested `EVAL(EVAL(...))` is parsed but not fully optimized.
- **Future:** Full backtracking support in dynamic executor, recursive evaluation optimization.

### Table-Backed Substitutions

- **Table registration:** Tables must be accessed via template syntax; automatic VM registration is not yet implemented.
- **Named table resolution:** Template compiler uses placeholder `table_id=0`; runtime resolves by name.
- **Future:** Explicit table registration API, named table resolution in VM.

### Labelled Control Flow

- **Interpreter only:** Labels and gotos are interpreted; JIT skips patterns with control flow opcodes.
- **Future:** JIT compilation for simple labelled patterns without backtracking.

### Unicode and Case Insensitivity

- **ASCII fast-path:** Case-insensitive matching uses ASCII bitmap for codepoints < 256.
- **Full Unicode:** Unicode ranges work but case folding is limited to Latin-1.
- **Future:** Full Unicode case folding, locale-aware matching.

### JIT Coverage

- **Limited to simple patterns:** JIT optimizes straight-line literal/character class patterns.
- **Skips complex features:** Tables, dynamic evaluation, control flow, and formatting fall back to interpreter.
- **By design:** Profitability gate correctly identifies patterns that don't benefit from JIT.

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

Key test files:

- `tests/php/UnicodeTest.php`: Verifies UTF-8 ranges, multi-byte characters, and case-insensitive matching.
- `tests/php/AsciiRegressionTest.php`: Ensures 100% backward compatibility with standard ASCII patterns.
- `tests/php/BuilderTest.php` & `tests/php/PatternTest.php`: Core API functionality.

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