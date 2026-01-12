PHP_ARG_ENABLE(snobol, whether to enable snobol support,
[  --enable-snobol           Enable snobol support])

if test "$PHP_SNOBOL" != "no"; then
  PHP_NEW_EXTENSION(snobol, php_snobol.c snobol_compiler.c snobol_pattern.c snobol_vm.c, $ext_shared)
fi
