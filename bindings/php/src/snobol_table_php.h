#ifndef SNOBOL_TABLE_PHP_H
#define SNOBOL_TABLE_PHP_H

#include "php.h"

/**
 * @brief Register the Snobol\Table internal class.
 *
 * Called from PHP_MINIT_FUNCTION(snobol); registers the class entry in
 * #snobol_table_ce with the object handlers for table lifecycle
 * (create/free) and the table registry.
 */
void snobol_table_php_minit(void);

/** @brief Class entry for Snobol\Table, populated by snobol_table_php_minit(). */
extern zend_class_entry *snobol_table_ce;

#endif /* SNOBOL_TABLE_PHP_H */
