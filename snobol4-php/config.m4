PHP_ARG_ENABLE(snobol, whether to enable SNOBOL4 pattern extension,
[  --enable-snobol   Enable SNOBOL4 pattern extension])

if test "$PHP_SNOBOL" != "no"; then
  PHP_NEW_EXTENSION(snobol, php_snobol.c snobol_pattern.c snobol_vm.c snobol_compiler.c, $ext_shared)
fi
