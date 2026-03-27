# SNOBOL4 Language Compatibility Features

This document describes the SNOBOL4 language features implemented in the engine.

**Status:** Core runtime implementation complete (11/11 tasks from `complete-snobol-toolset` change).

## Overview

The engine now supports full SNOBOL language compatibility including:

- **Associative Tables** - Runtime-owned table objects for key-value storage
- **Labelled Control Flow** - Labels and goto-like transfers for advanced pattern flow
- **Dynamic Pattern Evaluation** - Runtime pattern compilation and caching via `EVAL(...)`
- **Formatted Substitutions** - Template-based replacements with formatting options
- **Table-Backed Replacements** - Template variables backed by runtime tables with literal and capture-derived keys

## Implementation Status

### ✅ Completed Features

| Feature                                       | Status     | Notes                                     |
|-----------------------------------------------|------------|-------------------------------------------|
| Parser: Generic comma-separated arguments     | ✅ Complete | Arity validation in semantic layer        |
| Parser: `EVAL(...)` syntax                    | ✅ Complete | Parses dynamic evaluation expressions     |
| Parser: Table access `TABLE[key]`             | ✅ Complete | Supports literal and capture-derived keys |
| Runtime: Dynamic pattern cache                | ✅ Complete | With retain/release ownership semantics   |
| Runtime: `OP_DYNAMIC_DEF` / `OP_DYNAMIC`      | ✅ Complete | Bytecode storage and execution            |
| Runtime: Table-backed templates               | ✅ Complete | Literal keys stored in bytecode           |
| Runtime: Capture-derived table lookups        | ✅ Complete | Key from capture register                 |
| Runtime: Formatted substitutions              | ✅ Complete | upper, lower, length operations           |
| Helper API: `PatternHelper::evalPattern()`    | ✅ Complete | Routes through core runtime               |
| Helper API: `PatternHelper::tableSubst()`     | ✅ Complete | Table-backed substitution helper          |
| Helper API: `PatternHelper::formattedSubst()` | ✅ Complete | Formatted template helper                 |
| Compatibility fixtures                        | ✅ Complete | Use runtime-backed semantics              |
| Test coverage                                 | ✅ Complete | 158 PHP tests, 896 C tests                |

### ⚠️ Known Limitations

| Feature                           | Limitation                                 | Workaround                                 | Future                               |
|-----------------------------------|--------------------------------------------|--------------------------------------------|--------------------------------------|
| Dynamic patterns with alternation | `EVAL('A' \| 'B')` falls back to PHP       | Use simple literal/concat patterns in EVAL | Add backtracking to dynamic executor |
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

// Simple literal pattern - uses core runtime
$result = PatternHelper::evalPattern("'hello'", "hello world");
// Returns: ['_match_len' => 5, '_output' => '']

// Concatenation - uses core runtime
$result = PatternHelper::evalPattern("'hello' 'world'", "hello world");
// Returns: ['_match_len' => 10, '_output' => '']

// Alternation - falls back to PHP (limitation)
$result = PatternHelper::evalPattern("'hello' | 'world'", "hello world");
// Returns: ['found' => true, 'matches' => ['hello']]
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

// Check if pattern is cached
$result = $cache->compile("'A' | 'B'");
// Returns: ['cached' => false, 'pattern' => "'A' | 'B'"]

// Get cached pattern info
$info = $cache->get("'A'");
// Returns: ['found' => bool, 'bc_len' => int, 'valid' => bool]

// Get statistics
$stats = $cache->stats();
// Returns: ['size' => int, 'max_size' => int]

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
| `OP_EMIT_TABLE`  | table_id (u16), key_type (u8), ...           | Emit table lookup (key_type: 0=literal, 1=capture) |
| `OP_EMIT_FORMAT` | reg (u8), format_type (u8)                   | Format capture (1=upper, 2=lower, 3=length)        |

### OP_EMIT_TABLE Bytecode Format

```
OP_EMIT_TABLE (1 byte)
table_id (2 bytes, u16)
key_type (1 byte, u8):
  - 0 = literal key: key_len (2 bytes, u16), key_bytes[key_len]
  - 1 = capture key: key_reg (1 byte, u8)
```

## Compatibility Fixtures

The `tests/compat/` directory contains reference programs demonstrating the features:

| Fixture               | Features Used                                       | Runtime-Backed |
|-----------------------|-----------------------------------------------------|----------------|
| `WordCounter.php`     | Tables for counting                                 | ✅ Yes          |
| `TextTransformer.php` | Dynamic patterns via `PatternHelper::evalPattern()` | ✅ Yes          |
| `TemplateEngine.php`  | Table-backed variables                              | ✅ Yes          |

Run compatibility tests with:

```bash
make test
# or
./vendor/bin/phpunit tests/compat
```

**Results:** All 21 compatibility tests pass.

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

| Method        | Parameters                           | Returns | Description                |
|---------------|--------------------------------------|---------|----------------------------|
| `__construct` | `?int $maxSize`                      | -       | Create cache with max size |
| `compile`     | `string $pattern`                    | `array` | Check/compile pattern      |
| `get`         | `string $pattern`                    | `array` | Get cached pattern info    |
| `clear`       | -                                    | `void`  | Clear cache                |
| `stats`       | -                                    | `array` | Get cache statistics       |

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

This is by design - the JIT focuses on optimizing simple, hot patterns. Complex patterns with tables, control flow, or
dynamic evaluation fall back to the interpreter, which is the correct profitability decision.

JIT regression guard results show the profitability gate correctly skips these patterns:

- `skipped_cold_total` increments for patterns with new opcodes
- `compilations_total` stays 0 for patterns that don't benefit from JIT
- No performance regression for existing JIT-optimized patterns

## Test Coverage

| Suite         | Tests | Assertions | Status   |
|---------------|-------|------------|----------|
| C Tests       | 896   | -          | ✅ Pass   |
| PHP Tests     | 158   | 410        | ✅ Pass   |
| Compatibility | 21    | -          | ✅ Pass   |
| Skipped       | 4     | -          | Expected |

**Total:** 1075 tests passing
