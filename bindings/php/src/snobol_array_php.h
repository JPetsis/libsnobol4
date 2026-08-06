#ifndef SNOBOL_ARRAY_PHP_H
#define SNOBOL_ARRAY_PHP_H

#include "php.h"

/**
 * @brief Register the Snobol\Array_ internal class.
 *
 * Called from PHP_MINIT_FUNCTION(snobol); registers the class entry in
 * #snobol_array_ce with the object handlers for array lifecycle
 * (create/free).
 */
void snobol_array_php_minit(void);

/** @brief Class entry for Snobol\Array_, populated by snobol_array_php_minit(). */
extern zend_class_entry *snobol_array_ce;

#endif
