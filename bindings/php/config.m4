dnl config.m4 for libsnobol4 PHP extension

PHP_ARG_ENABLE([snobol],
  [whether to enable snobol support],
  [AS_HELP_STRING([--enable-snobol],
    [Enable snobol support])],
  [no])

if test "$PHP_SNOBOL" != "no"; then
  dnl Check if core library exists
  dnl When running configure from bindings/php/, core is at ../../core/
  CORE_REL=""
  if test -f "$abs_srcdir/../../core/src/lexer.c"; then
    CORE_REL="../../core"
  elif test -f "$abs_srcdir/../core/src/lexer.c"; then
    CORE_REL="../core"
  elif test -f "$abs_srcdir/core/src/lexer.c"; then
    CORE_REL="core"
  fi

  if test -z "$CORE_REL"; then
    AC_MSG_ERROR([libsnobol4 core not found. Please ensure core/ directory exists.])
  fi

  AC_MSG_NOTICE([Using core directory: $CORE_REL])

  dnl Add core source files (relative to configure location)
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

  dnl Add PHP extension sources
  snobol_sources="
    src/php_snobol.c
    src/snobol_pattern.c
    src/snobol_table_php.c
    src/snobol_dynamic_pattern_php.c
    $snobol_core_sources
  "

  dnl Add include paths
  PHP_ADD_INCLUDE([$abs_srcdir/$CORE_REL/include])
  PHP_ADD_INCLUDE([$abs_srcdir/src])

  dnl Enable JIT if available
  AC_DEFINE(HAVE_SNOBOL_JIT, 1, [Have libsnobol4 JIT support])

  dnl Create the extension
  PHP_NEW_EXTENSION([snobol], $snobol_sources, $ext_shared,, [-I$abs_srcdir/$CORE_REL/include])
fi
