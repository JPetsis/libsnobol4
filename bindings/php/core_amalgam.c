/**
 * Core amalgamation file for PHP extension build
 *
 * This file #includes all core translation units so that phpize sees a single
 * source file (core_amalgam.c → core_amalgam.o) with no ".." components in
 * the name.  The "#include" paths below use ".." which is fine for the C
 * preprocessor — only the source-file *name* passed to PHP_NEW_EXTENSION must
 * be free of ".." components.
 *
 * Include paths are relative to this file's directory (bindings/php/).
 */

/* Include all core source files */
#include "../../core/src/lexer.c"
#include "../../core/src/parser.c"
#include "../../core/src/ast.c"
#include "../../core/src/compiler.c"
#include "../../core/src/vm.c"
#include "../../core/src/table.c"
#include "../../core/src/dynamic_pattern.c"
#include "../../core/src/jit.c"
#include "../../core/src/version.c"
