# Changelog

All notable changes to the SNOBOL4 project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- AST versioning API (`snobol_ast_get_version()`, `snobol_ast_version_check()`)
- AST version macros (`SNOBOL_AST_VERSION_MAJOR`, `SNOBOL_AST_VERSION_CHECK`)
- C test suite for AST versioning and memory management (`tests/c/test_ast.c`)

### Changed

- Renamed `snobol4-php/` to `snobol4-core/` to reflect language-agnostic architecture
- `snobol_ast_create_label()` now makes a copy of the label name (handles string literals safely)
- `snobol_ast_free()` now properly handles `AST_CONCAT` nodes (frees parts array + children)

### Fixed

- Memory leak in `snobol_ast_free()` for `AST_CONCAT` nodes
- Crash when freeing label nodes created with string literals

### Version Status

- **AST API**: v1.0.0 (stable)
- **Core**: v1.x (active development)
- **Full 1.0 Release**: Awaiting more language bindings (Python, Rust, JavaScript)

## [1.0.0-alpha] - 2026-03-28

### Added

- **Language-Agnostic Core**: Complete C implementation of lexer, parser, and AST
- **C Lexer** (`snobol_lexer.h/c`): UTF-8 aware tokenizer with save/restore for backtracking
- **C Parser** (`snobol_parser.h/c`): Recursive descent parser following EBNF grammar
- **C AST** (`snobol_ast.h/c`): Tagged union AST with 16 node types
- **Formal Grammar** (`grammar/snobol.ebnf`): EBNF specification of SNOBOL pattern syntax
- **PHP Binding**: `Pattern::fromString()` method using C parser
- **C Tests**: 12 lexer tests, 14 parser tests, 11 AST tests
- **Documentation**:
    - Architecture documentation in README.md
    - Contributor guide in docs/CONTRIBUTORS.md
    - Python binding proof-of-concept in examples/python-binding/

### Changed

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
