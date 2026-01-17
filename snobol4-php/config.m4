PHP_ARG_ENABLE(snobol, whether to enable snobol support,
[  --enable-snobol           Enable snobol support])

PHP_ARG_ENABLE(snobol-profile, whether to enable snobol VM profiling,
[  --enable-snobol-profile   Enable snobol VM profiling], no, no)

if test "$PHP_SNOBOL_PROFILE" != "no"; then
  AC_DEFINE(SNOBOL_PROFILE, 1, [Enable VM profiling])
fi

if test "$PHP_SNOBOL" != "no"; then
  PHP_NEW_EXTENSION(snobol, php_snobol.c snobol_compiler.c snobol_pattern.c snobol_vm.c, $ext_shared)
fi
