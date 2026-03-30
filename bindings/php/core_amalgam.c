/**
 * Core amalgamation file for PHP extension build
 * 
 * This file #includes all core translation units to avoid
 * path issues with phpize (which doesn't handle ".." paths well).
 */

/* Include all core source files - include paths are set via -I flags in config.m4 */
#include "../core/src/lexer.c"
#include "../core/src/parser.c"
#include "../core/src/ast.c"
#include "../core/src/compiler.c"
#include "../core/src/vm.c"
#include "../core/src/table.c"
#include "../core/src/dynamic_pattern.c"
#include "../core/src/jit.c"
#include "../core/src/version.c"
