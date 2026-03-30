dnl config.m4 for libsnobol4 PHP extension

PHP_ARG_ENABLE([snobol],
  [whether to enable snobol support],
  [AS_HELP_STRING([--enable-snobol],
    [Enable snobol support])],
  [no])

if test "$PHP_SNOBOL" != "no"; then
  dnl Check if core library exists
  dnl When running configure from bindings/php/, core is at ../../core/
  dnl CORE_DIR  = absolute path (used for -I include flags)
  dnl CORE_REL  = relative path from $abs_srcdir (used for source file lists;
  dnl             phpize prepends $ext_srcdir/ to every source path, so passing
  dnl             absolute paths here causes path doubling)
  CORE_DIR=""
  CORE_REL=""
  if test -f "$abs_srcdir/../../core/src/lexer.c"; then
    CORE_DIR="$abs_srcdir/../../core"
    CORE_REL="../../core"
  elif test -f "$abs_srcdir/../core/src/lexer.c"; then
    CORE_DIR="$abs_srcdir/../core"
    CORE_REL="../core"
  elif test -f "$abs_srcdir/core/src/lexer.c"; then
    CORE_DIR="$abs_srcdir/core"
    CORE_REL="core"
  fi

  if test -z "$CORE_DIR"; then
    AC_MSG_ERROR([libsnobol4 core not found. Please ensure core/ directory exists.])
  fi

  AC_MSG_NOTICE([Using core directory: $CORE_DIR (relative: $CORE_REL)])

  dnl Add core source files using relative paths so phpize does not double them
  snobol_core_sources="
    $CORE_REL/src/lexer.c
    $CORE_REL/src/parser.c
    $CORE_REL/src/ast.c
    $CORE_REL/src/compiler.c
    $CORE_REL/src/vm.c
    $CORE_REL/src/table.c
    $CORE_REL/src/dynamic_pattern.c
    $CORE_REL/src/jit.c
    $CORE_REL/src/version.c
  "

  dnl Add PHP extension sources (relative to $ext_srcdir / $abs_srcdir)
  snobol_sources="
    src/php_snobol.c
    src/snobol_pattern.c
    src/snobol_table_php.c
    src/snobol_dynamic_pattern_php.c
    $snobol_core_sources
  "

  dnl Add include paths (absolute paths are fine for -I compiler flags)
  PHP_ADD_INCLUDE([$CORE_DIR/include])
  PHP_ADD_INCLUDE([$abs_srcdir/src])

  dnl Enable JIT if available
  AC_DEFINE(HAVE_SNOBOL_JIT, 1, [Have libsnobol4 JIT support])

  dnl Create the extension
  PHP_NEW_EXTENSION([snobol], $snobol_sources, $ext_shared,, [-I$CORE_DIR/include])
fi
