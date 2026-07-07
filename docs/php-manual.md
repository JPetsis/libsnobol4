# libsnobol4 PHP Binding — Complete Reference

> **Version:** 0.11.0 · **API:** 0x000B00 · **ABI:** 1 · **Namespaces:** `Snobol\`

---

## Table of Contents

1. [Overview](#1-overview)
2. [Installation & Setup](#2-installation--setup)
3. [Architecture Overview](#3-architecture-overview)
4. [Pattern Construction: The Builder API](#4-pattern-construction-the-builder-api)
5. [Opcode Reference (Complete)](#5-opcode-reference-complete)
   - 5.1 Core Matching Opcodes
   - 5.2 Positional Opcodes
   - 5.3 Character-Class Opcodes
   - 5.4 Capture & Assignment
   - 5.5 Control Flow
   - 5.6 Output & Template
   - 5.7 Repetition
   - 5.8 Extended Primitives
   - 5.9 Table/Array Operations
   - 5.10 Dynamic Patterns
6. [Pattern Compilation](#6-pattern-compilation)
7. [Pattern Matching](#7-pattern-matching)
8. [Search Operations](#8-search-operations)
9. [Template Substitution](#9-template-substitution)
10. [Tables & Arrays](#10-tables--arrays)
11. [Text Functions](#11-text-functions)
12. [Comparison & Type Functions](#12-comparison--type-functions)
13. [Caching](#13-caching)
14. [Tier Dispatch System](#14-tier-dispatch-system)
15. [Common Use Cases](#15-common-use-cases)
16. [Performance Characteristics](#16-performance-characteristics)
17. [Appendix: SNOBOL4 String Syntax](#17-appendix-snobol4-string-syntax)

---

## 1. Overview

libsnobol4 is a high-performance C library implementing **SNOBOL4-style pattern matching**. Unlike PCRE (Perl-Compatible Regular Expressions), SNOBOL4 uses a **pattern-as-values** model: patterns are first-class objects built from composable primitives (concatenation, alternation, repetition), not a string of special characters.

### Key Differences from PCRE/Regex

| Aspect             | PCRE/Regex                     | SNOBOL4 / libsnobol4                                        |
|--------------------|--------------------------------|-------------------------------------------------------------|
| Pattern model      | String with metacharacters     | Composable pattern objects                                  |
| Backtracking       | Implicit, greedy by default    | Explicit via choice points (`OP_SPLIT`)                     |
| Captures           | `$1`, `$2` by group position   | Named capture registers (`cap(0, sub)`, `cap(1, sub)`)      |
| Character classes  | `[a-z]` as syntax              | `SPAN`, `BREAK`, `ANY`, `NOTANY` as operators               |
| String functions   | Not part of regex              | Built-in: SIZE, TRIM, DUPL, REVERSE, SUBSTR, etc.           |
| Case-insensitivity | Flag `/i`                      | Compile-time `'caseInsensitive' => true` option             |
| Search vs match    | One model with `^`/`$` anchors | Explicit `match()` (anchored) vs `searchAll()` (unanchored) |
| Replace            | `preg_replace()`               | `searchReplace()` with template syntax `${v0.upper()}`      |
| Dynamic patterns   | `eval` inside regex (rare)     | First-class `EVAL` opcode for runtime pattern assembly      |

### When to Reach for libsnobol4

- You need **O(n) search as a lower bound** — the multi-tier engine auto-selects linear DFA automaton (Tier 7), SIMD Thompson NFA (Tier 9), or fast-path literal scans (Tier 2–3) for eligible patterns
- You want **zero-allocation literal matching** for pure-string patterns
- You need **template substitution** with `.upper()`, `.lower()`, `.lpad()`, `.length()` transforms
- You need **SNOBOL4-specific semantics** like `ARB`, `BAL` (balanced delimiters), `BREAKX` (tokenization with retry), or `FENCE` (cut)
- You want a **purely programmatic pattern construction API** (the Builder) without string interpolation concerns

---

## 2. Installation & Setup

### Build from Source

```bash
# Build C core + PHP extension
make build-php

# Or from the PHP binding directory
cd bindings/php/
cmake -B build && cmake --build build
```

### DDEV (recommended for development)

```bash
ddev start                        # First-time setup
ddev build-snobol-extension       # Rebuild PHP extension
ddev test                         # Run PHP test suite
```

### Autoload

```php
require 'vendor/autoload.php';
```

The extension registers all classes under `Snobol\` and global functions in the root namespace.

---

## 3. Architecture Overview

```
User Code (PHP)
    │
    ▼
┌─────────────────────────────┐
│   Snobol\Builder            │  ← AST construction (static methods)
│   Snobol\Pattern            │  ← Compiled pattern, matching, search
│   Snobol\PatternHelper      │  ← Convenience: matchOnce, matchAll, split, replace
│   Snobol\Table              │  ← String-keyed associative table
│   Snobol\Array_             │  ← Integer-keyed sparse array
│   Snobol\PatternCache       │  ← LRU cache for compiled patterns
│   Snobol\DynamicPatternCache│  ← LRU cache for dynamic/evaluated patterns
└──────────┬──────────────────┘
           │
           ▼ (C extension: php_snobol.c)
┌─────────────────────────────┐
│   C Core API                │
│   ┌──────────────────────┐  │
│   │ Lexer → Parser → AST │  │  ← Source string compilation
│   └──────────┬───────────┘  │
│              ▼              │
│   ┌──────────────────────┐  │
│   │ Compiler → Bytecode  │  │  ← 42 opcodes
│   └──────────┬───────────┘  │
│              ▼              │
│   ┌──────────────────────┐  │
│   │ Search Meta Derive   │  │  ← Tier classification
│   └──────────┬───────────┘  │
│              ▼              │
│   ┌──────────────────────┐  │
│   │ Tier 0-9 Dispatch    │  │  ← Auto-selected strategy
│   │  0: BREAK_SCAN       │  │
│   │  1: SPAN_SCAN        │  │
│   │  2: LITERAL (memcmp) │  │
│   │  3: PREFIX (memmem)  │  │
│   │  4: BITMAP           │  │
│   │  5: ALT_LIT (trie)   │  │
│   │  6: SEARCH_VM (NFA)  │  │
│   │  7: AUTOMATON (DFA)  │  │
│   │  8: GENERAL VM       │  │
│   │  9: SIMD_NFA (AVX2)  │  │
│   └──────────────────────┘  │
└─────────────────────────────┘
```

### Compilation Pipeline

```
SNOBOL4 source string
    → Lexer (tokenizer)
    → Parser (recursive-descent, builds AST)
    → Compiler (AST → bytecode, 42 opcodes)
    → Fusion pass (SPLIT+ANY/SPAN fusion)
    → Search metadata derivation (tier, candidate bitmaps, skip tables)
    → Pattern object ready for matching
```

Patterns can also be constructed directly via the **Builder API** (PHP arrays → AST → bytecode), bypassing the lexer/parser entirely.

---

## 4. Pattern Construction: The Builder API

`Snobol\Builder` is a **final class with all static methods**. Each method returns an **associative array** representing an AST node. Pass these arrays to `Pattern::compileFromAst()` or `PatternHelper::fromAst()`.

### Literal

```php
Builder::lit(string $s): array
```

Match literal text `$s`.

**PCRE equivalent:** `preg_quote($s)`

```php
$p = Pattern::compileFromAst(Builder::lit("hello"));
$p->match("hello world"); // matches "hello" at position 0
```

### Character Classes

```php
Builder::span(string $set): array      // Match 1+ chars IN set
Builder::brk(string $set): array       // Consume 0+ until char IN set
Builder::any(?string $set = null): array  // Match 1 char IN set (or any if null)
Builder::notany(string $set): array    // Match 1 char NOT IN set
Builder::breakx(string $set): array    // BREAK with retry on backtrack
```

**PCRE equivalents:**

| Snobol | PCRE | PHP |
|--------|------|-----|
| `span("a-z0-9")` | `[a-z0-9]+` | `strspn()` |
| `brk(":")` | `[^:]*` | `strcspn()` |
| `any("aeiou")` | `[aeiou]` | `str_contains($set, $s[$pos])` |
| `notany("aeiou")` | `[^aeiou]` | `!str_contains(...)` |
| `breakx(",")` | `(.*?),` | tokenization loop |
| `any()` | `.` | always true |

**Range syntax:** Strings like `"a-z"`, `"0-9"`, `"a-zA-Z"` are expanded into ranges. Hyphen at start/end is literal. Multi-byte Unicode characters can be included directly.

```php
// Digit span match
$p = Pattern::compileFromAst(Builder::span("0123456789"));
$p->match("12345abc");  // match_len = 5

// Range syntax
$p = Pattern::compileFromAst(Builder::span("a-z0-9"));
$p->match("abc123");    // match_len = 6

// BREAK until delimiter
$p = Pattern::compileFromAst(Builder::brk(":"));
$p->match("hello:world"); // match_len = 5 ("hello")
```

### Position Primitives

```php
Builder::pos(int $n): array     // Succeed only if cursor at codepoint n from start
Builder::rpos(int $n): array    // Succeed only if cursor at codepoint n from end
Builder::tab(int $n): array     // Advance cursor to codepoint n from start
Builder::rtab(int $n): array    // Advance cursor to codepoint n from end
```

**PCRE equivalents:**

| Snobol    | PCRE          | Semantics                    |
|-----------|---------------|------------------------------|
| `pos(5)`  | `(?<=^.{5})`  | Assert cursor at codepoint 5 |
| `rpos(0)` | `$`           | Assert at end of string      |
| `tab(5)`  | `.{5}`        | Skip 5 codepoints            |
| `rtab(3)` | `.*(?=.{3}$)` | Leave 3 codepoints remaining |

```php
// Assert cursor at position 3, then match "hello"
$ast = Builder::concat([Builder::pos(3), Builder::lit("hello")]);
$p = Pattern::compileFromAst($ast);
$p->match("xxhello");  // false (wrong position)
$p->match("   hello"); // true after 3 spaces

// Match remainder
$ast = Builder::concat([Builder::lit("foo"), Builder::rem()]);
$p = Pattern::compileFromAst($ast);
$p->match("foobar");   // match_len = 6
```

### Compound Patterns

```php
Builder::concat(array $parts): array       // Sequence of patterns (AND)
Builder::alt(mixed $left, mixed $right): array  // Alternation (OR)
Builder::arbno(array $sub): array          // Zero-or-more (greedy)
Builder::arb(): array                      // Shorthand for arbno(len(1))
```

**PCRE equivalents:**

| Snobol           | PCRE     | Notes                                                |
|------------------|----------|------------------------------------------------------|
| `concat([a, b])` | `ab`     | Sequence — same concept                              |
| `alt(a, b)`      | `a\|b`   | Alternation — same concept                           |
| `arbno(a)`       | `(?:a)*` | Greedy zero-or-more                                  |
| `arb()`          | `.*?`    | Match 0+ any (lazy), use with `FENCE` to avoid O(n²) |

```php
// Alternation: "foo" or "bar"
$ast = Builder::alt(Builder::lit("foo"), Builder::lit("bar"));
$p = Pattern::compileFromAst($ast);
$p->match("foo");      // true
$p->match("bar");      // true
$p->match("baz");      // false

// Concatenation: "hello world"
$ast = Builder::concat([
    Builder::lit("hello"),
    Builder::span(" "),
    Builder::lit("world")
]);

// ARB (arbitrary skip): skip to "world"
$ast = Builder::concat([Builder::arb(), Builder::lit("world")]);
$p = Pattern::compileFromAst($ast);
$p->match("hello world"); // true, matches at the right position
```

### Repetition

```php
Builder::repeat(array $sub, int $min, ?int $max = -1): array
```

Bounded repetition of pattern `$sub` for `$min` to `$max` times (inclusive). `$max = -1` means unbounded (≥min).

**PCRE equivalent:** `(?:<sub>){<min>,<max>}` or `(?:<sub>){<min>,}` for unbounded. See [§5.7](#57-repetition) for examples.

```php
// Exactly 3 a's
$ast = Builder::repeat(Builder::lit('a'), 3, 3);
$p = Pattern::compileFromAst($ast);
$p->match("aaaaa");   // match_len = 3

// 1 to 3 a's (greedy)
$ast = Builder::repeat(Builder::lit('a'), 1, 3);
$p = Pattern::compileFromAst($ast);
$p->match("aaaaa");   // match_len = 3

// At least 2 a's
$ast = Builder::repeat(Builder::lit('a'), 2);
$p = Pattern::compileFromAst($ast);
$p->match("aaa");     // match_len = 3
$p->match("a");       // false
```

### Capture & Assignment

```php
Builder::cap(int $reg, array $sub): array        // Capture sub-pattern into register
Builder::assign(int $var, int $reg): array       // Assign register to variable
```

Captures are stored in **numbered registers** (0–63). The matched text of a capture register appears in the result as `$result['v<reg>']`. Assigning to a variable makes it available for template substitution.

```php
// Capture digits after "id:"
$ast = Builder::concat([
    Builder::lit("id:"),
    Builder::cap(0, Builder::span("0-9")),
    Builder::assign(0, 0)   // assign reg 0 → var 0
]);
$p = Pattern::compileFromAst($ast);
$res = $p->match("id:12345");
echo $res['v0'];  // "12345"
echo $res['_match_len']; // 8

// Capture with alternation
$ast = Builder::alt(
    Builder::cap(0, Builder::lit("hello")),
    Builder::cap(0, Builder::lit("goodbye"))
);
$p = Pattern::compileFromAst($ast);
$res = $p->match("goodbye");
echo $res['v0'];  // "goodbye" (only matched branch's capture)
```

### Anchors

```php
Builder::anchor(string $atype): array   // "start" or "end"
```

**PCRE equivalents:**

| Snobol            | PCRE |
|-------------------|------|
| `anchor('start')` | `^`  |
| `anchor('end')`   | `$`  |

```php
// Start anchor
$ast = Builder::concat([Builder::anchor('start'), Builder::lit('hello')]);
$p = Pattern::compileFromAst($ast);
$p->match("hello world");   // true
$p->match(" hello world");  // false

// End anchor
$ast = Builder::concat([Builder::lit('world'), Builder::anchor('end')]);
$p = Pattern::compileFromAst($ast);
$p->match("world");   // true
$p->match("world ");  // false
```

### Extended Primitives

```php
Builder::bal(?string $open = "(", ?string $close = ")"): array  // Balanced delimiter
Builder::fence(): array    // Cut — prevent backtracking past this point
Builder::rem(): array      // Match remainder of subject to end
Builder::abort(): array    // Terminate entire match immediately
Builder::fail(): array     // Force failure / trigger backtracking
Builder::succeed(): array  // Force immediate success at current position
```

**PCRE equivalents:**

| Snobol          | PCRE                     | Notes                                                |
|-----------------|--------------------------|------------------------------------------------------|
| `bal('(', ')')` | `\((?:[^()]++\|(?R))*\)` | Recursive balanced parens                            |
| `fence()`       | `(?>...)`                | Atomic group / cut                                   |
| `abort()`       | —                        | Kill entire match, all branches — no PCRE equivalent |
| `fail()`        | `(*FAIL)`                | Trigger backtrack                                    |
| `succeed()`     | empty alternative        | Force success at current position                    |

```php
// BAL: balanced parentheses
$ast = Builder::concat([
    Builder::cap(0, Builder::bal('(', ')')),
    Builder::assign(0, 0)
]);
$p = Pattern::compileFromAst($ast);
$res = $p->match("(hello (world))");
echo $res['v0'];  // "(hello (world))"

// FENCE: prevent backtracking
// Without FENCE: span("a") would backtrack to let lit("b") match
$ast = Builder::concat([Builder::span('a'), Builder::fence(), Builder::lit('b')]);
$p = Pattern::compileFromAst($ast);
$p->match("aaab");   // true (FENCE prevents span from giving up 'a's)

// REM: remainder
$ast = Builder::concat([Builder::lit('foo'), Builder::rem()]);
$p = Pattern::compileFromAst($ast);
$p->match("foobar");   // match_len = 6

// ABORT: kill entire match immediately
$ast = Builder::concat([Builder::lit('hello'), Builder::abort()]);
$p = Pattern::compileFromAst($ast);
$p->match("hello");  // false (ABORT terminates)

// FAIL: force backtrack
$ast = Builder::concat([Builder::lit('hello'), Builder::fail()]);
$p = Pattern::compileFromAst($ast);
$p->match("hello");  // false (FAIL forces backtrack, no alternatives remain)

// SUCCEED: force success
$ast = Builder::succeed();
$p = Pattern::compileFromAst($ast);
$p->match("anything");  // true (matches 0-length at position 0)
```

### Output & Emit

```php
Builder::emit(string $text): array      // Append literal to output
Builder::emitRef(int $reg): array       // Append capture register to output
```

The output buffer appears in the match result as `$result['_output']`.

```php
// Emit transformed text
$ast = Builder::concat([
    Builder::lit('h'),
    Builder::emit('H'),
    Builder::lit('e'),
    Builder::emit('E')
]);
$p = Pattern::compileFromAst($ast);
$res = $p->match("hello");
echo $res['_output'];  // "HE"

// Emit capture
$ast = Builder::concat([
    Builder::cap(1, Builder::lit('hel')),
    Builder::emitRef(1),
    Builder::lit('lo')
]);
$p = Pattern::compileFromAst($ast);
$res = $p->match("hello");
echo $res['_output'];  // "hel"
```

### Control Flow: Labels & Goto

```php
Builder::label(string $name, array $target): array   // Define label
Builder::goto(string $label): array                  // Jump to label
```

These enable looping and structured control flow in patterns.

```php
// Label + Goto for repetition
$ast = Builder::concat([
    Builder::label('loop', Builder::lit('a')),
    Builder::goto('loop'),
    Builder::lit('b')
]);
$p = Pattern::compileFromAst($ast);
// Warning: this creates an infinite loop unless bounded
```

### Dynamic Evaluation

```php
Builder::dynamicEval(array $expr): array   // Evaluate pattern at match time
```

The pattern inside `$expr` is compiled at match time rather than at pattern compile time. Useful when the pattern depends on runtime data.

```php
$pattern = Pattern::compileFromAst([
    'type' => 'dynamic_eval',
    'expr' => ['type' => 'lit', 'text' => 'A']
]);
$pattern->match('A'); // true
```

---

## 5. Opcode Reference (Complete)

All 42 VM opcodes with their PCRE/PHP equivalents and descriptions.

### 5.1 Core Matching

| Opcode            | Equivalent          | Description                                         |
|-------------------|---------------------|-----------------------------------------------------|
| `OP_ACCEPT` (0)   | —                   | Pattern succeeds at end of bytecode                 |
| `OP_FAIL` (1)     | `(*FAIL)`           | Force failure, triggers backtracking                |
| `OP_SUCCEED` (40) | `\|` (empty alt)    | Force success at current position, consumes nothing |
| `OP_LIT` (4)      | `str_starts_with()` | Match literal text at current position              |
| `OP_NOP` (41)     | —                   | No-op (fusion pass)                                 |

### 5.2 Positional

| Opcode           | Equivalent                   | Description                                                                |
|------------------|------------------------------|----------------------------------------------------------------------------|
| `OP_POS` (37)    | `$pos === $n` · `(?<=^.{n})` | Assert cursor at codepoint n from start. `pos(0)` = `^`. Builder: `pos(n)` |
| `OP_TAB` (38)    | `substr($s, $n)`             | Advance cursor to codepoint n from start. Builder: `tab(n)`                |
| `OP_RPOS` (35)   | `(?=.{n}$)`                  | Assert cursor at codepoint n from end. `rpos(0)` = `$`. Builder: `rpos(n)` |
| `OP_RTAB` (36)   | —                            | Advance cursor to leave n codepoints from end. Builder: `rtab(n)`          |
| `OP_REM` (34)    | `substr($s, $pos)` · `.*$`   | Match remainder of subject. Builder: `rem()`                               |
| `OP_ANCHOR` (14) | `^` / `$`                    | Assert subject start or end. Builder: `anchor('start'/'end')`              |

### 5.3 Character-Class

| Opcode           | Equivalent                     | Semantics                                             | Builder       |
|------------------|--------------------------------|-------------------------------------------------------|---------------|
| `OP_ANY` (5)     | `[aeiou]` · `str_contains()`   | Match **one** char in set                             | `any(set)`    |
| `OP_NOTANY` (6)  | `[^aeiou]` · `!str_contains()` | Match **one** char NOT in set                         | `notany(set)` |
| `OP_SPAN` (7)    | `[set]+` · `strspn()`          | **Greedy** — match **1+** consecutive chars in set    | `span(set)`   |
| `OP_BREAK` (8)   | `[^:]*` · `strcspn()`          | Consume **0+** until char in set (stops before it)    | `brk(set)`    |
| `OP_BREAKX` (31) | `(.*?)` with retry             | BREAK with backtrack choice point (O(n) tokenization) | `breakx(set)` |

**Character-class notes:**
- `OP_BREAKX` is like `OP_BREAK` but **creates a backtracking choice point**. On failure it retries with one fewer character consumed — this enables O(n) CSV-style tokenization.
- `OP_SPAN` is always greedy (consumes maximum). Use `OP_BREAK` / `OP_BREAKX` when you need to stop at a delimiter.
- Range syntax: strings like `"a-z"`, `"0-9"`, `"a-zA-Z"` expand into ranges. Hyphen at start/end is literal.

### 5.4 Capture & Assignment

| Opcode             | Equivalent        | Description                                                        |
|--------------------|-------------------|--------------------------------------------------------------------|
| `OP_CAP_START` (9) | `(`               | Begin capture group. Builder: embedded in `cap(reg, sub)`          |
| `OP_CAP_END` (10)  | `)`               | End capture group. Builder: embedded in `cap(reg, sub)`            |
| `OP_ASSIGN` (11)   | `$var = $capture` | Copy register `reg` to variable `var`. Builder: `assign(var, reg)` |

**Cap/Assign relationship:**
- `cap(reg, sub)` wraps `sub` in a capture group for register `reg`. The captured text appears in the match result as `$result['v<reg>']`.
- `assign(var, reg)` copies the captured text from register `reg` to variable `var`. This is primarily used for template substitution (`${v0}`).

Registers 0–63, variables 0–63. `var` and `reg` can be the same or different numbers.

```php
// Capture + assign for template use
$ast = Builder::concat([
    Builder::cap(0, Builder::span("0-9")),
    Builder::assign(0, 0)      // reg 0 → var 0
]);
// Template can now use ${v0}

// Multiple captures
$ast = Builder::concat([
    Builder::cap(0, Builder::span("a-z")),
    Builder::lit(" "),
    Builder::cap(1, Builder::span("a-z")),
]);
$p = Pattern::compileFromAst($ast);
$res = $p->match("foo bar");
echo $res['v0']; // "foo"
echo $res['v1']; // "bar"
```

### 5.5 Control Flow

| Opcode            | Equivalent             | Description                                                                                                   |
|-------------------|------------------------|---------------------------------------------------------------------------------------------------------------|
| `OP_JMP` (2)      | —                      | Unconditional jump to bytecode offset                                                                         |
| `OP_SPLIT` (3)    | `\|` with backtracking | Non-deterministic branch. Push second target as choice, continue at first. Generated by `alt()` and `arbno()` |
| `OP_FENCE` (33)   | `(?>...)` atomic group | Cut choice stack — no backtracking past this point. Builder: `fence()`                                        |
| `OP_ABORT` (39)   | immediate termination  | Terminate entire match immediately. Builder: `abort()`                                                        |
| `OP_FAIL` (1)     | `(*FAIL)`              | Force failure, triggers backtracking. Builder: `fail()`                                                       |
| `OP_SUCCEED` (40) | empty alt              | Force success at current position. Builder: `succeed()`                                                       |
| `OP_LABEL` (22)   | —                      | Define a label target. Builder: `label()`                                                                     |
| `OP_GOTO` (23)    | —                      | Jump to label. Builder: `goto()`                                                                              |
| `OP_GOTO_F` (24)  | —                      | Jump to label if last match failed                                                                            |

**How `alt(left, right)` compiles:**
```
SPLIT right_target, left_target
(left bytecode...)
JMP after_alt
(right_target: right bytecode...)
(after_alt:)
```

**How `arbno(sub)` compiles:**
```
(loop:)
SPLIT after_arbno, sub_target
(sub_target: sub bytecode...)
JMP loop
(after_arbno:)
```

### 5.6 Output & Template

| Opcode                 | Builder                  | Description                       |
|------------------------|--------------------------|-----------------------------------|
| `OP_EMIT_LITERAL` (17) | `emit(text)`             | Append literal to output buffer   |
| `OP_EMIT_CAPTURE` (18) | `emitRef(reg)`           | Append capture register to output |
| `OP_EMIT_FORMAT` (21)  | `${v0.upper()}` template | Append formatted capture          |
| `OP_EMIT_EXPR` (19)    | —                        | Legacy expression emit            |
| `OP_EMIT_TABLE` (20)   | `$TABLE[key]` template   | Table-backed emit                 |

**Format types** (for `OP_EMIT_FORMAT` and `${vN.format()}` templates):

| Expression   | Value | PHP Equivalent                             | Description                        |
|--------------|-------|--------------------------------------------|------------------------------------|
| `.upper()`   | 1     | `strtoupper()`                             | ASCII/Unicode uppercase            |
| `.lower()`   | 2     | `strtolower()`                             | ASCII/Unicode lowercase            |
| `.length()`  | 3     | `strlen()` / `mb_strlen()`                 | Codepoint length as decimal string |
| `.lpad(n,c)` | 4     | `str_pad($s, $width, $pad, STR_PAD_LEFT)`  | Left-pad to width with fill char   |
| `.rpad(n,c)` | 5     | `str_pad($s, $width, $pad, STR_PAD_RIGHT)` | Right-pad to width with fill char  |

```php
// Template with format expressions
$pattern = Pattern::compileFromAst(
    Builder::concat([
        Builder::cap(0, Builder::span("a-zA-Z0-9")),
        Builder::assign(0, 0),
    ])
);

$pattern->subst('hello', '${v0.upper()}');      // "HELLO"
$pattern->subst('HELLO', '${v0.lower()}');      // "hello"
$pattern->subst('hello', '${v0.length()}');      // "5"
$pattern->subst('42', '${v0.lpad(5,"0")}');      // "00042"
$pattern->subst('hi', '${v0.rpad(8,".")}');      // "hi......"
$pattern->subst('world', '${v0}');               // "world" (plain capture)

// Table-backed template
$table = new Table();
$table->set('sky', 'blue');
$pattern->subst('anything', "\$v0[colors['sky']]", ['colors' => $table]);
// → "blue"
```

### 5.7 Repetition

| Opcode                | Equivalent         | Description                                                     |
|-----------------------|--------------------|-----------------------------------------------------------------|
| `OP_REPEAT_INIT` (15) | `(?:sub){min,max}` | Initialize bounded repetition. Builder: `repeat(sub, min, max)` |
| `OP_REPEAT_STEP` (16) | —                  | Step loop iteration. Auto-generated.                            |

**Equivalent patterns:**

| SNOBOL4           | PCRE                          |
|-------------------|-------------------------------|
| `repeat(a, 3, 3)` | `(?:a){3}` (exactly 3)        |
| `repeat(a, 1, 3)` | `(?:a){1,3}` (1 to 3, greedy) |
| `repeat(a, 2)`    | `(?:a){2,}` (at least 2)      |
| `repeat(a, 0)`    | `(?:a)*` (zero or more)       |
| `arbno(a)`        | `(?:a)*` (zero or more)       |

### 5.8 Extended Primitives

| Opcode         | Equivalent               | Description                                                             |
|----------------|--------------------------|-------------------------------------------------------------------------|
| `OP_BAL` (32)  | `\((?:[^()]++\|(?R))*\)` | Match balanced delimiters (recursive). Builder: `bal(open, close)`      |
| `OP_LEN` (12)  | `.{n}`                   | Match exactly n codepoints. `mb_substr`-like. Builder: `len(n)` via AST |
| `OP_EVAL` (13) | `(?{code})`              | Evaluate PHP callable at match time                                     |

### 5.9 Table/Array Operations

| Opcode              | PHP Equivalent          | Description                                                 |
|---------------------|-------------------------|-------------------------------------------------------------|
| `OP_TABLE_GET` (25) | `$table[$key]`          | Lookup table entry. Builder: `tableAccess(name, key)`       |
| `OP_TABLE_SET` (26) | `$table[$key] = $value` | Set table entry. Builder: `tableUpdate(name, key, value)`   |
| `OP_ARRAY_GET` (27) | `$array[$key]`          | Lookup array entry. Builder: `arrayAccess(name, index)`     |
| `OP_ARRAY_SET` (28) | `$array[$key] = $value` | Set array entry. Builder: `arrayUpdate(name, index, value)` |

### 5.10 Dynamic Patterns

| Opcode                | Description                                                       |
|-----------------------|-------------------------------------------------------------------|
| `OP_DYNAMIC` (29)     | Evaluate dynamic pattern from pending definition                  |
| `OP_DYNAMIC_DEF` (30) | Define inline dynamic pattern block. Builder: `dynamicEval(expr)` |

---

## 6. Pattern Compilation

### From Source String (SNOBOL4 syntax)

```php
use Snobol\Pattern;

$p = Pattern::fromString("'hello'");                    // Literal
$p = Pattern::fromString("SPAN('0-9')");                // Digit span
$p = Pattern::fromString("'id:' SPAN('0-9')");          // Concatenation
$p = Pattern::fromString("'foo' | 'bar'");              // Alternation
```

See [Appendix: SNOBOL4 String Syntax](#17-appendix-snobol4-string-syntax) for the full SNOBOL4 pattern language.

### From Builder AST

```php
use Snobol\Builder;
use Snobol\Pattern;

$ast = Builder::concat([
    Builder::lit("id:"),
    Builder::cap(0, Builder::span("0-9")),
    Builder::assign(0, 0),
]);
$p = Pattern::compileFromAst($ast);
```

### With Options

```php
// Case-insensitive matching
$p = Pattern::fromString("'hello'", ['caseInsensitive' => true]);
$p->match("HELLO");   // true

// Builder with options
$p = Pattern::compileFromAst(
    Builder::span("abc"),
    ['caseInsensitive' => true]
);
$p->match("ABC");     // true
```

**Options:**

| Option            | Type   | Default | Description                                              |
|-------------------|--------|---------|----------------------------------------------------------|
| `caseInsensitive` | `bool` | `false` | Enable case-insensitive matching (ASCII + Latin-1 + BMP) |
| `cache`           | `bool` | `true`  | Enable internal pattern caching in PatternHelper         |
| `full`            | `bool` | `false` | Require full-subject match (used in `matchOnce`)         |

### Case-Insensitive Coverage

The `caseInsensitive` flag affects:
- **ASCII:** `a-z` ↔ `A-Z` (full bidirectional folding)
- **Latin-1 Supplement:** U+00C0–U+00FF pairs (e.g., `à` ↔ `À`)
- **Greek:** Full BMP Greek range (e.g., `α` ↔ `Α`)
- **Cyrillic:** Full BMP Cyrillic range (e.g., `а` ↔ `А`)
- **Latin Extended-A:** e.g., `ā` ↔ `Ā`

Caputred text **preserves original subject case** (not folded).

---

## 7. Pattern Matching

### `Pattern::match()` — Anchored Match

```php
$p = Pattern::fromString("'hello'");
$res = $p->match("hello world");
```

**Returns:** `array|false`

**Success result keys:**

| Key            | Type           | Description                                        |
|----------------|----------------|----------------------------------------------------|
| `_match_len`   | `int`          | Byte length of match                               |
| `_match_start` | `int`          | Start position (always 0 for anchored match)       |
| `_output`      | `string`       | Output buffer from `OP_EMIT_*`                     |
| `_metrics`     | `array`        | Choice-point push/peak depth/memory stats          |
| `v0`..`v63`    | `string\|null` | Capture register values (present only if captured) |

```php
$res = $p->match("hello world");
if ($res !== false) {
    echo $res['_match_len'];  // 5
    echo $res['v0'] ?? '';    // capture register 0 value
}
```

### `Pattern::matchLiteral()` — Zero-Allocation Literal Fast Path

For **pure-literal patterns only** (those that don't use span/break/etc.). Bypasses the VM entirely.

```php
$p = Pattern::fromString("'hello'");
$res = $p->matchLiteral("hello world");
// $res = ['success' => true, 'position' => 0, 'length' => 5]

$p2 = Pattern::fromString("SPAN('abc')");
$res2 = $p2->matchLiteral("abc");
// $res2 = ['success' => false] (non-literal pattern)
```

Use `matchLiteral()` when you know the pattern is a simple string — it avoids all VM setup (~50–100 ns faster). Falls through to VM for non-literal patterns.

### `Pattern::subst()` — Match + Replace Template

Run the pattern against subject and produce a string from the template.

```php
$p = Pattern::compileFromAst(
    Builder::concat([
        Builder::cap(0, Builder::span("0-9")),
        Builder::assign(0, 0),
    ])
);
$p->subst('subject', 'template', ?$tables);
```

Parameters:
- `$subject`: The input string to match against
- `$template`: Template with `${v0}`, `${v0.upper()}`, etc.
- `$tables`: Optional array of `Snobol\Table` objects for table-backed templates

Returns `string`.

### `PatternHelper::matchOnce()` — Convenience Match

```php
use Snobol\PatternHelper;

// Three input forms:
$r = PatternHelper::matchOnce("'hello'", "hello world");         // string source
$r = PatternHelper::matchOnce(Builder::lit("hello"), "test");    // Builder AST
$r = PatternHelper::matchOnce($patternObject, "hello world");   // Pattern object

// With options:
$r = PatternHelper::matchOnce("'hello'", "hello", ['full' => true]);
```

Returns `array|false` — same shape as `Pattern::match()`.

### `PatternHelper::matchAll()` — Find All Occurrences

```php
$matches = PatternHelper::matchAll("'a'", "ababab");
// Returns array of match results (without _match_* metadata keys)
```

Returns `array` (empty if no matches). Use for **unanchored scanning** across subject.

### `PatternHelper::formattedSubst()`

```php
$result = PatternHelper::formattedSubst($patternOrAst, $template, $subject, $options);
```

Convenience wrapper for `Pattern::subst()`.

---

## 8. Search Operations

Search operations find matches **anywhere** in the subject (unanchored), not just at position 0.

### `Pattern::searchAll()`

Find all non-overlapping matches.

```php
$p = Pattern::fromString("SPAN('0-9')");
$matches = $p->searchAll("abc 123 def 4567");
// Returns array of match-result arrays
```

### `Pattern::searchSplit()`

Split subject at matches, returning non-matching segments.

```php
$p = Pattern::fromString("SPAN('0-9')");
$segments = $p->searchSplit("abc123def456ghi");
// Returns ["abc", "def", "ghi"]
```

**PHP equivalent:** `preg_split('/[0-9]+/', "abc123def456ghi")`

### `Pattern::searchSplitOffsets()`

Return match positions as `[offset, length]` pairs instead of segment strings. Zero zend_string allocation — useful when you need positions for further processing without creating intermediate string objects.

```php
$p = Pattern::fromString("SPAN('0-9')");
$offsets = $p->searchSplitOffsets("abc123def456ghi");
// Returns [[3, 3], [9, 3]]
//           ^offset ^len  ^offset ^len
```

**PHP equivalent:** `preg_match_all('/[0-9]+/', "abc123def456ghi", $m, PREG_OFFSET_CAPTURE)`

```php
// Empty subject
$p = Pattern::fromString("'cat' | 'dog'");
$offsets = $p->searchSplitOffsets("");
// Returns []

// No matches
$offsets = $p->searchSplitOffsets("hello");
// Returns []

// Zero-length match (empty pattern)
$p = Pattern::fromString("''");
$offsets = $p->searchSplitOffsets("abc");
// Returns [[0, 0], [1, 0], [2, 0], [3, 0]]
```

### `Pattern::searchReplace()`

Replace all matches.

```php
$p = Pattern::fromString("SPAN('0-9')");
$result = $p->searchReplace("abc123def456ghi", "[digits]");
// Returns "abc[digits]def[digits]ghi"
```

**PHP equivalent:** `preg_replace('/[0-9]+/', '[digits]', "abc123def456ghi")`

### `PatternHelper::split()`

```php
$p = Builder::lit(",");
$segments = PatternHelper::split($p, "a,b,c");
// Returns ["a", "b", "c"]
```

**PHP equivalent:** `explode(",", "a,b,c")`

### `PatternHelper::replace()`

```php
$p = Builder::lit("old");
$result = PatternHelper::replace($p, "new", "old text with old words");
// Returns "new text with new words"
```

**PHP equivalent:** `str_replace("old", "new", "old text with old words")`

```php
// Advanced: replace with template
$pattern = Builder::cap(0, Builder::span("0-9"));
$result = PatternHelper::replace($pattern, "[\$v0]", "id:123 code:456");
// Returns "id:[123] code:[456]"
```

---

## 9. Template Substitution

Templates are used in `subst()`, `searchReplace()`, and `PatternHelper::replace()`.

### Template Syntax

| Syntax              | Description                 | Example                   |
|---------------------|-----------------------------|---------------------------|
| `literal text`      | Passed through as-is        | `"hello"` → `"hello"`     |
| `${v0}`             | Capture variable 0          | `"${v0}"` → captured text |
| `${v0.upper()}`     | Uppercase transform         | `"hello"` → `"HELLO"`     |
| `${v0.lower()}`     | Lowercase transform         | `"HELLO"` → `"hello"`     |
| `${v0.length()}`    | Codepoint length as string  | `"hello"` → `"5"`         |
| `${v0.lpad(5,'0')}` | Left-pad to width           | `"42"` → `"00042"`        |
| `${v0.rpad(8,'.')}` | Right-pad to width          | `"hi"` → `"hi......"`     |
| `$TABLE[key]`       | Table lookup by literal key | `$colors['sky']`          |
| `$TABLE[$v0]`       | Table lookup by capture key | `$STATE[$v0]`             |

### Template Transform Reference

| Expression   | C Format          | Semantics                             |
|--------------|-------------------|---------------------------------------|
| `.upper()`   | `SNBL_FMT_UPPER`  | ASCII + BMP uppercase                 |
| `.lower()`   | `SNBL_FMT_LOWER`  | ASCII + BMP lowercase                 |
| `.length()`  | `SNBL_FMT_LENGTH` | Codepoint count as decimal            |
| `.lpad(n,c)` | `SNBL_FMT_LPAD`   | Left-pad to width n with fill char c  |
| `.rpad(n,c)` | `SNBL_FMT_RPAD`   | Right-pad to width n with fill char c |

---

## 10. Tables & Arrays

### `Snobol\Table` — String-Keyed Associative Table

```php
use Snobol\Table;

$t = new Table('STATE');         // Optional name for debugging
$t->set('CA', 'California');
$t->set('NY', 'New York');

$t->get('CA');        // "California"
$t->get('XX');        // null (missing key)
$t->has('NY');        // true

$t->set('CA', null);  // Deletes the key
$t->delete('NY');     // Also deletes

$t->size();           // int
$t->clear();          // Remove all entries
```

### `Snobol\Array_` — Integer-Keyed Sparse Array

```php
use Snobol\Array_;

$a = new Array_(100);  // Optional size hint
$a->set(0, 'zero');
$a->set(42, 'forty-two');
$a->set(99, 'ninety-nine');

$a->get(42);           // "forty-two"
$a->has(0);            // true
$a->delete(42);        // removes entry

$a->size();            // int
$a->keys();            // [0, 99]
$a->values();          // ["zero", "ninety-nine"]
$a->clear();
```

**PHP equivalent:** `Snobol\Array_` is a sparse integer-keyed map where missing keys are distinct from empty values. `Snobol\Table` is a string-keyed hash table. Both use FNV-1a hashing.

### Table-Backed Templates

```php
// Pattern captures a state abbreviation
$pattern = Pattern::compileFromAst(
    Builder::cap(0, Builder::any("ABCDEFGHIJKLMNOPQRSTUVWXYZ"))
);

// Table maps abbreviations to full names
$table = new Table('STATE');
$table->set('CA', 'California');
$table->set('NY', 'New York');

// Template uses capture-derived key
$result = $pattern->subst('CA', "\$v0[STATE['CA']]", ['STATE' => $table]);
// "California"
```

---

## 11. Text Functions

These are global functions wrapping the C core's built-in string operations.

### Size & Manipulation

| Function                                   | PHP Equivalent                             | Description                                    |
|--------------------------------------------|--------------------------------------------|------------------------------------------------|
| `snobol_text_size($s)`                     | `mb_strlen($s, 'UTF-8')`                   | Unicode codepoint count                        |
| `snobol_text_trim($s)`                     | `rtrim($s)`                                | Right-trim whitespace (SNOBOL4 TRIM)           |
| `snobol_text_dupl($s, $n)`                 | `str_repeat($s, $n)`                       | Repeat string N times                          |
| `snobol_text_reverse($s)`                  | `strrev(utf8_encode(...))`                 | Reverse codepoints (Unicode-safe)              |
| `snobol_text_substr($s, $pos, $len)`       | `mb_substr($s, $pos-1, $len, 'UTF-8')`     | Extract substring by codepoint (1-based!)      |
| `snobol_text_replace($s, $from, $to)`      | `str_replace($from, $to, $s)`              | Replace all occurrences                        |
| `snobol_text_replace_char($s, $from, $to)` | `strtr($s, $from, $to)`                    | Character-by-character translation (like `tr`) |
| `snobol_text_lpad($s, $width, $pad)`       | `str_pad($s, $width, $pad, STR_PAD_LEFT)`  | Left-pad to codepoint width                    |
| `snobol_text_rpad($s, $width, $pad)`       | `str_pad($s, $width, $pad, STR_PAD_RIGHT)` | Right-pad to codepoint width                   |
| `snobol_text_char($cp)`                    | `mb_chr($cp, 'UTF-8')`                     | Codepoint → UTF-8 character                    |
| `snobol_text_ord($s)`                      | `mb_ord($s, 'UTF-8')`                      | First codepoint of string                      |
| `snobol_text_upper($s)`                    | `mb_strtoupper($s, 'UTF-8')`               | BMP uppercase                                  |
| `snobol_text_lower($s)`                    | `mb_strtolower($s, 'UTF-8')`               | BMP lowercase                                  |

### SUBSTR Note

`snobol_text_substr` uses **1-based codepoint positions** (SNOBOL4 convention), unlike PHP's 0-based byte offsets.

```php
snobol_text_substr("hello world", 7, 5);  // "world" (position 7 codepoint, 5 codepoints)
snobol_text_substr("café", 3, 2);          // "fé"
snobol_text_substr("hello", 0, 3);         // false! (invalid: positions start at 1)
```

### Unicode Coverage

All text functions operate on UTF-8 strings and are Unicode-safe:
- `SIZE`, `REVERSE`, `SUBSTR` — codepoint-aware
- `UPPER`, `LOWER` — BMP case folding (ASCII, Latin-1, Greek, Cyrillic, Latin Extended-A)
- `CHAR`, `ORD` — full Unicode support (up to U+10FFFF)
- `TRIM`, `DUPL`, `REPLACE`, `REPLACE_CHAR`, `LPAD`, `RPAD` — byte-level (Unicode-safe by passing through)

---

## 12. Comparison & Type Functions

### Identity & Lexical

| Function                     | PHP Equivalent | Description           |
|------------------------------|----------------|-----------------------|
| `snobol_text_ident($a, $b)`  | `$a === $b`    | Exact string identity |
| `snobol_text_differ($a, $b)` | `$a !== $b`    | Strings differ        |
| `snobol_text_lexeq($a, $b)`  | `$a === $b`    | Lexical equality      |
| `snobol_text_lexlt($a, $b)`  | `$a < $b`      | Lexical less-than     |
| `snobol_text_lexgt($a, $b)`  | `$a > $b`      | Lexical greater-than  |

### Numeric Comparison (SNOBOL4 semantics)

SNOBOL4 comparisons **convert strings to numbers** before comparing. Non-numeric strings become `0.0`.

| Function                 | PHP Equivalent            | True Examples                                                       |
|--------------------------|---------------------------|---------------------------------------------------------------------|
| `snobol_text_eq($a, $b)` | `(float)$a === (float)$b` | `eq("5", "5.0")`, `eq("1e2", "100")`, `eq("abc", "xyz")` (both 0.0) |
| `snobol_text_ne($a, $b)` | `(float)$a !== (float)$b` | `ne("5", "6")`                                                      |
| `snobol_text_lt($a, $b)` | `(float)$a < (float)$b`   | `lt("3", "7")`                                                      |
| `snobol_text_gt($a, $b)` | `(float)$a > (float)$b`   | `gt("7", "3")`                                                      |
| `snobol_text_le($a, $b)` | `(float)$a <= (float)$b`  | `le("-5", "3")`                                                     |
| `snobol_text_ge($a, $b)` | `(float)$a >= (float)$b`  | `ge("7", "3")`                                                      |

**Important:** `snobol_text_eq("abc", "xyz")` returns `true` because both convert to `0.0`. This is the SNOBOL4 semantic. Use `IDENT` for string identity.

### Type Predicates

| Function                  | Description                     | True Examples                   |
|---------------------------|---------------------------------|---------------------------------|
| `snobol_text_integer($s)` | String represents an integer    | `"123"`, `"-456"`, `"+789"`     |
| `snobol_text_real($s)`    | String represents a real number | `"12.34"`, `"1.23e-4"`, `"123"` |
| `snobol_text_numeric($s)` | `INTEGER` OR `REAL`             | `"123"`, `"12.34"`              |

---

## 13. Caching

### `Snobol\PatternCache` — LRU Pattern Cache

Explicit LRU cache for compiled patterns. Uses **access-order** eviction.

```php
use Snobol\PatternCache;
use Snobol\PatternHelper;
use Snobol\Builder;

$cache = new PatternCache(128);  // Default capacity

// Get or create
$pattern = $cache->get('my-pattern', fn() =>
    PatternHelper::fromAst(Builder::lit("hello"))
);

$cache->has('my-pattern');   // bool
$cache->size();              // int
$cache->clear();             // Flush all
```

**Use case:** When compiling the same pattern string repeatedly (e.g., in a loop).

### `Snobol\DynamicPatternCache` — Dynamic Pattern Cache

LRU cache for dynamically-evaluated patterns (from `EVAL` opcode). Tracks compile and evaluate counts.

```php
use Snobol\DynamicPatternCache;

$cache = new DynamicPatternCache(128);

// Compile (returns compile status)
$r = $cache->compile("'A' | 'B'");
// $r = ['cached' => false, 'pattern' => "'A' | 'B'", 'compiled' => true]

// Compile again (cached)
$r = $cache->compile("'A' | 'B'");
// $r = ['cached' => true, ...]

// Evaluate (compile + match in one shot)
$r = $cache->evaluate("'A' | 'B'", "B");
// $r = ['cached' => false, 'evaluated' => true, 'matches' => [...]]

// Inspect
$cache->get("'A' | 'B'");    // ['found' => true, 'pattern' => Pattern]
$cache->stats();              // ['size', 'max_size', 'compile_count', 'evaluate_count']
$cache->clear();              // Flush
```

### Internal PatternHelper Cache

`PatternHelper::matchOnce()`, `matchAll()`, `split()`, and `replace()` maintain an internal **128-slot ring buffer cache** keyed by AST hash. Disable with `['cache' => false]`.

```php
// Bypass cache for this call
PatternHelper::matchOnce($ast, $subject, ['cache' => false]);

// Flush internal cache
PatternHelper::clearCache();
```

---

## 14. Tier Dispatch System

libsnobol4's search engine automatically selects the optimal matching strategy based on pattern analysis. This happens **once at compile time** during metadata derivation.

### Tier Summary

| Tier | Name         | Speed | Eligible Patterns                 | Engine                                |
|------|--------------|-------|-----------------------------------|---------------------------------------|
| 0    | `BREAK_SCAN` | ★★★★★ | BREAK/BREAKX at root              | Direct bitmap scan                    |
| 1    | `SPAN_SCAN`  | ★★★★★ | SPAN at root                      | Direct bitmap scan                    |
| 2    | `LITERAL`    | ★★★★★ | Pure literal only                 | memcmp                                |
| 3    | `PREFIX`     | ★★★★★ | Literal prefix pattern            | memmem + BMH skip                     |
| 4    | `BITMAP`     | ★★★★☆ | Single-char alternation           | 128-bit candidate bitmap              |
| 5    | `ALT_LIT`    | ★★★★☆ | Flat literal alternation          | Trie-based matching                   |
| 6    | `SEARCH_VM`  | ★★★☆☆ | Search-VM eligible patterns       | Lightweight VM (~424 bytes)           |
| 7    | `AUTOMATON`  | ★★★★☆ | ASCII charclass + no side effects | DFA via powerset construction         |
| 8    | `GENERAL`    | ★★☆☆☆ | Any pattern                       | Full VM (~2.5 KB) + start-byte bitmap |
| 9    | `SIMD_NFA`   | ★★★★☆ | ASCII charclass + SIMD available  | AVX2/NEON byte-parallel NFA           |

### What Triggers Each Tier

- **Tier 0** — Pattern whose first effective opcode is `OP_BREAK` or `OP_BREAKX` with ASCII-only charclass
- **Tier 1** — Pattern whose first effective opcode is `OP_SPAN` with ASCII-only charclass
- **Tier 2** — Pattern that is only `OP_LIT` instructions (no SPAN/BREAK/ANY/etc.)
- **Tier 3** — Pattern with a literal prefix (starts with one or more `OP_LIT`)
- **Tier 4** — Pattern whose root is `OP_SPLIT` with all branches being single-character match and no captures
- **Tier 5** — Pattern whose root is flat alternation of literal strings
- **Tier 6** — Patterns eligible for search-VM (charclass-only, no captures, no side effects)
- **Tier 7** — Same as Tier 6 + DFA powerset construction succeeds (< 4096 states)
- **Tier 8** — Everything else (fallback through full VM)
- **Tier 9** — Same as Tier 6 + `check_simd_eligible()` returns true + platform has AVX2/NEON

### Performance Impact

```
Pattern Type       → Tier       → Speed (ns/iter, C probe)
─────────────────────────────────────────────────────
Pure literal         Tier 2       ~183 ns
SPAN(',')            Tier 1       ~269 ns
SPAN('a-z')          Tier 1       ~235 ns
BREAK(':')           Tier 0       ~250 ns
Alternation          Tier 4       ~237 ns
Alt-of-literals      Tier 5       ~300 ns
Automaton pattern    Tier 7       ~173 ns
Tokenize (BREAKX)    Tier 0       ~397 ns
General pattern      Tier 8       ~500-1500 ns
```

For the PHP binding, add ~50–300 ns overhead for the extension bridge, depending on call frequency.

---

## 15. Common Use Cases

### 1. Substring Extraction (like `preg_match`)

```php
// SNOBOL4: "id:" SPAN("0-9")
// PCRE:    /id:(\d+)/
$ast = Builder::concat([
    Builder::lit("id:"),
    Builder::cap(0, Builder::span("0-9")),
    Builder::assign(0, 0),
]);
$p = Pattern::compileFromAst($ast);
$res = $p->match("id:12345");
echo $res['v0'];  // "12345"
```

### 2. Split by Delimiter (like `explode` / `preg_split`)

```php
// PCRE: preg_split('/\s+/', $subject)
$p = Pattern::fromString("SPAN(' ")");
$parts = $p->searchSplit("hello   world  foo");
// ["hello", "world", "foo"]

// Comma split: like explode(",")
$p = Pattern::compileFromAst(Builder::lit(","));
$parts = PatternHelper::split($p, "a,b,c");
// ["a", "b", "c"]
```

### 3. Validate Email Format (Simplified)

```php
// PCRE: /^[a-z0-9._%+-]+@[a-z0-9.-]+\.[a-z]{2,}$/i
$local = Builder::span("a-z0-9._%+-");
$domain = Builder::span("a-z0-9.-");
$tld = Builder::repeat(
    Builder::span("a-z"),
    2
);

$ast = Builder::concat([
    Builder::anchor('start'),
    $local,
    Builder::lit("@"),
    $domain,
    Builder::lit("."),
    $tld,
    Builder::anchor('end'),
]);

$p = Pattern::compileFromAst($ast, ['caseInsensitive' => true]);
$res = $p->match("user@example.com");  // true
```

### 4. Balanced Parentheses Matching

```php
// PCRE: /\( (?: [^()]++ | (?R) )* \)/x  (recursive, complex)
// SNOBOL4: BAL('(', ')')
$ast = Builder::concat([
    Builder::cap(0, Builder::bal('(', ')')),
    Builder::assign(0, 0),
]);
$p = Pattern::compileFromAst($ast);
$res = $p->match("prefix (hello (nested)) suffix");
echo $res['v0'];  // "(hello (nested))"
```

### 5. CSV Tokenization (BREAKX)

```php
// PCRE: preg_match_all('/("(?:[^"]|"")*"|[^,]*),?/', ...)
// SNOBOL4: BREAKX(',') for each column
$ast = Builder::concat([
    Builder::cap(0, Builder::breakx(',')),
    Builder::assign(0, 0),
    Builder::lit(','),
    Builder::cap(1, Builder::breakx(',')),
    Builder::assign(1, 1),
]);
$p = Pattern::compileFromAst($ast);
$res = $p->match("hello,world");
echo $res['v0'];  // "hello"
echo $res['v1'];  // "world"
```

### 6. Search and Replace with Transform

```php
// Pattern: capture word characters
$pattern = Pattern::compileFromAst(
    Builder::concat([
        Builder::cap(0, Builder::span("a-zA-Z")),
        Builder::assign(0, 0),
    ])
);

// Replace each word with its uppercase version
$result = PatternHelper::replace(
    $pattern,
    "\${v0.upper()}",
    "hello world from snobol"
);
// "HELLO WORLD FROM SNOBOL"
```

### 7. Fixed-Width Field Parsing

```php
// PCRE: /^(.{5})(.{3})(.{4})/
// SNOBOL4: LEN(5) LEN(3) LEN(4)
$ast = Builder::concat([
    Builder::cap(0, Builder::len(5)),      // Remember: len(5) via AST type!
    Builder::cap(1, Builder::len(3)),
    Builder::cap(2, Builder::len(4)),
]);
```

Note: `LEN(n)` is available through AST construction but not directly through `Builder::len()` currently.

### 8. Date Format Parsing

```php
// PCRE: /^(\d{4})-(\d{2})-(\d{2})$/
$digits = Builder::span("0123456789");

$ast = Builder::concat([
    Builder::anchor('start'),
    Builder::cap(0, Builder::repeat($digits, 4, 4)),
    Builder::lit("-"),
    Builder::cap(1, Builder::repeat($digits, 2, 2)),
    Builder::lit("-"),
    Builder::cap(2, Builder::repeat($digits, 2, 2)),
    Builder::anchor('end'),
]);
$p = Pattern::compileFromAst($ast);
$res = $p->match("2024-01-15");
echo $res['v0'];  // "2024"
echo $res['v1'];  // "01"
echo $res['v2'];  // "15"
```

### 9. String Padding (like `str_pad`)

```php
// LPAD: zero-pad numbers
$result = \snobol_text_lpad("42", 5, "0");  // "00042"

// RPAD: right-pad with dots
$result = \snobol_text_rpad("hello", 10, ".");  // "hello....."

// Via template:
$pattern->subst("42", '${v0.lpad(5,"0")}');  // "00042"
```

### 10. Balanced Bracket Matching for JSON-like Structure

```php
// Deeply nested brackets
$ast = Builder::concat([
    Builder::cap(0, Builder::bal('[', ']')),
    Builder::assign(0, 0),
]);
$p = Pattern::compileFromAst($ast);
$res = $p->match("[outer [inner [deep]] tail]");
echo $res['v0'];  // "[outer [inner [deep]] tail]"
```

### 11. Replace Character Translation (like `strtr`)

```php
// Rot13 with REPLACE_CHAR
$result = \snobol_text_replace_char(
    "hello",
    "abcdefghijklmnopqrstuvwxyz",
    "nopqrstuvwxyzabcdefghijklm"
);  // "uryyb"
```

### 12. Word Tokenization with Position

```php
// Capture words using SPAN + enforce position
$ast = Builder::concat([
    Builder::pos(0),                       // Start at position 0
    Builder::cap(0, Builder::span("a-z")),  // First word
    Builder::span(" "),                     // Skip spaces
    Builder::cap(1, Builder::span("a-z")),  // Second word
]);
$p = Pattern::compileFromAst($ast);
$res = $p->match("hello world");
echo $res['v0'];  // "hello"
echo $res['v1'];  // "world"
```

### 13. Fast Contains Check (Literal Only)

```php
// PCRE: preg_match('/' . preg_quote($needle) . '/', $haystack)
// Fastest snobol path: matchLiteral (zero-allocation)
$p = Pattern::fromString("'needle'");
$res = $p->matchLiteral("haystack with needle inside");
if ($res['success']) {
    echo "Found at position {$res['position']}";
}
```

### 14. Numeric Validation

```php
// Integer check
$ast = Builder::concat([
    Builder::anchor('start'),
    Builder::any("+-"),
    Builder::span("0123456789"),
    Builder::anchor('end'),
]);
$p = Pattern::compileFromAst($ast);
$p->match("+42");   // true
$p->match("12.5");  // false (decimal not in span set)
```

---

## 16. Performance Characteristics

### Optimization Strategies

| Strategy                   | When to Use              | Pattern Requirements         |
|----------------------------|--------------------------|------------------------------|
| `matchLiteral()`           | Pure literal patterns    | No `SPAN`, `BREAK`, etc.     |
| `searchSplit()`            | Tokenization (segments)  | Any pattern (auto-tiered)    |
| `searchSplitOffsets()`     | Tokenization (positions) | Any pattern (auto-tiered)    |
| Builder API                | Compile once, match many | Avoids lexer/parse overhead  |
| `PatternCache`             | Repeated patterns        | Same AST/source recompiled   |
| `match()` vs `matchOnce()` | Direct call vs helper    | `match()` is slightly faster |

### Tier Selection Guidance

1. **Prefilter with `matchLiteral()`** — for simple string matching, this is the fastest path by far. Check `$res['success']` first; fall through to full match if needed.

2. **Use `SPAN` and `BREAK`** — these trigger Tiers 0–1 (direct bitmap scan). They're **10–50x faster** than PCRE for equivalent operations.

3. **Avoid unnecessary alternation** — `alt(lit("foo"), lit("bar"), lit("baz"))` triggers Tier 5 (trie) which is fast, but deep nested alternation triggers Tier 8 (VM fallback).

4. **Consider `FENCE`** for pruning — if backtracking is wasted on alternatives that can't succeed, `FENCE` cuts the choice stack and avoids exponential slowdowns.

5. **Use `BREAKX` for tokenization** — `BREAKX` creates backtracking choice points that are O(n) for tokenization, unlike `ARB` which can be O(n²).

### Benchmark Reference (C core, ns/iter)

| Scenario              | Snobol  | PCRE2    | Ratio |
|-----------------------|---------|----------|-------|
| Literal fail (short)  | ~550 ns | ~650 ns  | 0.85x |
| Literal match (short) | ~358 ns | ~700 ns  | 0.51x |
| SPAN comma            | ~336 ns | ~900 ns  | 0.37x |
| SPAN search           | ~445 ns | ~1010 ns | 0.44x |
| Alternation           | ~339 ns | ~810 ns  | 0.42x |
| Tokenization          | ~397 ns | ~870 ns  | 0.46x |

PHP overhead adds ~50–300 ns per call depending on the tier. The PHP/C coupling ratio is typically < 100x for most operations.

---

## 17. Appendix: SNOBOL4 String Syntax

The `Pattern::fromString()` method accepts SNOBOL4 pattern syntax:

| Syntax           | Builder Equivalent       | Description                    |
|------------------|--------------------------|--------------------------------|
| `'hello'`        | `Builder::lit("hello")`  | Literal string (single quotes) |
| `SPAN('abc')`    | `Builder::span("abc")`   | Span characters                |
| `BREAK('x')`     | `Builder::brk("x")`      | Break at character             |
| `ANY('abc')`     | `Builder::any("abc")`    | Any character in set           |
| `NOTANY('abc')`  | `Builder::notany("abc")` | Not any character in set       |
| `LEN(5)`         | `len(5)` via AST         | Match N characters             |
| `POS(0)`         | `Builder::pos(0)`        | Position assertion             |
| `RPOS(0)`        | `Builder::rpos(0)`       | Reverse position assertion     |
| `TAB(5)`         | `Builder::tab(5)`        | Tab to position                |
| `RTAB(5)`        | `Builder::rtab(5)`       | Reverse tab                    |
| `REM`            | `Builder::rem()`         | Remainder                      |
| `FENCE`          | `Builder::fence()`       | Cut                            |
| `ARB`            | `Builder::arb()`         | Arbitrary (0+ any)             |
| `ARBNO(pattern)` | `Builder::arbno(sub)`    | Zero or more of pattern        |
| `BAL('(', ')')`  | `Builder::bal('(', ')')` | Balanced                       |
| `A \| B`         | `Builder::alt(a, b)`     | Alternation                    |
| `$v0` / `${v0}`  | `${v0}` in templates     | Variable reference             |
| `@r0(pattern)`   | `Builder::cap(0, sub)`   | Capture into register 0        |
| `. v0`           | `Builder::assign(0, 0)`  | Assignment                     |

### Notes on SNOBOL4 Syntax Support

The string parser covers the most common patterns but may not support all SNOBOL4 features. For full access, use the Builder API, which covers all 29 AST node types and 42 opcodes.
