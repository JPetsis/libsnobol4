# Contributing to libsnobol4

Thank you for considering contributing to libsnobol4! This document provides guidelines and instructions for setting up
your development environment, building the project, and submitting changes.

## Project Structure

libsnobol4 uses a **monorepo** structure with a language-agnostic C core and language-specific bindings:

```
libsnobol4/
├── core/                    # Core C library (language-agnostic)
│   ├── include/snobol/      # Public API headers
│   ├── src/                 # Core implementation
│   └── grammar/             # SNOBOL pattern grammar
├── bindings/                # Language-specific bindings
│   └── php/                 # PHP binding
│       ├── src/             # PHP extension source
│       ├── php-src/         # PHP helper classes
│       └── tests/           # PHPUnit tests
├── tests/c/                 # Core C test suite
├── examples/c/              # C usage examples
└── README.md                # Project overview
```

## Development Environment

### Option 1: DDEV (Recommended for PHP Development)

[DDEV](https://ddev.com/) provides a consistent containerized environment:

```bash
# PHP binding development
cd bindings/php
ddev start
```

This will:

- Start a PHP 8.5 container
- Build the libsnobol4 extension from `core/`
- Enable the extension automatically

**Rebuilding:**
If you make changes to the C core or the PHP binding source, you can rebuild the extension without a full restart:

```bash
ddev build-snobol-extension
```

This is the canonical build path that handles amalgamation regeneration and artifact cleanup.

### Option 2: Native Build

For core library development without PHP:

```bash
# Install CMake and a C compiler
# macOS: brew install cmake
# Ubuntu: apt install cmake build-essential

# Configure and build
cmake -B build -DBUILD_TESTS=ON
cmake --build build

# Run tests
ctest --test-dir build
```

## Development Workflow

### Building

```bash
# Core library only
make build

# Debug build
make build-debug

# With PHP binding
make build-php

# Clean build
make clean
```

### Running Tests

```bash
# Core C tests
make test

# Verbose test output
make test-verbose

# PHP tests (requires PHP binding)
cd bindings/php
vendor/bin/phpunit tests/php

# Memory leak detection with Valgrind (delegates to CMake test-valgrind target)
make test-valgrind

# AddressSanitizer + UndefinedBehaviorSanitizer (GCC or Clang required)
# Step 1: configure and build with sanitizers enabled
make build-asan
# Step 2: run the test suite under ASan/UBSan
make test-asan

# Alternatively, use CMake directly:
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSNOBOL_SANITIZE=ON
cmake --build build-asan --target test-asan
```

### Regenerating Unicode Case-Folding Tables

The BMP case-folding tables in `core/src/unicode_fold_data.c` are auto-generated from
[Unicode CaseFolding.txt](https://www.unicode.org/Public/UCD/latest/ucd/CaseFolding.txt).

```bash
# Regenerate tables (requires C compiler; fetches UCD if not cached locally)
make gen-unicode-fold

# Rebuild after regeneration
make build
```

The generator tool is in `dev/gen_unicode_fold.c`. It parses the full-case fold
(staus C and S) entries from Unicode CaseFolding.txt and emits statically-initialized
C arrays with sorted pair tables for O(log n) binary search.

### Code Quality

```bash
# Build with strict warnings
make warnings

# Run clang-tidy (requires LLVM)
make lint

# Format code (requires clang-format)
make format
```

## Submitting Changes

### 1. Create a Branch

```bash
git checkout -b feature/your-feature-name
```

### 2. Make Changes

- Keep changes focused and minimal
- Follow existing code style
- Add tests for new functionality
- Update documentation as needed

### 3. Run Tests

Ensure all tests pass before submitting:

```bash
# Core tests
make test

# PHP tests (if applicable)
cd bindings/php && vendor/bin/phpunit tests/php
```

### 4. Update the Changelog

**Every change must add an entry to [`CHANGELOG.md`](CHANGELOG.md)** under the
`[Unreleased]` section, using
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/) categorization
(Added / Changed / Fixed / Removed). Describe the change, the affected
tiers/surfaces, and any migration notes. PRs without a changelog entry will
not be merged.

### 5. Commit Changes

Write clear, descriptive commit messages:

```bash
git commit -m "feat: add bounded repetition support

- Implement repeat(P, min, max) pattern
- Add C tests for bounded repetition
- Update documentation"
```

### 6. Submit a Pull Request

- Push to your fork
- Open a PR against the `main` branch (the PR template lists the required gate)
- Include a description of changes
- Reference any related issues

## Coding Standards

### C Code (Core Library)

- **Standard**: C11/C17 (C17 on MSVC, C11 baseline)
- **Formatting**: Use `clang-format` (run `make format`)
- **Naming**:
  - Types: `snake_case_t` (e.g., `ast_node_t`)
  - Functions: `snake_case` (e.g., `snobol_ast_create`)
  - Macros: `SCREAMING_SNAKE_CASE` (e.g., `SNOBOL_AST_VERSION`)
- **Documentation**: Add file-level and function-level comments
- **C++ interop**: Public headers must stay C++-consumable. Wrap any new
  public header in `extern "C"` guards and keep it self-contained (it must
  compile standalone). The `header-cxx` CI job compiles the header set as C++
  with `g++` and `clang++` — a C-ism that breaks that build will fail CI.

### PHP Code (Bindings)

- **Standard**: PSR-12
- **Formatting**: Use `phpcbf` or configure your editor
- **Naming**:
  - Classes: `PascalCase` (e.g., `PatternHelper`)
  - Methods: `camelCase` (e.g., `matchOnce`)
  - Constants: `SCREAMING_SNAKE_CASE` (e.g., `VERSION`)

## Testing Guidelines

### C Tests

- Place tests in `tests/c/`
- Use the existing test framework in `test_runner.c`
- Test both success and failure cases
- Include stress tests for edge cases

### PHP Tests

- Place tests in `bindings/php/tests/php/`
- Use PHPUnit framework
- Test public API methods
- Include regression tests for bugs

## API Stability Policy

Starting with **v0.11.0**, libsnobol4 makes the following stability guarantees:

- **Public C API** — Functions declared in `core/include/snobol/snobol.h` with `snobol_` prefix follow [SemVer](https://semver.org/).
  - A **major** bump means breaking changes to the public API or ABI.
  - A **minor** bump adds functionality in a backward-compatible manner.
  - A **patch** bump contains only bug fixes.
- **ABI version** — `snobol_get_abi_version()` returns a monotonically-increasing integer. Load-time checks MUST compare this value at runtime (not at compile time) to detect incompatible dynamic libraries. A change in `SNOBOL_ABI_VERSION` always accompanies a major version bump.
- **Deprecation** — Public functions marked `SNOBOL_DEPRECATED` in the header remain available for one minor-version cycle before removal. Compiler warnings guide migration.
- **Internal headers** — Everything inside `core/include/snobol/` not in `snobol.h` or the public API section is subject to change without notice.
- **PHP binding** — Follows the major/minor/patch scheme independently. The PHP extension version `PHP_SNOBOL_VERSION` tracks the binding, not the core.

> ⚠️ Pre-v1.0.0: While the project is still below v1.0.0, minor bumps may include breaking changes to internal interfaces. The declared public API (`snobol.h`) is kept stable within a minor version, but the ABI version is the authoritative compatibility signal.

### Public API audit (v0.11.0)

The v0.11.0 public surface (declared in `core/include/snobol/snobol.h`) was audited before the v0.11.0 tag:

- Context/pattern/match lifecycle: `snobol_context_create`, `snobol_context_destroy`, `snobol_pattern_compile`, `snobol_pattern_compile_ex`, `snobol_pattern_free`, `snobol_match_free` — stable.
- Pattern execution: `snobol_pattern_match`, `snobol_pattern_search`, `snobol_pattern_search_state_create`, `snobol_pattern_search_state_destroy`, `snobol_pattern_search_ex` — stable; no reshape before v1.0.
- Match accessors: `snobol_match_success`, `snobol_match_get_output`, `snobol_match_get_variable`, `snobol_match_get_position`, `snobol_match_get_length` — stable.
- One-shot API: `snobol_match`, `snobol_match_result_free` — stable.
- Builder API: `snobol_pattern_build_*` family — stable shape; returns `NULL` on allocation failure (without setting an error channel — the caller should check the return value).
- Flag constants: `SNOBOL_FLAG_CASE_INSENSITIVE`, `SNOBOL_FLAG_SEARCH_MODE` — values stable (any new flags must use previously free bits).
- Version macros: `SNOBOL_VERSION_MAJOR/MINOR/PATCH/STRING`, `SNOBOL_ABI_VERSION` — stable shape.

No functions were deprecated or marked for removal in v0.11.0. The next minor bump (v0.12) MAY deprecate a function (with `SNOBOL_DEPRECATED`) — that deprecation will appear in `CHANGELOG.md` for that release.

## Error Handling Convention

The C core libsnobol4 makes the following stability guarantees around allocation failures:

- **Allocation failures are recoverable.** Every public API function in `core/src/api.c` checks the result of every `snobol_malloc` / `snobol_calloc` and returns a sentinel (`NULL` for handle-returning functions, `0` for size-returning functions, or a `snobol_match_result_t{.success=false, .error="..."}` for `snobol_match()`) on failure. There is no `abort()` / `exit()` path triggered by OOM conditions out-of-the-box.
- **Cleanup is partial-state-safe.** When an allocation fails mid-construction, any earlier allocations in the same call are `snobol_free`d before the failure return. New code MUST follow this pattern — never return `NULL` from a partially-constructed object.
- **`snobol_check_alloc` helper.** A defensive macro `snobol_check_alloc(ptr)` is declared in `snobol/snobol_internal.h`. In standalone builds it is `((ptr) != NULL)`; in PHP builds it is a no-op (the Zend allocator does not return `NULL`). Use it for symmetry across build types when wrapping allocation sites.
- **Allocation failure tests.** The custom test runner (`tests/c/test_runner.c`) does not inject OOM. ASan/UBSan CI sanity-checks the standalone failure paths. Future work: add an OOM injection test framework (see roadmap, v0.12+).

## Release Process

### Versioning

libsnobol4 uses independent versioning for core and bindings and follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html):

- **Core**: `v<major>.<minor>.<patch>` (e.g., `v0.12.0`)
- **PHP Binding**: `v<major>.<minor>.<patch>` (e.g., `v0.12.0`)

The core version has a **single source of truth**: the
`project(libsnobol4 VERSION X.Y.Z)` declaration in the top-level
`CMakeLists.txt`. The `SNOBOL_VERSION_*` macros in `<snobol/version.h>` are
generated from it at configure time via `core/cmake/version.h.in` — do **not**
hand-edit version literals in any header.

### Creating a Release

1. Bump `project(libsnobol4 VERSION X.Y.Z)` in the top-level `CMakeLists.txt`
   (and reconfigure); the version header regenerates automatically.
2. Move the `[Unreleased]` changelog entries under a new version heading in
   `CHANGELOG.md`.
3. Create git tags:
   ```bash
   git tag core/v0.12.0
   git tag php/v0.12.0
   git push origin --tags
   ```
4. Create GitHub release with changelog

## Getting Help

- **Documentation**: See `README.md` and `bindings/php/README.md`
- **Issues**: Open an issue on GitHub
- **Discussions**: Use GitHub Discussions for questions

## SLJIT JIT Backend

The JIT compiler uses **SLJIT** as the single backend covering all supported
architectures (x86-64, AArch64, ARMv7, RISC-V 64). SLJIT is the default and
only supported `SNOBOL_JIT_BACKEND`; no architecture-specific CMake option is
needed.

```bash
cmake -B build -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### JIT Configuration

The JIT has two tiers:

- **Per-IP hot-trace JIT** (always on when `SNOBOL_JIT=1`): compiles frequently
  executed bytecode regions into native code at runtime. Hotness and budget
  thresholds can be tuned via `SnobolJitConfig`.
- **Method JIT** (on by default via `SnobolJitConfig.method_enabled`):
  compiles entire patterns ahead of execution. The compiled function is
  cached and reused across calls. Toggle via:

```c
SnobolJitConfig config = snobol_jit_get_config();
config.method_enabled = 1;   // enable (default)
config.max_compiled_patterns = 128;
snobol_jit_set_config(&config);
```

### W^X Policy

The JIT uses a **write-then-execute** (W^X) model on all platforms:

1. Pages are allocated writable, not executable.
2. After code emission, pages are switched to executable, not writable.
3. Pages are never simultaneously writable and executable.

On Linux this uses `mmap(PROT_READ|PROT_WRITE)` → `mprotect(PROT_READ|PROT_EXEC)`;
on macOS `mmap` with `MAP_JIT`; on Windows `VirtualAlloc` → `VirtualProtect`.

### QEMU Multi-Architecture Testing

The CI matrix uses QEMU user-mode emulation via `docker/setup-qemu-action`
to validate correctness on non-native architectures. To reproduce locally:

```bash
# Enable QEMU binfmt support (one-time)
docker run --privileged --rm tonistiigi/binfmt --install all

# Build multi-platform Docker image (single Dockerfile for all archs)
docker build -t jit-qemu -f ci/Dockerfile.jit-qemu .

# Run with platform emulation
docker run --rm --platform linux/arm64 jit-qemu
docker run --rm --platform linux/arm/v7 jit-qemu
docker run --rm --platform linux/riscv64 jit-qemu
```

CI jobs in `.github/workflows/ci-core.yml`:
- `jit-qemu-smoke`: consolidated job with `matrix.platform: [ linux/arm64, linux/arm/v7, linux/riscv64 ]`

## `SNOBOL_JIT_DUMP_IR` Debugging Workflow

Set the `SNOBOL_JIT_DUMP_IR` environment variable to `1` to dump the architecture-neutral
IR to `stderr` before the backend lowerer runs:

```bash
# Enable IR dumping
SNOBOL_JIT_DUMP_IR=1 ctest --test-dir build --output-on-failure

# Or with a specific test
SNOBOL_JIT_DUMP_IR=1 ctest --test-dir build -R test_jit --verbose
```

**What you will see:**

```
=== IR dump (region @ bc[0..47], 12 instrs) ===
   0: COPY            v1, v0
   1: LIT_IMM         v2, 104        # 'h'
   2: LIT_IMM         v3, 101        # 'e'
   3: COPY            v4, v2
   ...
  11: ACCEPT          v0
=== end IR dump ===
```

**Workflow:**
1. Run a specific pattern test with `SNOBOL_JIT_DUMP_IR=1`
2. Inspect the IR dump to verify the lifter produced correct IR
3. If a backend miscompiles, compare IR dumps across backends to isolate whether the issue is in the lifter (shared) or the lowerer (backend-specific)
4. For backend-specific debugging, add `fprintf(stderr, ...)` in the lowerer function (`jit_backend_*.c`)

**Note**: `SNOBOL_JIT_DUMP_IR` is an environment variable, not a CMake option. No rebuild is needed to toggle it.

---

**Quick start:**

```bash
# Visual Studio 2022
cmake --preset windows-msvc
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure

# MinGW-w64
cmake --preset windows-mingw
cmake --build build-mingw
ctest --test-dir build-mingw --output-on-failure
```

`CMakePresets.json` at the project root includes `windows-msvc` and `windows-mingw` presets alongside `debug`, `release`, and `asan` presets for Unix.

Note: `SNOBOL_SANITIZE=ON` is not supported on MSVC — configure with GCC or Clang.

## Community Language Bindings

libsnobol4's **officially maintained** components are:
- **C engine** (`core/`) — language-agnostic pattern matching library
- **PHP binding** (`bindings/php/`) — PHP extension and helper classes

Additional language bindings (Python, Rust, Go, Java, etc.) are **community contributions** guided by the
principles below.

### Scope

Community bindings wrap the same language-agnostic C core and provide idiomatic surface APIs for their
respective languages. Examples include:
- A Python binding providing a `snobol.match(pattern, subject)` function
- A Rust crate exposing a `Pattern::compile()` builder API
- A Go module with `snobol.MatchString()`

### What We Provide

- A **reference prototype** at `examples/python-binding/` (Python) — not feature-complete, but demonstrates the C API
- Stabilized C headers under `core/include/snobol/`
- `snobol_match()` and `snobol_pattern_build_*()` convenience APIs (v0.11.0+) for one-shot usage
- A pkg-config / CMake target for linker integration
- PHP distribution via `pie install libsnobol4/snobol` (single command for the entire PHP binding)

### Reference Implementation

A **Python reference prototype** is available at `examples/python-binding/`. This is not
feature-complete but demonstrates the C API integration pattern — use it as a starting
point for your own binding.

### Minimal Binding Checklist

| Check | Requirement                                                                                                            |
|-------|------------------------------------------------------------------------------------------------------------------------|
| ☐     | **Link to C core** via `pkg-config --cflags --libs libsnobol4` or CMake `find_package(libsnobol4)`                     |
| ☐     | **Wrap `snobol_match()`** — the one-shot convenience API for simple use cases                                          |
| ☐     | **Wrap `snobol_pattern_compile()` / `snobol_pattern_match()`** — the multi-step API for repeated matching              |
| ☐     | **Wrap `snobol_match_result_free()`** — proper memory management for match results                                     |
| ☐     | **Provide idiomatic surface API** matching your language's conventions (e.g., a `Pattern` class, a `match()` function) |
| ☐     | **Permissive open-source license** — MIT, Apache 2.0, BSD-2, or similar                                                |
| ☐     | **Publish to standard distribution channel** — PyPI, crates.io, npm, etc.                                              |
| ☐     | **Host in your own repository** — community bindings live outside the libsnobol4 monorepo                              |
| ☐     | **Open a PR** to add your binding to the project README (listing at maintainers' discretion)                           |

### Maintainer Expectations

1. **Community bindings live in their own repositories**, not in this monorepo (except the Python reference).
2. Bindings must use a permissive open-source license (MIT, Apache 2.0, BSD-2, or similar).
3. Bindings follow their language's standard packaging and distribution channels (PyPI, crates.io, etc.).
4. The core maintainers will not break the C ABI without notice (guaranteed by SemVer).
5. The project README may list community binding repositories at the maintainers' discretion.

### Getting Started

1. Start from the **Python reference prototype** at `examples/python-binding/`.
2. Use `snobol_match()` for a one-shot convenience path or the full `snobol_pattern_compile()` / `snobol_pattern_match()`
   API for advanced usage.
3. Link via `pkg-config --cflags --libs libsnobol4` or CMake `find_package(libsnobol4)`.
4. Follow the [Minimal Binding Checklist](#minimal-binding-checklist) above.
5. Publish to your language's ecosystem and open a PR adding your binding to the project README.

## Code of Conduct

Please be respectful and constructive in all interactions. We welcome contributors of all backgrounds and experience
levels.
