#include "php.h"
#include "php_snobol.h"
#include "zend_exceptions.h"
#include "snobol/compiler.h"
#include "snobol/vm.h"
#include "snobol/lexer.h"
#include "snobol/parser.h"
#include "snobol/snobol.h"
#include "snobol/snobol_internal.h"
#include "snobol/search.h"
#include "snobol/table.h"

#include <stdio.h>
#include <time.h>
#include <stdarg.h>

/* Forward declaration: extract C table pointer from a PHP Snobol\Table zval */
extern snobol_table_t *php_snobol_get_table_from_zval(zval *zv);

/* DEBUG LOGGING DISABLED
static inline void snobol_log_impl(const char *file, int line, const char *fmt, ...) {
    FILE *f = fopen("/var/www/html/snobol_debug.log", "a");
    if (f) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);
        fprintf(f, "[%s] [%s:%d] ", ts, file, line); 
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fprintf(f, "\n");
        fflush(f);
        fclose(f);
    }
}
*/
/* No-op macro to disable logging */
#define SNOBOL_LOG(fmt, ...) ((void)0)

extern zend_class_entry *snobol_pattern_ce;
static zend_object_handlers snobol_pattern_object_handlers;

static void php_snobol_pattern_dtor(zend_object *object) {
    snobol_pattern_t *intern = php_snobol_fetch(object);
    SNOBOL_LOG("php_snobol_pattern_dtor: intern=%p, bc=%p", (void*)intern, (void*)intern->bc);

    if (intern->bc) {
        compiler_free(intern->bc);
        intern->bc = NULL;
    }

    zend_object_std_dtor(object);
    SNOBOL_LOG("php_snobol_pattern_dtor: done");
}

static zend_object *snobol_pattern_create(zend_class_entry *ce) {
    snobol_pattern_t *intern = zend_object_alloc(sizeof(snobol_pattern_t), ce);
    SNOBOL_LOG("snobol_pattern_create: intern=%p", (void*)intern);
    
    intern->bc = NULL;
    intern->bc_len = 0;
    
    zend_object_std_init(&intern->std, ce);
    object_properties_init(&intern->std, ce);
    intern->std.handlers = &snobol_pattern_object_handlers;
    
    return &intern->std;
}

/* argument info */
ZEND_BEGIN_ARG_INFO_EX(ai_compileFromAst, 0, 0, 1)
    ZEND_ARG_ARRAY_INFO(0, ast, 0)
    ZEND_ARG_ARRAY_INFO(0, options, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_fromString, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, source, IS_STRING, 0)
    ZEND_ARG_ARRAY_INFO(0, options, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_match, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, input, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_subst, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, subject, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, template, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, tables, IS_ARRAY, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_setEval, 0, 0, 1)
    ZEND_ARG_ARRAY_INFO(0, callbacks, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_setJit, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, enabled, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_searchAll, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, subject, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_matchLiteral, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, input, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_searchSplit, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, subject, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_searchReplace, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, subject, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, replacement, IS_STRING, 0)
ZEND_END_ARG_INFO()

/* PHP Methods */

PHP_METHOD(Snobol_Pattern, compileFromAst) {
    zval *ast;
    zval *options = NULL;
    ZEND_PARSE_PARAMETERS_START(1,2)
        Z_PARAM_ARRAY(ast)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_OR_NULL(options)
    ZEND_PARSE_PARAMETERS_END();

    uint8_t *bc = NULL;
    size_t bc_len = 0;

    SNOBOL_LOG("Snobol_Pattern::compileFromAst: START");

    if (compile_ast_to_bytecode(ast, options, &bc, &bc_len) != 0) {
        SNOBOL_LOG("Snobol_Pattern::compileFromAst: compilation FAILED");
        zend_throw_exception(zend_ce_exception, "Failed to compile AST", 0);
        RETURN_NULL();
    }

    if (object_init_ex(return_value, snobol_pattern_ce) != SUCCESS) {
        SNOBOL_LOG("Snobol_Pattern::compileFromAst: object_init_ex FAILED");
        if (bc) compiler_free(bc);
        RETURN_NULL();
    }

    snobol_pattern_t *intern = php_snobol_fetch(Z_OBJ_P(return_value));
    intern->bc = bc;
    intern->bc_len = bc_len;

    SNOBOL_LOG("Snobol_Pattern::compileFromAst: SUCCESS, intern=%p, bc=%p, len=%zu", (void*)intern, (void*)bc, bc_len);
}

/**
 * Pattern::fromString(string $source, ?array $options = null): Pattern
 * 
 * Parse and compile a pattern from source text using the C parser.
 * This is the new language-agnostic compilation path.
 */
PHP_METHOD(Snobol_Pattern, fromString) {
    zend_string *source;
    zval *options = NULL;
    ZEND_PARSE_PARAMETERS_START(1,2)
        Z_PARAM_STR(source)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_OR_NULL(options)
    ZEND_PARSE_PARAMETERS_END();

    SNOBOL_LOG("Snobol_Pattern::fromString: START, source='%.*s'", (int)ZSTR_LEN(source), ZSTR_VAL(source));

    /* Create lexer and parser */
    snobol_lexer_t* lexer = snobol_lexer_create(ZSTR_VAL(source), ZSTR_LEN(source));
    if (!lexer) {
        zend_throw_exception(zend_ce_exception, "Failed to create lexer", 0);
        RETURN_NULL();
    }

    snobol_parser_t* parser = snobol_parser_create();
    if (!parser) {
        snobol_lexer_destroy(lexer);
        zend_throw_exception(zend_ce_exception, "Failed to create parser", 0);
        RETURN_NULL();
    }

    /* Parse the source */
    ast_node_t* ast = snobol_parser_parse(parser, lexer);
    
    if (snobol_parser_has_error(parser)) {
        const char* error = snobol_parser_get_error(parser);
        size_t line, col;
        snobol_parser_get_error_location(parser, &line, &col);
        
        char msg[512];
        snprintf(msg, sizeof(msg), "Parse error at line %zu, column %zu: %s", line, col, error ? error : "unknown error");
        
        snobol_parser_destroy(parser);
        snobol_lexer_destroy(lexer);
        zend_throw_exception(zend_ce_exception, msg, 0);
        RETURN_NULL();
    }

    if (!ast) {
        snobol_parser_destroy(parser);
        snobol_lexer_destroy(lexer);
        zend_throw_exception(zend_ce_exception, "Parser returned NULL AST", 0);
        RETURN_NULL();
    }

    /* Extract caseInsensitive option */
    bool case_insensitive = false;
    if (options && Z_TYPE_P(options) == IS_ARRAY) {
        zval *ci = zend_hash_str_find(Z_ARRVAL_P(options), "caseInsensitive", sizeof("caseInsensitive") - 1);
        if (ci && zend_is_true(ci)) {
            case_insensitive = true;
        }
    }

    /* Compile AST to bytecode */
    uint8_t *bc = NULL;
    size_t bc_len = 0;

    if (compile_ast_to_bytecode_c(ast, case_insensitive, &bc, &bc_len) != 0) {
        SNOBOL_LOG("Snobol_Pattern::fromString: compilation FAILED");
        snobol_ast_free(ast);
        snobol_parser_destroy(parser);
        snobol_lexer_destroy(lexer);
        zend_throw_exception(zend_ce_exception, "Failed to compile AST", 0);
        RETURN_NULL();
    }

    /* Free AST - bytecode is now compiled */
    snobol_ast_free(ast);
    snobol_parser_destroy(parser);
    snobol_lexer_destroy(lexer);

    /* Create Pattern object */
    if (object_init_ex(return_value, snobol_pattern_ce) != SUCCESS) {
        SNOBOL_LOG("Snobol_Pattern::fromString: object_init_ex FAILED");
        if (bc) compiler_free(bc);
        RETURN_NULL();
    }

    snobol_pattern_t *intern = php_snobol_fetch(Z_OBJ_P(return_value));
    intern->bc = bc;
    intern->bc_len = bc_len;

    SNOBOL_LOG("Snobol_Pattern::fromString: SUCCESS, intern=%p, bc=%p, len=%zu", (void*)intern, (void*)bc, bc_len);
}

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} EmitBuf;

static void php_snobol_emit_cb(const char *data, size_t len, void *udata) {
    EmitBuf *eb = (EmitBuf *)udata;
    if (eb->len + len > eb->cap) {
        size_t new_cap = eb->cap ? eb->cap * 2 : 1024;
        while (eb->len + len > new_cap) new_cap *= 2;
        eb->buf = erealloc(eb->buf, new_cap);
        eb->cap = new_cap;
    }
    memcpy(eb->buf + eb->len, data, len);
    eb->len += len;
}

PHP_METHOD(Snobol_Pattern, match) {
    zend_string *input;
    ZEND_PARSE_PARAMETERS_START(1,1)
        Z_PARAM_STR(input)
    ZEND_PARSE_PARAMETERS_END();

    snobol_pattern_t *intern = php_snobol_fetch(Z_OBJ_P(ZEND_THIS));
    SNOBOL_LOG("Snobol_Pattern::match: START, intern=%p, bc=%p, input_len=%zu", (void*)intern, (void*)intern->bc, ZSTR_LEN(input));

    if (!intern->bc || intern->bc_len == 0) {
        SNOBOL_LOG("Snobol_Pattern::match: ABORT, no bytecode");
        zend_throw_exception(zend_ce_exception, "Pattern not compiled", 0);
        RETURN_FALSE;
    }

    /* Fast path: literal-only pattern — skip VM setup entirely.
     * Only safe when there are NO position constraints (POS, RPOS) anywhere
     * in the bytecode, since we match at position 0 unconditionally. */
    {
        snobol_search_meta_t meta;
        snobol_search_derive_meta(intern->bc, intern->bc_len, &meta);
        if (meta.is_literal_only) {
            const uint8_t *bc = intern->bc;
            size_t bc_len = intern->bc_len;
            /* Scan entire bytecode for any POS/RPOS op — even after the
             * literal, position constraints can cause the VM to fail.
             * LIT bytecode layout: [0]:op [1..4]:lit_off(==9) [5..8]:lit_len [9..]:data */
            bool has_position_op = false;
            for (size_t i = 0; i < bc_len; ) {
                uint8_t op = bc[i];
                if (op == OP_POS || op == OP_RPOS) {
                    has_position_op = true;
                    break;
                }
                if (op == OP_LIT && i + 9 <= bc_len) {
                    uint32_t lit_len = ((uint32_t)bc[i + 5] << 24) |
                                       ((uint32_t)bc[i + 6] << 16) |
                                       ((uint32_t)bc[i + 7] << 8) | (uint32_t)bc[i + 8];
                    i += 9 + lit_len;
                    continue;
                }
                if (op == OP_NOP || op == OP_FENCE || op == OP_ANCHOR) { i++; continue; }
                if (op == OP_ACCEPT || op == OP_ABORT) { i++; continue; }
                break;
            }
            if (has_position_op) {
                SNOBOL_LOG("Snobol_Pattern::match: literal fast-path SKIPPED (position op)");
            } else {
                /* Extract the literal from bytecode */
                size_t ip = 0;
                while (ip < bc_len) {
                    uint8_t op = bc[ip];
                    if (op == OP_NOP || op == OP_FENCE || op == OP_ANCHOR) { ip++; continue; }
                    if ((op == OP_POS || op == OP_RPOS) && ip + 5 <= bc_len) { ip += 5; continue; }
                    break;
                }
                if (ip + 9 <= bc_len && bc[ip] == OP_LIT) {
                    uint32_t lit_off = ((uint32_t)bc[ip + 1] << 24) | ((uint32_t)bc[ip + 2] << 16) |
                                       ((uint32_t)bc[ip + 3] << 8) | (uint32_t)bc[ip + 4];
                    uint32_t lit_len = ((uint32_t)bc[ip + 5] << 24) | ((uint32_t)bc[ip + 6] << 16) |
                                       ((uint32_t)bc[ip + 7] << 8) | (uint32_t)bc[ip + 8];
                    const char *lit = (const char *)(bc + lit_off);
                    if (ZSTR_LEN(input) >= lit_len && memcmp(ZSTR_VAL(input), lit, lit_len) == 0) {
                        SNOBOL_LOG("Snobol_Pattern::match: literal fast-path SUCCESS");
                        array_init(return_value);
                        add_assoc_long(return_value, "_match_len", (zend_long)lit_len);
                        add_assoc_string(return_value, "_output", "");
                        return;
                    }
                    SNOBOL_LOG("Snobol_Pattern::match: literal fast-path NO MATCH");
                    RETURN_FALSE;
                }
            }
        }
    }

    EmitBuf eb = {NULL, 0, 0};

    VM vm;
    memset(&vm, 0, sizeof(VM));
    vm.bc = intern->bc;
    vm.bc_len = intern->bc_len;
    vm.s = ZSTR_VAL(input);
    vm.len = ZSTR_LEN(input);
    vm.emit_fn = php_snobol_emit_cb;
    vm.emit_udata = &eb;

#ifdef SNOBOL_DYNAMIC_PATTERN
    /* Initialize dynamic pattern cache for EVAL(...) support */
    dynamic_pattern_cache_t dyn_cache;
    if (dynamic_pattern_cache_init(&dyn_cache, 64)) {
        vm.dyn_cache = &dyn_cache;
    } else {
        vm.dyn_cache = NULL;
    }
    vm.dyn_pending_source = NULL;
    vm.dyn_pending_source_len = 0;
    vm.dyn_pending_bc = NULL;
    vm.dyn_pending_bc_len = 0;
#endif

#ifdef SNOBOL_DYNAMIC_PATTERN
    /* Initialize table registry for the VM */
    vm_init_tables(&vm);

    /* Bind any unbound table references in the bytecode using all named tables in PHP */
    snobol_table_t **php_tables = NULL;
    size_t tbl_count = php_snobol_get_all_tables(&php_tables);
    if (tbl_count > 0) {
        const char **names = (const char **)emalloc(tbl_count * sizeof(char *));
        uint16_t *ids = (uint16_t *)emalloc(tbl_count * sizeof(uint16_t));
        
        for (size_t k = 0; k < tbl_count; k++) {
            names[k] = php_tables[k]->name;
            /* Register table in VM and get its internal ID */
            vm_register_table(&vm, php_tables[k], &ids[k]);
            SNOBOL_LOG("Snobol_Pattern::match: registered table[%zu] name='%s' id=%u", k, names[k] ? names[k] : "(null)", ids[k]);
        }
        
        /* Bind the bytecode (in-place) */
        snobol_template_bind_tables((uint8_t *)vm.bc, vm.bc_len, names, ids, tbl_count);
        
        efree(names);
        efree(ids);
    }
#endif

    bool ok = vm_exec(&vm);

    SNOBOL_LOG("Snobol_Pattern::match: VM returned %d, pos=%zu, var_count=%zu", (int)ok, vm.pos, vm.var_count);

    if (!ok) {
        if (eb.buf) efree(eb.buf);
#ifdef SNOBOL_DYNAMIC_PATTERN
        vm_free_tables(&vm);
        vm_free_arrays(&vm);
        if (vm.dyn_cache) {
            dynamic_pattern_cache_destroy(vm.dyn_cache);
        }
        if (vm.dyn_pending_source) {
            efree(vm.dyn_pending_source);
        }
#endif
        RETURN_FALSE;
    }

    array_init(return_value);
    for (size_t i = 0; i < vm.var_count; ++i) {
        size_t a = vm.var_start[i];
        size_t b = vm.var_end[i];
        char key[32];
        snprintf(key, sizeof(key), "v%u", (unsigned)i);

        SNOBOL_LOG("  Capture %s: range [%zu, %zu]", key, a, b);

        if (b >= a && b <= vm.len) {
            add_assoc_stringl(return_value, key, vm.s + a, b - a);
        } else {
            add_assoc_null(return_value, key);
        }
    }
    add_assoc_long(return_value, "_match_len", (zend_long)vm.pos);

    if (eb.buf) {
        add_assoc_stringl(return_value, "_output", eb.buf, eb.len);
        efree(eb.buf);
    } else {
        add_assoc_string(return_value, "_output", "");
    }

    /* Attach VM metrics for observability */
    zval metrics;
    array_init(&metrics);
    add_assoc_long(&metrics, "choice_push_count", (zend_long)vm.choice_push_count);
    add_assoc_long(&metrics, "choice_allocated", (zend_long)vm.choice_allocated);
    add_assoc_long(&metrics, "choice_peak_depth", (zend_long)vm.choice_peak_depth);
    add_assoc_long(&metrics, "choice_peak_memory", (zend_long)vm.choice_peak_memory);
    snobol_assoc_zval(return_value, "_metrics", 8, &metrics);
    zval_ptr_dtor(&metrics);

#ifdef SNOBOL_DYNAMIC_PATTERN
    vm_free_tables(&vm);
    if (vm.dyn_cache) {
        dynamic_pattern_cache_destroy(vm.dyn_cache);
    }
    if (vm.dyn_pending_source) {
        efree(vm.dyn_pending_source);
    }
    if (vm.dyn_pending_bc) {
        efree(vm.dyn_pending_bc);
    }
#endif

    SNOBOL_LOG("Snobol_Pattern::match: DONE");
}

PHP_METHOD(Snobol_Pattern, subst) {
    zend_string *subject, *tpl_str;
    zval *tables_zval = NULL;
    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_STR(subject)
        Z_PARAM_STR(tpl_str)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_EX(tables_zval, 1, 0)
    ZEND_PARSE_PARAMETERS_END();

    snobol_pattern_t *intern = php_snobol_fetch(Z_OBJ_P(ZEND_THIS));
    if (!intern->bc || intern->bc_len == 0) {
        zend_throw_exception(zend_ce_exception, "Pattern not compiled", 0);
        RETURN_FALSE;
    }

    uint8_t *tpl_bc = NULL;
    size_t tpl_bc_len = 0;
    if (compile_template_to_bytecode(ZSTR_VAL(tpl_str), ZSTR_LEN(tpl_str), &tpl_bc, &tpl_bc_len) != 0) {
        zend_throw_exception(zend_ce_exception, "Failed to compile template", 0);
        RETURN_FALSE;
    }

#ifdef SNOBOL_DYNAMIC_PATTERN
    /* Collect tables from the optional third parameter and bind by name */
    snobol_table_t **php_tables = NULL;
    const char     **tbl_names  = NULL;
    uint16_t        *tbl_ids    = NULL;
    size_t           tbl_count  = 0;

    if (tables_zval && Z_TYPE_P(tables_zval) == IS_ARRAY) {
        HashTable *ht = Z_ARRVAL_P(tables_zval);
        size_t raw_count = zend_hash_num_elements(ht);
        if (raw_count > 0) {
            php_tables = (snobol_table_t **)emalloc(raw_count * sizeof(snobol_table_t *));
            tbl_names  = (const char **)emalloc(raw_count * sizeof(const char *));
            tbl_ids    = (uint16_t *)emalloc(raw_count * sizeof(uint16_t));
            zend_string *tbl_key;
            zval *entry;
            ZEND_HASH_FOREACH_STR_KEY_VAL(ht, tbl_key, entry) {
                if (!tbl_key) continue; /* skip integer-keyed entries */
                snobol_table_t *ct = php_snobol_get_table_from_zval(entry);
                if (ct) {
                    php_tables[tbl_count] = ct;
                    tbl_names[tbl_count]  = ZSTR_VAL(tbl_key); /* use PHP array key as binding name */
                    tbl_ids[tbl_count]    = (uint16_t)tbl_count;
                    tbl_count++;
                }
            } ZEND_HASH_FOREACH_END();
        }
    }

    /* Always bind (even with tbl_count==0) so unresolved table refs are detected */
    {
        int bind_rc = snobol_template_bind_tables(tpl_bc, tpl_bc_len,
                                                  tbl_names, tbl_ids, tbl_count);
        if (bind_rc != 0) {
            if (php_tables) { efree(php_tables); php_tables = NULL; }
            if (tbl_names)  { efree(tbl_names);  tbl_names  = NULL; }
            if (tbl_ids)    { efree(tbl_ids);    tbl_ids    = NULL; }
            compiler_free(tpl_bc);
            zend_throw_exception(zend_ce_exception,
                "Template references an unregistered table name", 0);
            RETURN_FALSE;
        }
    }
#endif /* SNOBOL_DYNAMIC_PATTERN */

    snobol_buf out;
    snobol_buf_init(&out);

    const char *subject_val = ZSTR_VAL(subject);
    size_t subject_len = ZSTR_LEN(subject);
    size_t last_match_end = 0;

    /* Derive search metadata once for this pattern */
    snobol_search_meta_t meta;
    snobol_search_derive_meta(intern->bc, intern->bc_len, &meta);

    /* Core search loop */
    size_t search_offset = 0;
    while (search_offset <= subject_len) {
        VM vm;
        memset(&vm, 0, sizeof(VM));
        vm.bc     = intern->bc;
        vm.bc_len = intern->bc_len;

#ifdef SNOBOL_DYNAMIC_PATTERN
        dynamic_pattern_cache_t dyn_cache;
        if (dynamic_pattern_cache_init(&dyn_cache, 64)) {
            vm.dyn_cache = &dyn_cache;
        } else {
            vm.dyn_cache = NULL;
        }
        vm.dyn_pending_source     = NULL;
        vm.dyn_pending_source_len = 0;
        vm.dyn_pending_bc         = NULL;
        vm.dyn_pending_bc_len     = 0;
#endif

        snobol_search_result_t match_result;
        bool found = snobol_search_exec(&vm, subject_val, subject_len,
                                         search_offset, &meta, NULL,
                                         &match_result, NULL);

#ifdef SNOBOL_DYNAMIC_PATTERN
        if (vm.dyn_cache) dynamic_pattern_cache_destroy(vm.dyn_cache);
        if (vm.dyn_pending_source) efree(vm.dyn_pending_source);
        if (vm.dyn_pending_bc)     efree(vm.dyn_pending_bc);
#endif

        if (!found) break;

        /* Append prefix */
        snobol_buf_append(&out, subject_val + last_match_end,
                          match_result.match_start - last_match_end);

        /* Switch VM to template bytecode */
        vm.bc     = tpl_bc;
        vm.bc_len = tpl_bc_len;
        vm.ip     = 0;
        vm.out    = &out;

#ifdef SNOBOL_DYNAMIC_PATTERN
        /* Register tables in the same sequential order as the bind step */
        if (tbl_count > 0) {
            vm_init_tables(&vm);
            for (size_t k = 0; k < tbl_count; k++) {
                uint16_t assigned_id;
                vm_register_table(&vm, php_tables[k], &assigned_id);
                (void)assigned_id;
            }
        }
#endif

        vm_run(&vm);

#ifdef SNOBOL_DYNAMIC_PATTERN
        if (tbl_count > 0) vm_free_tables(&vm);
        if (vm.array_count > 0) vm_free_arrays(&vm);
#endif

        /* Advance past the match */
        size_t match_len = match_result.match_end - match_result.match_start;
        if (match_len == 0) match_len = 1;
        search_offset = match_result.match_start + match_len;
        last_match_end = search_offset;

        if (search_offset > subject_len) break;
    }

    /* Append remainder */
    if (last_match_end < subject_len) {
        snobol_buf_append(&out, subject_val + last_match_end,
                          subject_len - last_match_end);
    }

#ifdef SNOBOL_DYNAMIC_PATTERN
    if (php_tables) efree(php_tables);
    if (tbl_names)  efree(tbl_names);
    if (tbl_ids)    efree(tbl_ids);
#endif

    if (tpl_bc) compiler_free(tpl_bc);

    RETVAL_STRINGL(out.data, out.len);
    snobol_buf_free(&out);
}

PHP_METHOD(Snobol_Pattern, setEvalCallbacks) {
    SNOBOL_LOG("Snobol_Pattern::setEvalCallbacks: CALLED");
    RETURN_TRUE;
}

PHP_METHOD(Snobol_Pattern, setJit) {
    bool enabled;
    ZEND_PARSE_PARAMETERS_START(1,1)
        Z_PARAM_BOOL(enabled)
    ZEND_PARSE_PARAMETERS_END();

    RETURN_TRUE;
}

/* -------------------------------------------------------------------------
 * Helper: initialise a VM from a snobol_pattern_t for search operations
 * (shared setup for searchAll / searchSplit / searchReplace)
 *
 * The VM struct contains many pointer and length fields (write_log,
 * choices, tables, dyn_cache, etc.). The caller declares a stack-allocated
 * VM and passes it here uninitialised, so we MUST memset it to a known
 * state before filling in the search-loop-specific fields. search_reset_vm
 * (in core/src/search.c) handles the per-candidate reset, but it only
 * touches the fields that change between candidates — pointer fields
 * like write_log, choices, etc. must be zeroed once at first init.
 *
 * Optimisation opportunity: callers can avoid this memset by using the
 * stateful snobol_pattern_search_ex() API, which manages the VM state
 * internally. That refactor is captured in the jit-search-perf-baseline
 * change's searchSplit/searchAll/searchReplace tasks.
 * ----------------------------------------------------------------------- */
static void php_snobol_init_vm_for_search(VM *vm,
                                           snobol_pattern_t *intern,
                                           EmitBuf *eb) {
    memset(vm, 0, sizeof(VM));
    vm->bc     = intern->bc;
    vm->bc_len = intern->bc_len;
    if (eb) {
        vm->emit_fn    = php_snobol_emit_cb;
        vm->emit_udata = eb;
    }
}

/* Core search loop shared by Pattern::searchAll and PatternHelper::matchAll.
 * Returns an array of match-result arrays (each with _match_len, _match_start, _output, _metrics).
 * The caller must pass a valid compiled pattern internals. */
void php_snobol_do_search_all(snobol_pattern_t *intern,
                               const char *subject_val, size_t subject_len,
                               zval *result) {
    snobol_search_meta_t meta;
    snobol_search_derive_meta(intern->bc, intern->bc_len, &meta);

    array_init(result);
    size_t search_offset = 0;

    VM vm;
    EmitBuf eb = {NULL, 0, 0};
    php_snobol_init_vm_for_search(&vm, intern, &eb);
    vm.keep_choices = true;

#ifdef SNOBOL_DYNAMIC_PATTERN
    dynamic_pattern_cache_t dyn_cache;
    if (dynamic_pattern_cache_init(&dyn_cache, 64)) vm.dyn_cache = &dyn_cache;
#endif

#define MAX_CHOICE_BYTES (1024 * 1024)

    while (search_offset <= subject_len) {
#ifdef SNOBOL_DYNAMIC_PATTERN
        vm.dyn_pending_source = NULL; vm.dyn_pending_source_len = 0;
        vm.dyn_pending_bc     = NULL; vm.dyn_pending_bc_len = 0;
#endif

        snobol_search_result_t match;
        bool found = snobol_search_exec(&vm, subject_val, subject_len,
                                         search_offset, &meta, NULL,
                                         &match, NULL);

#ifdef SNOBOL_DYNAMIC_PATTERN
        if (vm.dyn_pending_source) { efree(vm.dyn_pending_source); vm.dyn_pending_source = NULL; }
        if (vm.dyn_pending_bc)     { efree(vm.dyn_pending_bc);     vm.dyn_pending_bc     = NULL; }
#endif

        if (!found) break;

        zval match_arr;
        array_init(&match_arr);
        for (size_t i = 0; i < vm.var_count; ++i) {
            size_t a = vm.var_start[i];
            size_t b = vm.var_end[i];
            char key[32];
            snprintf(key, sizeof(key), "v%u", (unsigned)i);
            if (b >= a && b <= vm.len + match.match_start) {
                add_assoc_stringl(&match_arr, key,
                    subject_val + match.match_start + a, b - a);
            } else {
                add_assoc_null(&match_arr, key);
            }
        }
        add_assoc_long(&match_arr, "_match_len", (zend_long)match.match_end);
        add_assoc_long(&match_arr, "_match_start", (zend_long)match.match_start);
        if (eb.buf && eb.len > 0) {
            add_assoc_stringl(&match_arr, "_output", eb.buf, eb.len);
            eb.len = 0;
        } else {
            add_assoc_string(&match_arr, "_output", "");
        }

        zval metrics;
        array_init(&metrics);
        add_assoc_long(&metrics, "choice_push_count", (zend_long)vm.choice_push_count);
        add_assoc_long(&metrics, "choice_allocated", (zend_long)vm.choice_allocated);
        add_assoc_long(&metrics, "choice_peak_depth", (zend_long)vm.choice_peak_depth);
        add_assoc_long(&metrics, "choice_peak_memory", (zend_long)vm.choice_peak_memory);
        snobol_assoc_zval(&match_arr, "_metrics", 8, &metrics);
        zval_ptr_dtor(&metrics);

        add_next_index_zval(result, &match_arr);

        if (vm.choice_allocated > MAX_CHOICE_BYTES) {
            snobol_free(vm.choices); vm.choices = NULL;
            if (vm.write_log) { snobol_free(vm.write_log); vm.write_log = NULL; vm.write_log_cap = 0; }
        }

        size_t match_len = match.match_end - match.match_start;
        if (match_len == 0) match_len = 1;
        search_offset = match.match_start + match_len;
    }

    if (eb.buf) efree(eb.buf);
#ifdef SNOBOL_DYNAMIC_PATTERN
    if (vm.dyn_cache) dynamic_pattern_cache_destroy(vm.dyn_cache);
#endif
    vm.keep_choices = false;
    if (vm.choices) { snobol_free(vm.choices); vm.choices = NULL; }
    if (vm.write_log && vm.use_compact_choice) { vm_write_log_free(&vm); }
}

/**
 * Pattern::searchAll(string $subject): array
 *
 * Find all non-overlapping matches using one native C search loop.
 * Returns an array of match-result arrays (same structure as match()).
 */
PHP_METHOD(Snobol_Pattern, searchAll) {
    zend_string *subject;
    ZEND_PARSE_PARAMETERS_START(1,1)
        Z_PARAM_STR(subject)
    ZEND_PARSE_PARAMETERS_END();

    snobol_pattern_t *intern = php_snobol_fetch(Z_OBJ_P(ZEND_THIS));
    if (!intern->bc || intern->bc_len == 0) {
        zend_throw_exception(zend_ce_exception, "Pattern not compiled", 0);
        RETURN_FALSE;
    }

    php_snobol_do_search_all(intern, ZSTR_VAL(subject), ZSTR_LEN(subject), return_value);
}

/**
 * Pattern::matchLiteral(string $input): array
 *
 * Lightweight anchored literal pattern match. Returns a simple associative
 * array with {success: bool, position: int, length: int} — no captures,
 * no output, no VM metrics. Only works for literal-only patterns; returns
 * {success: false, position: 0, length: 0} for non-literal patterns.
 */
PHP_METHOD(Snobol_Pattern, matchLiteral) {
    zend_string *input;
    ZEND_PARSE_PARAMETERS_START(1,1)
        Z_PARAM_STR(input)
    ZEND_PARSE_PARAMETERS_END();

    snobol_pattern_t *intern = php_snobol_fetch(Z_OBJ_P(ZEND_THIS));
    if (!intern->bc || intern->bc_len == 0) {
        zend_throw_exception(zend_ce_exception, "Pattern not compiled", 0);
        RETURN_FALSE;
    }

    snobol_search_meta_t meta;
    snobol_search_derive_meta(intern->bc, intern->bc_len, &meta);

    array_init(return_value);
    if (!meta.is_literal_only) {
        add_assoc_bool(return_value, "success", 0);
        add_assoc_long(return_value, "position", 0);
        add_assoc_long(return_value, "length", 0);
        return;
    }

    /* Scan entire bytecode for any POS/RPOS op.
     * LIT bytecode layout: [0]:op [1..4]:lit_off(==9) [5..8]:lit_len [9..]:data */
    const uint8_t *bc = intern->bc;
    size_t bc_len = intern->bc_len;
    bool has_position_op = false;
    for (size_t i = 0; i < bc_len; ) {
        uint8_t op = bc[i];
        if (op == OP_POS || op == OP_RPOS) {
            has_position_op = true;
            break;
        }
        if (op == OP_LIT && i + 9 <= bc_len) {
            uint32_t lit_len = ((uint32_t)bc[i + 5] << 24) |
                               ((uint32_t)bc[i + 6] << 16) |
                               ((uint32_t)bc[i + 7] << 8) | (uint32_t)bc[i + 8];
            i += 9 + lit_len;
            continue;
        }
        if (op == OP_NOP || op == OP_FENCE || op == OP_ANCHOR) { i++; continue; }
        if (op == OP_ACCEPT || op == OP_ABORT) { i++; continue; }
        break;
    }

    zend_long pos = 0, len = 0;
    bool matched = false;
    if (!has_position_op) {
        size_t ip = 0;
        while (ip < bc_len) {
            uint8_t op = bc[ip];
            if (op == OP_NOP || op == OP_FENCE || op == OP_ANCHOR) { ip++; continue; }
            if ((op == OP_POS || op == OP_RPOS) && ip + 5 <= bc_len) { ip += 5; continue; }
            break;
        }
        if (ip + 9 <= bc_len && bc[ip] == OP_LIT) {
            uint32_t lit_off = ((uint32_t)bc[ip + 1] << 24) | ((uint32_t)bc[ip + 2] << 16) |
                               ((uint32_t)bc[ip + 3] << 8) | (uint32_t)bc[ip + 4];
            uint32_t lit_len = ((uint32_t)bc[ip + 5] << 24) | ((uint32_t)bc[ip + 6] << 16) |
                               ((uint32_t)bc[ip + 7] << 8) | (uint32_t)bc[ip + 8];
            const char *lit = (const char *)(bc + lit_off);
            if (ZSTR_LEN(input) >= lit_len && memcmp(ZSTR_VAL(input), lit, lit_len) == 0) {
                matched = true;
                len = (zend_long)lit_len;
            }
        }
    }

    add_assoc_bool(return_value, "success", matched ? 1 : 0);
    add_assoc_long(return_value, "position", pos);
    add_assoc_long(return_value, "length", len);
}

/**
 * Pattern::searchSplit(string $subject): array
 *
 * Split a subject on non-overlapping pattern matches using one native C loop.
 * Returns an array of string segments between matches (same semantics as
 * PatternHelper::split).
 *
 * For small subjects (< SNOBOL_SEARCHSPLIT_BULK_THRESHOLD bytes) the
 * implementation uses a single-pass add_next_index_stringl loop — the
 * per-iteration overhead is amortised over very few matches and a
 * pre-pass would be net-negative.
 *
 * For larger subjects the implementation uses a two-pass approach:
 *   1. Pre-pass: count matches and record (start, end) pairs.
 *   2. Allocate a single C buffer of subject_len bytes and memcpy every
 *      segment from the subject into the buffer at a running cursor.
 *   3. Wrap the buffer in one parent zend_string, pre-size the result
 *      hash table to N+1 slots, and insert each segment as a
 *      zend_string_init over a sub-range of the parent (which copies
 *      once per child but reuses the parent's already-copied bytes).
 *
 * The result array is byte-for-byte identical to the small-subject path
 * (and to the pre-change implementation).
 */
#define SNOBOL_SEARCHSPLIT_BULK_THRESHOLD (1024 * 1024)
#define SNOBOL_SEARCHSPLIT_REC_STACK_CAP 16

typedef struct {
    size_t start;
    size_t end;
} snobol_match_record_t;

static inline size_t snobol_searchsplit_advance_len(size_t m) {
    return m == 0 ? 1 : m;
}

/* Bulk-result path for large subjects. Extracted into its own
 * translation-unit function so the small-subject fast path in
 * PHP_METHOD(Snobol_Pattern, searchSplit) does not pay the I-cache
 * cost of the bulk path's ~120 lines. Returns true on success;
 * on OOM, throws and returns false (state is destroyed by caller
 * regardless, return_value is left untouched on the false path so
 * the caller can RETURN_FALSE). */
static bool snobol_searchsplit_bulk_path(
    snobol_pattern_search_state_t *state,
    const char *subject_val, size_t subject_len,
    zval *return_value)
{
    snobol_match_record_t stack_recs[SNOBOL_SEARCHSPLIT_REC_STACK_CAP];
    snobol_match_record_t *recs      = stack_recs;
    size_t                 rec_count = 0;
    size_t                 rec_cap   = SNOBOL_SEARCHSPLIT_REC_STACK_CAP;

    size_t search_offset  = 0;
    size_t last_match_end = 0;
    size_t match_start    = 0;
    size_t match_len      = 0;

    while (search_offset <= subject_len) {
        snobol_match_t *m = snobol_pattern_search_ex(state, subject_val, subject_len,
                                                      search_offset);
        if (!m || !snobol_match_success(m)) {
            break;
        }

        match_start = snobol_match_get_position(m);
        match_len   = snobol_match_get_length(m);

        if (rec_count == rec_cap) {
            size_t new_cap = rec_cap * 2;
            if (recs == stack_recs) {
                recs = emalloc(new_cap * sizeof(snobol_match_record_t));
                memcpy(recs, stack_recs, rec_count * sizeof(snobol_match_record_t));
            } else {
                recs = erealloc(recs, new_cap * sizeof(snobol_match_record_t));
            }
            rec_cap = new_cap;
        }
        recs[rec_count].start = match_start;
        recs[rec_count].end   = match_start + snobol_searchsplit_advance_len(match_len);
        rec_count++;

        match_len      = snobol_searchsplit_advance_len(match_len);
        search_offset  = match_start + match_len;
        last_match_end = search_offset;
    }

    /* Allocate one contiguous buffer for every segment, memcpy each
     * segment from the subject. The buffer is at most subject_len
     * bytes (the trailing segment alone is at most subject_len, and
     * all interior segments sum to the same). */
    char *buf = emalloc(subject_len + 1);
    if (!buf) {
        if (recs != stack_recs) efree(recs);
        zend_throw_exception(zend_ce_exception, "Out of memory", 0);
        return false;
    }

    size_t cur = 0;
    last_match_end = 0;
    for (size_t i = 0; i < rec_count; i++) {
        size_t seg_len = recs[i].start - last_match_end;
        if (seg_len > 0) {
            memcpy(buf + cur, subject_val + last_match_end, seg_len);
            cur += seg_len;
        }
        last_match_end = recs[i].end;
    }
    /* Trailing segment */
    size_t tail_len = subject_len - last_match_end;
    if (tail_len > 0) {
        memcpy(buf + cur, subject_val + last_match_end, tail_len);
        cur += tail_len;
    }
    buf[cur] = '\0';

    /* Build the result array: a single array_init + a single C buffer
     * backing all segments. The Zend hash table grows at most O(log N)
     * times across the N+1 inserts (Zend's growth policy); the previous
     * implementation re-allocated on every add_next_index_stringl, so
     * the bulk path wins on the hash-table rehashes alone. */
    array_init(return_value);

    zend_string *parent = zend_string_init(buf, cur, 0);
    /* zend_string_init took ownership of buf; do not efree(buf) here. */

    /* Track each segment's start offset in `parent->val` so the
     * insert loop can construct child zend_strings by sub-range
     * without re-walking the subject. */
    size_t stack_offs[SNOBOL_SEARCHSPLIT_REC_STACK_CAP + 1];
    size_t *offs = (rec_count + 1 > SNOBOL_SEARCHSPLIT_REC_STACK_CAP + 1)
                       ? emalloc((rec_count + 1) * sizeof(size_t))
                       : stack_offs;
    size_t off_cur = 0;
    size_t last_end = 0;
    for (size_t i = 0; i < rec_count; i++) {
        offs[i] = off_cur;
        size_t seg_len = recs[i].start - last_end;
        off_cur += seg_len;
        last_end = recs[i].end;
    }
    offs[rec_count] = off_cur; /* trailing segment offset */

    /* Insert N+1 segments */
    for (size_t i = 0; i <= rec_count; i++) {
        size_t seg_off = offs[i];
        size_t seg_len = (i < rec_count)
                             ? recs[i].start - (i == 0 ? 0 : recs[i - 1].end)
                             : subject_len - (rec_count > 0 ? recs[rec_count - 1].end : 0);
        if (seg_len == 0) {
            /* Empty segment: insert an empty string to preserve indices */
            zval tmp;
            ZVAL_EMPTY_STRING(&tmp);
            zend_hash_next_index_insert(Z_ARRVAL_P(return_value), &tmp);
        } else {
            zval tmp;
            ZVAL_STR(&tmp, zend_string_init(parent->val + seg_off, seg_len, 0));
            zend_hash_next_index_insert(Z_ARRVAL_P(return_value), &tmp);
        }
    }

    /* The parent zend_string is now ref-counted by the array's slot
     * count; release our reference so it is freed when the array is
     * destroyed. */
    zend_string_release(parent);

    if (offs != stack_offs) efree(offs);
    if (recs != stack_recs) efree(recs);
    return true;
}

PHP_METHOD(Snobol_Pattern, searchSplit) {
    zend_string *subject;
    ZEND_PARSE_PARAMETERS_START(1,1)
        Z_PARAM_STR(subject)
    ZEND_PARSE_PARAMETERS_END();

    snobol_pattern_t *intern = php_snobol_fetch(Z_OBJ_P(ZEND_THIS));
    if (!intern->bc || intern->bc_len == 0) {
        zend_throw_exception(zend_ce_exception, "Pattern not compiled", 0);
        RETURN_FALSE;
    }

    const char *subject_val = ZSTR_VAL(subject);
    size_t subject_len      = ZSTR_LEN(subject);

    /* JIT-search-perf-baseline: use the stateful search API.
     * snobol_pattern_search_ex amortises the per-iteration cost of
     * VM init, JIT context lookup, and search metadata derivation.
     * The state takes raw bytecode (bc, bc_len) so it works with both
     * the C API's and the PHP binding's pattern structs. */
    snobol_pattern_search_state_t *state =
        snobol_pattern_search_state_create(intern->bc, intern->bc_len);
    if (!state) {
        zend_throw_exception(zend_ce_exception, "Out of memory", 0);
        RETURN_FALSE;
    }

    if (subject_len < SNOBOL_SEARCHSPLIT_BULK_THRESHOLD) {
        /* Fast path: identical to the pre-change implementation. */
        array_init(return_value);
        size_t search_offset  = 0;
        size_t last_match_end = 0;

        while (search_offset <= subject_len) {
            snobol_match_t *m = snobol_pattern_search_ex(state, subject_val, subject_len,
                                                          search_offset);
            if (!m || !snobol_match_success(m)) {
                break;
            }

            size_t match_start = snobol_match_get_position(m);
            size_t match_len   = snobol_match_get_length(m);

            add_next_index_stringl(return_value,
                subject_val + last_match_end,
                match_start - last_match_end);

            if (match_len == 0) match_len = 1;
            search_offset  = match_start + match_len;
            last_match_end = search_offset;
        }

        snobol_pattern_search_state_destroy(state);

        add_next_index_stringl(return_value,
            subject_val + last_match_end,
            subject_len - last_match_end);
        return;
    }

    /* Bulk path: extracted into a separate function so the fast
     * path above does not pay the I-cache cost of the bulk
     * implementation's ~120 lines. */
    if (!snobol_searchsplit_bulk_path(state, subject_val, subject_len, return_value)) {
        snobol_pattern_search_state_destroy(state);
        RETURN_FALSE;
    }
    snobol_pattern_search_state_destroy(state);
}

/**
 * Pattern::searchReplace(string $subject, string $replacement): string
 *
 * Replace non-overlapping pattern matches with a literal replacement using
 * one native C search loop.  For template-based substitution use subst().
 */
PHP_METHOD(Snobol_Pattern, searchReplace) {
    zend_string *subject, *replacement;
    ZEND_PARSE_PARAMETERS_START(2,2)
        Z_PARAM_STR(subject)
        Z_PARAM_STR(replacement)
    ZEND_PARSE_PARAMETERS_END();

    snobol_pattern_t *intern = php_snobol_fetch(Z_OBJ_P(ZEND_THIS));
    if (!intern->bc || intern->bc_len == 0) {
        zend_throw_exception(zend_ce_exception, "Pattern not compiled", 0);
        RETURN_FALSE;
    }

    const char *subject_val     = ZSTR_VAL(subject);
    size_t      subject_len     = ZSTR_LEN(subject);
    const char *repl_val        = ZSTR_VAL(replacement);
    size_t      repl_len        = ZSTR_LEN(replacement);

    /* JIT-search-perf-baseline: use the stateful search API. */
    snobol_pattern_search_state_t *state =
        snobol_pattern_search_state_create(intern->bc, intern->bc_len);
    if (!state) {
        zend_throw_exception(zend_ce_exception, "Out of memory", 0);
        RETURN_FALSE;
    }

    snobol_buf out;
    snobol_buf_init(&out);

    size_t search_offset  = 0;
    size_t last_match_end = 0;

    while (search_offset <= subject_len) {
        snobol_match_t *m = snobol_pattern_search_ex(state, subject_val, subject_len,
                                                      search_offset);
        if (!m || !snobol_match_success(m)) {
            break;
        }

        size_t match_start = snobol_match_get_position(m);
        size_t match_end   = match_start + snobol_match_get_length(m);

        /* Append prefix */
        snobol_buf_append(&out, subject_val + last_match_end,
                          match_start - last_match_end);
        /* Append replacement */
        snobol_buf_append(&out, repl_val, repl_len);

        size_t match_len = match_end - match_start;
        if (match_len == 0) match_len = 1;
        search_offset  = match_start + match_len;
        last_match_end = search_offset;
    }

    /* Append remainder */
    snobol_buf_append(&out, subject_val + last_match_end,
                      subject_len - last_match_end);

    RETVAL_STRINGL(out.data, out.len);
    snobol_buf_free(&out);
    snobol_pattern_search_state_destroy(state);
}

static const zend_function_entry snobol_pattern_methods[] = {
    PHP_ME(Snobol_Pattern, compileFromAst, ai_compileFromAst, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Pattern, fromString, ai_fromString, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Pattern, match, ai_match, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Pattern, subst, ai_subst, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Pattern, setEvalCallbacks, ai_setEval, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Pattern, setJit, ai_setJit, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Pattern, searchAll, ai_searchAll, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Pattern, matchLiteral, ai_matchLiteral, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Pattern, searchSplit, ai_searchSplit, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Pattern, searchReplace, ai_searchReplace, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

zend_class_entry *snobol_pattern_ce;

void snobol_pattern_minit(void) {
    SNOBOL_LOG("snobol_pattern_minit: START");
    zend_class_entry ce;
    
    memcpy(&snobol_pattern_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    snobol_pattern_object_handlers.offset = XtOffsetOf(snobol_pattern_t, std);
    snobol_pattern_object_handlers.free_obj = php_snobol_pattern_dtor;

    INIT_CLASS_ENTRY(ce, "Snobol\\Pattern", snobol_pattern_methods);
    snobol_pattern_ce = zend_register_internal_class(&ce);
    snobol_pattern_ce->create_object = snobol_pattern_create;
    SNOBOL_LOG("snobol_pattern_minit: DONE");
}

/* Forward declarations for PHP AST conversion */
static ast_node_t* php_ast_to_c(zval *php_ast);

/* Convert PHP AST array to C AST */
static ast_node_t* php_ast_to_c(zval *php_ast) {
    if (Z_TYPE_P(php_ast) != IS_ARRAY) {
        return NULL;
    }

    zval *type_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "type", sizeof("type")-1);
    if (!type_zv || Z_TYPE_P(type_zv) != IS_STRING) {
        return NULL;
    }

    const char *type = Z_STRVAL_P(type_zv);

    /* Convert based on type */
    if (strcmp(type, "lit") == 0) {
        zval *text_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "text", sizeof("text")-1);
        if (text_zv && Z_TYPE_P(text_zv) == IS_STRING) {
            return snobol_ast_create_literal(Z_STRVAL_P(text_zv), Z_STRLEN_P(text_zv));
        }
    }
    else if (strcmp(type, "concat") == 0) {
        zval *parts_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "parts", sizeof("parts")-1);
        if (parts_zv && Z_TYPE_P(parts_zv) == IS_ARRAY) {
            zend_array *parts = Z_ARRVAL_P(parts_zv);
            size_t count = zend_hash_num_elements(parts);
            ast_node_t **children = malloc(count * sizeof(ast_node_t*));
            if (!children) return NULL;

            zval *part;
            size_t i = 0;
            ZEND_HASH_FOREACH_VAL(parts, part) {
                children[i++] = php_ast_to_c(part);
            } ZEND_HASH_FOREACH_END();

            ast_node_t *node = snobol_ast_create_concat(children, count);
            /* Don't free children - concat node takes ownership */
            return node;
        }
    }
    else if (strcmp(type, "alt") == 0) {
        zval *left_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "left", sizeof("left")-1);
        zval *right_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "right", sizeof("right")-1);
        if (left_zv && right_zv) {
            ast_node_t *left = php_ast_to_c(left_zv);
            ast_node_t *right = php_ast_to_c(right_zv);
            if (left && right) {
                return snobol_ast_create_alt(left, right);
            }
        }
    }
    else if (strcmp(type, "span") == 0) {
        zval *set_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "set", sizeof("set")-1);
        if (set_zv && Z_TYPE_P(set_zv) == IS_STRING) {
            return snobol_ast_create_span(Z_STRVAL_P(set_zv), Z_STRLEN_P(set_zv));
        }
    }
    else if (strcmp(type, "break") == 0) {
        zval *set_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "set", sizeof("set")-1);
        if (set_zv && Z_TYPE_P(set_zv) == IS_STRING) {
            return snobol_ast_create_break(Z_STRVAL_P(set_zv), Z_STRLEN_P(set_zv));
        }
    }
    else if (strcmp(type, "any") == 0) {
        zval *set_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "set", sizeof("set")-1);
        if (set_zv && Z_TYPE_P(set_zv) == IS_STRING) {
            return snobol_ast_create_any(Z_STRVAL_P(set_zv), Z_STRLEN_P(set_zv));
        }
        return snobol_ast_create_any(NULL, 0);
    }
    else if (strcmp(type, "notany") == 0) {
        zval *set_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "set", sizeof("set")-1);
        if (set_zv && Z_TYPE_P(set_zv) == IS_STRING) {
            return snobol_ast_create_notany(Z_STRVAL_P(set_zv), Z_STRLEN_P(set_zv));
        }
    }
    else if (strcmp(type, "arbno") == 0) {
        zval *sub_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "sub", sizeof("sub")-1);
        if (sub_zv) {
            ast_node_t *sub = php_ast_to_c(sub_zv);
            if (sub) {
                return snobol_ast_create_arbno(sub);
            }
        }
    }
    else if (strcmp(type, "repeat") == 0) {
        zval *sub_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "sub", sizeof("sub")-1);
        zval *min_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "min", sizeof("min")-1);
        zval *max_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "max", sizeof("max")-1);
        if (sub_zv && min_zv && max_zv &&
            Z_TYPE_P(min_zv) == IS_LONG && Z_TYPE_P(max_zv) == IS_LONG) {
            ast_node_t *sub = php_ast_to_c(sub_zv);
            if (sub) {
                return snobol_ast_create_repeat(sub, Z_LVAL_P(min_zv), Z_LVAL_P(max_zv));
            }
        }
    }
    else if (strcmp(type, "cap") == 0) {
        zval *reg_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "reg", sizeof("reg")-1);
        zval *sub_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "sub", sizeof("sub")-1);
        if (reg_zv && sub_zv && Z_TYPE_P(reg_zv) == IS_LONG) {
            ast_node_t *sub = php_ast_to_c(sub_zv);
            if (sub) {
                return snobol_ast_create_cap(Z_LVAL_P(reg_zv), sub);
            }
        }
    }
    else if (strcmp(type, "assign") == 0) {
        zval *var_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "var", sizeof("var")-1);
        zval *reg_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "reg", sizeof("reg")-1);
        if (var_zv && reg_zv && Z_TYPE_P(var_zv) == IS_LONG && Z_TYPE_P(reg_zv) == IS_LONG) {
            return snobol_ast_create_assign(Z_LVAL_P(var_zv), Z_LVAL_P(reg_zv));
        }
    }
    else if (strcmp(type, "len") == 0) {
        zval *n_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "n", sizeof("n")-1);
        if (n_zv && Z_TYPE_P(n_zv) == IS_LONG) {
            return snobol_ast_create_len(Z_LVAL_P(n_zv));
        }
    }
    else if (strcmp(type, "anchor") == 0) {
        zval *atype_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "atype", sizeof("atype")-1);
        if (atype_zv && Z_TYPE_P(atype_zv) == IS_STRING) {
            const char *atype = Z_STRVAL_P(atype_zv);
            if (strcmp(atype, "start") == 0) {
                return snobol_ast_create_anchor(ANCHOR_START);
            } else if (strcmp(atype, "end") == 0) {
                return snobol_ast_create_anchor(ANCHOR_END);
            }
        }
    }
    else if (strcmp(type, "emit") == 0) {
        zval *text_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "text", sizeof("text")-1);
        zval *reg_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "reg", sizeof("reg")-1);
        if (text_zv && Z_TYPE_P(text_zv) == IS_STRING) {
            int reg = reg_zv && Z_TYPE_P(reg_zv) == IS_LONG ? Z_LVAL_P(reg_zv) : -1;
            return snobol_ast_create_emit(Z_STRVAL_P(text_zv), Z_STRLEN_P(text_zv), reg);
        } else if (reg_zv && Z_TYPE_P(reg_zv) == IS_LONG) {
            /* emitRef - only has reg, no text */
            return snobol_ast_create_emit(NULL, 0, Z_LVAL_P(reg_zv));
        }
    }
    else if (strcmp(type, "dynamic_eval") == 0) {
        zval *expr_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "expr", sizeof("expr")-1);
        if (expr_zv) {
            ast_node_t *expr = php_ast_to_c(expr_zv);
            if (expr) {
                return snobol_ast_create_dynamic_eval(expr);
            }
        }
    }
    else if (strcmp(type, "eval") == 0) {
        zval *fn_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "fn", sizeof("fn")-1);
        zval *reg_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "reg", sizeof("reg")-1);
        if (fn_zv && reg_zv && Z_TYPE_P(fn_zv) == IS_LONG && Z_TYPE_P(reg_zv) == IS_LONG) {
            return snobol_ast_create_eval((int)Z_LVAL_P(fn_zv), (int)Z_LVAL_P(reg_zv));
        }
    }
    else if (strcmp(type, "table_access") == 0) {
        zval *table_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "table", sizeof("table")-1);
        zval *key_zv   = zend_hash_str_find(Z_ARRVAL_P(php_ast), "key",   sizeof("key")-1);
        if (table_zv && key_zv && Z_TYPE_P(table_zv) == IS_STRING) {
            ast_node_t *key = php_ast_to_c(key_zv);
            if (key) {
                return snobol_ast_create_table_access(Z_STRVAL_P(table_zv), key);
            }
        }
    }
    else if (strcmp(type, "table_update") == 0) {
        zval *table_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "table", sizeof("table")-1);
        zval *key_zv   = zend_hash_str_find(Z_ARRVAL_P(php_ast), "key",   sizeof("key")-1);
        zval *val_zv   = zend_hash_str_find(Z_ARRVAL_P(php_ast), "value", sizeof("value")-1);
        if (table_zv && key_zv && val_zv && Z_TYPE_P(table_zv) == IS_STRING) {
            ast_node_t *key = php_ast_to_c(key_zv);
            ast_node_t *val = php_ast_to_c(val_zv);
            if (key && val) {
                return snobol_ast_create_table_update(Z_STRVAL_P(table_zv), key, val);
            }
        }
    }
    /* ---- Pattern primitives ---- */
    else if (strcmp(type, "breakx") == 0) {
        zval *set_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "set", sizeof("set")-1);
        if (set_zv && Z_TYPE_P(set_zv) == IS_STRING) {
            return snobol_ast_create_breakx(Z_STRVAL_P(set_zv), Z_STRLEN_P(set_zv));
        }
    }
    else if (strcmp(type, "bal") == 0) {
        zval *open_zv  = zend_hash_str_find(Z_ARRVAL_P(php_ast), "open",  sizeof("open")-1);
        zval *close_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "close", sizeof("close")-1);
        if (open_zv && close_zv &&
            Z_TYPE_P(open_zv) == IS_STRING && Z_TYPE_P(close_zv) == IS_STRING) {
            /* Decode first codepoint of each delimiter string */
            uint32_t open_cp = 0, close_cp = 0;
            int bytes = 0;
            if (!utf8_peek_next(Z_STRVAL_P(open_zv),  Z_STRLEN_P(open_zv),  0, &open_cp,  &bytes)) return NULL;
            if (!utf8_peek_next(Z_STRVAL_P(close_zv), Z_STRLEN_P(close_zv), 0, &close_cp, &bytes)) return NULL;
            return snobol_ast_create_bal(open_cp, close_cp);
        }
    }
    else if (strcmp(type, "fence") == 0) {
        return snobol_ast_create_fence();
    }
    else if (strcmp(type, "rem") == 0) {
        return snobol_ast_create_rem();
    }
    else if (strcmp(type, "rpos") == 0) {
        zval *n_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "n", sizeof("n")-1);
        if (n_zv && Z_TYPE_P(n_zv) == IS_LONG) {
            return snobol_ast_create_rpos((int32_t)Z_LVAL_P(n_zv));
        }
    }
    else if (strcmp(type, "rtab") == 0) {
        zval *n_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "n", sizeof("n")-1);
        if (n_zv && Z_TYPE_P(n_zv) == IS_LONG) {
            return snobol_ast_create_rtab((int32_t)Z_LVAL_P(n_zv));
        }
    }
    else if (strcmp(type, "pos") == 0) {
        zval *n_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "n", sizeof("n")-1);
        if (n_zv && Z_TYPE_P(n_zv) == IS_LONG) {
            return snobol_ast_create_pos((int32_t)Z_LVAL_P(n_zv));
        }
    }
    else if (strcmp(type, "tab") == 0) {
        zval *n_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "n", sizeof("n")-1);
        if (n_zv && Z_TYPE_P(n_zv) == IS_LONG) {
            return snobol_ast_create_tab((int32_t)Z_LVAL_P(n_zv));
        }
    }
    else if (strcmp(type, "abort") == 0) {
        return snobol_ast_create_abort();
    }
    else if (strcmp(type, "fail") == 0) {
        return snobol_ast_create_fail();
    }
    else if (strcmp(type, "succeed") == 0) {
        return snobol_ast_create_succeed();
    }
    /* ---- Control flow ---- */
    else if (strcmp(type, "label") == 0) {
        zval *name_zv   = zend_hash_str_find(Z_ARRVAL_P(php_ast), "name",   sizeof("name")-1);
        zval *target_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "target", sizeof("target")-1);
        if (name_zv && target_zv && Z_TYPE_P(name_zv) == IS_STRING) {
            ast_node_t *target = php_ast_to_c(target_zv);
            if (target) {
                return snobol_ast_create_label(Z_STRVAL_P(name_zv), target);
            }
        }
    }
    else if (strcmp(type, "goto") == 0) {
        zval *label_zv = zend_hash_str_find(Z_ARRVAL_P(php_ast), "label", sizeof("label")-1);
        if (label_zv && Z_TYPE_P(label_zv) == IS_STRING) {
            return snobol_ast_create_goto(Z_STRVAL_P(label_zv));
        }
    }

    return NULL;
}

/* PHP AST compilation - converts PHP Builder AST to C AST then compiles */
int compile_ast_to_bytecode(zval *ast, zval *options, uint8_t **out_bc, size_t *out_len) {
    /* Convert PHP AST to C AST */
    ast_node_t *c_ast = php_ast_to_c(ast);
    if (!c_ast) {
        zend_throw_exception(zend_ce_exception, "Failed to convert PHP AST to C AST", 0);
        return -1;
    }

    /* Extract case_insensitive option if present */
    bool case_insensitive = false;
    if (options && Z_TYPE_P(options) == IS_ARRAY) {
        zval *ci = zend_hash_str_find(Z_ARRVAL_P(options), "caseInsensitive", sizeof("caseInsensitive")-1);
        if (ci && (Z_TYPE_P(ci) == IS_TRUE || (Z_TYPE_P(ci) == IS_LONG && Z_LVAL_P(ci)))) {
            case_insensitive = true;
        }
    }

    /* Compile C AST to bytecode */
    int result = compile_ast_to_bytecode_c(c_ast, case_insensitive, out_bc, out_len);

    /* Free C AST */
    snobol_ast_free(c_ast);

    return result;
}

/* compile_template_to_bytecode is provided by the core (compiler.c / core_amalgam.c).
 * The PHP-side duplicate has been removed; template compilation is
 * handled entirely by the core. */
#if 0 /* REMOVED: duplicate template compiler – delegate to core */
int compile_template_to_bytecode_REMOVED(const char *tpl, size_t len, uint8_t **out_bc, size_t *out_len) {
    SNOBOL_LOG("compile_template_to_bytecode START: tpl='%.*s'", (int)len, tpl);
    CodeBuf cb;
    cb_init(&cb);

    size_t i = 0;
    while (i < len) {
        if (tpl[i] == '$') {
            size_t start_of_dollar = i;
            i++;
            if (i >= len) {
                cb_emit_u8(&cb, OP_EMIT_LITERAL);
                size_t off = cb_pos(&cb) + 4 + 4;
                cb_emit_u32(&cb, (uint32_t)off); cb_emit_u32(&cb, 1); cb_emit_u8(&cb, '$');
                break;
            }
            
            bool braced = (tpl[i] == '{');
            if (braced) i++;

            if (i < len && tpl[i] == 'v') {
                i++;
                uint8_t reg = 0;
                bool has_digits = false;
                while (i < len && tpl[i] >= '0' && tpl[i] <= '9') {
                    reg = reg * 10 + (tpl[i] - '0');
                    i++;
                    has_digits = true;
                }
                
                if (!has_digits) {
                    i = start_of_dollar + 1;
                    cb_emit_u8(&cb, OP_EMIT_LITERAL);
                    size_t off = cb_pos(&cb) + 4 + 4;
                    cb_emit_u32(&cb, (uint32_t)off); cb_emit_u32(&cb, 1); cb_emit_u8(&cb, '$');
                    continue;
                }

                uint8_t expr_type = 0;
                if (braced) {
                    if (i < len && tpl[i] == '.') {
                        i++;
                        if (len - i >= 7 && memcmp(tpl + i, "upper()", 7) == 0) {
                            expr_type = 1; i += 7;
                        } else if (len - i >= 8 && memcmp(tpl + i, "length()", 8) == 0) {
                            expr_type = 2; i += 8;
                        }
                    }
                    if (i < len && tpl[i] == '}') {
                        i++;
                        if (expr_type == 0) {
                            cb_emit_u8(&cb, OP_EMIT_CAPTURE); cb_emit_u8(&cb, reg);
                        } else {
                            cb_emit_u8(&cb, OP_EMIT_EXPR); cb_emit_u8(&cb, reg); cb_emit_u8(&cb, expr_type);
                        }
                    } else {
                        i = start_of_dollar + 1;
                        cb_emit_u8(&cb, OP_EMIT_LITERAL);
                        size_t off = cb_pos(&cb) + 4 + 4;
                        cb_emit_u32(&cb, (uint32_t)off); cb_emit_u32(&cb, 1); cb_emit_u8(&cb, '$');
                    }
                } else if (i < len && tpl[i] == '[') {
                    /* Table-backed replacement: $TABLE[key]
                     * Parse TABLE name and key, emit OP_EMIT_TABLE */
                    i++; /* skip '[' */
                    
                    /* Parse table name (identifier until '.' or '[') */
                    size_t table_name_start = i;
                    while (i < len && tpl[i] != '.' && tpl[i] != '[' && tpl[i] != ']') {
                        i++;
                    }
                    size_t table_name_len = i - table_name_start;
                    
                    if (table_name_len == 0 || i >= len || tpl[i] != '[') {
                        /* Invalid syntax, emit as literal '$' */
                        i = start_of_dollar + 1;
                        cb_emit_u8(&cb, OP_EMIT_LITERAL);
                        size_t off = cb_pos(&cb) + 4 + 4;
                        cb_emit_u32(&cb, (uint32_t)off); cb_emit_u32(&cb, 1); cb_emit_u8(&cb, '$');
                        continue;
                    }
                    
                    /* For now, table name must be a literal identifier */
                    /* Extract table name */
                    const char *table_name = tpl + table_name_start;
                    
                    /* Skip '[' and parse key */
                    i++; /* skip '[' */
                    size_t key_start = i;
                    
                    /* Key can be: quoted literal or identifier */
                    bool quoted = (i < len && tpl[i] == '\'');
                    if (quoted) {
                        i++; /* skip opening quote */
                        key_start = i;
                        while (i < len && tpl[i] != '\'') {
                            i++;
                        }
                        if (i >= len) {
                            /* Unclosed quote, emit as literal '$' */
                            i = start_of_dollar + 1;
                            cb_emit_u8(&cb, OP_EMIT_LITERAL);
                            size_t off = cb_pos(&cb) + 4 + 4;
                            cb_emit_u32(&cb, (uint32_t)off); cb_emit_u32(&cb, 1); cb_emit_u8(&cb, '$');
                            continue;
                        }
                        /* Key is from key_start to i (exclusive of closing quote) */
                        size_t key_len = i - key_start;
                        i++; /* skip closing quote */

                        /* Check for closing ']' */
                        if (i >= len || tpl[i] != ']') {
                            i = start_of_dollar + 1;
                            cb_emit_u8(&cb, OP_EMIT_LITERAL);
                            size_t off = cb_pos(&cb) + 4 + 4;
                            cb_emit_u32(&cb, (uint32_t)off); cb_emit_u32(&cb, 1); cb_emit_u8(&cb, '$');
                            continue;
                        }
                        i++; /* skip ']' */

                        /* Emit OP_EMIT_TABLE with literal key stored in bytecode
                         * Format: opcode u8, table_id u16, key_type u8 (0=literal), key_len u16, key_bytes...
                         * Table ID 0 means "resolve by name at runtime" - for now use placeholder */
                        cb_emit_u8(&cb, OP_EMIT_TABLE);
                        cb_emit_u16(&cb, 0); /* table_id placeholder - resolved at runtime */
                        cb_emit_u8(&cb, 0);  /* key_type: 0 = literal key */
                        cb_emit_u16(&cb, (uint16_t)key_len); /* literal key length */
                        cb_emit_bytes(&cb, (const uint8_t*)(tpl + key_start), key_len);
                    } else {
                        /* Identifier key (capture-derived) */
                        while (i < len && tpl[i] >= '0' && tpl[i] <= '9') {
                            i++;
                        }
                        if (i >= len || tpl[i] != ']') {
                            i = start_of_dollar + 1;
                            cb_emit_u8(&cb, OP_EMIT_LITERAL);
                            size_t off = cb_pos(&cb) + 4 + 4;
                            cb_emit_u32(&cb, (uint32_t)off); cb_emit_u32(&cb, 1); cb_emit_u8(&cb, '$');
                            continue;
                        }
                        size_t key_reg = 0;
                        /* Parse register number from identifier like v0, v1, etc. */
                        if (key_start > 0 && tpl[key_start - 1] == 'v') {
                            const char *reg_start = tpl + key_start;
                            key_reg = (uint8_t)(reg_start[0] - '0');
                        }
                        i++; /* skip ']' */

                        /* Emit OP_EMIT_TABLE with capture-derived key
                         * Format: opcode u8, table_id u16, key_type u8 (1=capture), key_reg u8 */
                        cb_emit_u8(&cb, OP_EMIT_TABLE);
                        cb_emit_u16(&cb, 0); /* table_id placeholder - needs resolution */
                        cb_emit_u8(&cb, 1);  /* key_type: 1 = capture-derived key */
                        cb_emit_u8(&cb, (uint8_t)key_reg);
                    }
                } else {
                    cb_emit_u8(&cb, OP_EMIT_CAPTURE); cb_emit_u8(&cb, reg);
                }
            } else {
                i = start_of_dollar + 1;
                cb_emit_u8(&cb, OP_EMIT_LITERAL);
                size_t off = cb_pos(&cb) + 4 + 4;
                cb_emit_u32(&cb, (uint32_t)off); cb_emit_u32(&cb, 1); cb_emit_u8(&cb, '$');
            }
        } else {
            // scan literal segment
            size_t start = i;
            while (i < len && tpl[i] != '$') i++;
            size_t seglen = i - start;
            cb_emit_u8(&cb, OP_EMIT_LITERAL);
            size_t off = cb_pos(&cb) + 4 + 4;
            cb_emit_u32(&cb, (uint32_t)off);
            cb_emit_u32(&cb, (uint32_t)seglen);
            cb_emit_bytes(&cb, (const uint8_t*)tpl + start, seglen);
        }
    }

    cb_emit_u8(&cb, OP_ACCEPT);

    uint8_t *out = snobol_malloc(cb.len);
    if (!out) { cb_free(&cb); return -1; }
    memcpy(out, cb.buf, cb.len);
    *out_bc = out;
    *out_len = cb.len;

    cb_free(&cb);
    SNOBOL_LOG("compile_template_to_bytecode SUCCESS, len=%zu", *out_len);
    return 0;
}
#endif /* REMOVED */
