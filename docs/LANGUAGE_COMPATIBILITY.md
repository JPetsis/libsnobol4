# SNOBOL4 Language Compatibility Features

This document describes the SNOBOL4 language features implemented in the engine.

## Overview

The engine now supports full SNOBOL language compatibility including:

- **Associative Tables** - Runtime-owned table objects for key-value storage
- **Labelled Control Flow** - Labels and goto-like transfers for advanced pattern flow
- **Dynamic Pattern Evaluation** - Runtime pattern compilation and caching via `EVAL(...)`
- **Formatted Substitutions** - Template-based replacements with formatting options
- **Table-Backed Replacements** - Template variables backed by runtime tables with literal and capture-derived keys

## Implementation Status

### ✅ Completed Features

| Feature                                       | Status     | Notes                                         |
|-----------------------------------------------|------------|-----------------------------------------------|
| Parser: Generic comma-separated arguments     | ✅ Complete | Arity validation in semantic layer            |
| Parser: `EVAL(...)` syntax                    | ✅ Complete | Parses dynamic evaluation expressions         |
| Parser: Table access `TABLE[key]`             | ✅ Complete | Supports literal and capture-derived keys     |
| Runtime: Dynamic pattern cache                | ✅ Complete | With retain/release ownership semantics       |
| Runtime: `OP_DYNAMIC_DEF` / `OP_DYNAMIC`      | ✅ Complete | Source-based cache keying, bytecode execution |
| Runtime: Full pattern semantics in EVAL       | ✅ Complete | Alternation, backtracking, nested EVAL        |
| Runtime: Table-backed templates               | ✅ Complete | Literal keys stored in bytecode               |
| Runtime: Capture-derived table lookups        | ✅ Complete | Key from capture register                     |
| Runtime: Formatted substitutions              | ✅ Complete | upper, lower, length operations               |
| Helper API: `PatternHelper::evalPattern()`    | ✅ Complete | Routes through core runtime with caching      |
| Helper API: `PatternHelper::tableSubst()`     | ✅ Complete | Table-backed substitution helper              |
| Helper API: `PatternHelper::formattedSubst()` | ✅ Complete | Formatted template helper                     |
| Helper API: `DynamicPatternCache`             | ✅ Complete | Truthful runtime-backed cache interface       |
| Compatibility fixtures                        | ✅ Complete | Use runtime-backed semantics (no fallback)    |
| Test coverage                                 | ✅ Complete | 158 PHP tests, 896 C tests                    |

### ⚠️ Known Limitations

| Feature                           | Limitation                                 | Workaround                                 | Future                               |
|-----------------------------------|--------------------------------------------|--------------------------------------------|--------------------------------------|
| Table registration                | Placeholder `table_id=0`                   | Runtime resolves by name                   | Explicit registration API            |
| Recursive EVAL                    | `EVAL(EVAL(...))` parsed but not optimized | Avoid deep nesting                         | Optimize recursive evaluation        |
| JIT coverage                      | Skips tables, dynamic, control flow        | Interpreter handles complex patterns       | JIT for simple labelled patterns     |

## Parser Extensions

### Labelled Patterns

Labels can be attached to pattern fragments for control flow:

```php
use Snobol\Parser;

// Label syntax: LABEL: pattern
$parser = new Parser("LOOP: 'A'*");
$ast = $parser->parse();
// AST: ['type' => 'label', 'name' => 'LOOP', 'target' => ...]
```

### Goto-like Control Flow

Transfer control to labelled positions:

```php
// Goto syntax: :(LABEL)
$parser = new Parser("'A' :(END)");
$ast = $parser->parse();
// AST: ['type' => 'concat', 'parts' => [literal('A'), goto('END')]]
```

### Dynamic Evaluation

Evaluate patterns constructed at runtime:

```php
// Dynamic eval syntax: EVAL(pattern_expr)
use Snobol\PatternHelper;

// Simple literal pattern - uses core runtime with caching
$result = PatternHelper::evalPattern("'hello'", "hello world");
// Returns: ['_match_len' => 5, '_output' => '']

// Concatenation - uses core runtime with caching
$result = PatternHelper::evalPattern("'hello' 'world'", "hello world");
// Returns: ['_match_len' => 10, '_output' => '']

// Alternation - uses core runtime with full backtracking support
$result = PatternHelper::evalPattern("'hello' | 'world'", "hello world");
// Returns: ['v0' => 'hello', '_match_len' => 5, '_output' => '']

// Repeated evaluation - pattern is cached by source
$result1 = PatternHelper::evalPattern("'A' | 'B' | 'C'", "B");  // Cache miss
$result2 = PatternHelper::evalPattern("'A' | 'B' | 'C'", "C");  // Cache hit
```

### Table Access and Update

Access and modify associative tables:

```php
// Table access: TABLE[key]
$parser = new Parser("T['key']");
$ast = $parser->parse();
// AST: ['type' => 'table_access', 'table' => 'T', 'key' => literal('key')]

// Table update: TABLE[key] = value
$parser = new Parser("T['key'] = 'value'");
$ast = $parser->parse();
// AST: ['type' => 'table_update', 'table' => 'T', 'key' => ..., 'value' => ...]
```

## Runtime Features

### Tables (Snobol\Table)

```php
use Snobol\Table;

// Create a table
$table = new Table('mytable');

// Set values
$table->set('name', 'Alice');
$table->set('city', 'Boston');

// Get values
$value = $table->get('name');  // Returns 'Alice'
$exists = $table->has('name'); // Returns true

// Delete values
$table->set('name', null);     // Deletes the entry
$table->delete('city');        // Also deletes

// Clear all entries
$table->clear();

// Get size
$count = $table->size();       // Returns number of entries
```

### Dynamic Pattern Cache (Snobol\DynamicPatternCache)

```php
use Snobol\DynamicPatternCache;

// Create a cache
$cache = new DynamicPatternCache(64);  // Max 64 patterns

// Compile and cache a pattern
$result = $cache->compile("'A' | 'B'");
// Returns: ['cached' => false, 'pattern' => "'A' | 'B'", 'compiled' => true]

// Compile again - will be cached
$result = $cache->compile("'A' | 'B'");
// Returns: ['cached' => true, 'pattern' => "'A' | 'B'", 'compiled' => true]

// Evaluate a pattern through the cache
$result = $cache->evaluate("'A' | 'B'", "A");
// Returns: ['cached' => bool, 'evaluated' => bool, 'matches' => [...]]

// Get cached pattern info
$info = $cache->get("'A'");
// Returns: ['found' => bool, 'pattern' => Pattern|null]

// Get statistics
$stats = $cache->stats();
// Returns: ['size' => int, 'max_size' => int, 'compile_count' => int, 'evaluate_count' => int]

// Clear cache
$cache->clear();
```

### Formatted Template Output

Templates support formatted replacements:

```php
use Snobol\PatternHelper;

// Simple variable: {VAR}
// Uppercase: {VAR.upper()}
// Lowercase: {VAR.lower()}
// Length: {VAR.length()}

$pattern = Builder::cap(0, Builder::span("abcdefghijklmnopqrstuvwxyz"));
$result = PatternHelper::replace(
    $pattern,
    "NAME: \${v0.upper()} (len: \${v0.length()})",
    "name:alice"
);
// Output: "NAME: ALICE (len: 5)"
```

### Table-Backed Template Syntax

```php
// Literal key: $TABLE['key']
$template = "\$STATE['CA']";  // Looks up 'CA' in STATE table

// Capture-derived key: $TABLE[$v0]
$pattern = Builder::cap(0, Builder::any("ABCDEFGHIJKLMNOPQRSTUVWXYZ"));
$template = "\$STATE[\$v0]";  // Uses captured value as key
```

## Bytecode Opcodes

The following opcodes support these features:

| Opcode           | Operands                                     | Description                                        |
|------------------|----------------------------------------------|----------------------------------------------------|
| `OP_LABEL`       | label_id (u16)                               | Define a label target                              |
| `OP_GOTO`        | label_id (u16)                               | Unconditional transfer to label                    |
| `OP_GOTO_F`      | label_id (u16)                               | Transfer to label if last match failed             |
| `OP_TABLE_GET`   | table_id (u16), key_reg (u8), dest_reg (u8)  | Lookup table[key]                                  |
| `OP_TABLE_SET`   | table_id (u16), key_reg (u8), value_reg (u8) | Set table[key] = value                             |
| `OP_DYNAMIC_DEF` | len (u32), bytecode[len]                     | Define dynamic pattern bytecode block              |
| `OP_DYNAMIC`     | (uses pending bytecode from OP_DYNAMIC_DEF)  | Execute stored dynamic pattern                     |
| `OP_EMIT_TABLE`  | table_id (u16), key_type (u8), name_len (u8), name_bytes[name_len], ... | Emit table lookup (key_type: 0=literal, 1=capture); name resolved by `snobol_template_bind_tables()` |
| `OP_EMIT_FORMAT` | reg (u8), format_type (u8) [+ width u16, fill_char u8 for LPAD/RPAD]   | Format capture: `SNBL_FMT_UPPER=1`, `SNBL_FMT_LOWER=2`, `SNBL_FMT_LENGTH=3`, `SNBL_FMT_LPAD=4`, `SNBL_FMT_RPAD=5` |
| `OP_EMIT_EXPR`   | reg (u8), expr_type (u8)                                                | **Deprecated** legacy opcode. VM maps old discriminants (1→UPPER, 2→LENGTH) to `OP_EMIT_FORMAT`. New compilation always emits `OP_EMIT_FORMAT`. |

### OP_EMIT_TABLE Bytecode Format

```
OP_EMIT_TABLE (1 byte)
table_id (2 bytes, u16) — 0xFFFF (SNBL_TABLE_ID_UNBOUND) until snobol_template_bind_tables() is called
key_type (1 byte, u8):
name_len (1 byte, u8) — byte length of the embedded table name
name_bytes[name_len] — UTF-8 table name (resolved by snobol_template_bind_tables, skipped at runtime)
  then key payload:
  - key_type=0 (literal key): key_len (2 bytes, u16), key_bytes[key_len]
  - key_type=1 (capture key): key_reg (1 byte, u8)
```

Call `snobol_template_bind_tables(bc, bc_len, names, ids, n)` after compilation
to patch all `0xFFFF` table_id entries to runtime-assigned IDs.

### Template Expression Syntax

Inside `${vN.expr()}` braces the following expression forms are supported:

| Expression         | Emitted opcode                                   | Effect                              |
|--------------------|--------------------------------------------------|-------------------------------------|
| `${vN}`            | `OP_EMIT_CAPTURE reg`                            | Emit raw capture                    |
| `${vN.upper()}`    | `OP_EMIT_FORMAT reg SNBL_FMT_UPPER`              | ASCII uppercase                     |
| `${vN.lower()}`    | `OP_EMIT_FORMAT reg SNBL_FMT_LOWER`              | ASCII lowercase                     |
| `${vN.length()}`   | `OP_EMIT_FORMAT reg SNBL_FMT_LENGTH`             | Decimal codepoint count             |
| `${vN.lpad(W)}`    | `OP_EMIT_FORMAT reg SNBL_FMT_LPAD width 0x20`   | Left-pad to width W with spaces     |
| `${vN.lpad(W,'c')}` | `OP_EMIT_FORMAT reg SNBL_FMT_LPAD width fill`  | Left-pad to width W with char `c`   |
| `${vN.rpad(W)}`    | `OP_EMIT_FORMAT reg SNBL_FMT_RPAD width 0x20`   | Right-pad to width W with spaces    |
| `${vN.rpad(W,'c')}` | `OP_EMIT_FORMAT reg SNBL_FMT_RPAD width fill`  | Right-pad to width W with char `c`  |

Width is a 1–1024 decimal integer; it is capped at 1024 in the VM dispatch.

### C API: snobol_template_bind_tables

```c
int snobol_template_bind_tables(uint8_t *bc, size_t bc_len,
                                 const char **names, const uint16_t *ids,
                                 size_t n);
```

Walks `bc` for `OP_EMIT_TABLE` entries with `table_id == SNBL_TABLE_ID_UNBOUND`.
For each such entry the embedded name is matched against `names[0..n-1]`; on a
match, `table_id` is patched in-place to `ids[k]`.  Returns 0 if all names were
resolved, -1 if any entry could not be resolved.  Unresolvable entries retain
`SNBL_TABLE_ID_UNBOUND`.

## Compatibility Fixtures

The `tests/compat/` directory contains reference programs demonstrating the features:

| Fixture                       | Features Used                                                            | Runtime-Backed |
|-------------------------------|--------------------------------------------------------------------------|----------------|
| `WordCounter.php`             | Tables for counting                                                      | ✅ Yes          |
| `TextTransformer.php`         | Dynamic patterns via `PatternHelper::evalPattern()`                      | ✅ Yes          |
| `TemplateEngine.php`          | Table-backed variables                                                   | ✅ Yes          |
| `WordCounterWithGoto.php`     | `Builder::label` / `Builder::goto` for labelled control flow             | ✅ Yes          |
| `TextTransformerWithGoto.php` | Labelled classification patterns + forward goto via `Builder` API        | ✅ Yes          |
| `TemplateEngineWithGoto.php`  | Label-wrapped variable detection + forward `goto` for structure checking | ✅ Yes          |

Run compatibility tests with:

```bash
make test
# or
./vendor/bin/phpunit tests/compat
```

**Results:** All 34 compatibility tests pass (21 existing + 13 new labelled control flow tests).

### Labelled Control Flow in Compatibility Fixtures

The three `*WithGoto` fixtures (v0.4.0) demonstrate:

1. **`Builder::label(name, pattern)`** — wraps a sub-pattern with a named label.
   The compiler emits `OP_LABEL label_id` followed by the body bytecode.
   The label's registered offset points to the first instruction of the body.

2. **`Builder::goto(label)`** — emits `OP_GOTO label_id` unconditionally.
   Control transfers to the target without popping the backtracking choice stack,
   distinguishing it from backtracking.

3. **Forward goto** — a goto that references a label defined later in the same
   concat sequence. The VM pre-registers all labels from the bytecode tail table
   before execution begins, so forward references always resolve.

4. **Compile-time validation** — duplicate label names and goto references to
   undefined labels are rejected at compile time (not at runtime).

#### Example: Forward Goto Pattern

```php
use Snobol\Builder;
use Snobol\PatternHelper;

// Pattern: lit(">>") :(content) content: SPAN('A-Za-z')
// Bytecode: LIT(">>"), GOTO(content), LABEL(content), SPAN, ACCEPT
// On ">>hello": match ">>", GOTO jumps to SPAN body → matches "hello"
$ast = Builder::concat([
    Builder::lit('>>'),
    Builder::goto('content'),
    Builder::label('content', Builder::span('A-Za-z')),
]);
$result = PatternHelper::matchOnce($ast, '>>hello'); // array with _match_len
```

## API Reference

### PatternHelper Methods

| Method             | Parameters                                                                  | Returns        | Description                                   |
|--------------------|-----------------------------------------------------------------------------|----------------|-----------------------------------------------|
| `evalPattern()`    | `string $patternExpr`, `string $subject`, `array $options`                  | `array\|false` | Evaluate dynamic pattern through core runtime |
| `tableSubst()`     | `Table $table`, `string $keyPattern`, `string $template`, `string $subject` | `string`       | Table-backed substitution                     |
| `formattedSubst()` | `Pattern $pattern`, `string $template`, `string $subject`, `array $options` | `string`       | Formatted template substitution               |

### Snobol\Table

| Method        | Parameters                      | Returns   | Description                        |
|---------------|---------------------------------|-----------|------------------------------------|
| `__construct` | `?string $name`                 | -         | Create table with optional name    |
| `get`         | `string $key`                   | `?string` | Get value by key (null if missing) |
| `set`         | `string $key`, `?string $value` | `bool`    | Set value (null deletes)           |
| `has`         | `string $key`                   | `bool`    | Check if key exists                |
| `delete`      | `string $key`                   | `bool`    | Delete key                         |
| `clear`       | -                               | `void`    | Clear all entries                  |
| `size`        | -                               | `int`     | Get entry count                    |

### Snobol\DynamicPatternCache

| Method        | Parameters                           | Returns | Description                    |
|---------------|--------------------------------------|---------|--------------------------------|
| `__construct` | `int $maxSize`                       | -       | Create cache with max size     |
| `compile`     | `string $pattern`                    | `array` | Compile and cache pattern      |
| `evaluate`    | `string $pattern`, `string $subject` | `array` | Evaluate pattern through cache |
| `get`         | `string $pattern`                    | `array` | Get cached pattern             |
| `clear`       | -                                    | `void`  | Clear cache                    |
| `stats`       | -                                    | `array` | Get cache statistics           |

## Memory Management

All runtime objects (tables, dynamic patterns) use reference counting:

- PHP objects hold references to C core objects
- Objects are freed when reference count reaches zero
- No manual memory management required in PHP code
- Graceful degradation for missing values (returns null/empty)

## Error Handling

- Invalid table operations throw exceptions
- Missing table keys return null (not exceptions)
- Missing template variables render as empty strings
- Invalid labels cause controlled failure via backtracking
- Dynamic pattern compilation failures fail gracefully

## JIT Compatibility Notes

The micro-JIT remains fully compatible with the new features:

- **Label opcodes** (`OP_LABEL`, `OP_GOTO`, `OP_GOTO_F`) are interpreted only - JIT skips patterns with control flow
- **Table opcodes** (`OP_TABLE_GET`, `OP_TABLE_SET`, `OP_EMIT_TABLE`) are interpreted - JIT skips patterns with table
  operations
- **Dynamic pattern opcodes** (`OP_DYNAMIC_DEF`, `OP_DYNAMIC`) are interpreted - JIT skips patterns with dynamic
  evaluation
- **Format opcodes** (`OP_EMIT_FORMAT`) are interpreted - JIT skips patterns with formatting

**Phase 1c additions (CFG-based multi-block JIT):**

- **Multi-arm alternation** (`'a' | 'b' | 'c'`) — now compiled with the CFG builder; each SPLIT arm is a separate
  compiled stub, eliminating interpreter round-trips between arms.
- **ARBNO loops** — backward JMP edges are now compiled with a counted iteration guard (`JIT_LOOP_ITER_MAX` = 1024);
  the loop body runs fully compiled for up to 1024 iterations before bailing to interpreter.
- **`jit_blocks_compiled_total`** — new `SnobolJitStats` field counts compiled CFG blocks for observability.

This is by design - the JIT focuses on optimizing hot patterns. Complex patterns with tables, control flow, or
dynamic evaluation fall back to the interpreter, which is the correct profitability decision.

JIT regression guard results show the profitability gate correctly skips these patterns:

- `skipped_cold_total` increments for patterns with new opcodes
- `compilations_total` stays 0 for patterns that don't benefit from JIT
- No performance regression for existing JIT-optimized patterns

## Test Coverage

| Suite         | Tests | Assertions | Status   |
|---------------|-------|------------|----------|
| C Tests       | 1,265 | -          | ✅ Pass   |
| PHP Tests     | 183   | 410        | ✅ Pass   |
| Compatibility | 21    | -          | ✅ Pass   |
| Skipped       | 4     | -          | Expected |

**Total:** 1,469 tests passing
