#include "php.h"
#include "php_snobol.h"
#include "zend_exceptions.h"
#include "snobol/compiler.h"
#include "snobol/vm.h"
#include "snobol/lexer.h"
#include "snobol/parser.h"
#include "../core/src/snobol_internal.h"
#ifdef SNOBOL_JIT
#include "snobol/jit.h"
#endif

#include <stdio.h>
#include <time.h>
#include <stdarg.h>

/* Code buffer for template compilation */
typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
} CodeBuf;

static void cb_init(CodeBuf *c) {
    c->cap = 4096;
    c->buf = snobol_malloc(c->cap);
    c->len = 0;
}
static void cb_free(CodeBuf *c) {
    if (c->buf) {
        snobol_free(c->buf);
        c->buf = NULL;
    }
    c->cap = c->len = 0;
}
static void cb_ensure(CodeBuf *c, size_t need) {
    if (c->len + need <= c->cap) return;
    size_t newcap = c->cap ? c->cap * 2 : 4096;
    while (c->len + need > newcap) newcap *= 2;
    c->buf = snobol_realloc(c->buf, newcap);
    c->cap = newcap;
}
static size_t cb_pos(CodeBuf *c) { return c->len; }
static void cb_emit_u8(CodeBuf *c, uint8_t v) { cb_ensure(c,1); c->buf[c->len++] = v; }
static void cb_emit_u16(CodeBuf *c, uint16_t v) { cb_ensure(c,2); c->buf[c->len++] = (v >> 8) & 0xff; c->buf[c->len++] = v & 0xff; }
static void cb_emit_u32(CodeBuf *c, uint32_t v) { cb_ensure(c,4); c->buf[c->len++] = (v >> 24) & 0xff; c->buf[c->len++] = (v >> 16) & 0xff; c->buf[c->len++] = (v >> 8) & 0xff; c->buf[c->len++] = v & 0xff; }
static void cb_emit_bytes(CodeBuf *c, const uint8_t *b, size_t n) { if (n==0) return; cb_ensure(c,n); memcpy(c->buf + c->len, b, n); c->len += n; }

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

/* Standard PHP Custom Object Pattern: zend_object at the END */
typedef struct {
    uint8_t *bc;
    size_t bc_len;
#ifdef SNOBOL_JIT
    struct SnobolJitContext *jit_ctx;
    bool jit_enabled;
#endif
    zend_object std;
} snobol_pattern_t;

extern zend_class_entry *snobol_pattern_ce;
static zend_object_handlers snobol_pattern_object_handlers;

static inline snobol_pattern_t* php_snobol_fetch(zend_object *obj) {
    return (snobol_pattern_t *)((char *)(obj) - XtOffsetOf(snobol_pattern_t, std));
}

static void snobol_pattern_free(zend_object *object) {
    snobol_pattern_t *intern = php_snobol_fetch(object);
    SNOBOL_LOG("snobol_pattern_free: intern=%p, bc=%p", (void*)intern, (void*)intern->bc);
    
    if (intern->bc) {
        compiler_free(intern->bc);
        intern->bc = NULL;
    }

#ifdef SNOBOL_JIT
    if (intern->jit_ctx) {
        snobol_jit_release_context(intern->jit_ctx);
        intern->jit_ctx = NULL;
    }
#endif
    
    zend_object_std_dtor(object);
    SNOBOL_LOG("snobol_pattern_free: done");
}

static zend_object *snobol_pattern_create(zend_class_entry *ce) {
    snobol_pattern_t *intern = zend_object_alloc(sizeof(snobol_pattern_t), ce);
    SNOBOL_LOG("snobol_pattern_create: intern=%p", (void*)intern);
    
    intern->bc = NULL;
    intern->bc_len = 0;
#ifdef SNOBOL_JIT
    intern->jit_ctx = NULL;
    intern->jit_enabled = true;
#endif
    
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
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_setEval, 0, 0, 1)
    ZEND_ARG_ARRAY_INFO(0, callbacks, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_setJit, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, enabled, _IS_BOOL, 0)
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

#ifdef SNOBOL_JIT
    intern->jit_ctx = snobol_jit_acquire_context(bc, bc_len);
#endif

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

    /* Compile AST to bytecode */
    uint8_t *bc = NULL;
    size_t bc_len = 0;
    
    if (compile_ast_to_bytecode_c(ast, false, &bc, &bc_len) != 0) {
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

#ifdef SNOBOL_JIT
    intern->jit_ctx = snobol_jit_acquire_context(bc, bc_len);
#endif

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

    EmitBuf eb = {NULL, 0, 0};

    VM vm;
    memset(&vm, 0, sizeof(VM));
    vm.bc = intern->bc;
    vm.bc_len = intern->bc_len;
    vm.s = ZSTR_VAL(input);
    vm.len = ZSTR_LEN(input);
    vm.emit_fn = php_snobol_emit_cb;
    vm.emit_udata = &eb;

#ifdef SNOBOL_JIT
    if (intern->jit_ctx) {
        vm.jit.ip_counts = intern->jit_ctx->ip_counts;
        vm.jit.traces = intern->jit_ctx->traces;
        vm.jit.ctx = intern->jit_ctx;
    }
    vm.jit.enabled = intern->jit_enabled;
    vm.jit.stats = snobol_jit_get_stats();
#endif

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

    bool ok = vm_exec(&vm);

    SNOBOL_LOG("Snobol_Pattern::match: VM returned %d, pos=%zu, var_count=%zu", (int)ok, vm.pos, vm.var_count);

    if (!ok) {
        if (eb.buf) efree(eb.buf);
#ifdef SNOBOL_DYNAMIC_PATTERN
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

#ifdef SNOBOL_DYNAMIC_PATTERN
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
    zend_string *subject, *template;
    ZEND_PARSE_PARAMETERS_START(2,2)
        Z_PARAM_STR(subject)
        Z_PARAM_STR(template)
    ZEND_PARSE_PARAMETERS_END();

    snobol_pattern_t *intern = php_snobol_fetch(Z_OBJ_P(ZEND_THIS));
    if (!intern->bc || intern->bc_len == 0) {
        zend_throw_exception(zend_ce_exception, "Pattern not compiled", 0);
        RETURN_FALSE;
    }

    uint8_t *tpl_bc = NULL;
    size_t tpl_bc_len = 0;
    if (compile_template_to_bytecode(ZSTR_VAL(template), ZSTR_LEN(template), &tpl_bc, &tpl_bc_len) != 0) {
        zend_throw_exception(zend_ce_exception, "Failed to compile template", 0);
        RETURN_FALSE;
    }

    snobol_buf out;
    snobol_buf_init(&out);

    size_t offset = 0;
    size_t subject_len = ZSTR_LEN(subject);
    const char *subject_val = ZSTR_VAL(subject);
    size_t last_match_end = 0;

    while (offset <= subject_len) {
        VM vm;
        memset(&vm, 0, sizeof(VM));
        vm.bc = intern->bc;
        vm.bc_len = intern->bc_len;
        vm.s = subject_val + offset;
        vm.len = subject_len - offset;

#ifdef SNOBOL_JIT
        // IMPORTANT: subst() creates many short-lived VMs while scanning through the subject.
        // Sharing the per-pattern JIT counters/trace cache across these temporary VMs can lead
        // to stale trace pointers being executed after memory was freed/reused.
        // For safety, keep JIT disabled for these per-offset scan VMs.
        vm.jit.ip_counts = NULL;
        vm.jit.op_counts = NULL;
        vm.jit.traces = NULL;
        vm.jit.enabled = false;
#endif

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

        if (vm_exec(&vm)) {
            // Match found at offset.
            // 1. Append prefix (before this match)
            snobol_buf_append(&out, subject_val + last_match_end, offset - last_match_end);

            // 2. Run template BC to append replacement
            // Template BC needs access to captures from VM.
            // We can reuse VM state, just change BC.
            vm.bc = tpl_bc;
            vm.bc_len = tpl_bc_len;
            vm.ip = 0;
            vm.out = &out;
            vm_run(&vm); // This will execute template ops and append to out.

            // 3. Advance
            size_t match_len = vm.pos;
            if (match_len == 0) match_len = 1; // avoid infinite loop

            offset += match_len;
            last_match_end = offset;

            // Allow matching at the very end (empty string) only once
            if (offset > subject_len) break;
        } else {
            // No match at current offset
            offset++;
        }

#ifdef SNOBOL_DYNAMIC_PATTERN
        /* Clean up dynamic pattern cache for this VM iteration */
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
    }

    // Append remainder
    if (last_match_end < subject_len) {
        snobol_buf_append(&out, subject_val + last_match_end, subject_len - last_match_end);
    }

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

#ifdef SNOBOL_JIT
    snobol_pattern_t *intern = php_snobol_fetch(Z_OBJ_P(ZEND_THIS));
    intern->jit_enabled = enabled;
#endif
    RETURN_TRUE;
}

static const zend_function_entry snobol_pattern_methods[] = {
    PHP_ME(Snobol_Pattern, compileFromAst, ai_compileFromAst, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Pattern, fromString, ai_fromString, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Pattern, match, ai_match, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Pattern, subst, ai_subst, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Pattern, setEvalCallbacks, ai_setEval, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Pattern, setJit, ai_setJit, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

zend_class_entry *snobol_pattern_ce;

void snobol_pattern_minit(void) {
    SNOBOL_LOG("snobol_pattern_minit: START");
    zend_class_entry ce;
    
    memcpy(&snobol_pattern_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    snobol_pattern_object_handlers.offset = XtOffsetOf(snobol_pattern_t, std);
    snobol_pattern_object_handlers.free_obj = snobol_pattern_free;

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

/* Stub for template compilation - not yet implemented */
int compile_template_to_bytecode(const char *tpl, size_t len, uint8_t **out_bc, size_t *out_len) {
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
