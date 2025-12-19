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

If you modify files in `snobol4-php/`, rebuild the extension:

```bash
# Preferred: use Makefile
make build

# Or explicitly inside DDEV
./dev/build_in_ddev.sh
```

Under the hood, the DDEV hook and `make build` will:

- Copy `snobol4-php/` to `/tmp/snobol_build` in the web container.
- Run `phpize`, `./configure`, `make`.
- Install `snobol.so` into the PHP extension directory.

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

### Manual Testing / Examples

In addition to automated tests, you can experiment using the example scripts under `public/` while DDEV is running:

```bash
# Open in browser (URL from `ddev describe`)
# or run a CLI script

ddev exec php public/test.php
```

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
6. For non‑trivial behavior or architectural changes, consider adding an OpenSpec change under `openspec/changes/` and
   reference it from the PR.

## License

By contributing, you agree that your contributions will be licensed under the [Apache License, Version 2.0](LICENSE).
