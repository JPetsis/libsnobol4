# Changelog

All notable changes to the libsnobol4 project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.10.0] - 2026-05-23

### Added — JIT Neutral IR Layer (`jit-neutral-ir`)

- **Architecture-neutral IR definition** (`core/include/snobol/jit_ir.h`):
  `jit_ir_opcode_t` enum and `jit_ir_instr_t` struct with pre-decoded operands
  and virtual register operands (max 256 per region, `uint16_t vreg_next`).

- **IR region builder** (`core/src/jit_ir.c`):
  `jit_ir_region_new`, `jit_ir_append`, `jit_ir_alloc_vreg`, `jit_ir_inc_use`.
  Arena-style growable instruction array; marks region `non_compilable` and logs
  a warning if the 256-register limit is exceeded.

- **VM opcode lifter** (`jit_ir_lift_region`):
  Translates all VM bytecodes to IR in a single linear pass.  Covers every opcode
  listed in the JIT coverage matrix (jit-compiled, call-out, and pseudo groups).

- **IR optimiser passes**:
  - **DCE** (`jit_ir_dce`): removes pure instructions whose output register has
    zero uses.
  - **Copy-propagation** (`jit_ir_copy_propagation`): folds `JIT_IR_COPY`
    instructions into their consumers, then triggers DCE to remove dead copies.

- **`SNOBOL_JIT_DUMP_IR=1` environment variable**: when set, writes a
  human-readable IR dump to `stderr` before the backend lowerer runs.

- **`jit_backend_t` vtable** (`core/include/snobol/jit_backend.h`):
  `lower`, `flush_icache`, `name` function pointers.
  `jit_backend_register()` and `jit_backend_get()` registration API in `jit.c`.

- **`SNOBOL_JIT_BACKEND` CMake option** (default: `arm64`):
  selects the backend at compile time; unknown values produce a `FATAL_ERROR`
  listing valid backend names.

- **ARM64 backend** (`core/src/jit_backend_arm64.c`):
  Implements the vtable; moves all ARM64 code-generation out of `jit.c`.
  Has its own CFG builder (`ir_cfg_build`) and IR-based block emitter
  (`emit_block_ops_ir`).

- **Unit tests** (`tests/c/test_jit_ir.c`): covers region builder, vreg
  allocation, 256-register limit, DCE, and copy-propagation.  Registered as
  `test_jit_ir` and `test_ir_roundtrip` CTest targets with `jit-ir` / `roundtrip`
  labels.

- **CI job** (`ci-jit-backend-tests`): builds and runs the full JIT test suite on
  macOS Apple Silicon and Linux AArch64 runners using `SNOBOL_JIT_BACKEND=arm64`.

### Changed

- **`jit.c` legacy fallback removed**: the pre-IR direct ARM64 emission code
  (~1500 lines) has been deleted from `jit.c`.  The new two-phase IR pipeline is
  the only compilation path; `snobol_jit_compile` returns `nullptr` if no backend
  is registered (should not occur after `snobol_jit_init()`).

- **`vreg_next` type** in `jit_ir_region_t` changed from `uint8_t` to `uint16_t`
  to prevent silent wrap-around at the 256-register boundary.



### Added

- **Full JIT opcode coverage**: All VM opcodes now have compiled-region implementations in the ARM64 micro-JIT — no more interpreter fallback for any opcode group:
  - **Position guards** (`OP_REM`, `OP_RPOS`, `OP_RTAB`): compiled inline as integer comparisons against `vm->sp` / `vm->subject_len`.
  - **Cut/fence** (`OP_FENCE`): compiled inline; truncates the choice stack at the current depth.
  - **Labeled control flow** (`OP_LABEL`, `OP_GOTO`, `OP_GOTO_F`): `OP_LABEL` is a no-emit pseudo-op; `OP_GOTO` / `OP_GOTO_F` compile as unconditional / conditional branches to resolved label targets.
  - **Emit opcodes** (`OP_EMIT_LITERAL`, `OP_EMIT_CAPTURE`, `OP_EMIT_FORMAT`, `OP_EMIT_TABLE`, `OP_EMIT_EXPR`): compiled as inline call-outs to the registered `vm->emit_fn` callback via `BLR`.
  - **Table operations** (`OP_TABLE_GET`, `OP_TABLE_SET`): compiled as call-outs to `snobol_jit_helper_table_get` / `snobol_jit_helper_table_set`.
  - **Balanced match** (`OP_BAL`): compiled as a call-out to `snobol_jit_helper_bal`.
  - **Host callbacks** (`OP_EVAL`): compiled as a call-out with full caller-saved register spill/restore around the `BLR`.
  - **Dynamic patterns** (`OP_DYNAMIC`, `OP_DYNAMIC_DEF`): `OP_DYNAMIC` compiled as a call-out to `snobol_jit_helper_dynamic`; `OP_DYNAMIC_DEF` treated as a region-termination pseudo-op.

- **JIT observability counter test suite** (`tests/JitOpcodeCoverageTest.php`): one test case per opcode group asserting `jit_bailouts_total == 0` and `jit_exec_time_ns_total > 0` after representative patterns run under `SNOBOL_JIT=1`.

### Changed

- **Benchmark gate** (`bench/compare_jit.php`): added `jit_ratio_check()` function and an end-of-script gate that reads `jit_exec_time_ns_total` / `jit_interp_time_ns_total` from `snobol_jit_get_stats()` and exits with code 1 if the interpreter-time ratio exceeds 5%.
- **Opcode coverage comment** in `core/src/jit.c`: all entries updated to `jit-compiled`, `call-out`, or `pseudo` — no `fallback` entries remain.

## [0.8.0] - 2026-05-21

### Build & Tooling Hardening (`build-tooling-hardening`)

### Added

- **`SNOBOL_SANITIZE` CMake option**: When `ON`, compiles the library and all
  test binaries with `-fsanitize=address,undefined -fno-omit-frame-pointer`.
  A fatal error is emitted if `SNOBOL_SANITIZE=ON` is requested on MSVC.

- **`test-asan` CMake custom target**: Runs the C test suite under
  AddressSanitizer + UndefinedBehaviorSanitizer.  Available in any build
  configured with `-DSNOBOL_SANITIZE=ON`.

- **`test-valgrind` CMake custom target**: Runs the C test suite under
  Valgrind (`--error-exitcode=1 --leak-check=full --track-origins=yes`).
  Not created if Valgrind is absent from `PATH` (warning emitted instead).

- **`make build-asan` and `make test-asan`** Makefile convenience aliases
  delegating to the CMake targets.  `make test-valgrind` now delegates to the
  CMake `test-valgrind` target rather than running Valgrind via shell directly.

- **`libsnobol4.pc` pkg-config file**: Generated by `configure_file()` in
  `core/CMakeLists.txt` and installed to `${CMAKE_INSTALL_LIBDIR}/pkgconfig/`.
  Consumers can discover the library with `pkg-config --cflags --libs libsnobol4`.

- **`CMakePresets.json`** at project root: named presets `debug`, `release`,
  `asan`, `windows-msvc`, `windows-mingw` with corresponding build and test
  presets for `debug`, `release`, and `asan`.

- **Optional GitHub Actions workflows**:
  - `.github/workflows/sanitizers.yml`: ASan + UBSan build on `ubuntu-latest`,
    triggered by `workflow_dispatch` or nightly `schedule: cron: '0 2 * * *'`.
    Uses `-DSNOBOL_SANITIZE=ON` and the `test-asan` CMake target.
    Not part of the default PR gate.
  - `.github/workflows/benchmarks.yml`: full benchmark suite on `ubuntu-latest`,
    triggered by `workflow_dispatch` (with optional `base_ref` input) or nightly
    schedule.  Runs `php bench/run_all.php` and uploads results as a 30-day artifact.

- **Doxygen doc comments** on all 14 public headers under `core/include/snobol/`:
  every public function, struct, enum, macro, and typedef now has a
  `/** @brief ... @param ... @return ... */` comment.

### Changed

- **`core-build`**: `cmake --install` now also installs `libsnobol4.pc` to
  `${CMAKE_INSTALL_LIBDIR}/pkgconfig/`.  Install summary message updated.
- **`core-build`**: `SNOBOL_JIT` is explicitly force-set to `OFF` on WIN32
  (in addition to the existing `NOT WIN32` processor guard) so that
  `SNOBOL_JIT=ON` passed by a user is silently overridden on Windows.
- **`ci-core.yml`** matrix already included `windows-latest`; no change needed.
  Both `sanitizers.yml` and `benchmarks.yml` are separate from the PR gate.

---

## [0.7.0] - 2026-05-20

### Unicode Completeness (`unicode-completeness`)

### Added

- **UPPER / LOWER v2** (`core/src/unicode_fold.c`): Full Unicode case conversion
  covering Latin-1 Supplement (U+00C0–U+00FF), Latin Extended-A (U+0100–U+017F),
  and multi-character expansion for German sharp-s (ß → SS). `snobol_upper()` and
  `snobol_lower()` now decode UTF-8 codepoints with `utf8_peek_next()` and use a
  self-contained static fold table; ASCII fast-path is preserved.

- **`SNOBOL_FLAG_CASE_INSENSITIVE` (0x0001u)** in `core/include/snobol/snobol.h`:
  Compile-time flag enabling case-folded pattern matching.

- **`snobol_pattern_compile_ex(ctx, source, len, flags, error)`**: New public API
  function that accepts a `flags` bitmask. Pass `SNOBOL_FLAG_CASE_INSENSITIVE` to
  enable case-insensitive matching; unknown flag bits are silently ignored. The
  existing `snobol_pattern_compile()` now delegates to `compile_ex` with `flags=0`.

- **`snobol_get_api_version()`**: Returns `(MAJOR << 16) | (MINOR << 8) | PATCH` as
  `uint32_t`. For v0.7.0 this returns `0x00000700u`. Intended for binding load-time
  compatibility checks. Declared in `snobol.h`, implemented in `core/src/version.c`.

- **PHP binding load-time version check**: `PHP_MINIT_FUNCTION(snobol)` now calls
  `snobol_get_api_version()`, extracts the major version, and throws a PHP
  `RuntimeException` (returning `FAILURE`) if the linked library's major version
  does not match the compile-time constant.

- **`snobol_get_api_version()` PHP function**: Exposed as a first-class PHP function;
  returns the library's API version integer directly from the C library.

- **`Pattern::fromString()` supports `caseInsensitive` option**: The `$options`
  array parameter now accepts `['caseInsensitive' => true]` to compile source-text
  patterns via the case-insensitive `compile_ast_to_bytecode_c()` path.

### Verified

- All C unit tests pass (1359/1359) ✅
- All PHP tests pass (236/236) ✅
- New C test suites: `test_unicode_fold` (22 cases), `test_string_case` (Unicode),
  `test_pattern_case` (11 cases), `test_api_version` (5 cases)
- `core_amalgam.c` regenerated (13 source files) ✅

---

## [0.6.0] - 2026-05-10

### PHP Binding Cleanup (`php-binding-cleanup`)

### Verified / Enforced

- **No PHP-native lexer or parser in `bindings/php/php-src/`**: confirmed that
  `Lexer.php` and `Parser.php` do not exist; the "C core owns all parsing"
  architectural goal is now formally verified and permanently guarded.

- **`Pattern::fromString()` fully C-backed**: the C extension method
  (`snobol_pattern.c`) routes through `snobol_lexer_create()` →
  `snobol_parser_parse()` → `compile_ast_to_bytecode_c()`. C-side parse errors
  are caught with `snobol_parser_has_error()` / `snobol_parser_get_error_location()`
  and thrown as PHP `\Exception` with a message of the form
  `"Parse error at line N, column M: <detail>"`.

- **`PatternHelper::fromAst()` fully C-backed**: the helper validates
  `isset($ast['type'])` then delegates the entire compilation to
  `Pattern::compileFromAst()` (C extension `compile_ast_to_bytecode_c()`).
  Zero PHP-side AST traversal.

### Added

- **`ArchitecturalConstraintsTest`** (`bindings/php/tests/php/ArchitecturalConstraintsTest.php`):
  new PHPUnit test class that enforces the no-native-parsing rule permanently:
  - `testNoPhpNativeLexerInstantiation()` — asserts zero `new Lexer(` under `php-src/`
  - `testNoPhpNativeParserInstantiation()` — asserts zero `new Parser(` under `php-src/`
  - `testLexerPhpFileDoesNotExist()` — asserts `php-src/Lexer.php` is absent
  - `testParserPhpFileDoesNotExist()` — asserts `php-src/Parser.php` is absent

### Verified

- All 198 PHP tests pass (`ddev exec vendor/bin/phpunit tests/`) ✅
- Zero regressions in `PatternTest`, `BuilderTest`, `PatternHelper`-exercising tests ✅
- New architectural constraints: 4/4 tests pass ✅

---

### C23 Code-Quality Adoption (`adopt-c23-features`)

### Changed

- **`nullptr` throughout `core/src/*.c`**: all `NULL` pointer literals replaced
  with the typed C23 `nullptr` keyword. The single surviving `NULL` in the
  codebase is the string literal `"(NULL)\n"` in `ast.c` (intentional).

- **`[[nodiscard]]` on public headers**: annotated in `core/include/snobol/`:
  - `search.h` — `snobol_search_exec()`
  - `table.h` — `table_create()`, `table_retain()`, `table_set()`, `table_delete()`
  - `string_fn.h` — all twelve `bool`-returning functions (`snobol_trim`,
    `snobol_dupl`, `snobol_reverse`, `snobol_substr`, `snobol_replace`,
    `snobol_replace_char`, `snobol_lpad`, `snobol_rpad`, `snobol_char_fn`,
    `snobol_ord`, `snobol_upper`, `snobol_lower`)
  - All previously-silently-discarded return values in `core/src/vm.c` and the
    C test suite wrapped with explicit `(void)` casts.

- **`[[maybe_unused]]` on intentionally-unused parameters**: `snobol_jit_compile()`
  parameters `vm` and `start_ip` are `[[maybe_unused]]` on non-ARM64 builds,
  replacing the old `(void)vm; (void)start_ip;` suppression casts.

- **`constexpr` variables replacing typed `#define` constants**:
  - `core/src/table.c` — `FNV_OFFSET_BASIS`, `FNV_PRIME` → `constexpr uint32_t`
  - `core/src/vm.c` — `SNOBOL_LABEL_TABLE_MAGIC` → `constexpr uint32_t`
  - `core/src/jit.c` — `JIT_CACHE_MAX_HARD`, `JIT_CFG_MAX_BLOCKS`,
    `JIT_LOOP_ITER_MAX`, `MAX_OPS_IN_REGION` → `constexpr int`
  - `core/include/snobol/vm.h` — `MAX_CAPS`, `MAX_VARS`, `MAX_LOOPS` →
    `constexpr int`
  - Duplicate-definition guards (`#ifndef … #define … #endif`) added for
    constants shared across translation units (`SNOBOL_LABEL_TABLE_MAGIC`,
    `FNV_OFFSET_BASIS`, `FNV_PRIME`) so both standalone and amalgam builds work.

- **JIT A64 macros → typed `static inline` functions** (`core/src/jit.c`):
  - Four pure-constant `A64_*` macros converted to `constexpr uint32_t`
    (`A64_RET`, `A64_STP_X19_X30_PRE16`, `A64_LDP_X19_X30_POST16`,
    `A64_SUBS_X19_X19_1`).
  - Twenty-one argument-taking `A64_*` macros converted to
    `static inline uint32_t` functions with typed `uint32_t` parameters
    for register fields and immediates, catching argument-type errors at
    compile time. Call-site syntax is unchanged.

- **PHP binding `config.m4`**: `./configure` now probes for C23 support
  (`-std=c23`, falling back to `-std=c2x`) using a correct `AC_LANG_PROGRAM`
  test and passes the detected flag to `PHP_NEW_EXTENSION`. Requires GCC 13+
  or Clang 17+; fails with a descriptive error on older toolchains.

### Verified

- All 220 PHP tests pass (`ddev test`) ✅
- All C tests pass ✅
- Zero `NULL` remaining in `core/src/*.c` ✅
- Zero `__typeof__` or `_Static_assert` in `core/` ✅

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
