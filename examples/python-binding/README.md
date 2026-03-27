# SNOBOL4 Python Binding Example

This is a **proof-of-concept** showing how to create a Python binding for the SNOBOL4 C core.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Python Layer                         │
│  ┌──────────────────────────────────────────────────┐   │
│  │  pattern.py                                      │   │
│  │  class Pattern:                                  │   │
│  │    def __init__(self, source): ...               │   │
│  │    def match(self, subject): ...                 │   │
│  └──────────────────────────────────────────────────┘   │
│                          │                              │
│                          │ CFFI / ctypes                │
│                          ▼                              │
│  ┌──────────────────────────────────────────────────┐   │
│  │  _core.c (C extension module)                    │   │
│  │  - pattern_from_string()                         │   │
│  │  - pattern_match()                               │   │
│  └──────────────────────────────────────────────────┘   │
└──────────────────────────┼──────────────────────────────┘
                           │
┌──────────────────────────┼──────────────────────────────┐
│              SNOBOL4 C Core (snobol4-core/)             │
│                          │                              │
│  ┌──────────────┐  ┌─────▼──────┐  ┌──────────────┐     │
│  │ Lexer (C)    │─▶│ Parser (C) │─▶│  Compiler    │     │
│  └──────────────┘  └────────────┘  └──────┬───────┘     │
│                                           │             │
│                                      ┌────▼───────┐     │
│                                      │     VM     │     │
│                                      └────────────┘     │
└─────────────────────────────────────────────────────────┘
```

## Files

### 1. C Extension Module (`_core.c`)

```c
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "snobol_lexer.h"
#include "snobol_parser.h"
#include "snobol_compiler.h"
#include "snobol_vm.h"

typedef struct {
    PyObject_HEAD
    uint8_t* bytecode;
    size_t bytecode_len;
} PatternObject;

static void Pattern_dealloc(PatternObject* self) {
    if (self->bytecode) {
        snobol_free(self->bytecode);
    }
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* Pattern_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
    PatternObject* self;
    self = (PatternObject*)type->tp_alloc(type, 0);
    if (self) {
        self->bytecode = NULL;
        self->bytecode_len = 0;
    }
    return (PyObject*)self;
}

static int Pattern_init(PatternObject* self, PyObject* args, PyObject* kwds) {
    static char* kwlist[] = {"source", NULL};
    char* source;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, &source))
        return -1;
    
    /* Create lexer and parser */
    snobol_lexer_t* lexer = snobol_lexer_create(source, strlen(source));
    snobol_parser_t* parser = snobol_parser_create();
    
    /* Parse to AST */
    ast_node_t* ast = snobol_parser_parse(parser, lexer);
    
    if (!ast || snobol_parser_has_error(parser)) {
        const char* error = snobol_parser_get_error(parser);
        PyErr_Format(PyExc_ValueError, "Parse error: %s", error ? error : "unknown");
        snobol_lexer_destroy(lexer);
        snobol_parser_destroy(parser);
        if (ast) snobol_ast_free(ast);
        return -1;
    }
    
    /* Compile to bytecode */
    if (compile_ast_to_bytecode_c(ast, NULL, &self->bytecode, &self->bytecode_len) != 0) {
        PyErr_SetString(PyExc_RuntimeError, "Compilation failed");
        snobol_ast_free(ast);
        snobol_lexer_destroy(lexer);
        snobol_parser_destroy(parser);
        return -1;
    }
    
    snobol_ast_free(ast);
    snobol_lexer_destroy(lexer);
    snobol_parser_destroy(parser);
    
    return 0;
}

static PyObject* Pattern_match(PatternObject* self, PyObject* args) {
    char* subject;
    
    if (!PyArg_ParseTuple(args, "s", &subject))
        return NULL;
    
    /* Create VM and execute */
    VM vm;
    vm_init(&vm, self->bytecode, self->bytecode_len, subject, strlen(subject));
    
    int result = vm_exec(&vm);
    
    if (result) {
        /* Build result dict with captures */
        PyObject* dict = PyDict_New();
        
        /* Add match length */
        PyDict_SetItemString(dict, "_match_len", 
            PyLong_FromSize_t(vm.pos));
        
        /* Add captures */
        for (int i = 0; i < MAX_CAPS; i++) {
            if (vm.cap_end[i] > vm.cap_start[i]) {
                char key[16];
                snprintf(key, sizeof(key), "v%d", i);
                size_t len = vm.cap_end[i] - vm.cap_start[i];
                PyObject* value = Py_BuildValue("s#", 
                    subject + vm.cap_start[i], len);
                PyDict_SetItemString(dict, key, value);
                Py_DECREF(value);
            }
        }
        
        return dict;
    } else {
        Py_RETURN_NONE;
    }
}

static PyMethodDef Pattern_methods[] = {
    {"match", (PyCFunction)Pattern_match, METH_VARARGS,
     "Match pattern against subject string"},
    {NULL}
};

static PyTypeObject PatternType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "snobol.Pattern",
    .tp_doc = "SNOBOL Pattern object",
    .tp_basicsize = sizeof(PatternObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = Pattern_new,
    .tp_init = (initproc)Pattern_init,
    .tp_dealloc = (destructor)Pattern_dealloc,
    .tp_methods = Pattern_methods,
};

static PyMethodDef module_methods[] = {
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef snobol_module = {
    PyModuleDef_HEAD_INIT,
    "snobol._core",
    "SNOBOL4 pattern matching core",
    -1,
    module_methods
};

PyMODINIT_FUNC PyInit__core(void) {
    PyObject* m;
    
    if (PyType_Ready(&PatternType) < 0)
        return NULL;
    
    m = PyModule_Create(&snobol_module);
    if (!m)
        return NULL;
    
    Py_INCREF(&PatternType);
    if (PyModule_AddObject(m, "Pattern", (PyObject*)&PatternType) < 0) {
        Py_DECREF(&PatternType);
        Py_DECREF(m);
        return NULL;
    }
    
    return m;
}
```

### 2. Python Wrapper (`pattern.py`)

```python
"""
SNOBOL4 Pattern Matching for Python

Example usage:
    from snobol import Pattern
    
    # Create pattern from string
    p = Pattern("'hello' | 'world'")
    
    # Match against string
    result = p.match("hello world")
    if result:
        print(f"Matched! Length: {result['_match_len']}")
        print(f"Captures: {result}")
    else:
        print("No match")
"""

from ._core import Pattern as _Pattern

class Pattern:
    """SNOBOL pattern object."""
    
    def __init__(self, source: str):
        """
        Create a pattern from SNOBOL syntax.
        
        Args:
            source: SNOBOL pattern string (e.g., "'A' | 'B'", "SPAN('0-9')*")
        
        Raises:
            ValueError: If pattern syntax is invalid
        """
        self._pattern = _Pattern(source)
    
    def match(self, subject: str) -> dict | None:
        """
        Match pattern against subject string.
        
        Args:
            subject: String to match against
        
        Returns:
            dict with captures on success, None on failure
            - '_match_len': length of matched portion
            - 'v0', 'v1', etc.: captured groups
        """
        return self._pattern.match(subject)
    
    def search(self, subject: str) -> dict | None:
        """
        Search for pattern anywhere in subject.
        
        Args:
            subject: String to search in
        
        Returns:
            dict with captures on success, None on failure
        """
        # Try matching at each position
        for i in range(len(subject)):
            result = self.match(subject[i:])
            if result:
                # Adjust positions
                result['_start'] = i
                result['_end'] = i + result['_match_len']
                return result
        return None
    
    def findall(self, subject: str) -> list[dict]:
        """
        Find all non-overlapping matches.
        
        Args:
            subject: String to search in
        
        Returns:
            List of match dicts
        """
        results = []
        pos = 0
        while pos < len(subject):
            result = self.match(subject[pos:])
            if not result:
                break
            result['_start'] = pos
            result['_end'] = pos + result['_match_len']
            results.append(result)
            pos += max(1, result['_match_len'])
        return results
    
    def sub(self, replacement: str, subject: str, count: int = 0) -> str:
        """
        Replace pattern matches with replacement string.
        
        Args:
            replacement: Replacement string
            subject: String to perform replacements in
            count: Maximum number of replacements (0 = all)
        
        Returns:
            String with replacements applied
        """
        result = []
        pos = 0
        replacements = 0
        
        while pos < len(subject):
            match_result = self.match(subject[pos:])
            if not match_result:
                result.append(subject[pos:])
                break
            
            result.append(subject[pos:pos + match_result['_match_len']])
            pos += match_result['_match_len']
            replacements += 1
            
            if count > 0 and replacements >= count:
                result.append(subject[pos:])
                break
        
        return ''.join(result)


# Convenience functions

def compile(source: str) -> Pattern:
    """Compile a pattern from SNOBOL syntax."""
    return Pattern(source)


def match(pattern: str, subject: str) -> dict | None:
    """Match pattern against subject string."""
    return Pattern(pattern).match(subject)


def search(pattern: str, subject: str) -> dict | None:
    """Search for pattern anywhere in subject."""
    return Pattern(pattern).search(subject)


def findall(pattern: str, subject: str) -> list[dict]:
    """Find all non-overlapping matches."""
    return Pattern(pattern).findall(subject)
```

### 3. Setup Script (`setup.py`)

```python
from setuptools import setup, Extension
import os

# Get the absolute path to the C core
CORE_DIR = os.path.join(os.path.dirname(__file__), '..', '..', 'snobol4-core')

core_sources = [
    'snobol_ast.c',
    'snobol_lexer.c',
    'snobol_parser.c',
    'snobol_compiler.c',
    'snobol_vm.c',
    'snobol_table.c',
    'snobol_dynamic_pattern.c',
]

module = Extension(
    'snobol._core',
    sources=['_core.c'] + [os.path.join(CORE_DIR, f) for f in core_sources],
    include_dirs=[CORE_DIR],
    define_macros=[
        ('STANDALONE_BUILD', '1'),
        ('SNOBOL_PROFILE', '1'),
    ],
    extra_compile_args=['-std=c11', '-Wall', '-Wextra', '-g', '-O2'],
)

setup(
    name='snobol4',
    version='0.1.0',
    description='SNOBOL4 pattern matching for Python',
    packages=['snobol'],
    ext_modules=[module],
    python_requires='>=3.8',
)
```

### 4. Example Usage (`example.py`)

```python
#!/usr/bin/env python3
"""Example usage of SNOBOL4 Python binding."""

from snobol import Pattern, match, search, findall

print("="*60)
print("SNOBOL4 Python Binding Examples")
print("="*60)

# Example 1: Simple literal match
print("\n1. Simple literal match:")
p = Pattern("'hello'")
result = p.match("hello world")
print(f"   Pattern: 'hello'")
print(f"   Subject: 'hello world'")
print(f"   Result: {result}")

# Example 2: Alternation
print("\n2. Alternation:")
p = Pattern("'cat' | 'dog' | 'bird'")
for animal in ["cat", "dog", "fish"]:
    result = p.match(animal)
    print(f"   '{animal}': {'matched' if result else 'no match'}")

# Example 3: Character class
print("\n3. Character class (SPAN):")
p = Pattern("SPAN('0-9')")
result = p.match("123abc")
print(f"   Pattern: SPAN('0-9')")
print(f"   Subject: '123abc'")
print(f"   Matched: {result['_match_len']} characters")

# Example 4: Capture
print("\n4. Capture:")
p = Pattern("@1 'hello' @2 ' ' @3 'world'")
result = p.match("hello world")
print(f"   Pattern: @1 'hello' @2 ' ' @3 'world'")
print(f"   Subject: 'hello world'")
print(f"   Captures: v1={result.get('v0')}, v2={result.get('v1')}, v3={result.get('v2')}")

# Example 5: Repetition
print("\n5. Repetition (arbno):")
p = Pattern("'x'*")
for s in ["", "x", "xx", "xxx"]:
    result = p.match(s)
    print(f"   '{s}': matched {result['_match_len'] if result else 0} chars")

# Example 6: Search
print("\n6. Search (find pattern anywhere):")
p = Pattern("'error'")
subject = "No errors found in this error log"
result = p.search(subject)
print(f"   Pattern: 'error'")
print(f"   Subject: '{subject}'")
if result:
    print(f"   Found at position {result['_start']}")

# Example 7: Find all
print("\n7. Find all matches:")
p = Pattern("'a'")
subject = "banana"
results = p.findall(subject)
print(f"   Pattern: 'a'")
print(f"   Subject: '{subject}'")
print(f"   Found {len(results)} matches at positions: {[r['_start'] for r in results]}")

# Example 8: Complex pattern
print("\n8. Complex pattern (date matching):")
# Match ISO date: YYYY-MM-DD
p = Pattern("SPAN('0-9') SPAN('0-9') SPAN('0-9') SPAN('0-9') '-' " +
            "SPAN('0-9') SPAN('0-9') '-' SPAN('0-9') SPAN('0-9')")
result = p.match("2024-03-27")
print(f"   Pattern: YYYY-MM-DD")
print(f"   Subject: '2024-03-27'")
print(f"   Result: {'matched' if result else 'no match'}")

print("\n" + "="*60)
```

## Building

```bash
# From the examples/python-binding directory
python3 setup.py build_ext --inplace

# Test the module
python3 -c "from snobol import Pattern; p = Pattern(\"'hello'\"); print(p.match('hello'))"

# Run examples
python3 example.py
```

## Notes

1. **Memory Management**: The C core uses `snobol_malloc()` / `snobol_free()` for VM memory. The Python binding must
   respect this.

2. **Thread Safety**: The C core is not thread-safe by default. For thread safety, add mutex protection around VM
   execution.

3. **Error Handling**: Parse errors are raised as Python `ValueError` exceptions.

4. **Performance**: For repeated matching of the same pattern, create the `Pattern` object once and reuse it.

5. **Unicode**: The C core handles UTF-8 natively. Python strings are automatically encoded to UTF-8.

## Next Steps

To make this a production-ready binding:

1. Add comprehensive tests
2. Add type hints and stubs
3. Add documentation
4. Set up CI/CD
5. Publish to PyPI
6. Add more SNOBOL features (tables, EVAL, etc.)
