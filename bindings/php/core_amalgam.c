/**
 * core_amalgam.c — phpize build helper
 *
 * phpize's build system cannot reliably derive object-file names for source
 * paths that contain ".." components (e.g. "../../core/src/lexer.c"), which
 * causes all nine core translation units to collapse onto the same ".o" name
 * and produce "multiple definition" linker errors.
 *
 * This single file includes every core translation unit so that phpize sees
 * ONE relative source ("core_amalgam.c", in the same directory as config.m4)
 * and produces exactly one well-named object file.
 *
 * NOTE: this file is ONLY used by the phpize/autoconf build path.
 *       The CMake build compiles each core file as its own translation unit
 *       inside the libsnobol4 static library and does NOT include this file.
 */

#include "../../core/src/lexer.c"
#include "../../core/src/parser.c"
#include "../../core/src/ast.c"
#include "../../core/src/compiler.c"
#include "../../core/src/vm.c"
#include "../../core/src/table.c"
#include "../../core/src/dynamic_pattern.c"
#include "../../core/src/jit.c"
#include "../../core/src/version.c"

