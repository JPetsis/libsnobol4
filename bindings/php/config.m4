dnl config.m4 for libsnobol4 PHP extension

PHP_ARG_ENABLE([snobol],
  [whether to enable snobol support],
  [AS_HELP_STRING([--enable-snobol],
    [Enable snobol support])],
  [no])

if test "$PHP_SNOBOL" != "no"; then
  dnl Check if core library exists
  dnl When running configure from bindings/php/, core is at ../../core/
  dnl CORE_DIR = absolute path, used only for -I compiler flags.
  dnl
  dnl NOTE: phpize's PHP_NEW_EXTENSION prepends $ext_srcdir/ to every source
  dnl path.  Absolute paths cause path doubling; relative paths with ".."
  dnl cause empty object-file names (phpize cannot handle ".." components).
  dnl We therefore compile all core sources via the in-directory amalgamation
  dnl file core_amalgam.c which lives next to config.m4.
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

  dnl All source paths are relative to $ext_srcdir (= $abs_srcdir).
  dnl core_amalgam.c lives here and #include-s all core translation units,
  dnl avoiding any ".." path components that confuse phpize.
  snobol_sources="
    src/php_snobol.c
    src/snobol_pattern.c
    src/snobol_table_php.c
    src/snobol_dynamic_pattern_php.c
    core_amalgam.c
  "

  dnl Add include paths (absolute paths are fine for -I compiler flags)
  PHP_ADD_INCLUDE([$CORE_DIR/include])
  PHP_ADD_INCLUDE([$abs_srcdir/src])

  dnl Enable JIT if available
  AC_DEFINE(HAVE_SNOBOL_JIT, 1, [Have libsnobol4 JIT support])

  dnl phpize always builds shared extensions; make sure COMPILE_DL_SNOBOL is
  dnl present in config.h regardless of ext_shared value (belt-and-suspenders
  dnl for macOS where PHP_NEW_EXTENSION sometimes omits it).
  AC_DEFINE([COMPILE_DL_SNOBOL], [1], [Build snobol as a dynamically-loaded module])

  dnl Create the extension
  PHP_NEW_EXTENSION([snobol], $snobol_sources, $ext_shared,, [-I$CORE_DIR/include])
fi
