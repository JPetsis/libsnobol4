# Changelog

All notable changes to the libsnobol4 project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] - 2026-04-14

### Added

- **Pattern Primitives** – New VM opcodes and AST nodes for classic SNOBOL4 patterns:
  - `BREAKX` – pre-scan optimisation; O(n) advance to character-set boundary with retry choice point (8× fewer backtrack
    ops vs ARB)
  - `BAL` – balanced delimiter matching (configurable open/close characters)
  - `FENCE` – backtracking cut; prevents retrying the current choice point
  - `REM` – matches the remainder of the subject string
  - `RPOS(n)` – end-relative position (n codepoints from end)
  - `RTAB(n)` – end-relative tab (advance to n codepoints from end)
- **Built-in String Functions** (`core/src/string_fn.c`, `core/include/snobol/string_fn.h`):
  - `SIZE` – Unicode codepoint count with ASCII fast path
  - `TRIM` – trailing whitespace removal
  - `DUPL` – string repetition
  - `REVERSE` – Unicode codepoint-safe reversal (two-pass)
  - `SUBSTR` – codepoint-based substring (1-based positions)
  - `REPLACE` – all-occurrences substitution (≈ PHP `str_replace` speed)
  - `REPLACE_CHAR` – 256-byte lookup table character translation
  - `LPAD` / `RPAD` – Unicode-width-aware padding
  - `CHAR` – codepoint-to-UTF-8 conversion
  - `ORD` – UTF-8-to-codepoint conversion
  - `UPPER` / `LOWER` – case conversion (v1: ASCII a-z/A-Z; v2 Unicode planned)
- **Built-in Comparison Predicates** (`core/src/type_fn.c`, `core/include/snobol/type_fn.h`):
  - `IDENT` / `DIFFER` – string identity predicates
  - `LEXEQ` / `LEXLT` / `LEXGT` – lexicographic comparisons
  - `INTEGER` / `REAL` / `NUMERIC` – numeric type predicates
- **VM Built-in Dispatch** – `OP_EVAL` handler with function dispatch table; SNOBOL_TRACE logging; memory
  pre-allocation (20 KiB slab per match call)
- **PHP Binding** (`bindings/php/`):
  - `Snobol\Text` class with static methods mirroring all built-in functions
  - PHP pattern primitive wrappers: `Builder::breakx()`, `Builder::bal()`, `Builder::fence()`, `Builder::rem()`,
    `Builder::rpos()`, `Builder::rtab()`
  - 177 PHPUnit tests total (up from 122), all passing
- **C Test Suite**: 10 new test files covering all new built-ins and primitives
- **Benchmarks** (`bench/`):
  - `bench/tokenize.php` – BREAKX vs ARB comparison
  - `bench/transform.php` – built-in string function performance vs PHP native
  - `bench/unicode.php` – Unicode vs ASCII benchmark
  - `bench/results_builtin.json` – consolidated performance analysis
- **Examples**:
  - `examples/c/builtin_examples.c` – C API usage for all built-in functions
  - `examples/php/text_functions.php` – PHP API usage for all Text:: methods and pattern primitives

### Changed

- `core/src/compiler.c` – added `emit_breakx_c`, `emit_bal_c`, `emit_fence_c`, `emit_rem_c`, `emit_rpos_c`,
  `emit_rtab_c` emit helpers
- `core/include/snobol/ast.h` – added `AST_BREAKX`, `AST_BAL`, `AST_FENCE`, `AST_REM`, `AST_RPOS`, `AST_RTAB` enum
  values and union fields
- `core/src/ast.c` – added creator functions and free/name cases for new AST nodes
- `bindings/php/core_amalgam.c` – includes `string_fn.c`, `type_fn.c`, `pattern_build.c`
- PHP binding version bumped from 0.1.0 → 0.2.0

### Fixed

- PHP `PrimitivesTest` – capture/assign semantics (must use `Builder::assign` to expose `v{n}` in result)

### Performance Notes (v0.2.0)

| Scenario                   | SNOBOL         | PHP native               | Ratio          |
|----------------------------|----------------|--------------------------|----------------|
| `Text::replace` (9 KB)     | 614K ops/s     | 623K ops/s (str_replace) | **0.98×**      |
| `Text::upper/lower` (9 KB) | 3.4-3.7M ops/s | 3.8-4.0M ops/s           | **0.88-0.92×** |
| `Text::size` (Unicode)     | 910K ops/s     | 909K ops/s (mb_strlen)   | **1.00×**      |
| BREAKX choice pushes       | 1K/iter        | 8.3K/iter (ARB)          | **8.3× fewer** |

### Version Status

- **Core Library**: v0.2.0
- **PHP Binding**: v0.2.0
- **AST API**: v1.1.0 (new primitive nodes added, backwards compatible)

---

### Added

- **Monorepo Structure**: Language-agnostic core with separate bindings directories
- **Core C23 Library** (`core/`):
  - Complete lexer, parser, AST, compiler, VM implementation
  - Public API headers in `core/include/snobol/`
  - CMake build system with proper installation rules
  - Optional micro-JIT for ARM64
  - Associative tables for runtime lookups
  - Dynamic pattern evaluation with caching
- **PHP Binding** (`bindings/php/`):
  - Complete PHP extension with DDEV support
  - PHP helper classes (Pattern, PatternHelper, Builder, Table)
  - Full PHPUnit test suite (122 tests passing)
  - Native CMake build option
- **C Test Suite** (`tests/c/`):
  - 1,065+ tests covering all core functionality
  - JIT correctness and performance tests
  - Stress tests for backtracking and edge cases
- **Examples** (`examples/c/`):
  - Basic pattern matching example
  - Capture and assignment example
- **Documentation**:
  - Language-agnostic README.md
  - PHP-specific documentation in `bindings/php/README.md`
  - Updated CONTRIBUTING.md for monorepo structure
  - Updated ELEVATOR_PITCH.md and PITCH.md
- **CI/CD**:
  - GitHub Actions workflows for core (Linux, macOS, Windows)
  - PHP binding tests across PHP 8.0-8.4
  - AddressSanitizer and UBSan testing

### Changed

- **Repository Rename**: `snobol4-ddev` → `libsnobol4`
- **Project Structure**: Complete restructure to monorepo layout
  - Core C code moved from `snobol4-core/` to `core/`
  - PHP binding moved to `bindings/php/`
  - `.ddev/` moved to `bindings/php/.ddev/`
- **Build System**: Migrated from phpize to CMake
- **Include Paths**: Updated to namespaced `snobol/*.h` paths
- **AST API**: Full C AST compilation support
- **Template Compilation**: Full implementation for pattern replacements

### Fixed

- All PHP extension tests now pass (122/122)
- Capture and assign operations for all register numbers
- Template compilation for table-backed substitutions
- Emit literal and capture reference operations
- Dynamic pattern evaluation (EVAL)

### Removed

- Old `snobol4-core/` directory (merged into `core/`)
- Old `php-src/` directory (moved to `bindings/php/php-src/`)
- Old `.ddev/` at root (moved to `bindings/php/.ddev/`)
- PHP-coupled build system (replaced with CMake)

### Version Status

- **Core Library**: v0.1.0 (initial release)
- **PHP Binding**: v0.1.0 (initial release)
- **AST API**: v1.0.0 (stable)

---

- **Architecture**: Separated language-agnostic C core from language-specific bindings
- **Performance**: Eliminated PHP parser overhead (5-15% improvement for simple patterns)
- **Maintainability**: Single source of truth for grammar and parsing logic

### Removed

- `php-src/Lexer.php` - Replaced by C lexer
- `php-src/Parser.php` - Replaced by C parser
- `tests/php/ParserTest.php` - PHP parser tests no longer applicable

### Fixed

- PHP coupling in core - C core now has no dependencies on PHP internals
- Memory management - Proper ownership semantics for AST nodes

## Pre-1.0 (PHP-Coupled Architecture)

Before the language-agnostic core refactoring, the project used PHP-based lexer and parser
with C-based VM and compiler. This architecture was functional but made it difficult to
create bindings for other languages.

Key components:

- `php-src/Lexer.php` - PHP lexer
- `php-src/Parser.php` - PHP parser producing PHP arrays
- `snobol4-php/snobol_compiler.c` - Compiled PHP arrays to bytecode
- `snobol4-php/snobol_vm.c` - C VM for bytecode execution
