#ifndef SNOBOL_COMPILER_H
#define SNOBOL_COMPILER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* AST node kinds used by the PHP -> C compiler bridge.
   The AST is represented in PHP as associative arrays; the extension reads them.
   Node format (PHP array):
     [ 'type' => 'lit', 'text' => 'abc' ]
     [ 'type' => 'concat', 'parts' => [node1, node2, ...] ]
     [ 'type' => 'alt', 'left' => node1, 'right' => node2 ]
     [ 'type' => 'span', 'set' => string_of_chars ] // set is literal list (compiler builds bitmap)
     [ 'type' => 'break', 'set' => string_of_chars ]
     [ 'type' => 'any' ]  // match any single codepoint
     [ 'type' => 'notany', 'set' => string_of_chars ]
     [ 'type' => 'arbno', 'sub' => node ] // zero or more of subpattern
     [ 'type' => 'cap', 'reg' => integer, 'sub' => node ] // capture sub into reg
     [ 'type' => 'assign', 'var' => integer, 'reg' => integer ] // assign capture reg to var
     [ 'type' => 'len', 'n' => integer ]
     [ 'type' => 'eval', 'fn' => integer, 'reg' => integer ] // call eval fn
*/

#include "php.h"

int compile_ast_to_bytecode(zval *ast, uint8_t **out_bc, size_t *out_len);

/* helper to free bc */
void compiler_free(uint8_t *bc);

#endif // SNOBOL_COMPILER_H