#ifndef SNOBOL_AST_H
#define SNOBOL_AST_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/**
 * @file snobol_ast.h
 * @brief Abstract Syntax Tree representation for SNOBOL patterns
 *
 * Uses tagged union structs for type-safe AST nodes.
 * Memory ownership: parent nodes own child nodes recursively.
 * Caller is responsible for calling snobol_ast_free() to free entire tree.
 *
 * Version: 1.0.0
 * - Major: Breaking changes to AST structure or memory layout
 * - Minor: New node types or non-breaking additions
 * - Patch: Bug fixes, no API changes
 */

/**
 * AST version information
 * Use snobol_ast_get_version() to retrieve at runtime
 */
#define SNOBOL_AST_VERSION_MAJOR 1
#define SNOBOL_AST_VERSION_MINOR 0
#define SNOBOL_AST_VERSION_PATCH 0
#define SNOBOL_AST_VERSION_STRING "1.0.0"

/**
 * Check if AST version is compatible
 * Returns true if major version matches and minor version >= required
 */
#define SNOBOL_AST_VERSION_CHECK(major, minor) \
    ((major) == SNOBOL_AST_VERSION_MAJOR && (minor) <= SNOBOL_AST_VERSION_MINOR)

/**
 * AST version structure for runtime queries
 */
typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    const char* string;
} snobol_ast_version_t;

/**
 * AST node type tags
 */
typedef enum {
    /* Core pattern types */
    AST_LITERAL,      /* Literal string match */
    AST_CONCAT,       /* Concatenation of patterns */
    AST_ALT,          /* Alternation (pattern1 | pattern2) */
    AST_REPETITION,   /* Bounded repetition: repeat(P, min, max) */
    
    /* Character class types */
    AST_SPAN,         /* Match span of characters in set */
    AST_BREAK,        /* Match until break character */
    AST_ANY,          /* Match any character (optionally in set) */
    AST_NOTANY,       /* Match any character NOT in set */
    
    /* Quantifiers */
    AST_ARBNO,        /* Arbitrary number (zero or more) */
    
    /* Capture and assignment */
    AST_CAP,          /* Capture match into register */
    AST_ASSIGN,       /* Assign register to variable */
    
    /* Length matching */
    AST_LEN,          /* Match exactly N characters */
    
    /* Dynamic evaluation */
    AST_EVAL,         /* Evaluate function result as pattern */
    AST_DYNAMIC_EVAL, /* Dynamic pattern from source text */
    
    /* Anchors */
    AST_ANCHOR,       /* Start/end anchor */
    
    /* Output/emission */
    AST_EMIT,         /* Emit literal or capture to output */
    
    /* Control flow */
    AST_LABEL,        /* Label definition */
    AST_GOTO,         /* Goto label reference */
    
    /* Table operations */
    AST_TABLE_ACCESS, /* Table lookup */
    AST_TABLE_UPDATE  /* Table assignment */
} ast_type_t;

/**
 * Anchor type enumeration
 */
typedef enum {
    ANCHOR_START,     /* Beginning of string (^) */
    ANCHOR_END        /* End of string ($) */
} anchor_type_t;

/**
 * Forward declarations
 */
typedef struct ast_node ast_node_t;

/**
 * AST node - tagged union representation
 * 
 * Memory layout:
 * - type: identifies which union member is active
 * - data: union of node-specific data
 * - Children are owned by parent (freed recursively)
 */
struct ast_node {
    ast_type_t type;
    
    union {
        /* AST_LITERAL */
        struct {
            char* text;       /* Owned: literal string content */
            size_t len;       /* Length of text */
        } literal;
        
        /* AST_CONCAT */
        struct {
            ast_node_t** parts;  /* Owned array of child nodes */
            size_t count;        /* Number of parts */
        } concat;
        
        /* AST_ALT */
        struct {
            ast_node_t* left;    /* Owned: left alternative */
            ast_node_t* right;   /* Owned: right alternative */
        } alt;
        
        /* AST_REPETITION */
        struct {
            ast_node_t* sub;     /* Owned: sub-pattern to repeat */
            int32_t min;         /* Minimum repetitions (-1 = unbounded) */
            int32_t max;         /* Maximum repetitions (-1 = unbounded) */
        } repetition;
        
        /* AST_SPAN, AST_BREAK, AST_ANY, AST_NOTANY */
        struct {
            char* set;           /* Owned: character set string */
            size_t len;          /* Length of set */
        } charclass;
        
        /* AST_ARBNO */
        struct {
            ast_node_t* sub;     /* Owned: sub-pattern */
        } arbno;
        
        /* AST_CAP */
        struct {
            int reg;             /* Register number for capture */
            ast_node_t* sub;     /* Owned: sub-pattern to capture */
        } cap;
        
        /* AST_ASSIGN */
        struct {
            int var;             /* Variable number */
            int reg;             /* Register number */
        } assign;
        
        /* AST_LEN */
        struct {
            int32_t n;           /* Exact length to match */
        } len;
        
        /* AST_EVAL */
        struct {
            int fn;              /* Function ID */
            int reg;             /* Register for result */
        } eval;
        
        /* AST_DYNAMIC_EVAL */
        struct {
            ast_node_t* expr;    /* Owned: expression to evaluate */
        } dynamic_eval;
        
        /* AST_ANCHOR */
        struct {
            anchor_type_t atype; /* Start or end anchor */
        } anchor;
        
        /* AST_EMIT */
        struct {
            char* text;          /* Owned: literal text to emit (or NULL) */
            int reg;             /* Register to emit (-1 if text only) */
        } emit;
        
        /* AST_LABEL */
        struct {
            char* name;          /* Owned: label name */
            ast_node_t* target;  /* Owned: target pattern */
        } label;
        
        /* AST_GOTO */
        struct {
            char* label;         /* Owned: target label name */
        } goto_stmt;
        
        /* AST_TABLE_ACCESS */
        struct {
            char* table;         /* Owned: table name */
            ast_node_t* key;     /* Owned: key expression */
        } table_access;
        
        /* AST_TABLE_UPDATE */
        struct {
            char* table;         /* Owned: table name */
            ast_node_t* key;     /* Owned: key expression */
            ast_node_t* value;   /* Owned: value expression */
        } table_update;
    } data;
};

/**
 * Create a literal AST node
 * @param text Literal text (copied, must be freed by caller if not using create_lit)
 * @param len Length of text
 * @return New AST node (caller owns, must free with snobol_ast_free)
 */
ast_node_t* snobol_ast_create_lit(const char* text, size_t len);

/**
 * Create a concatenation AST node
 * @param parts Array of child nodes (ownership transferred)
 * @param count Number of parts
 * @return New AST node (caller owns)
 */
ast_node_t* snobol_ast_create_concat(ast_node_t** parts, size_t count);

/**
 * Create an alternation AST node
 * @param left Left alternative (ownership transferred)
 * @param right Right alternative (ownership transferred)
 * @return New AST node (caller owns)
 */
ast_node_t* snobol_ast_create_alt(ast_node_t* left, ast_node_t* right);

/**
 * Create an arbno (zero-or-more) AST node
 * @param sub Sub-pattern (ownership transferred)
 * @return New AST node (caller owns)
 */
ast_node_t* snobol_ast_create_arbno(ast_node_t* sub);

/**
 * Create a capture AST node
 * @param reg Register number
 * @param sub Sub-pattern to capture (ownership transferred)
 * @return New AST node (caller owns)
 */
ast_node_t* snobol_ast_create_cap(int reg, ast_node_t* sub);

/**
 * Create a span AST node
 * @param set Character set string (copied)
 * @param len Length of set
 * @return New AST node (caller owns)
 */
ast_node_t* snobol_ast_create_span(const char* set, size_t len);

/**
 * Create an any AST node
 * @param set Character set string (copied, or NULL for any char)
 * @param len Length of set (0 if NULL)
 * @return New AST node (caller owns)
 */
ast_node_t* snobol_ast_create_any(const char* set, size_t len);

/**
 * Create a repetition AST node
 * @param sub Sub-pattern (ownership transferred)
 * @param min Minimum repetitions
 * @param max Maximum repetitions (-1 for unbounded)
 * @return New AST node (caller owns)
 */
ast_node_t* snobol_ast_create_repeat(ast_node_t* sub, int32_t min, int32_t max);

/**
 * Create a label AST node
 * @param name Label name (copied, ownership transferred)
 * @param target Target pattern (ownership transferred)
 * @return New AST node (caller owns)
 */
ast_node_t* snobol_ast_create_label(char* name, ast_node_t* target);

/**
 * Free an AST node and all children recursively
 * @param node Node to free (NULL is safe)
 */
void snobol_ast_free(ast_node_t* node);

/**
 * Get a string representation of AST node type for debugging
 * @param type Node type
 * @return Static string (do not free)
 */
const char* snobol_ast_type_name(ast_type_t type);

/**
 * Dump AST to file for debugging
 * @param node Root node
 * @param out File to write to (e.g., stdout)
 * @param indent Current indentation level (0 for root)
 */
void snobol_ast_dump(const ast_node_t* node, FILE* out, int indent);

/**
 * Get AST version information
 * @return Version structure with major, minor, patch, and string representation
 */
snobol_ast_version_t snobol_ast_get_version(void);

/**
 * Check if AST library version is compatible with required version
 * @param required_major Required major version
 * @param required_minor Required minor version
 * @return true if compatible (same major, minor >= required)
 */
bool snobol_ast_version_check(uint16_t required_major, uint16_t required_minor);

/**
 * Get AST version as a string (e.g., "1.0.0")
 * @return Version string (static, do not free)
 */
const char* snobol_ast_version_string(void);

#endif /* SNOBOL_AST_H */
