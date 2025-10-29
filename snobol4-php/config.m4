PHP_ARG_ENABLE(snobol, whether to enable SNOBOL4 pattern extension,
[  --enable-snobol   Enable SNOBOL4 pattern extension])

if test "$PHP_SNOBOL" != "no"; then
  PHP_NEW_EXTENSION(snobol, php_snobol.c, $ext_shared)
fi
