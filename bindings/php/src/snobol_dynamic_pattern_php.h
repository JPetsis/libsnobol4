#ifndef SNOBOL_DYNAMIC_PATTERN_PHP_H
#define SNOBOL_DYNAMIC_PATTERN_PHP_H

#include "php.h"

/**
 * @brief Register the Snobol\DynamicPatternCache internal class.
 *
 * Called from PHP_MINIT_FUNCTION(snobol); registers the class entry in
 * #snobol_dynamic_pattern_cache_ce with the LRU-cache object handlers.
 */
void snobol_dynamic_pattern_cache_php_minit(void);

/** @brief Class entry for Snobol\DynamicPatternCache, populated by
 *  snobol_dynamic_pattern_cache_php_minit(). */
extern zend_class_entry *snobol_dynamic_pattern_cache_ce;

#endif /* SNOBOL_DYNAMIC_PATTERN_PHP_H */
