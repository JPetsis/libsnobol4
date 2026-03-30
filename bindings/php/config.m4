dnl config.m4 for libsnobol4 PHP extension

PHP_ARG_ENABLE([snobol],
  [whether to enable snobol support],
  [AS_HELP_STRING([--enable-snobol],
    [Enable snobol support])],
  [no])

if test "$PHP_SNOBOL" != "no"; then
  dnl Check if core library exists
  dnl When running configure from bindings/php/, core is at ../../core/
  CORE_DIR=""
  if test -f "$abs_srcdir/../../core/src/lexer.c"; then
    CORE_DIR="$abs_srcdir/../../core"
  elif test -f "$abs_srcdir/../core/src/lexer.c"; then
    CORE_DIR="$abs_srcdir/../core"
  elif test -f "$abs_srcdir/core/src/lexer.c"; then
    CORE_DIR="$abs_srcdir/core"
  fi

  if test -z "$CORE_DIR"; then
    AC_MSG_ERROR([libsnobol4 core not found. Please ensure core/ directory exists.])
  fi

  AC_MSG_NOTICE([Using core directory: $CORE_DIR])

  dnl Add core source files with absolute paths
  snobol_core_sources="
    $CORE_DIR/src/lexer.c
    $CORE_DIR/src/parser.c
    $CORE_DIR/src/ast.c
    $CORE_DIR/src/compiler.c
    $CORE_DIR/src/vm.c
    $CORE_DIR/src/table.c
    $CORE_DIR/src/dynamic_pattern.c
    $CORE_DIR/src/jit.c
    $CORE_DIR/src/version.c
  "

  dnl Add PHP extension sources
  snobol_sources="
    $abs_srcdir/src/php_snobol.c
    $abs_srcdir/src/snobol_pattern.c
    $abs_srcdir/src/snobol_table_php.c
    $abs_srcdir/src/snobol_dynamic_pattern_php.c
    $snobol_core_sources
  "

  dnl Add include paths
  PHP_ADD_INCLUDE([$CORE_DIR/include])
  PHP_ADD_INCLUDE([$abs_srcdir/src])

  dnl Enable JIT if available
  AC_DEFINE(HAVE_SNOBOL_JIT, 1, [Have libsnobol4 JIT support])

  dnl Create the extension
  PHP_NEW_EXTENSION([snobol], $snobol_sources, $ext_shared,, [-I$CORE_DIR/include])
fi
