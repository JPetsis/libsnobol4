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

### 4. Commit Changes

Write clear, descriptive commit messages:

```bash
git commit -m "feat: add bounded repetition support

- Implement repeat(P, min, max) pattern
- Add C tests for bounded repetition
- Update documentation"
```

### 5. Submit a Pull Request

- Push to your fork
- Open a PR against the `main` branch
- Include a description of changes
- Reference any related issues

## Coding Standards

### C Code (Core Library)

- **Standard**: C23
- **Formatting**: Use `clang-format` (run `make format`)
- **Naming**:
  - Types: `snake_case_t` (e.g., `ast_node_t`)
  - Functions: `snake_case` (e.g., `snobol_ast_create`)
  - Macros: `SCREAMING_SNAKE_CASE` (e.g., `SNOBOL_AST_VERSION`)
- **Documentation**: Add file-level and function-level comments

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

## Release Process

### Versioning

libsnobol4 uses independent versioning for core and bindings:

- **Core**: `v<major>.<minor>.<patch>` (e.g., `v0.1.0`)
- **PHP Binding**: `v<major>.<minor>.<patch>` (e.g., `v0.1.0`)

### Creating a Release

1. Update version constants in code
2. Update CHANGELOG.md
3. Create git tags:
   ```bash
   git tag core/v0.1.0
   git tag php/v0.1.0
   git push origin --tags
   ```
4. Create GitHub release with changelog

## Getting Help

- **Documentation**: See `README.md` and `bindings/php/README.md`
- **Issues**: Open an issue on GitHub
- **Discussions**: Use GitHub Discussions for questions

## Windows Development

The project supports MSVC (Visual Studio 2022+) and MinGW-w64 toolchains on Windows. The JIT is supported on Windows x86-64 via the `x86_64` backend (Microsoft x64 ABI), using `VirtualAlloc`/`VirtualProtect` for W^X code-page management (DEP-compliant — never uses `PAGE_EXECUTE_READWRITE`). Build with:

```bash
cmake -B build -DBUILD_TESTS=ON -DSNOBOL_JIT_BACKEND=x86_64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Linux / macOS x86-64 JIT

The micro-JIT runs on x86-64 Linux and macOS (Intel) via the `x86_64` backend using the
System V AMD64 ABI. Build with:

```bash
cmake -B build -DBUILD_TESTS=ON -DSNOBOL_JIT_BACKEND=x86_64
cmake --build build
ctest --test-dir build --output-on-failure
```

On Linux, code pages use `mmap(PROT_READ|PROT_WRITE)` → `mprotect(PROT_READ|PROT_EXEC)` (W^X model);
on macOS Intel, `mmap` with `MAP_JIT` is used. The x86 instruction cache is coherent on both platforms,
so no explicit icache flush is needed (except `FlushInstructionCache` on Windows).

## Windows x86-64 JIT

The micro-JIT runs on Windows x86-64 (MSVC or MinGW-w64) via the `x86_64` backend using the
Microsoft x64 ABI. Build with:

```bash
cmake -B build -DBUILD_TESTS=ON -DBUILD_PHP=OFF -DSNOBOL_JIT_BACKEND=x86_64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

**DEP compliance**: Code pages are allocated with `VirtualAlloc(MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE)`,
then switched to `PAGE_EXECUTE_READ` via `VirtualProtect` after emission. `PAGE_EXECUTE_READWRITE` is
never used. The `/NXCOMPAT` linker flag (enabled by default since VS 2010) ensures hardware DEP is active.

## Linux AArch64 JIT

The micro-JIT runs on Linux AArch64 (bare-metal or QEMU). The build uses the same
`-DSNOBOL_JIT_BACKEND=arm64` CMake option as macOS:

```bash
# Native AArch64 build
cmake -B build -DBUILD_TESTS=ON -DSNOBOL_JIT_BACKEND=arm64
cmake --build build
ctest --test-dir build --output-on-failure

# QEMU AArch64 (from x86-64 host)
docker run --rm --platform linux/arm64 \
  -v $(pwd):/workspace -w /workspace \
  arm64v8/ubuntu:24.04 \
  bash -c "apt-get update && apt-get install -y cmake build-essential \
    && cmake -B build -DBUILD_TESTS=ON -DBUILD_PHP=OFF -DSNOBOL_JIT_BACKEND=arm64 \
    && cmake --build build \
    && ctest --test-dir build --output-on-failure"
```

### W^X Policy

On Linux the JIT uses a **write-then-execute** (W^X) model:
1. Pages are allocated with `PROT_READ | PROT_WRITE` (writable, not executable).
2. After code emission, `mprotect` transitions them to `PROT_READ | PROT_EXEC`.
3. Pages are never simultaneously writable and executable.

This is the default behaviour on Linux AArch64. If you encounter `SIGSEGV` during
JIT compilation, check that `mprotect` is permitted in your environment
(e.g., some hardened kernels or seccomp profiles may block `PROT_EXEC`).

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

## Code of Conduct

Please be respectful and constructive in all interactions. We welcome contributors of all backgrounds and experience
levels.
