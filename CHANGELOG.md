# Changelog

All notable changes to the libsnobol4 project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.5.0] - 2026-05-03

### Added — Template & Substitution Completeness (template-substitution-completeness)

- **`SNBL_FMT_*` named constants** (`core/include/snobol/vm.h`): five `#define`
  constants replace bare integer discriminants in all template opcode handling:
  `SNBL_FMT_UPPER=1`, `SNBL_FMT_LOWER=2`, `SNBL_FMT_LENGTH=3`,
  `SNBL_FMT_LPAD=4`, `SNBL_FMT_RPAD=5`.  Also adds `SNBL_TABLE_ID_UNBOUND
  (0xFFFF)` sentinel and documents the extended `OP_EMIT_FORMAT` encoding for
  `SNBL_FMT_LPAD` / `SNBL_FMT_RPAD` (`reg u8, format_type u8, width u16,
  fill_char u8`) and the new `OP_EMIT_TABLE` name-bytes encoding.

- **`.lower()` template expression** (`core/src/compiler.c`): `${vN.lower()}`
  compiles to `OP_EMIT_FORMAT, reg, SNBL_FMT_LOWER`, enabling ASCII lowercase
  transformation entirely in the C runtime.

- **`.lpad(W[,'c'])` template expression** (`core/src/compiler.c`): `${vN.lpad(W)}`
  / `${vN.lpad(W,'c')}` compile to `OP_EMIT_FORMAT, reg, SNBL_FMT_LPAD, width_hi,
  width_lo, fill_char`.  Width is capped at 1024 in the VM.

- **`.rpad(W[,'c'])` template expression** (`core/src/compiler.c`): same as above
  but emits `SNBL_FMT_RPAD` for right-padding.

- **`snobol_template_bind_tables` API** (`core/include/snobol/compiler.h`,
  `core/src/compiler.c`): new public function that walks compiled template
  bytecode looking for `OP_EMIT_TABLE` entries with `table_id == 0xFFFF`
  (unbound), resolves the embedded name against a caller-supplied `names`/`ids`
  array, and patches the ID in-place.  Returns 0 on full success, -1 if any name
  is unresolvable.

- **`OP_EMIT_TABLE` name-encoding** (`core/src/compiler.c`, `core/src/vm.c`):
  `compile_template_to_bytecode` now writes `table_id=0xFFFF, key_type,
  name_len:u8, name_bytes[name_len]` before the key payload; the VM dispatch
  skips name bytes at runtime after `snobol_template_bind_tables` has resolved
  IDs; previously the table_id was always emitted as 0 with no name.

- **`OP_EMIT_EXPR` legacy alias** (`core/src/vm.c`): `OP_EMIT_EXPR` bytecode
  (old discriminants: 1=upper, 2=length) is mapped to the `OP_EMIT_FORMAT` path
  in the VM dispatch, preserving backward compatibility for any serialised
  patterns compiled with the previous compiler.

- **`Pattern::subst()` table binding** (`bindings/php/src/snobol_pattern.c`):
  `subst(subject, template, tables)` now accepts an optional array of
  `\Snobol\Table` objects; their names are resolved via `snobol_template_bind_tables`
  before execution, and they are registered in the VM table registry for
  `OP_EMIT_TABLE` dispatch.  Throws `\Exception` if any template
  table reference cannot be resolved.

- **PHP test suite** (`bindings/php/tests/php/TemplateOpsTest.php`): eight new
  integration tests covering `.lower()`, `.lpad(5,'0')`, `.rpad(8,'.')`, table-backed
  substitution, unregistered-table exception, and regression tests for `.length()`,
  `.upper()`, plain capture, and literal template.

- **C test suite** (`tests/c/test_template_ops.c`): ten new unit tests
  covering `.lower()`, `.lpad()`, `.rpad()`, no-op padding, graceful
  degradation for missing captures, `snobol_template_bind_tables` patching,
  unresolvable-name return value, end-to-end literal-key and capture-key table
  lookups, and legacy `OP_EMIT_EXPR` alias.

### Changed

- **`compile_template_to_bytecode` now uses `OP_EMIT_FORMAT`** instead of the
  legacy `OP_EMIT_EXPR` opcode for `.upper()` and `.length()` expressions.
  Any code that inspects raw template bytecode must recompile.  The VM still
  accepts old `OP_EMIT_EXPR` bytecode via the legacy alias.

- **`OP_EMIT_TABLE` bytecode layout changed**: a `name_len:u8 + name_bytes[]`
  field is now inserted between `key_type` and the key payload, and `table_id`
  is always written as `0xFFFF` (unbound) by the compiler.  Any previously
  serialised template bytecode that contains `OP_EMIT_TABLE` must be recompiled.

### Removed

- **Duplicate `compile_template_to_bytecode` in PHP binding**
  (`bindings/php/src/snobol_pattern.c`): the old PHP-side implementation (which
  lacked `.lower()`, `.lpad()`, `.rpad()` support and used the old
  `OP_EMIT_TABLE` encoding) has been removed.  All calls now route to the
  canonical core implementation via `compiler.h`.

### Versioning

- **Core library**: `SNOBOL_VERSION_MINOR` bumped from 2 → 3;
  `SNOBOL_VERSION_STRING` is now `"0.3.0"` (`core/include/snobol/snobol.h`).
- **CMake project**: bumped from `0.1.0` → `0.5.0` (`CMakeLists.txt`).
- **PHP binding**: `PHP_SNOBOL_VERSION` bumped from `"0.2.0"` → `"0.5.0"`
  (`bindings/php/src/php_snobol.h`).

## [0.4.0] - 2026-04-25

### Added — Labelled Control Flow (complete-labelled-control-flow)

- **`snobol_ast_create_goto`** (`core/src/ast.c`, `core/include/snobol/ast.h`):
  New AST creation function for `AST_GOTO` nodes, symmetrical with
  `snobol_ast_create_label`.
- **Parser: AST_GOTO emission** (`core/src/parser.c`): `parse_statement` now
  constructs an `AST_GOTO` node for `:(LABEL)` goto syntax and wraps it in a
  `concat([pattern, goto_node])` structure, making goto visible to the compiler.
- **Parser: duplicate-label detection** (`core/src/parser.c`): A `seen_labels`
  array tracks label names within each `snobol_parser_parse()` call; nested
  duplicate labels produce a parse error immediately.
- **Compiler: `AST_LABEL` emission** (`core/src/compiler.c`): `emit_node_c` now
  emits `OP_LABEL label_id` for label nodes, records the bytecode offset
  immediately after the instruction (the label's execution target), and detects
  duplicate definitions at compile time.
- **Compiler: `AST_GOTO` emission** (`core/src/compiler.c`): `emit_node_c` emits
  `OP_GOTO label_id` for goto nodes and marks the referenced label as needing a
  definition.
- **Compiler: unknown-label validation** (`core/src/compiler.c`): after all nodes
  are emitted, `compile_ast_to_bytecode_c` rejects any referenced label that was
  never defined.
- **Bytecode label table** (`core/src/compiler.c`, `core/src/vm.c`): a label
  offset table `[u32 × label_count, u32 label_count]` is appended to the end of
  the bytecode. `vm_exec` reads this table before `vm_run` and pre-registers
  all labels via `vm_register_label`, enabling forward goto references.
- **`get_ranges_ptr` updated** (`core/src/vm.c`): skips the new label table when
  computing the charclass section position so existing charclass-based patterns
  are unaffected.
- **C tests** (`tests/c/test_control_flow.c`): five new test functions covering
  duplicate-label detection via parser, duplicate-label detection via compiler,
  unknown-label detection via compiler, simple label pattern execution, and
  forward goto execution through the full pipeline.
- **PHP compatibility fixtures** (`tests/compat/fixtures/`):
  `WordCounterWithGoto`, `TextTransformerWithGoto`, `TemplateEngineWithGoto` —
  three new fixture classes demonstrating labelled control flow (`Builder::label`,
  `Builder::goto`) via the PHP binding API.
- **PHP compatibility tests** (`tests/compat/CompatibilityTest.php`): thirteen
  new test methods covering all three WithGoto fixtures.
- **PHP binding: `label`/`goto` AST conversion** (`bindings/php/src/snobol_pattern.c`):
  `php_ast_to_c` now handles `"label"` and `"goto"` array nodes from `Builder::label()`
  and `Builder::goto()`, wiring PHP-side construction to `snobol_ast_create_label` /
  `snobol_ast_create_goto` in the C core.
- **PHP Builder tests** (`bindings/php/tests/php/BuilderTest.php`): two additional
  test methods verifying the `label` and `goto` AST node shapes.
- **Test coverage**: 1,300 C tests (35 net-new in Control Flow suite) + 211 PHP tests
  pass; zero regressions.

## [0.3.0] - unreleased

### Added — Compact Backtracking (Phase 2)

- **Compact choice stack** (`core/src/vm.c`): default choice-stack mode now uses
  delta/write-log records storing only `(ip, pos, changed-capture-diff)` instead
  of full capture-array snapshots. Reduces per-choice memory by ≥50% for patterns
  with ≥10 choice points.
- **Write-log mechanism**: 64-entry circular buffer tracks capture modifications
  (`CAP_START`/`CAP_END`) with deduplication; entries compressed at choice point
  creation and replayed in reverse on backtrack.
- **Choice-stack statistics**: `vm_choice_stack_memory_usage()`,
  `vm_choice_stack_depth()`, `vm_choice_record_average_size()` expose runtime
  metrics for observability and testing.
- **Legacy mode**: set `SNOBOL_LEGACY_CHOICE=1` environment variable to restore
  full-snapshot behaviour for compatibility or benchmarking.
- **Test coverage**: all 1,265 C tests + 183 PHP tests pass in both compact and
  legacy modes; no regressions.

## [0.2.3] - 2026-04-22

### Added — jit-cfg-split (Phase 1c)

- **CFG-based multi-block JIT** (`core/src/jit.c`): replaced the single-basic-block
  linear pass-1 with a BFS CFG builder (`jit_cfg_build()`) that discovers up to
  `JIT_CFG_MAX_BLOCKS` (64) reachable blocks per compilation, following both SPLIT
  arms and forward JMPs.
- **Per-block stub emitter** (`snobol_jit_compile_cfg()`): allocates one contiguous
  ARM64 code buffer and emits a separate stub per CFG block in BFS order; stubs
  branch directly to each other via ARM64 `B imm26` — zero interpreter round-trips
  between blocks.
- **Forward-branch fixup pass**: after all stubs are emitted, resolves all
  placeholder `B(0)` instructions to their target stub addresses.
- **ARBNO / backward-edge loop guard**: backward JMP edges get a counted iteration
  guard using callee-saved register `x19` (initialised to `JIT_LOOP_ITER_MAX` = 1024
  per JIT entry); bails out to interpreter when the counter reaches zero, preventing
  infinite compiled loops.
- **`jit_blocks_compiled_total`** counter added to `SnobolJitStats`: cumulative count
  of CFG blocks emitted across all compilations; accessible via `snobol_jit_get_stats()`.
- **Zero-SPLIT fast path**: patterns with a single linear block and no backward edges
  continue to use the existing `op_seq[]` linear compiler path, preserving compile
  latency for straight-line regions.
- **Benchmark scenario** (`bench/tokenize.php`): added 3-arm delimiter scenario
  `',' | ';' | '|'` to exercise the new multi-block SPLIT chain path.
- **CFG unit tests** (`tests/c/test_jit_cfg.c`): 5 new test cases covering
  `jit_blocks_compiled_total` init, single-block counting, 3-arm SPLIT chain block
  discovery, SPLIT backtrack state restoration, and ARBNO loop compilation.

## [0.2.2] - 2026-04-20

### Added — SPLIT→ANY Fusion & Bitmap Optimization (Phase 1b)

- **Compile-time fusion pass** (`core/src/compiler.c`): `snobol_bc_fuse_split_any()` detects
  `OP_SPLIT a b` where both arms are a single `OP_LIT`/`OP_ANY`/`OP_NOTANY` followed
  by `OP_JMP` to the same merge point; rewrites to a single `OP_ANY` with a synthesised
  union charclass.
- **N-arm generalisation**: chained SPLIT chains over single-char arms are collapsed
  iteratively into one `OP_ANY`; `'a'|'b'|'c'` → one `OP_ANY`.
- **ASCII Bitmap Optimization** (`core/src/search.c`): added `OP_ANY` recognition in
  `snobol_search_derive_meta()`, routing fused patterns to Tier 3 (bitmap-accelerated)
  search path for O(match_count) execution.
- **JIT ARM64 Fusion Support**: `OP_ANY` already had a compiled path; fusion allows the
  fused op to run in JIT with zero choice-stack pressure.
- **Benchmark Result**: `tokenize_mixed` achieved ~1,600 ops/sec (≥3.4x baseline) with
  Choice Pushes reduced to 0.

## [0.2.0] - 2026-04-15

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

- **Core Library**: v0.4.0
- **PHP Binding**: v0.4.0
- **AST API**: v1.1.0 (new primitive nodes added, backwards compatible)

---

### Added

- **Monorepo Structure**: Language-agnostic core with separate bindings directories
- **Core C Library** (`core/`):
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
  - PHP binding tests across PHP 8.0-8.5
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
