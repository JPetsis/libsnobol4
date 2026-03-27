/**
 * @file snobol_ast.c
 * @brief AST node creation and memory management
 */

#include "snobol_ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Internal helper: duplicate a string
 */
static char* str_dup(const char* s, size_t len) {
    if (!s) return NULL;
    char* dup = (char*)malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len);
        dup[len] = '\0';
    }
    return dup;
}

ast_node_t* snobol_ast_create_lit(const char* text, size_t len) {
    ast_node_t* node = (ast_node_t*)calloc(1, sizeof(ast_node_t));
    if (!node) return NULL;
    
    node->type = AST_LITERAL;
    node->data.literal.text = str_dup(text, len);
    node->data.literal.len = len;
    return node;
}

ast_node_t* snobol_ast_create_concat(ast_node_t** parts, size_t count) {
    ast_node_t* node = (ast_node_t*)calloc(1, sizeof(ast_node_t));
    if (!node) return NULL;
    
    node->type = AST_CONCAT;
    node->data.concat.parts = parts;
    node->data.concat.count = count;
    return node;
}

ast_node_t* snobol_ast_create_alt(ast_node_t* left, ast_node_t* right) {
    ast_node_t* node = (ast_node_t*)calloc(1, sizeof(ast_node_t));
    if (!node) return NULL;
    
    node->type = AST_ALT;
    node->data.alt.left = left;
    node->data.alt.right = right;
    return node;
}

ast_node_t* snobol_ast_create_arbno(ast_node_t* sub) {
    ast_node_t* node = (ast_node_t*)calloc(1, sizeof(ast_node_t));
    if (!node) return NULL;
    
    node->type = AST_ARBNO;
    node->data.arbno.sub = sub;
    return node;
}

ast_node_t* snobol_ast_create_cap(int reg, ast_node_t* sub) {
    ast_node_t* node = (ast_node_t*)calloc(1, sizeof(ast_node_t));
    if (!node) return NULL;
    
    node->type = AST_CAP;
    node->data.cap.reg = reg;
    node->data.cap.sub = sub;
    return node;
}

ast_node_t* snobol_ast_create_span(const char* set, size_t len) {
    ast_node_t* node = (ast_node_t*)calloc(1, sizeof(ast_node_t));
    if (!node) return NULL;
    
    node->type = AST_SPAN;
    node->data.charclass.set = str_dup(set, len);
    node->data.charclass.len = len;
    return node;
}

ast_node_t* snobol_ast_create_any(const char* set, size_t len) {
    ast_node_t* node = (ast_node_t*)calloc(1, sizeof(ast_node_t));
    if (!node) return NULL;
    
    node->type = AST_ANY;
    if (set) {
        node->data.charclass.set = str_dup(set, len);
        node->data.charclass.len = len;
    } else {
        node->data.charclass.set = NULL;
        node->data.charclass.len = 0;
    }
    return node;
}

ast_node_t* snobol_ast_create_repeat(ast_node_t* sub, int32_t min, int32_t max) {
    ast_node_t* node = (ast_node_t*)calloc(1, sizeof(ast_node_t));
    if (!node) return NULL;
    
    node->type = AST_REPETITION;
    node->data.repetition.sub = sub;
    node->data.repetition.min = min;
    node->data.repetition.max = max;
    return node;
}

ast_node_t* snobol_ast_create_label(char* name, ast_node_t* target) {
    ast_node_t* node = (ast_node_t*)calloc(1, sizeof(ast_node_t));
    if (!node) {
        return NULL;
    }

    node->type = AST_LABEL;
    /* Make a copy of the name - caller may pass string literals */
    if (name) {
        size_t len = strlen(name);
        node->data.label.name = (char*)malloc(len + 1);
        if (node->data.label.name) {
            strcpy(node->data.label.name, name);
        }
        /* Note: We don't free 'name' - caller is responsible for it.
         * If caller passed a malloc'd string, they should free it.
         * If caller passed a string literal, we have our own copy. */
    } else {
        node->data.label.name = NULL;
    }
    node->data.label.target = target;
    return node;
}

void snobol_ast_free(ast_node_t* node) {
    if (!node) return;
    
    /* Free children based on type */
    switch (node->type) {
        case AST_CONCAT:
            for (size_t i = 0; i < node->data.concat.count; i++) {
                snobol_ast_free(node->data.concat.parts[i]);
            }
            free(node->data.concat.parts);
            break;
            
        case AST_ALT:
            snobol_ast_free(node->data.alt.left);
            snobol_ast_free(node->data.alt.right);
            break;

        case AST_REPETITION:
        case AST_ARBNO:
            snobol_ast_free(node->data.repetition.sub);
            break;
            
        case AST_CAP:
            snobol_ast_free(node->data.cap.sub);
            break;
            
        case AST_SPAN:
        case AST_BREAK:
        case AST_ANY:
        case AST_NOTANY:
            free(node->data.charclass.set);
            break;
            
        case AST_LITERAL:
        case AST_EMIT:
            free(node->data.literal.text);
            break;
            
        case AST_LABEL:
            free(node->data.label.name);
            snobol_ast_free(node->data.label.target);
            break;
            
        case AST_GOTO:
            free(node->data.goto_stmt.label);
            break;
            
        case AST_TABLE_ACCESS:
            free(node->data.table_access.table);
            snobol_ast_free(node->data.table_access.key);
            break;
            
        case AST_TABLE_UPDATE:
            free(node->data.table_update.table);
            snobol_ast_free(node->data.table_update.key);
            snobol_ast_free(node->data.table_update.value);
            break;
            
        case AST_DYNAMIC_EVAL:
            snobol_ast_free(node->data.dynamic_eval.expr);
            break;
            
        default:
            /* No children to free */
            break;
    }
    
    free(node);
}

const char* snobol_ast_type_name(ast_type_t type) {
    switch (type) {
        case AST_LITERAL:      return "LITERAL";
        case AST_CONCAT:       return "CONCAT";
        case AST_ALT:          return "ALT";
        case AST_REPETITION:   return "REPETITION";
        case AST_SPAN:         return "SPAN";
        case AST_BREAK:        return "BREAK";
        case AST_ANY:          return "ANY";
        case AST_NOTANY:       return "NOTANY";
        case AST_ARBNO:        return "ARBNO";
        case AST_CAP:          return "CAP";
        case AST_ASSIGN:       return "ASSIGN";
        case AST_LEN:          return "LEN";
        case AST_EVAL:         return "EVAL";
        case AST_DYNAMIC_EVAL: return "DYNAMIC_EVAL";
        case AST_ANCHOR:       return "ANCHOR";
        case AST_EMIT:         return "EMIT";
        case AST_LABEL:        return "LABEL";
        case AST_GOTO:         return "GOTO";
        case AST_TABLE_ACCESS: return "TABLE_ACCESS";
        case AST_TABLE_UPDATE: return "TABLE_UPDATE";
        default:               return "UNKNOWN";
    }
}

void snobol_ast_dump(const ast_node_t* node, FILE* out, int indent) {
    if (!node) {
        fprintf(out, "%*s(NULL)\n", indent, "");
        return;
    }
    
    const char* type_name = snobol_ast_type_name(node->type);
    
    switch (node->type) {
        case AST_LITERAL:
            fprintf(out, "%*sLITERAL \"%.*s\"\n", indent, "", 
                    (int)node->data.literal.len, node->data.literal.text);
            break;
            
        case AST_CONCAT:
            fprintf(out, "%*sCONCAT[%zu]\n", indent, "", node->data.concat.count);
            for (size_t i = 0; i < node->data.concat.count; i++) {
                snobol_ast_dump(node->data.concat.parts[i], out, indent + 2);
            }
            break;
            
        case AST_ALT:
            fprintf(out, "%*sALT\n", indent, "");
            fprintf(out, "%*sLEFT:\n", indent + 2, "");
            snobol_ast_dump(node->data.alt.left, out, indent + 4);
            fprintf(out, "%*sRIGHT:\n", indent + 2, "");
            snobol_ast_dump(node->data.alt.right, out, indent + 4);
            break;
            
        case AST_ARBNO:
            fprintf(out, "%*sARBNO\n", indent, "");
            snobol_ast_dump(node->data.arbno.sub, out, indent + 2);
            break;
            
        case AST_CAP:
            fprintf(out, "%*sCAP(v%d)\n", indent, "", node->data.cap.reg);
            snobol_ast_dump(node->data.cap.sub, out, indent + 2);
            break;
            
        case AST_SPAN:
            fprintf(out, "%*sSPAN(\"%.*s\")\n", indent, "", 
                    (int)node->data.charclass.len, node->data.charclass.set);
            break;
            
        case AST_ANY:
            if (node->data.charclass.set) {
                fprintf(out, "%*sANY(\"%.*s\")\n", indent, "",
                        (int)node->data.charclass.len, node->data.charclass.set);
            } else {
                fprintf(out, "%*sANY()\n", indent, "");
            }
            break;
            
        case AST_REPETITION:
            fprintf(out, "%*sREPEAT(%d,%d)\n", indent, "", 
                    node->data.repetition.min, node->data.repetition.max);
            snobol_ast_dump(node->data.repetition.sub, out, indent + 2);
            break;
            
        default:
            fprintf(out, "%*s%s\n", indent, "", type_name);
            break;
    }
}

/**
 * Get AST version information
 */
snobol_ast_version_t snobol_ast_get_version(void) {
    snobol_ast_version_t version = {
        .major = SNOBOL_AST_VERSION_MAJOR,
        .minor = SNOBOL_AST_VERSION_MINOR,
        .patch = SNOBOL_AST_VERSION_PATCH,
        .string = SNOBOL_AST_VERSION_STRING
    };
    return version;
}

/**
 * Check if AST library version is compatible
 */
bool snobol_ast_version_check(uint16_t required_major, uint16_t required_minor) {
    return SNOBOL_AST_VERSION_CHECK(required_major, required_minor);
}

/**
 * Get AST version as a string
 */
const char* snobol_ast_version_string(void) {
    return SNOBOL_AST_VERSION_STRING;
}
