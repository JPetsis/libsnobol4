# Contributor Guide: Language-Agnostic Core

This guide explains the architecture of the SNOBOL4 language-agnostic core and how to contribute to it.

## Overview

As of the `language-agnostic-core` change, the SNOBOL4 engine has been restructured to separate the **language-agnostic
C core** from **language-specific bindings**. This enables:

- Multiple language bindings (PHP, Python, Rust, etc.)
- Consistent behavior across all bindings
- Easier maintenance (single source of truth)
- Better performance (no host language overhead in core)

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Language Bindings                    │
│  ┌────────────┐    ┌────────────┐    ┌────────────┐     │
│  │    PHP     │    │   Python   │    │    Rust    │     │
│  │  Binding   │    │  Binding   │    │  Binding   │     │
│  └─────┬──────┘    └─────┬──────┘    └─────┬──────┘     │
│        │                 │                 │            │
│        └─────────────────┼─────────────────┘            │
│                          │                              │
│                   C Extension API                       │
└──────────────────────────┼──────────────────────────────┘
                           │
┌──────────────────────────┼──────────────────────────────┐
│              Language-Agnostic Core (C23)               │
│                          │                              │
│  ┌──────────────┐  ┌─────▼──────┐  ┌──────────────┐     │
│  │ Lexer (C)    │─▶│ Parser (C) │─▶│  Compiler    │     │
│  │ UTF-8 aware  │  │ Recursive  │  │  Bytecode    │     │
│  └──────────────┘  │  Descent   │  └──────┬───────┘     │
│                    └────────────┘         │             │
│                          │                │             │
│                    EBNF Grammar      ┌────▼───────┐     │
│                    (snobol.ebnf)     │  Runtime   │     │
│                                      │   Cache    │     │
│                                      └────┬───────┘     │
│                                           │             │
│  ┌──────────────┐  ┌──────────────┐  ┌───▼────────┐     │
│  │      VM      │◀─│   Bytecode   │◀─│   Tables   │     │
│  │  Backtracking│  │   Execution  │  │  (Assoc)   │     │
│  └──────────────┘  └──────────────┘  └────────────┘     │
└─────────────────────────────────────────────────────────┘
```

## Core Components

### 1. Lexer (`snobol_lexer.h/c`)

**Purpose:** Tokenize SNOBOL pattern source text into tokens.

**Key Features:**

- UTF-8 aware (handles multi-byte characters)
- Save/restore for backtracking in parser
- Token types: `TOKEN_LIT`, `TOKEN_IDENT`, `TOKEN_PIPE`, `TOKEN_STAR`, etc.

**Example:**

```c
snobol_lexer_t* lexer = snobol_lexer_create("'A' | 'B'", 9);
token_t tok = snobol_lexer_next(lexer);  // TOKEN_LIT "A"
tok = snobol_lexer_next(lexer);          // TOKEN_PIPE
tok = snobol_lexer_next(lexer);          // TOKEN_LIT "B"
snobol_lexer_destroy(lexer);
```

### 2. Parser (`snobol_parser.h/c`)

**Purpose:** Parse tokens into an Abstract Syntax Tree (AST).

**Key Features:**

- Recursive descent parser
- Follows grammar in `grammar/snobol.ebnf`
- Produces tagged union AST (`ast_node_t`)
- Error reporting with line/column information

**Example:**

```c
snobol_parser_t* parser = snobol_parser_create();
snobol_lexer_t* lexer = snobol_lexer_create("'A' | 'B'", 9);
ast_node_t* ast = snobol_parser_parse(parser, lexer);
// ast->type == AST_ALT
// ast->data.alt.left->type == AST_LITERAL
// ast->data.alt.right->type == AST_LITERAL
snobol_ast_free(ast);
snobol_lexer_destroy(lexer);
snobol_parser_destroy(parser);
```

### 3. AST (`snobol_ast.h/c`)

**Purpose:** Represent parsed pattern structure as a tagged union.

**Version:** 1.0.0 (semantic versioning)

**Version API:**

```c
// Get version at runtime
snobol_ast_version_t ver = snobol_ast_get_version();
printf("AST version: %s\n", ver.string);  // "1.0.0"

// Check compatibility
if (!snobol_ast_version_check(1, 0)) {
    fprintf(stderr, "AST version mismatch!\n");
    return -1;
}

// Or use macro
#if !SNOBOL_AST_VERSION_CHECK(1, 0)
#error "AST version 1.0 or higher required"
#endif
```

**Node Types:**

- `AST_LITERAL` - Literal string match
- `AST_CONCAT` - Concatenation of patterns
- `AST_ALT` - Alternation (pattern1 | pattern2)
- `AST_ARBNO` - Zero or more repetitions (P*)
- `AST_REPETITION` - Bounded repetition (repeat(P, min, max))
- `AST_SPAN`, `AST_BREAK`, `AST_ANY`, `AST_NOTANY` - Character classes
- `AST_CAP` - Capture into register
- `AST_ANCHOR` - Start/end anchor
- `AST_EVAL`, `AST_DYNAMIC_EVAL` - Dynamic evaluation
- `AST_LABEL`, `AST_GOTO` - Control flow
- `AST_TABLE_ACCESS`, `AST_TABLE_UPDATE` - Table operations

**Memory Management:**

- Parent nodes own child nodes
- Call `snobol_ast_free()` to recursively free entire tree
- Use `snobol_ast_create_*()` functions to create nodes
- `snobol_ast_create_label()` makes a copy of the label name (handles string literals safely)

### 4. Compiler (`snobol_compiler.c`)

**Purpose:** Compile AST into VM bytecode.

**Key Function:**

```c
int compile_ast_to_bytecode_c(
    ast_node_t* ast,      // Input AST
    zval *options,        // Compilation options (can be NULL)
    uint8_t **out_bc,     // Output: bytecode buffer
    size_t *out_len       // Output: bytecode length
);
```

**Bytecode Format:**

- Variable-length instructions
- Character class tables appended at end
- See `snobol_vm.h` for opcode definitions

### 5. VM (`snobol_vm.c`)

**Purpose:** Execute bytecode with backtracking support.

**Key Features:**

- Backtracking with choice points
- Catastrophic backtracking protection
- Capture registers
- Optional JIT compilation

### 6. Grammar (`grammar/snobol.ebnf`)

**Purpose:** Formal definition of SNOBOL pattern syntax.

**Usage:**

- Reference for parser implementation
- Reference for test case generation
- Documentation for binding authors

## Adding a New Language Binding

To add a new language binding (e.g., Python, Rust):

### 1. Create Binding Directory

```
snobol4-python/
├── snobol/
│   ├── __init__.py
│   ├── pattern.py
│   └── _core.c  # C extension module
├── setup.py
└── tests/
```

### 2. Implement C Extension

```c
// snobol4-python/snobol/_core.c
#include "snobol_lexer.h"
#include "snobol_parser.h"
#include "snobol_compiler.h"

static PyObject* pattern_from_string(PyObject* self, PyObject* args) {
    const char* source;
    if (!PyArg_ParseTuple(args, "s", &source))
        return NULL;
    
    // Create lexer and parser
    snobol_lexer_t* lexer = snobol_lexer_create(source, strlen(source));
    snobol_parser_t* parser = snobol_parser_create();
    
    // Parse to AST
    ast_node_t* ast = snobol_parser_parse(parser, lexer);
    
    // Compile to bytecode
    uint8_t* bc;
    size_t bc_len;
    compile_ast_to_bytecode_c(ast, NULL, &bc, &bc_len);
    
    // Create Python Pattern object with bytecode
    // ...
    
    snobol_ast_free(ast);
    snobol_lexer_destroy(lexer);
    snobol_parser_destroy(parser);
    compiler_free(bc);
    
    return pattern_obj;
}
```

### 3. Implement Python Wrapper

```python
# snobol4-python/snobol/pattern.py
from . import _core

class Pattern:
    def __init__(self, source: str):
        self._ptr = _core.pattern_from_string(source)
    
    def match(self, subject: str) -> dict | None:
        return _core.pattern_match(self._ptr, subject)
```

### 4. Write Tests

```python
# snobol4-python/tests/test_pattern.py
from snobol import Pattern

def test_literal():
    p = Pattern("'hello'")
    result = p.match("hello world")
    assert result is not None

def test_alternation():
    p = Pattern("'A' | 'B'")
    assert p.match("A") is not None
    assert p.match("B") is not None
    assert p.match("C") is None
```

## Modifying the Grammar

To add new syntax to SNOBOL patterns:

### 1. Update EBNF Grammar

Edit `grammar/snobol.ebnf`:

```ebnf
(* Add new production *)
new_feature = 'NEW' , '(' , pattern , ')' ;

(* Integrate into existing grammar *)
primary = literal
        | charclass
        | new_feature    (* Add here *)
        | ...
        ;
```

### 2. Update Lexer (if new tokens needed)

Edit `snobol_lexer.h`:

```c
typedef enum {
    /* ... existing tokens ... */
    TOKEN_NEW,  /* Add new token */
} token_type_t;
```

Edit `snobol_lexer.c`:

```c
case 'N':
    if (strncmp(lexer->source + lexer->pos, "NEW", 3) == 0) {
        lexer->pos += 3;
        return make_token(TOKEN_NEW);
    }
    break;
```

### 3. Update Parser

Edit `snobol_parser.h`:

```c
typedef enum {
    /* ... existing types ... */
    AST_NEW_FEATURE,  /* Add new AST type */
} ast_type_t;

typedef struct ast_node {
    ast_type_t type;
    union {
        /* ... existing members ... */
        struct {
            ast_node_t* sub;  /* For new feature */
        } new_feature;
    } data;
} ast_node_t;
```

Edit `snobol_parser.c`:

```c
static ast_node_t* parse_new_feature(snobol_parser_t* parser, snobol_lexer_t* lexer) {
    /* Consume 'NEW' */
    advance(lexer);
    
    /* Expect '(' */
    if (!expect(parser, lexer, TOKEN_LPAREN))
        return NULL;
    
    /* Parse sub-pattern */
    ast_node_t* sub = parse_pattern(parser, lexer);
    if (!sub)
        return NULL;
    
    /* Expect ')' */
    if (!expect(parser, lexer, TOKEN_RPAREN)) {
        snobol_ast_free(sub);
        return NULL;
    }
    
    /* Create AST node */
    ast_node_t* node = snobol_ast_create_new_feature(sub);
    return node;
}
```

### 4. Update AST Creation Function

Edit `snobol_ast.c`:

```c
ast_node_t* snobol_ast_create_new_feature(ast_node_t* sub) {
    ast_node_t* node = calloc(1, sizeof(ast_node_t));
    if (!node) return NULL;
    
    node->type = AST_NEW_FEATURE;
    node->data.new_feature.sub = sub;
    return node;
}
```

### 5. Update Compiler

Edit `snobol_compiler.c`:

```c
static int emit_node_c(ast_node_t* node, CodeBuf *c) {
    switch (node->type) {
        /* ... existing cases ... */
        
        case AST_NEW_FEATURE:
            /* Emit bytecode for new feature */
            return emit_new_feature(node->data.new_feature.sub, c);
        
        /* ... */
    }
}
```

### 6. Write Tests

Add tests to `tests/c/test_parser.c`:

```c
static void test_parser_new_feature(void) {
    test_suite("Parser: new feature");
    
    snobol_parser_t* parser = snobol_parser_create();
    snobol_lexer_t* lexer = snobol_lexer_create("NEW('test')", 11);
    
    ast_node_t* ast = snobol_parser_parse(parser, lexer);
    
    test_assert(ast != NULL, "parser returns AST");
    test_assert(!snobol_parser_has_error(parser), "no parse error");
    test_assert(ast->type == AST_NEW_FEATURE, "AST node is NEW_FEATURE");
    
    snobol_ast_free(ast);
    snobol_lexer_destroy(lexer);
    snobol_parser_destroy(parser);
}
```

## Debugging Tips

### Enable Logging

Add to your code:

```c
#define SNOBOL_LOG(fmt, ...) \
    fprintf(stderr, "[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
```

### AST Dump

Use `snobol_ast_dump()` to print AST structure:

```c
snobol_ast_dump(ast, stderr, 0);
```

### Lexer Token Trace

Add to `snobol_lexer_next()`:

```c
fprintf(stderr, "LEXER: pos=%zu type=%d\n", lexer->pos, tok.type);
```

### Parser Trace

Add to parser functions:

```c
fprintf(stderr, "PARSE: %s\n", __func__);
```

## Performance Considerations

### Memory Management

- Use `snobol_malloc()` / `snobol_free()` for VM-allocated memory
- Use standard `malloc()` / `free()` for binding-allocated memory
- Always free AST nodes with `snobol_ast_free()`

### Caching

- Patterns are cached by source text in `DynamicPatternCache`
- Cache key is computed from source hash
- Cache eviction is LRU-based

### JIT Compilation

- Hot patterns (frequently executed) can be JIT-compiled
- JIT is optional and controlled by `Pattern::setJit(true)`
- JIT compilation happens after N executions (configurable)

## Testing

### C Tests

```bash
cd tests/c
make clean
make test
```

### PHP Tests

```bash
cd /path/to/project
make test
# or
ddev exec vendor/bin/phpunit
```

### Adding New Tests

**C Tests:** Add to `tests/c/test_*.c`

```c
static void test_my_feature(void) {
    test_suite("My Feature");
    
    /* Test code */
    test_assert(condition, "description");
}

void test_my_feature_suite(void) {
    test_my_feature();
}
```

Register in `tests/c/test_runner.c`:

```c
void test_my_feature_suite(void);

int main(void) {
    /* ... */
    if (setjmp(test_jump) == 0) {
        test_my_feature_suite();
    }
    /* ... */
}
```

**PHP Tests:** Add to `tests/php/`

```php
class MyFeatureTest extends TestCase
{
    public function testSomething(): void
    {
        $pattern = Pattern::fromString("'test'");
        $result = $pattern->match("test");
        $this->assertNotNull($result);
    }
}
```

## Questions?

- Check `grammar/snobol.ebnf` for grammar reference
- Check `snobol_*.h` files for API documentation
- Check existing tests for usage examples
- Open an issue on GitHub for questions
