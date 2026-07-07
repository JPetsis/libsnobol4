# Migration Guide: snobol4-ddev → libsnobol4

This guide helps existing contributors migrate from the old `snobol4-ddev` repository structure to the new `libsnobol4`
monorepo structure.

---

## v0.11.0 → v0.12.0 (JIT Retirement & Search Engine Consolidation)

### No breaking changes

This is a non-breaking release. There are **no C API changes** and **no PHP API changes**.
All existing consumers require no code changes.

### Behaviour changes consumers should be aware of

- **All JIT subsystems removed**: SLJIT backend, method JIT, tracing JIT IR, JIT config,
  stats, lifecycle, and CI matrix have been fully eliminated. The `-DSNOBOL_JIT` CMake
  option is ignored. Patterns that previously benefited from JIT compilation now execute
  through the multi-tier search engine (Tiers 0–9).
- **JIT-related public API removed**: `snobol_jit_config_t`, `snobol_jit_stats_t`,
  `snobol_get_jit_stats()`, `snobol_jit_acquire_context()`, `snobol_jit_release_context()`,
  and `SNOBOL_FLAG_SEARCH_MODE` no longer exist. Update any monitoring code that referenced
  these symbols.
- **JIT fields removed from VM struct**: `ip_counts`, `traces`, `ctx`, `jit_region` are
  gone from the `VM` struct.
- **JIT tests and CI removed**: `tests/c/test_jit*.c`, `.github/workflows/jit*.yml`, and
  QEMU Dockerfiles have been deleted.
- **Build options simplified**: `SNOBOL_JIT`, `SNOBOL_JIT_BACKEND`, `SNOBOL_JIT_METHOD`,
  and `SNOBOL_JIT_DUMP_IR` CMake options are no longer recognized.

### New: Multi-tier search engine (Tiers 0–9)

The search engine now auto-selects the optimal matching strategy via a function pointer
table (`tier_table[meta->tier]`). Pre-computed tier metadata is stored in
`snobol_search_meta_t.tier` at compile time. Key tiers:

| Tier | Name | Description |
|------|------|-------------|
| 0 | `BREAK_SCAN` | BREAK/BREAKX ASCII bitmap scan |
| 1 | `SPAN_SCAN` | SPAN ASCII bitmap scan |
| 2 | `LITERAL` | Pure literal (memcmp) |
| 3 | `PREFIX` | Literal prefix (memmem + BMH skip) |
| 4 | `BITMAP` | Single-char alternation (candidate bitmap) |
| 5 | `ALT_LIT` | Alt-of-literals (trie) |
| 6 | `SEARCH_VM` | Lightweight backtracking NFA (~424 bytes) |
| 7 | `AUTOMATON` | DFA via powerset construction |
| 8 | `GENERAL` | Full SNOBOL4 VM fallback |
| 9 | `SIMD_NFA` | SIMD Thompson NFA (AVX2/NEON) |

### New: `Pattern::searchSplitOffsets()` (PHP)

Returns `[[offset, length], ...]` pairs instead of segment strings. Zero zend_string
allocation. Useful when you need positions for further processing without creating
intermediate string objects.

### Migration Steps

1. **Remove JIT CMake flags** if present:
   ```diff
   - cmake -B build -DSNOBOL_JIT=ON -DSNOBOL_JIT_BACKEND=sljit
   + cmake -B build
   ```

2. **Remove JIT code** from custom builds:
   ```diff
   - #include <snobol/jit.h>
   - snobol_jit_config_t config = {...};
   - snobol_jit_stats_t stats;
   - snobol_get_jit_stats(&stats);
   ```

3. **No PHP code changes needed** — the PHP binding API is unchanged.

---

## v0.9.0 → v0.10.0 (Multi-Architecture JIT)

---

## v0.8.0 → v0.9.0 (Full JIT Opcode Coverage)

### No breaking changes

This is a non-breaking JIT improvement release. There are **no C API changes**
and **no PHP API changes**. All existing consumers require no code changes.

### Behaviour changes consumers should be aware of

- **Patterns that previously never entered the JIT now do.**  
  Opcodes that previously caused a JIT→interpreter context switch
  (`OP_REM`, `OP_RPOS`, `OP_RTAB`, `OP_FENCE`, `OP_LABEL`, `OP_GOTO`,
  `OP_GOTO_F`, `OP_EMIT_*`, `OP_TABLE_GET`, `OP_TABLE_SET`, `OP_BAL`,
  `OP_EVAL`, `OP_DYNAMIC`, `OP_DYNAMIC_DEF`) are now compiled inline or
  via call-out within the JIT region. Patterns using these opcodes will
  execute faster under `SNOBOL_JIT=1` with no observable difference in
  match results.

- **`jit_bailouts_total` will be 0 for previously-falling-back patterns.**  
  If your code reads `SnobolJitStats.jit_bailouts_total` and previously
  observed non-zero values for patterns containing the opcodes listed above,
  those values will now be 0. This is the expected and correct behaviour.

- **`jit_interp_time_ns_total` will be lower (or 0) for fully-compiled patterns.**  
  Patterns that previously triggered interpreter round-trips will no longer
  accumulate interpreter time. Monitoring dashboards or assertions that
  expected non-zero `jit_interp_time_ns_total` for such patterns should be
  updated to expect 0.

### New: benchmark gate in `bench/compare_jit.php`

`bench/compare_jit.php` now calls `snobol_jit_get_stats()` at the end of the
run (when the PHP extension is loaded) and exits with code 1 if the
interpreter-time ratio exceeds 5%. This gate is a no-op when the extension is
not loaded or when no timing data is available.

---

## v0.7.0 → v0.8.0 (Build & Tooling Hardening)

### No breaking changes

This is a non-breaking tooling release.  There are **no C API changes** and **no PHP API changes**.
All existing `find_package(libsnobol4)` and direct-header consumers require no code changes.

### New: pkg-config support

After `cmake --install build`, a `libsnobol4.pc` file is now installed to
`${libdir}/pkgconfig/libsnobol4.pc`.  Non-CMake build systems can use it
to discover the library:

```bash
pkg-config --cflags --libs libsnobol4
# If installed to a non-standard prefix:
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig pkg-config --cflags --libs libsnobol4
```

### New: SNOBOL_SANITIZE CMake option

```bash
cmake -B build-asan -DSNOBOL_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan --target test-asan
```

This option is only supported with GCC or Clang; MSVC builds will emit a
fatal error if `SNOBOL_SANITIZE=ON` is requested.

### New: CMakePresets.json

Named presets are available at the project root:

```bash
cmake --preset debug          # Debug build
cmake --preset asan           # ASan + UBSan (Linux/macOS)
cmake --preset windows-msvc   # Windows MSVC
cmake --preset windows-mingw  # Windows MinGW-w64
```

### New: Makefile targets

- `make build-asan` — configure and build in `build-asan/` with `SNOBOL_SANITIZE=ON`.
- `make test-asan`  — run the test suite under ASan/UBSan (requires `make build-asan` first).
- `make test-valgrind` — run the test suite under Valgrind memcheck (requires `make build` and Valgrind on PATH).

### Windows builds

MSVC (Visual Studio 2022+) and MinGW-w64 are now officially supported.
The JIT is automatically forced to `OFF` on Windows — no manual configuration needed.

---

## v0.6.x → v0.7.0 (Unicode Completeness)

### No breaking changes

All existing APIs are fully backward-compatible. This release is purely additive.

### New API: `snobol_pattern_compile_ex()`

```c
#include <snobol/snobol.h>

snobol_pattern_t *pat = snobol_pattern_compile_ex(
    ctx, "'hello'", 7,
    SNOBOL_FLAG_CASE_INSENSITIVE,
    &err
);
// pat now matches "HELLO", "Hello", etc.
```

Existing callers of `snobol_pattern_compile()` need no changes — it now internally
delegates to `compile_ex` with `flags=0`, producing identical behaviour.

### New API: `snobol_get_api_version()`

```c
uint32_t v = snobol_get_api_version();
uint32_t major = v >> 16;   // 0
uint32_t minor = (v >> 8) & 0xFF;  // 7
uint32_t patch = v & 0xFF;  // 0
```

Intended for use in binding MINIT or startup code to detect major ABI changes.

### PHP binding: new `RuntimeException` on major version mismatch

If your PHP application loads the `snobol` extension and the linked libsnobol4
shared library has a **different major version** from what the extension was
compiled against, PHP will now throw a `RuntimeException` at extension load time
(during MINIT). This surfaces immediately rather than causing subtle wrong
behaviour later.

**No action needed** if you keep the extension and library in sync (the normal
case). If you build from source, always rebuild the PHP extension after upgrading
the C core.

### PHP: `Pattern::fromString()` caseInsensitive option

```php
$pat = Pattern::fromString("'hello'", ['caseInsensitive' => true]);
$res = $pat->match('HELLO');  // matches
```

### UPPER / LOWER now full Unicode

`Text::upper()` / `Text::lower()` (and the `${vN.upper()}` / `${vN.lower()}`
template expressions) now handle the full Latin-1 Supplement and Latin Extended-A
Unicode blocks, including the German sharp-s expansion (ß → SS). ASCII strings
are unchanged — the ASCII fast-path is preserved.

---

## v0.5.x → v0.6.0 (Code Quality Improvements + PHP Binding Cleanup)

### No API changes

There are **no breaking API changes** in v0.6.0.

- All PHP public APIs (`Pattern`, `PatternHelper`, `Text`, `Table`, etc.) are
  unchanged.
- All C public APIs (`core/include/snobol/*.h`) are unchanged in signature and
  semantics; annotation macros (`SNOBOL_NODISCARD`, `[[maybe_unused]]`) were
  added with MSVC-compatible fallbacks.
- `nullptr` replaces `NULL` internally in `core/src/` but is not visible at
  the public API level.

### ArchitecturalConstraintsTest (PHPUnit test suite)

A new test class `ArchitecturalConstraintsTest` was added to
`bindings/php/tests/php/`. It enforces that no PHP-native `Lexer.php` /
`Parser.php` files exist and that no `new Lexer(` / `new Parser(` calls are
present in `php-src/`. If you maintain a fork with custom PHP-side parsing
code, these tests will now fail — migrate any such code to call the C extension
directly.

---

## v0.4.x → v0.5.0 (Template & Substitution Completeness)

### Template Bytecode Format Change

Template bytecode generated by `compile_template_to_bytecode()` has changed in
two ways.  If you serialise or cache compiled template bytecode, **recompile all
templates** when upgrading to v0.5.0.

#### 1. `OP_EMIT_TABLE` encoding — name bytes added

Before v0.5.0 the `OP_EMIT_TABLE` instruction was:

```
OP_EMIT_TABLE  table_id:u16  key_type:u8  <key payload>
```

From v0.5.0 the format is:

```
OP_EMIT_TABLE  table_id:u16(0xFFFF=unbound)  key_type:u8
              name_len:u8  name_bytes[name_len]  <key payload>
```

The `table_id` is always written as `0xFFFF` (= `SNBL_TABLE_ID_UNBOUND`) by
the compiler; call `snobol_template_bind_tables()` to resolve names to runtime
IDs before executing.

#### 2. `OP_EMIT_EXPR` → `OP_EMIT_FORMAT`

The compiler no longer emits `OP_EMIT_EXPR` (old discriminants: 1=upper,
2=length). It now emits `OP_EMIT_FORMAT` with the `SNBL_FMT_*` constants.

**The VM still accepts old `OP_EMIT_EXPR` bytecode** via a legacy alias that
maps discriminant 1 → `SNBL_FMT_UPPER` and discriminant 2 → `SNBL_FMT_LENGTH`.
Old compiled *pattern* bytecode is therefore compatible.  However, old *template*
bytecode with `OP_EMIT_TABLE` must be recompiled because the encoding changed.

### PHP `Pattern::subst()` signature change

`subst()` now accepts an optional third argument — an array of `\Snobol\Table`
objects keyed by table name:

```php
// Before (no table support)
$result = $pattern->subst($subject, $template);

// After (with table-backed templates)
$result = $pattern->subst($subject, '$v0[prices[\'key\']]', ['prices' => $pricesTable]);
```

If the template references a table name that is not present in the array,
`subst()` throws `\InvalidArgumentException`.  Previously, the unresolved
table ID was silently 0, which caused incorrect lookups.

---

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
| `snobol4-core/`         | `core/`                   | Language-agnostic C core   |
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
