# SNOBOL4 Language Compatibility Features

This document describes the new language features added to the SNOBOL4 engine as part of the
`full-snobol-language-compat` change.

## Overview

The engine now supports full SNOBOL language compatibility including:

- **Associative Tables** - Runtime-owned table objects for key-value storage
- **Labelled Control Flow** - Labels and goto-like transfers for advanced pattern flow
- **Dynamic Pattern Evaluation** - Runtime pattern compilation and caching
- **Formatted Substitutions** - Template-based replacements with formatting options
- **Table-Backed Replacements** - Template variables backed by runtime tables

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
// Dynamic eval syntax: EVAL(pattern_string)
$parser = new Parser("EVAL('A' | 'B')");
$ast = $parser->parse();
// AST: ['type' => 'dynamic_eval', 'expr' => alt('A', 'B')]
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

// Evaluate a dynamic pattern
$result = $cache->evaluate("'A' | 'B'", "A");
// Returns: ['cached' => ..., 'evaluated' => ...]

// Get cached pattern info
$info = $cache->get("'A' | 'B'");
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
// In template strings:
// {VAR} - Simple variable substitution
// {VAR.upper()} - Uppercase formatting
// {VAR.lower()} - Lowercase formatting  
// {VAR.length()} - Length as string
```

## Bytecode Opcodes

The following new opcodes were added to support these features:

| Opcode           | Operands                                     | Description                                 |
|------------------|----------------------------------------------|---------------------------------------------|
| `OP_LABEL`       | label_id (u16)                               | Define a label target                       |
| `OP_GOTO`        | label_id (u16)                               | Unconditional transfer to label             |
| `OP_GOTO_F`      | label_id (u16)                               | Transfer to label if last match failed      |
| `OP_TABLE_GET`   | table_id (u16), key_reg (u8), dest_reg (u8)  | Lookup table[key]                           |
| `OP_TABLE_SET`   | table_id (u16), key_reg (u8), value_reg (u8) | Set table[key] = value                      |
| `OP_DYNAMIC`     | pattern_reg (u8)                             | Evaluate dynamic pattern                    |
| `OP_EMIT_TABLE`  | table_id (u16), key_reg (u8)                 | Emit table[key] value                       |
| `OP_EMIT_FORMAT` | reg (u8), format_type (u8)                   | Format capture (1=upper, 2=lower, 3=length) |

## Compatibility Fixtures

The `tests/compat/` directory contains reference programs that demonstrate the new features:

- **WordCounter** - Uses tables to count word occurrences
- **TextTransformer** - Uses dynamic pattern cache for text transformations
- **TemplateEngine** - Uses table-backed substitutions for template rendering

Run compatibility tests with:

```bash
make test
# or
./vendor/bin/phpunit tests/compat
```

## API Reference

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
| `evaluate`    | `string $pattern`, `string $subject` | `array` | Evaluate pattern           |
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

## JIT Compatibility Notes

The micro-JIT remains fully compatible with the new features:

- **Label opcodes** (`OP_LABEL`, `OP_GOTO`, `OP_GOTO_F`) are interpreted only - JIT skips patterns with control flow
- **Table opcodes** (`OP_TABLE_GET`, `OP_TABLE_SET`, `OP_EMIT_TABLE`) are interpreted - JIT skips patterns with table
  operations
- **Dynamic pattern opcode** (`OP_DYNAMIC`) is interpreted - JIT skips patterns with dynamic evaluation
- **Format opcodes** (`OP_EMIT_FORMAT`) are interpreted - JIT skips patterns with formatting

This is by design - the JIT focuses on optimizing simple, hot patterns. Complex patterns with tables, control flow, or
dynamic evaluation fall back to the interpreter, which is the correct profitability decision.

JIT regression guard results show the profitability gate correctly skips these patterns:

- `skipped_cold_total` increments for patterns with new opcodes
- `compilations_total` stays 0 for patterns that don't benefit from JIT
- No performance regression for existing JIT-optimized patterns
