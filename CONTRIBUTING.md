# Contributing to libsnobol4

Thank you for considering contributing to libsnobol4! This document provides guidelines and instructions for setting up
your development environment, building the project, and submitting changes.

## Project Structure

libsnobol4 uses a **monorepo** structure with a language-agnostic C23 core and language-specific bindings:

```
libsnobol4/
├── core/                    # Core C23 library (language-agnostic)
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

- Start a PHP 8.4 container
- Build the libsnobol4 extension from `core/`
- Enable the extension automatically

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

# Memory leak detection (requires Valgrind)
make test-valgrind
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

## Code of Conduct

Please be respectful and constructive in all interactions. We welcome contributors of all backgrounds and experience
levels.
