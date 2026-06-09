# libsnobol4

![core-build](https://github.com/JPetsis/libsnobol4/actions/workflows/ci-core.yml/badge.svg)
![php-build](https://github.com/JPetsis/libsnobol4/actions/workflows/ci-php.yml/badge.svg)
![sanitizers](https://github.com/JPetsis/libsnobol4/actions/workflows/sanitizers.yml/badge.svg)

A high-performance C library implementing [SNOBOL4](https://en.wikipedia.org/wiki/SNOBOL)-style string pattern
matching and manipulation.

libsnobol4 provides a powerful, expressive alternative to PCRE (Perl Compatible Regular Expressions) for complex string
manipulation tasks. The core library is language-agnostic, with bindings available for multiple host languages.

## Features

### Core Library (C)

* **Language-Agnostic Core**: Pure C implementation with no external dependencies
* **Robust Backtracking Engine**:
  * **Catastrophic Backtracking Protection**: Detects and prevents infinite loops in nested zero-width matches
  * **Deep Recursion**: Dynamically growable choice stack handles deeply nested patterns without crashing
  * **Compact Choice Stack**: Write-log delta encoding stores only changed capture registers per choice point, reducing
    memory footprint by ≥50% for patterns with ≥10 choice points (default; legacy mode via `SNOBOL_LEGACY_CHOICE=1`)
* **Rich Pattern Primitives**:
  * **Literals**: Exact string matching
  * **Concatenation & Alternation**: Sequential and alternative pattern composition
  * **Span & Break**: Character class matching
  * **Any & NotAny**: Single character matching
  * **Unicode Support**: Full UTF-8 multi-byte character support
  * **Arbno & Bounded Repetition**: Variable-length pattern repetition
  * **Anchors**: Start/end of string anchors
  * **BREAKX**: Character-set break with O(n) pre-scan optimisation (8 fewer backtracks vs ARB)
  * **BAL**: Balanced delimiter matching (e.g. nested parentheses)
  * **FENCE**: Backtracking cut (prevents retrying a choice point)
  * **REM**: Match remainder of subject string
  * **RPOS / RTAB**: End-relative position and tab patterns
  * **Labelled Control Flow** (v0.4.0): Named labels and `goto` transfers within a pattern.
    Labels are compile-time names that resolve to bytecode offsets; `goto` is explicit control
    flow that does **not** pop the backtracking choice stack (distinguishing it from backtracking).
    Duplicate-label and unknown-label references are detected at compile time.
* **Template Substitution** (v0.5.0): `Pattern::subst(subject, template[, tables])` replaces
  every match in `subject` using a template string where capture references and formatting
  expressions are compiled entirely to C VM instructions.  Supported expressions inside `${vN...}`:
  * `${vN}` — raw capture
  * `${vN.upper()}` — Unicode uppercase (Latin-1 + Latin Extended-A; German ß→SS)
  * `${vN.lower()}` — Unicode lowercase (Latin-1 + Latin Extended-A)
  * `${vN.length()}` — decimal codepoint count
  * `${vN.lpad(W[,'c'])}` — left-pad to width W (fill char defaults to space)
  * `${vN.rpad(W[,'c'])}` — right-pad to width W (fill char defaults to space)
  * `$TABLE['key']` / `$TABLE[vN]` — table-backed lookup (fully compiled; no PHP post-processing)
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
  * **UPPER / LOWER** – Full Unicode case conversion (Latin-1 + Latin Extended-A, German sharp-s ß→SS)
* **Case-Insensitive Pattern Matching** (v0.7.0): `snobol_pattern_compile_ex()` with `SNOBOL_FLAG_CASE_INSENSITIVE` enables case-folded matching at compile time. Covers ASCII, Latin-1, and Latin Extended-A codepoints.
* **API Version Function** (v0.7.0): `snobol_get_api_version()` returns `(MAJOR << 16) | (MINOR << 8) | PATCH` for binding/library compatibility checks.
* **Built-in Comparison Predicates** (v0.2.0): Boolean predicates matching SNOBOL4 semantics:
  * **IDENT / DIFFER** – String identity / difference
  * **LEXEQ / LEXLT / LEXGT** – Lexicographic comparisons
  * **INTEGER / REAL / NUMERIC** – Numeric type predicates
* **Optional Micro-JIT** (v0.10.0): ARM64/ARM32/RISC-V 64/x86-64 JIT compilation for hot patterns via a two-phase
  architecture-neutral IR pipeline — runs on **macOS ARM64/Intel, Linux AArch64/x86-64, Linux ARMv7-A, Linux RISC-V 64, and Windows x86-64**:
  * `SNOBOL_JIT_DUMP_IR=1` — dump the IR to `stderr` before lowering (debug)
  * `SNOBOL_JIT_BACKEND=arm64|arm32|riscv64|x86_64` (CMake option) — selects the code-generation backend (default: `arm64`)
  * Pipeline: VM bytecode → IR lift → DCE + copy-prop → ARM64/Thumb-2/RV64I/x86-64 machine code
  * **ARM32 backend** targets ARMv7-A with Thumb-2 instruction set; uses W^X page permissions on Linux
  * **RISC-V 64 backend** targets RV64I base ISA; optional RV64C compressed support via `SNOBOL_JIT_RV64C=ON`
  * **x86-64 backend** supports both System V AMD64 ABI (Linux/macOS) and Microsoft x64 ABI (Windows) via
    compile-time `SNOBOL_JIT_WIN64_ABI`. Uses W^X page permissions: `VirtualAlloc`/`VirtualProtect` on Windows,
    `mmap`/`mprotect` on Linux, `MAP_JIT` on macOS. DEP-compliant: never uses `PAGE_EXECUTE_READWRITE`.
  * Linux uses W^X (write-then-exec) page permissions; macOS uses `MAP_JIT`
* **Modern C Code Quality** (v0.6.0): Core adopts `nullptr`, `SNOBOL_NODISCARD`, `[[maybe_unused]]`, and `constexpr` with MSVC-compatible fallbacks throughout.
* **Profiling Support**: Built-in execution profiling for performance analysis

### Available Bindings

| Binding                       | Status   | Version  |
|-------------------------------|----------|----------|
| [PHP](bindings/php/README.md) | ✅ Stable | v0.7.0   |

### JIT Backends

The optional micro-JIT compiles hot VM regions to native code via a two-phase architecture-neutral IR pipeline.
Four backends are supported; select one at CMake time via `SNOBOL_JIT_BACKEND`:

| Backend   | Target Architecture | Host OS               | ISA / ABI                                      | Code-Page Model           |
|-----------|---------------------|-----------------------|------------------------------------------------|---------------------------|
| `arm64`   | AArch64             | macOS, Linux          | ARMv8-A, MAP_JIT (macOS) / W^X (Linux)         | `mmap` + `mprotect`       |
| `arm32`   | ARMv7-A (Thumb-2)   | Linux                 | Thumb-2, AAPCS32                               | W^X (`mmap` + `mprotect`) |
| `riscv64` | RV64GC              | Linux                 | RV64I base + optional RV64C (compressed)       | W^X + `__clear_cache`     |
| `x86_64`  | x86-64 (AMD64)      | Linux, macOS, Windows | System V AMD64 (Linux/macOS), MS x64 (Windows) | W^X / VirtualAlloc        |

**Key features:**
- **Two-phase IR pipeline**: VM bytecode → architecture-neutral IR (`jit_ir_region_t`) → DCE + copy-prop optimiser → machine code via backend vtable
- **`SNOBOL_JIT_DUMP_IR=1`**: set the environment variable to dump the IR to `stderr` before lowering (debugging)
- **Full opcode coverage**: every opcode is `jit-compiled`, `call-out`, or `pseudo` — no interpreter fallback at runtime
- **CFG-based compilation**: multi-block regions with stub-based control flow; backward-edge loop guard (`JIT_LOOP_ITER_MAX` = 1024)
- **LRU code cache**: up to 128 entries (configurable), evicted when `ref_count == 0`
- **CI coverage**: native ARM64 runner + QEMU-emulated AArch64, ARMv7, RISC-V 64 + native x86-64 on Linux, macOS, Windows

## Project Structure

```
libsnobol4/
├── core/                    # Language-agnostic C core library
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

| Option               | Default | Description                                                                                              |
|----------------------|---------|----------------------------------------------------------------------------------------------------------|
| `BUILD_TESTS`        | ON      | Build C test suite                                                                                       |
| `BUILD_PHP`          | OFF     | Build PHP binding                                                                                        |
| `BUILD_SHARED_LIBS`  | OFF     | Build shared library                                                                                     |
| `SNOBOL_JIT`         | ON      | Enable micro-JIT (macOS ARM64/Intel / Linux AArch64/x86-64 / ARMv7 / RISC-V 64 / Windows x86-64)         |
| `SNOBOL_JIT_BACKEND` | arm64   | Selects backend: `arm64`, `arm32`, `riscv64`, or `x86_64`                                                |
| `SNOBOL_JIT_RV64C`   | OFF     | Enable RISC-V compressed (RV64C) instruction support                                                     |
| `SNOBOL_JIT_DUMP_IR` | OFF     | Dump architecture-neutral IR to stderr before lowering (env var, not CMake — set `SNOBOL_JIT_DUMP_IR=1`) |
| `SNOBOL_PROFILE`     | OFF     | Enable VM profiling                                                                                      |
| `SNOBOL_SANITIZE`    | OFF     | AddressSanitizer + UBSan (GCC/Clang)                                                                     |

### Example Configurations

```bash
# Debug build with tests
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON

# Build with PHP binding
cmake -B build -DBUILD_PHP=ON

# Release build with JIT and profiling
cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DSNOBOL_JIT=ON -DSNOBOL_PROFILE=ON

# ASan + UBSan build (GCC or Clang required)
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSNOBOL_SANITIZE=ON
cmake --build build-asan --target test-asan
```

### Windows

```bash
# Visual Studio 2022 (JIT disabled automatically on Windows)
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

# MinGW-w64
cmake -B build -G "MinGW Makefiles"
cmake --build build
ctest --test-dir build --output-on-failure
```

CMake presets are available via `CMakePresets.json`:

```bash
cmake --preset asan   # ASan + UBSan
cmake --preset debug  # Debug build
cmake --preset release
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

# Run with AddressSanitizer + UBSan
make build-asan
make test-asan

# Run under Valgrind (memcheck)
make test-valgrind
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
  the ARM64 micro-JIT compiles **all VM opcodes** — every opcode has a
  compiled-region implementation (`jit-compiled` inline or `call-out` via BLR),
  so patterns never fall back to the interpreter at runtime.
  JIT is supported on **macOS ARM64 (Apple Silicon)**, **Linux AArch64**,
  **Linux ARMv7-A**, and **Linux RISC-V 64**, including QEMU-emulated
  environments for CI.
  Observability counters (`jit_exec_time_ns_total`, `jit_interp_time_ns_total`,
  `jit_bailouts_total`, `jit_blocks_compiled_total`) are available via
  `snobol_jit_get_stats()` / `snobol_jit_stats_reset()`.
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
│              Language-Agnostic Core (C)                 │
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

## Installation

```bash
cmake --install build --prefix /usr/local
```

After installation, the library can be located by CMake or pkg-config:

```bash
# CMake (find_package)
find_package(libsnobol4 REQUIRED)
target_link_libraries(your_target PRIVATE libsnobol4::snobol4)

# pkg-config
pkg-config --cflags --libs libsnobol4
# If installed to a non-standard prefix:
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig pkg-config --cflags --libs libsnobol4
```

## CI / Contributing

The default pull-request CI gate (`.github/workflows/ci-core.yml`) runs on `ubuntu-latest`, `macos-latest`, and `windows-latest`.

**Optional workflows** (triggered via GitHub Actions "Run workflow" or nightly schedule):
- **Sanitizers** (`.github/workflows/sanitizers.yml`): ASan + UBSan build on Ubuntu — not part of the PR gate.
- **Benchmarks** (`.github/workflows/benchmarks.yml`): full benchmark suite with artifact upload — not part of the PR gate.

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines.

## License

[Apache License, Version 2.0](LICENSE)

## Versioning

libsnobol4 uses independent versioning for core and each binding:

- **Core**: v0.10.0 (Multi-Architecture JIT: IR layer, ARM64, ARM32, RISC-V 64, x86-64 backends)
- **PHP Binding**: v0.8.0

This allows bindings to evolve at their own pace while maintaining clear compatibility guarantees.

### Platform Support

| Platform             | JIT Backend | CI Status       |
|----------------------|-------------|-----------------|
| macOS ARM64          | `arm64`     | ✅ Native runner |
| Linux AArch64        | `arm64`     | ✅ Native + QEMU |
| Linux ARMv7-A        | `arm32`     | ✅ QEMU-emulated |
| Linux RISC-V 64      | `riscv64`   | ✅ QEMU-emulated |
| Linux x86-64         | `x86_64`    | ✅ Native runner |
| macOS x86-64 (Intel) | `x86_64`    | ✅ Native runner |
| Windows x86-64       | `x86_64`    | ✅ Native runner |

QEMU-based CI provides **correctness coverage**; native runners provide **performance coverage**.
