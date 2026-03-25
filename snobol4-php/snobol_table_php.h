#ifndef SNOBOL_TABLE_PHP_H
#define SNOBOL_TABLE_PHP_H

#include "php.h"

/* Module initialization for Table class */
void snobol_table_php_minit(void);

/* Extern class entry */
extern zend_class_entry *snobol_table_ce;

#endif /* SNOBOL_TABLE_PHP_H */
