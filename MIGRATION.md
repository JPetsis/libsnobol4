# Migration Guide: snobol4-ddev → libsnobol4

This guide helps existing contributors migrate from the old `snobol4-ddev` repository structure to the new `libsnobol4`
monorepo structure.

## What Changed?

### Repository Rename

The repository has been renamed from `snobol4-ddev` to `libsnobol4` to reflect its language-agnostic nature.

**Update your Git remote:**

```bash
git remote set-url origin https://github.com/JPetsis/libsnobol4.git
git fetch --all
```

### Directory Structure

| Old Path                | New Path                  | Notes                      |
|-------------------------|---------------------------|----------------------------|
| `snobol4-core/`         | `core/`                   | Language-agnostic C23 core |
| `snobol4-core/include/` | `core/include/snobol/`    | Namespaced headers         |
| `snobol4-core/src/*.c`  | `core/src/*.c`            | Core implementation        |
| `php-src/`              | `bindings/php/php-src/`   | PHP helper classes         |
| `.ddev/`                | `bindings/php/.ddev/`     | PHP dev environment        |
| `tests/php/`            | `bindings/php/tests/php/` | PHPUnit tests              |
| `tests/c/`              | `tests/c/`                | Now at project root        |

### Build System

**Old (phpize):**

```bash
cd snobol4-core
phpize
./configure --enable-snobol
make
```

**New (CMake):**

```bash
# Core library only
cmake -B build
cmake --build build

# With PHP binding
cmake -B build -DBUILD_PHP=ON
cmake --build build
```

**Using Makefile wrapper:**

```bash
make build          # Core library
make build-php      # With PHP binding
make test           # Run tests
```

### Include Paths

All includes have been updated to use namespaced paths:

**Old:**

```c
#include "snobol_lexer.h"
#include "snobol_parser.h"
#include "snobol_ast.h"
```

**New:**

```c
#include "snobol/lexer.h"
#include "snobol/parser.h"
#include "snobol/ast.h"
```

### PHP Extension

**Old:**

```bash
cd snobol4-core
phpize
./configure --enable-snobol
make
sudo make install
```

**New (DDEV):**

```bash
cd bindings/php
ddev start
# Extension auto-builds on start
```

**New (Native):**

```bash
cd bindings/php
cmake -B build -DBUILD_PHP=ON
cmake --build build
sudo cmake --install build
```

### Running Tests

**C Tests:**

Old:

```bash
cd tests/c
make test
```

New:

```bash
# Using CTest
ctest --test-dir build

# Using Makefile
make test
```

**PHP Tests:**

Old:

```bash
composer install
vendor/bin/phpunit tests/php
```

New:

```bash
cd bindings/php
composer install
vendor/bin/phpunit tests/php
```

## Step-by-Step Migration

### 1. Update Repository

```bash
# If you have a fork
git remote rename origin old-origin
git remote add origin https://github.com/JPetsis/libsnobol4.git
git fetch origin
git checkout main
git branch -D main  # Delete old local branch
git checkout -b main origin/main
```

### 2. Update Your Development Environment

```bash
# Clean old build artifacts
make clean  # or rm -rf build/ snobol4-core/*.o

# Rebuild with new system
cmake -B build -DBUILD_TESTS=ON
cmake --build build
```

### 3. Update IDE Configuration

If you use an IDE (PhpStorm, VSCode, etc.):

1. Close the IDE
2. Delete `.idea/` or `.vscode/` directories
3. Reopen the project
4. Update include paths in project settings:
    - Add `core/include` to C/C++ include paths
    - Update PHP include paths to `bindings/php/php-src/`

### 4. Update Custom Scripts

If you have custom build/test scripts, update them:

- Replace `snobol4-core/` with `core/`
- Replace `php-src/` with `bindings/php/php-src/`
- Replace `phpize` builds with CMake
- Update include paths to `snobol/*.h`

## Common Issues

### "File not found: snobol_lexer.h"

**Solution:** Update includes to use namespaced paths:

```c
// Old
#include "snobol_lexer.h"

// New
#include "snobol/lexer.h"
```

### "Extension not loading after build"

**Solution:** Make sure you're building from the correct directory:

```bash
# Old
cd snobol4-core

# New
cd bindings/php
```

### "CTest not found"

**Solution:** Install CMake:

```bash
# macOS
brew install cmake

# Ubuntu/Debian
apt install cmake

# Or use the Makefile wrapper
make test
```

### "PHP tests failing after migration"

**Solution:** Make sure you're running from the correct directory:

```bash
# Old
vendor/bin/phpunit tests/php

# New
cd bindings/php
vendor/bin/phpunit tests/php
```

## Need Help?

- **Documentation**: See `README.md` and `CONTRIBUTING.md`
- **Issues**: Open an issue on GitHub with the "migration" label
- **Discussions**: Start a discussion for questions

## Quick Reference

| Task       | Old Command                    | New Command                                       |
|------------|--------------------------------|---------------------------------------------------|
| Build core | `cd snobol4-core && make`      | `cmake -B build && cmake --build build`           |
| Build PHP  | `cd snobol4-core && make`      | `cd bindings/php && ddev start`                   |
| C tests    | `cd tests/c && make test`      | `ctest --test-dir build`                          |
| PHP tests  | `vendor/bin/phpunit tests/php` | `cd bindings/php && vendor/bin/phpunit tests/php` |
| Clean      | `make clean`                   | `make clean`                                      |

---

**Migration completed?** Run `make test` to verify everything works!
