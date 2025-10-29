#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_snobol.h"
#include "snobol_pattern.c" /* include to simplify build ordering: snobol_pattern.c depends on php API */
#include "snobol_vm.c"
#include "snobol_compiler.c"

zend_module_entry snobol_module_entry = {
    STANDARD_MODULE_HEADER,
    "snobol",
    NULL,
    PHP_MINIT(snobol),
    NULL,
    NULL,
    NULL,
    NULL,
    NO_VERSION_YET,
    STANDARD_MODULE_PROPERTIES
};

ZEND_GET_MODULE(snobol)

/* We implement PHP_MINIT inside snobol_pattern.c */