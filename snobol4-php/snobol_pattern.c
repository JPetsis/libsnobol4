#include "php.h"
#include "php_snobol.h"
#include "zend_exceptions.h"
#include "snobol_compiler.h"
#include "snobol_vm.h"
#ifdef SNOBOL_JIT
#include "snobol_jit.h"
#endif

#include <stdio.h>
#include <time.h>
#include <stdarg.h>

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
    }
    vm.jit.enabled = intern->jit_enabled;
    vm.jit.stats = snobol_jit_get_stats();
#endif

    bool ok = vm_exec(&vm); 
    
    SNOBOL_LOG("Snobol_Pattern::match: VM returned %d, pos=%zu, var_count=%zu", (int)ok, vm.pos, vm.var_count);

    if (!ok) {
        if (eb.buf) efree(eb.buf);
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
