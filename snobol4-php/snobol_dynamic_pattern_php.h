#ifndef SNOBOL_DYNAMIC_PATTERN_PHP_H
#define SNOBOL_DYNAMIC_PATTERN_PHP_H

#include "php.h"

/* Module initialization for DynamicPatternCache class */
void snobol_dynamic_pattern_cache_php_minit(void);

/* Extern class entry */
extern zend_class_entry *snobol_dynamic_pattern_cache_ce;

#endif /* SNOBOL_DYNAMIC_PATTERN_PHP_H */
