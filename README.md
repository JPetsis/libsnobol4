# libsnobol4

A high-performance C23 library implementing [SNOBOL4](https://en.wikipedia.org/wiki/SNOBOL)-style string pattern
matching and manipulation.

libsnobol4 provides a powerful, expressive alternative to PCRE (Perl Compatible Regular Expressions) for complex string
manipulation tasks. The core library is language-agnostic, with bindings available for multiple host languages.

## Features

### Core Library (C23)

* **Language-Agnostic Core**: Pure C23 implementation with no external dependencies
* **Robust Backtracking Engine**:
  * **Catastrophic Backtracking Protection**: Detects and prevents infinite loops in nested zero-width matches
  * **Deep Recursion**: Dynamically growable choice stack handles deeply nested patterns without crashing
* **Rich Pattern Primitives**:
  * **Literals**: Exact string matching
  * **Concatenation & Alternation**: Sequential and alternative pattern composition
  * **Span & Break**: Character class matching
  * **Any & NotAny**: Single character matching
  * **Unicode Support**: Full UTF-8 multi-byte character support
  * **Arbno & Bounded Repetition**: Variable-length pattern repetition
  * **Anchors**: Start/end of string anchors
  * **BREAKX**: Character-set break with O(n) pre-scan optimisation (8× fewer backtracks vs ARB)
  * **BAL**: Balanced delimiter matching (e.g. nested parentheses)
  * **FENCE**: Backtracking cut (prevents retrying a choice point)
  * **REM**: Match remainder of subject string
  * **RPOS / RTAB**: End-relative position and tab patterns
* **Captures & Assignments**: Register-based capture and variable assignment
* **Associative Tables**: Runtime-owned hash tables for key-value storage
* **Dynamic Pattern Evaluation**: Runtime pattern compilation with caching (`EVAL(...)`)
* **Built-in String Functions** (v0.2.0): C-native string manipulation on UTF-8 strings:
  * **SIZE** – Unicode codepoint count (ASCII fast path)
  * **TRIM** – Remove trailing whitespace
  * **DUPL** – Repeat a string N times
  * **REVERSE** – Reverse by codepoints (multi-byte safe)
  * **SUBSTR** – Codepoint-based substring extraction (1-based)
  * **REPLACE** – Replace all occurrences (≈ PHP `str_replace` speed)
  * **REPLACE_CHAR** – Character translation table (like POSIX `tr`)
  * **LPAD / RPAD** – Pad strings to a Unicode codepoint width
  * **CHAR / ORD** – Codepoint ↔ UTF-8 character conversions
  * **UPPER / LOWER** – Case conversion (v1: ASCII; v2: full Unicode planned)
* **Built-in Comparison Predicates** (v0.2.0): Boolean predicates matching SNOBOL4 semantics:
  * **IDENT / DIFFER** – String identity / difference
  * **LEXEQ / LEXLT / LEXGT** – Lexicographic comparisons
  * **INTEGER / REAL / NUMERIC** – Numeric type predicates
* **Optional Micro-JIT**: ARM64 JIT compilation for hot patterns (experimental)
* **Profiling Support**: Built-in execution profiling for performance analysis

### Available Bindings

| Binding              | Status   | Version |
|----------------------|----------|---------|
| [PHP](bindings/php/) | ✅ Stable | v0.2.0  |

## Project Structure

```
libsnobol4/
├── core/                    # Language-agnostic C23 core library
│   ├── CMakeLists.txt       # Core build configuration
│   ├── include/snobol/      # Public API headers
│   ├── src/                 # Core implementation
│   └── grammar/             # SNOBOL pattern grammar (EBNF)
├── bindings/                # Language-specific bindings
│   └── php/                 # PHP binding
│       ├── CMakeLists.txt   # PHP binding build
│       ├── src/             # PHP extension source
│       ├── php-src/         # PHP helper classes
│       └── tests/           # PHPUnit tests
├── tests/c/                 # Core C test suite
├── examples/c/              # C usage examples
├── CMakeLists.txt           # Root build configuration
└── README.md                # This file
```

## Quick Start

### Building the Core Library

```bash
# Configure
cmake -B build

# Build
cmake --build build

# Install (optional)
cmake --install build
```

### Using the Core Library (C)

```c
#include <snobol/snobol.h>

int main(void) {
    // Create context
    snobol_context_t* ctx = snobol_context_create();
    
    // Compile pattern: 'hello'
    snobol_pattern_t* pattern = snobol_pattern_compile(
        ctx, "'hello'", 7, NULL
    );
    
    // Match against subject
    snobol_match_t* match = snobol_pattern_match(
        pattern, "hello world", 11
    );
    
    if (snobol_match_success(match)) {
        printf("Match found!\n");
    }
    
    // Cleanup
    snobol_match_free(match);
    snobol_pattern_free(pattern);
    snobol_context_destroy(ctx);
    
    return 0;
}
```

### Using the PHP Binding

```bash
cd bindings/php
ddev start
```

```php
<?php
use Snobol\PatternHelper;

// Match pattern
$result = PatternHelper::matchOnce("'hello'", "hello world");

// Using Builder API
use Snobol\Builder;
$pattern = Builder::lit("hello");
$result = PatternHelper::matchOnce($pattern, "hello world");
```

See [bindings/php/README.md](bindings/php/README.md) for detailed PHP documentation.

## Build Options

| Option              | Default | Description              |
|---------------------|---------|--------------------------|
| `BUILD_TESTS`       | ON      | Build C test suite       |
| `BUILD_PHP`         | OFF     | Build PHP binding        |
| `BUILD_SHARED_LIBS` | OFF     | Build shared library     |
| `SNOBOL_JIT`        | ON      | Enable micro-JIT (ARM64) |
| `SNOBOL_PROFILE`    | OFF     | Enable VM profiling      |

### Example Configurations

```bash
# Debug build with tests
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON

# Build with PHP binding
cmake -B build -DBUILD_PHP=ON

# Release build with JIT and profiling
cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DSNOBOL_JIT=ON -DSNOBOL_PROFILE=ON
```

## Testing

### Core C Tests

```bash
# Run all tests
ctest --test-dir build

# Run with verbose output
ctest --test-dir build --verbose

# Run specific test
ctest --test-dir build -R test_lexer
```

### PHP Tests

```bash
cd bindings/php
composer install
vendor/bin/phpunit tests/php
```

## Performance

libsnobol4 is designed for high-performance string processing:

- **Streaming Substitution**: Native C implementation minimizes data copying
- **Pattern Caching**: Compiled patterns are cached for efficient reuse
- **Optional JIT**: Hot patterns can be JIT-compiled for maximum performance;
  the ARM64 micro-JIT now supports multi-arm alternation (chained SPLIT blocks),
  ARBNO loops (backward-edge loop guard with iteration counter), and tracks
  `jit_blocks_compiled_total` in `SnobolJitStats` for observability.
- **Built-in C functions**: `Text::replace` matches PHP `str_replace` within 2%; `Text::upper/lower/trim` within 4-15%
  of native PHP built-ins
- **BREAKX optimisation**: 8.3× fewer backtrack operations vs `ARB+NOTANY` for key extraction
- **Unicode parity**: `Text::size` on Unicode input equals `mb_strlen` performance

See `bench/` directory for benchmark scripts and `bench/results_builtin.json` for detailed results.

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
│  ┌──────────────┐  ┌──────────────┐  ┌────▼───────┐     │
│  │      VM      │◀─│   Bytecode   │◀─│   Tables   │     │
│  │  Backtracking│  │   Execution  │  │  (Assoc)   │     │
│  └──────────────┘  └──────────────┘  └────────────┘     │
│                          │                              │
│                    Optional JIT                         │
│                  (Micro-JIT for hot                     │
│                   patterns)                             │
└─────────────────────────────────────────────────────────┘
```

## Documentation

- **Core API**: Headers in `core/include/snobol/`
- **PHP Binding**: [bindings/php/README.md](bindings/php/README.md)
- **Grammar**: [core/grammar/snobol.ebnf](core/grammar/snobol.ebnf)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines.

## License

[Apache License, Version 2.0](LICENSE)

## Versioning

libsnobol4 uses independent versioning for core and each binding:

- **Core**: v0.2.0
- **PHP Binding**: v0.2.0

This allows bindings to evolve at their own pace while maintaining clear compatibility guarantees.
