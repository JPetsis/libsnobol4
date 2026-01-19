# Contributing to SNOBOL4 for PHP

Thank you for considering contributing to the SNOBOL4 for PHP extension! This document provides guidelines and
instructions for setting up your development environment, building the extension, and submitting changes.

## Development Environment

We recommend using **DDEV** for a consistent development environment that mirrors the production build process.

### Prerequisites

* [Docker Desktop](https://www.docker.com/products/docker-desktop) (or Colima/OrbStack on macOS)
* [DDEV](https://ddev.com/get-started/)
* [Composer](https://getcomposer.org/) (for PHP dev dependencies / PHPUnit)

### Setup

1. **Fork and Clone** the repository:
   ```bash
   git clone https://github.com/YOUR_USERNAME/snobol4-ddev.git
   cd snobol4-ddev
   ```

2. **Start the Environment:**
   ```bash
   ddev start
   ```
   This command will:
    * Start the web and database containers.
   * Build the C extension from `snobol4-php/` in a temporary directory inside the container.
   * Install and enable the `snobol.so` extension for CLI and FPM.

3. **Install PHP Dev Dependencies (optional but recommended):**
   ```bash
   ddev composer install
   ```
   This installs PHPUnit and wiring for the PHP test suite.

## Development Workflow

### Project Structure

* `snobol4-php/`: **Core C Code** – VM, compiler, and extension glue (`php_snobol.c`).
* `php-src/`: **PHP Helpers** – Userland PHP classes like `Snobol\Builder` (`Builder.php`).
* `public/`: **Examples / Entrypoints** – Example scripts and the DDEV web docroot.
* `tests/`: **Tests** – C tests (`tests/c`) and PHP tests (`tests/php`).
* `dev/`: **Developer Tools** – Scripts to build/test and trace the VM.
* `.ddev/`: **Config** – DDEV configuration files including the extension build hook.

### Using the Makefile

For most day‑to‑day work, use the top‑level `Makefile`:

```bash
# Show available targets
make help

# Build the extension (inside DDEV or natively)
make build

# Run C and PHP tests
make test

# Clean build artifacts
make clean

# Install/enable the extension
make install
```

The Makefile automatically detects whether it is running:

- Inside the DDEV container (builds from `/tmp/snobol_build`).
- On the host with `ddev` available (delegates to `ddev exec`).
- Purely natively (builds directly from `snobol4-php/`).

### Developer Tools (`dev/`)

Useful scripts for faster iteration:

```bash
# Build inside DDEV
de ./dev/build_in_ddev.sh

# Run tests inside DDEV
./dev/test_in_ddev.sh

# Toggle VM tracing for debug builds
./dev/trace_vm.sh on   # enable
./dev/trace_vm.sh off  # disable

# Run smoke tests (CLI + web endpoint)
./dev/run_smoke.sh
```

### Making Changes to C Code

If you modify files in `snobol4-php/`, rebuild and **reinstall** the extension:

```bash
# Preferred: use Makefile from host
make build
make install
ddev restart  # Crucial to clear PHP process-level caching
```

**Crucial Note on Reloading:** Simply running `make build` updates the binary in a temporary directory. You **must** run
`make install` to copy it to the PHP extension directory and `ddev restart` to ensure that both the CLI and FPM
processes reload the new shared object. Failing to restart can lead to "stale code" bugs where your changes appear to
have no effect.

### Running Tests

#### C Tests

C tests live under `tests/c/` and currently exercise the VM (`snobol_vm.c`) via a minimal test harness.

Run them via:

```bash
# From project root
make test

# Or directly
cd tests/c
make test
```

These tests are designed to run without PHP installed (pure C + standard library only).

#### PHP Tests (PHPUnit)

PHP tests live under `tests/php/` and are configured via `phpunit.xml`.
They exercise the `Snobol\Builder` and `Snobol\Pattern` API.

1. **Install dev dependencies (once):**
   ```bash
   ddev composer install
   ```

2. **Run tests:**
   ```bash
   # From project root
   make test

   # Or directly inside DDEV
   ddev exec vendor/bin/phpunit
   ```

### Benchmarking

The project includes a suite of benchmark scripts in `bench/` to measure performance.

```bash
# Run all benchmarks
ddev exec make bench
```

Results are saved to `bench/results_*.json`.

### Performance Profiling

To diagnose performance issues or optimized hotspots, you can build the extension with internal VM profiling enabled.

1. **Rebuild with profiling:**
   ```bash
   ddev exec "cd snobol4-php && phpize && ./configure --enable-snobol --enable-snobol-profile && make && sudo make install"
   ddev restart
   ```

2. **Run your script/test:**
   The VM will print statistics to `stderr` after each execution:
   ```text
   [SNOBOL PROFILE] dispatch=12345 push=500 pop=500 max_depth=12
   ```

3. **Disable profiling:**
   Rebuild without the flag to restore maximum performance.

### Manual Testing / Examples

In addition to automated tests, you can experiment using the example scripts under `public/` while DDEV is running:

```bash
# Open in browser (URL from `ddev describe`)
# or run a CLI script

ddev exec php public/test.php
```

## Micro-JIT (experimental)

The project includes an **opt-in micro-JIT** intended to accelerate hot, ASCII-heavy VM traces.

### Build with JIT enabled

Inside DDEV:

```bash
make build-jit
make install
```

### Correctness testing

The PHP test suite includes JIT correctness coverage. When investigating changes that might affect JIT execution,
start with:

```bash
ddev exec vendor/bin/phpunit tests/php/JitCorrectnessTest.php
```

### Benchmarking JIT OFF vs JIT ON

We keep two benchmark snapshots:

- `bench/results_*_jitoff.json`
- `bench/results_*_jiton.json`

To produce and compare them, see `openspec/specs/jit.md` and `bench/README.md`.

Note: `make clean` deletes generated benchmark result JSONs under `bench/` and preserves only
`bench/results_example.json`, so copy/rename any results you want to keep before cleaning.

## Technical Guidelines

### Memory Management

This is a PHP extension; memory management is critical to prevent `zend_mm_heap corrupted` errors.

- **Use Zend Allocators:** For all memory that will be associated with PHP objects or returned to PHP, you **must** use
  `emalloc`, `efree`, `erealloc`, and `estrndup`. These allocators are tracked by PHP's memory manager.
- **Avoid standard `malloc`/`free`:** Only use standard C allocators for internal, short-lived buffers that are
  completely invisible to the Zend engine and are guaranteed to be freed before returning control to PHP.
- **Object Lifecycle:** Ensure that any bytecode or internal buffers associated with a `Snobol\Pattern` object are
  correctly freed in the `free_obj` handler.

<!-- DEBUG LOGGING DISABLED
### Debugging & Logging

The extension includes a built-in logging mechanism for development:

- **Logging Macro:** Use `SNOBOL_LOG("format", ...)` in C code. It automatically includes the filename and line number.
- **Log Location:** Logs are written to `/var/www/html/snobol_debug.log` inside the container.
- **Real-time Tracing:** You can watch the logs while running tests:
  ```bash
  ddev exec tail -f /var/www/html/snobol_debug.log
  ```
-->

## Coding Standards

* **C Code:**
    - Follow the conventions used in existing files under `snobol4-php/`.
    - Target C23 where possible; avoid non‑portable extensions.
    - Keep functions small and focused; prefer explicit error handling and clear ownership of allocated memory.
* **PHP Code:**
    - Follow PSR‑12 coding standards.
    - Use the `Snobol\` namespace for library code.
    - Update or add PHPUnit tests when changing public behavior.

## Submitting Pull Requests

1. Create a new branch for your feature or fix:
   ```bash
   git checkout -b feature/my-new-feature
   ```
2. Run tests and linting (where available):
   ```bash
   make test
   ```
3. Commit your changes with clear, descriptive messages.
4. Push to your fork and submit a Pull Request (PR).
5. Ensure your PR description clearly explains the changes, their motivation, and any testing performed.

## License

By contributing, you agree that your contributions will be licensed under the [Apache License, Version 2.0](LICENSE).
