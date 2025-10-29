#ifndef PHP_SNOBOL_H
#define PHP_SNOBOL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"

extern zend_module_entry snobol_module_entry;
#define phpext_snobol_ptr &snobol_module_entry

#define PHP_SNOBOL_VERSION "0.1.0"

PHP_MINIT_FUNCTION(snobol);

#endif /* PHP_SNOBOL_H */
