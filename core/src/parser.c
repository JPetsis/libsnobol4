/**
 * @file parser.c
 * @brief Recursive descent parser for SNOBOL pattern syntax
 *
 * Consumes tokens from lexer and produces AST.
 * Implements the grammar defined in grammar/snobol.ebnf
 */

#include "snobol/parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * Parser state structure (opaque to callers)
 */
struct snobol_parser {
    parser_error_t error;
};

/* Forward declarations for recursive descent */
static ast_node_t* parse_pattern(snobol_parser_t* parser, snobol_lexer_t* lexer);
static ast_node_t* parse_statement(snobol_parser_t* parser, snobol_lexer_t* lexer);
static ast_node_t* parse_alternation(snobol_parser_t* parser, snobol_lexer_t* lexer);
static ast_node_t* parse_concatenation(snobol_parser_t* parser, snobol_lexer_t* lexer);
static ast_node_t* parse_repetition(snobol_parser_t* parser, snobol_lexer_t* lexer);
static ast_node_t* parse_primary(snobol_parser_t* parser, snobol_lexer_t* lexer);
static ast_node_t* parse_function_call(snobol_parser_t* parser, snobol_lexer_t* lexer);
static ast_node_t* parse_dynamic_eval(snobol_parser_t* parser, snobol_lexer_t* lexer);

/* Error handling helpers */
static void set_error(snobol_parser_t* parser, const char* msg, size_t line, size_t col);
static token_t advance(snobol_lexer_t* lexer);
static token_t peek(snobol_lexer_t* lexer);
static bool expect(snobol_parser_t* parser, snobol_lexer_t* lexer, token_type_t type);
static bool match(snobol_lexer_t* lexer, token_type_t type);

snobol_parser_t* snobol_parser_create(void) {
    snobol_parser_t* parser = (snobol_parser_t*)calloc(1, sizeof(snobol_parser_t));
    if (parser) {
        parser->error.has_error = false;
        parser->error.message[0] = '\0';
        parser->error.line = 0;
        parser->error.column = 0;
    }
    return parser;
}

static void set_error(snobol_parser_t* parser, const char* msg, size_t line, size_t col) {
    if (!parser || !msg) return;
    
    parser->error.has_error = true;
    parser->error.line = line;
    parser->error.column = col;
    
    /* Truncate message if too long */
    size_t len = strlen(msg);
    if (len >= SNOBOL_PARSER_ERROR_MAX) {
        len = SNOBOL_PARSER_ERROR_MAX - 1;
    }
    memcpy(parser->error.message, msg, len);
    parser->error.message[len] = '\0';
}

static token_t advance(snobol_lexer_t* lexer) {
    return snobol_lexer_next(lexer);
}

static token_t peek(snobol_lexer_t* lexer) {
    return snobol_lexer_peek(lexer);
}

static bool match(snobol_lexer_t* lexer, token_type_t type) {
    token_t tok = peek(lexer);
    return tok.type == type;
}

static bool expect(snobol_parser_t* parser, snobol_lexer_t* lexer, token_type_t type) {
    token_t tok = peek(lexer);
    if (tok.type == type) {
        advance(lexer);
        return true;
    }
    
    /* Error: unexpected token */
    char msg[128];
    snprintf(msg, sizeof(msg), "Expected %s, got %s", 
             snobol_token_name(type), snobol_token_name(tok.type));
    set_error(parser, msg, snobol_lexer_get_line(lexer), snobol_lexer_get_pos(lexer));
    return false;
}

ast_node_t* snobol_parser_parse(snobol_parser_t* parser, snobol_lexer_t* lexer) {
    if (!parser || !lexer) return NULL;
    
    /* Clear any previous error */
    parser->error.has_error = false;
    
    /* Parse the pattern */
    ast_node_t* ast = parse_pattern(parser, lexer);
    
    /* Check for trailing tokens */
    if (!parser->error.has_error) {
        token_t tok = peek(lexer);
        if (tok.type != TOKEN_EOF) {
            set_error(parser, "Unexpected token after pattern", 
                     snobol_lexer_get_line(lexer), snobol_lexer_get_pos(lexer));
            snobol_ast_free(ast);
            return NULL;
        }
    }
    
    return ast;
}

static ast_node_t* parse_pattern(snobol_parser_t* parser, snobol_lexer_t* lexer) {
    return parse_statement(parser, lexer);
}

static ast_node_t* parse_statement(snobol_parser_t* parser, snobol_lexer_t* lexer) {
    /* Check for label: IDENT ':' */
    token_t tok = peek(lexer);
    if (tok.type == TOKEN_IDENT) {
        /* Look ahead to check for ':' */
        snobol_lexer_state_t saved_state = snobol_lexer_save(lexer);
        advance(lexer);
        token_t next = peek(lexer);

        if (next.type == TOKEN_COLON) {
            /* This is a label */
            advance(lexer);  /* Consume ':' */

            /* Create label node - we'll wrap the pattern later */
            char* label_name = (char*)malloc(tok.data.string.len + 1);
            if (label_name) {
                memcpy(label_name, tok.data.string.text, tok.data.string.len);
                label_name[tok.data.string.len] = '\0';
            }

            /* Parse the target pattern */
            ast_node_t* target = parse_statement(parser, lexer);
            if (!target) {
                free(label_name);
                return NULL;
            }

            return snobol_ast_create_label(label_name, target);
        } else {
            /* Not a label, restore position */
            snobol_lexer_restore(lexer, saved_state);
        }
    }
    
    /* Parse the pattern expression */
    ast_node_t* pattern = parse_alternation(parser, lexer);
    if (!pattern) return NULL;
    
    /* Check for goto: ':' '(' IDENT ')' */
    tok = peek(lexer);
    if (tok.type == TOKEN_COLON) {
        advance(lexer);
        
        if (!expect(parser, lexer, TOKEN_LPAREN)) {
            snobol_ast_free(pattern);
            return NULL;
        }
        
        tok = peek(lexer);
        if (tok.type != TOKEN_IDENT) {
            set_error(parser, "Expected label name after ':('",
                     snobol_lexer_get_line(lexer), snobol_lexer_get_pos(lexer));
            snobol_ast_free(pattern);
            return NULL;
        }
        advance(lexer);
        
        if (!expect(parser, lexer, TOKEN_RPAREN)) {
            snobol_ast_free(pattern);
            return NULL;
        }
        
        /* For now, we don't have a goto AST node in the same way */
        /* The goto is handled at the statement level */
    }
    
    return pattern;
}

static ast_node_t* parse_alternation(snobol_parser_t* parser, snobol_lexer_t* lexer) {
    ast_node_t* left = parse_concatenation(parser, lexer);
    if (!left) return NULL;

    while (match(lexer, TOKEN_PIPE)) {
        advance(lexer);  /* Consume '|' */

        ast_node_t* right = parse_concatenation(parser, lexer);
        if (!right) {
            snobol_ast_free(left);
            return NULL;
        }

        left = snobol_ast_create_alt(left, right);
    }

    return left;
}

static ast_node_t* parse_concatenation(snobol_parser_t* parser, snobol_lexer_t* lexer) {
    /* Collect all concatenated parts */
    ast_node_t** parts = NULL;
    size_t count = 0;
    size_t capacity = 0;

    while (true) {
        token_t tok = peek(lexer);

        /* Check if this token starts a primary pattern */
        bool is_primary = (tok.type == TOKEN_LIT ||
                          tok.type == TOKEN_CHARCLASS ||
                          tok.type == TOKEN_LPAREN ||
                          tok.type == TOKEN_ANCHOR_START ||
                          tok.type == TOKEN_ANCHOR_END ||
                          tok.type == TOKEN_AT ||
                          tok.type == TOKEN_IDENT);

        /* Check for function calls */
        if (tok.type == TOKEN_IDENT) {
            /* Look ahead for '(' */
            snobol_lexer_state_t saved_state = snobol_lexer_save(lexer);
            advance(lexer);  /* Consume IDENT */
            token_t next = peek(lexer);  /* Peek at next token */
            if (next.type == TOKEN_LPAREN) {
                is_primary = true;
            }
            /* Restore lexer position */
            snobol_lexer_restore(lexer, saved_state);
        }

        if (!is_primary) {
            break;
        }

        ast_node_t* part = parse_repetition(parser, lexer);
        if (!part) {
            /* Free collected parts */
            for (size_t i = 0; i < count; i++) {
                snobol_ast_free(parts[i]);
            }
            free(parts);
            return NULL;
        }

        /* Add to parts array */
        if (count >= capacity) {
            capacity = (capacity == 0) ? 4 : capacity * 2;
            ast_node_t** new_parts = (ast_node_t**)realloc(parts, capacity * sizeof(ast_node_t*));
            if (!new_parts) {
                snobol_ast_free(part);
                for (size_t i = 0; i < count; i++) {
                    snobol_ast_free(parts[i]);
                }
                free(parts);
                return NULL;
            }
            parts = new_parts;
        }

        parts[count++] = part;
    }

    if (count == 0) {
        free(parts);
        set_error(parser, "Expected pattern element",
                 snobol_lexer_get_line(lexer), snobol_lexer_get_pos(lexer));
        return NULL;
    }

    if (count == 1) {
        ast_node_t* result = parts[0];
        free(parts);
        return result;
    }

    return snobol_ast_create_concat(parts, count);
}

static ast_node_t* parse_repetition(snobol_parser_t* parser, snobol_lexer_t* lexer) {
    ast_node_t* primary = parse_primary(parser, lexer);
    if (!primary) return NULL;

    token_t tok = peek(lexer);

    switch (tok.type) {
        case TOKEN_STAR:
            advance(lexer);
            return snobol_ast_create_arbno(primary);

        case TOKEN_PLUS:
            advance(lexer);
            /* x+ = x x* = concat(x, arbno(x)) */
            {
                ast_node_t* arbno = snobol_ast_create_arbno(primary);
                ast_node_t* clone = primary;  /* Simplified - should clone */
                ast_node_t** parts = (ast_node_t**)malloc(2 * sizeof(ast_node_t*));
                if (!parts) {
                    snobol_ast_free(arbno);
                    return NULL;
                }
                parts[0] = clone;
                parts[1] = arbno;
                return snobol_ast_create_concat(parts, 2);
            }

        case TOKEN_QUESTION:
            advance(lexer);
            /* x? = alt(x, empty) - simplified as repetition with min=0, max=1 */
            return snobol_ast_create_repeat(primary, 0, 1);

        default:
            return primary;
    }
}

static ast_node_t* parse_primary(snobol_parser_t* parser, snobol_lexer_t* lexer) {
    token_t tok = peek(lexer);

    switch (tok.type) {
        case TOKEN_LIT:
            advance(lexer);
            return snobol_ast_create_lit(tok.data.string.text, tok.data.string.len);
            
        case TOKEN_CHARCLASS:
            advance(lexer);
            return snobol_ast_create_span(tok.data.string.text, tok.data.string.len);
            
        case TOKEN_LPAREN:
            advance(lexer);
            {
                ast_node_t* inner = parse_alternation(parser, lexer);
                if (!inner) return NULL;
                
                if (!expect(parser, lexer, TOKEN_RPAREN)) {
                    snobol_ast_free(inner);
                    return NULL;
                }
                
                return inner;
            }
            
        case TOKEN_ANCHOR_START:
            advance(lexer);
            {
                ast_node_t* node = (ast_node_t*)calloc(1, sizeof(ast_node_t));
                if (node) {
                    node->type = AST_ANCHOR;
                    node->data.anchor.atype = ANCHOR_START;
                }
                return node;
            }
            
        case TOKEN_ANCHOR_END:
            advance(lexer);
            {
                ast_node_t* node = (ast_node_t*)calloc(1, sizeof(ast_node_t));
                if (node) {
                    node->type = AST_ANCHOR;
                    node->data.anchor.atype = ANCHOR_END;
                }
                return node;
            }
            
        case TOKEN_AT:
            advance(lexer);
            {
                /* Capture: @IDENT or @integer */
                tok = peek(lexer);
                int reg = 0;
                
                if (tok.type == TOKEN_IDENT) {
                    /* For now, use a fixed register for named captures */
                    /* A real implementation would manage a symbol table */
                    reg = 1;  /* Simplified */
                    advance(lexer);
                } else if (tok.type == TOKEN_STAR) {
                    /* This shouldn't happen, handle gracefully */
                    reg = 1;
                } else {
                    set_error(parser, "Expected capture target",
                             snobol_lexer_get_line(lexer), snobol_lexer_get_pos(lexer));
                    return NULL;
                }
                
                ast_node_t* sub = parse_primary(parser, lexer);
                if (!sub) return NULL;
                
                return snobol_ast_create_cap(reg, sub);
            }
            
        case TOKEN_IDENT:
            /* Could be function call or bare identifier */
            return parse_function_call(parser, lexer);
            
        default:
            set_error(parser, "Unexpected token in pattern",
                     snobol_lexer_get_line(lexer), snobol_lexer_get_pos(lexer));
            return NULL;
    }
}

static ast_node_t* parse_function_call(snobol_parser_t* parser, snobol_lexer_t* lexer) {
    token_t name_tok = peek(lexer);

    if (name_tok.type != TOKEN_IDENT) {
        set_error(parser, "Expected function name",
                 snobol_lexer_get_line(lexer), snobol_lexer_get_pos(lexer));
        return NULL;
    }

    /* Check function name */
    const char* name = name_tok.data.string.text;
    size_t name_len = name_tok.data.string.len;

    /* Look ahead for '(' */
    advance(lexer);
    token_t next = peek(lexer);

    if (next.type != TOKEN_LPAREN) {
        /* Not a function call, treat as identifier */
        /* For now, return error - identifiers alone aren't valid patterns */
        set_error(parser, "Bare identifier is not a valid pattern",
                 snobol_lexer_get_line(lexer), snobol_lexer_get_pos(lexer));
        return NULL;
    }

    advance(lexer);  /* Consume '(' */

    /* Parse arguments based on function name */
    if (strncmp(name, "SPAN", name_len) == 0) {
        token_t arg = peek(lexer);
        if (arg.type != TOKEN_LIT && arg.type != TOKEN_CHARCLASS) {
            set_error(parser, "SPAN expects string argument",
                     snobol_lexer_get_line(lexer), snobol_lexer_get_pos(lexer));
            return NULL;
        }
        advance(lexer);

        if (!expect(parser, lexer, TOKEN_RPAREN)) {
            return NULL;
        }

        return snobol_ast_create_span(arg.data.string.text, arg.data.string.len);
    }
    
    if (strncmp(name, "ANY", name_len) == 0) {
        token_t arg = peek(lexer);
        if (arg.type == TOKEN_RPAREN) {
            /* ANY() with no args */
            advance(lexer);
            return snobol_ast_create_any(NULL, 0);
        }
        
        if (arg.type != TOKEN_LIT && arg.type != TOKEN_CHARCLASS) {
            set_error(parser, "ANY expects string argument",
                     snobol_lexer_get_line(lexer), snobol_lexer_get_pos(lexer));
            return NULL;
        }
        advance(lexer);
        
        if (!expect(parser, lexer, TOKEN_RPAREN)) {
            return NULL;
        }
        
        return snobol_ast_create_any(arg.data.string.text, arg.data.string.len);
    }
    
    if (strncmp(name, "LEN", name_len) == 0) {
        /* Parse LEN(n) - argument is peeked but not used yet */
        (void)peek(lexer);  /* Argument parsed but not used - placeholder for future */
        advance(lexer);

        if (!expect(parser, lexer, TOKEN_RPAREN)) {
            return NULL;
        }

        /* Simplified - would need proper integer parsing */
        ast_node_t* node = (ast_node_t*)calloc(1, sizeof(ast_node_t));
        if (node) {
            node->type = AST_LEN;
            node->data.len.n = 1;  /* Placeholder */
        }
        return node;
    }
    
    if (strncmp(name, "EVAL", name_len) == 0) {
        return parse_dynamic_eval(parser, lexer);
    }
    
    /* Unknown function */
    set_error(parser, "Unknown function",
             snobol_lexer_get_line(lexer), snobol_lexer_get_pos(lexer));
    return NULL;
}

static ast_node_t* parse_dynamic_eval(snobol_parser_t* parser, snobol_lexer_t* lexer) {
    /* EVAL already consumed, '(' already consumed */
    
    /* Parse the inner pattern expression */
    ast_node_t* expr = parse_alternation(parser, lexer);
    if (!expr) return NULL;
    
    if (!expect(parser, lexer, TOKEN_RPAREN)) {
        snobol_ast_free(expr);
        return NULL;
    }
    
    /* Create dynamic eval node */
    ast_node_t* node = (ast_node_t*)calloc(1, sizeof(ast_node_t));
    if (node) {
        node->type = AST_DYNAMIC_EVAL;
        node->data.dynamic_eval.expr = expr;
    }
    
    return node;
}

bool snobol_parser_has_error(snobol_parser_t* parser) {
    if (!parser) return false;
    return parser->error.has_error;
}

const char* snobol_parser_get_error(snobol_parser_t* parser) {
    if (!parser || !parser->error.has_error) return NULL;
    return parser->error.message;
}

void snobol_parser_get_error_location(snobol_parser_t* parser, size_t* line, size_t* column) {
    if (!parser) return;
    if (line) *line = parser->error.line;
    if (column) *column = parser->error.column;
}

void snobol_parser_clear_error(snobol_parser_t* parser) {
    if (!parser) return;
    parser->error.has_error = false;
    parser->error.message[0] = '\0';
    parser->error.line = 0;
    parser->error.column = 0;
}

void snobol_parser_destroy(snobol_parser_t* parser) {
    if (parser) {
        free(parser);
    }
}
