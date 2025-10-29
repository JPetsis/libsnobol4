#include "php.h"
#include "zend_exceptions.h"
#include "snobol_compiler.h"
#include "snobol_vm.h"

/* declare class entry */
zend_class_entry *snobol_pattern_ce;

typedef struct {
    uint8_t *bc;
    size_t bc_len;
    zval eval_callbacks;
    zend_object std;
} snobol_pattern_t;

static inline snobol_pattern_t* php_snobol_fetch(zend_object *obj) {
    return (snobol_pattern_t *)((char *)(obj) - XtOffsetOf(snobol_pattern_t, std));
}

static zend_object *snobol_pattern_create(zend_class_entry *ce) {
    snobol_pattern_t *intern = ecalloc(1, sizeof(snobol_pattern_t) + zend_object_properties_size(ce));
    intern->bc = NULL;
    intern->bc_len = 0;
    ZVAL_UNDEF(&intern->eval_callbacks);
    zend_object_std_init(&intern->std, ce);
    object_properties_init(&intern->std, ce);
    return &intern->std;
}

static void snobol_pattern_free(zend_object *object) {
    snobol_pattern_t *intern = php_snobol_fetch(object);
    if (intern->bc) {
        compiler_free(intern->bc);
        intern->bc = NULL;
    }
    if (!Z_ISUNDEF(intern->eval_callbacks)) {
        zval_ptr_dtor(&intern->eval_callbacks);
    }
    zend_object_std_dtor(object);
}

/* forward method declarations */
PHP_METHOD(Snobol_Pattern, __construct);
PHP_METHOD(Snobol_Pattern, __destruct);
PHP_METHOD(Snobol_Pattern, compileFromAst);
PHP_METHOD(Snobol_Pattern, match);
PHP_METHOD(Snobol_Pattern, setEvalCallbacks);

/* argument info */
ZEND_BEGIN_ARG_INFO_EX(ai_compileFromAst, 0, 0, 1)
    ZEND_ARG_ARRAY_INFO(0, ast, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_match, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, input, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_setEval, 0, 0, 1)
    ZEND_ARG_ARRAY_INFO(0, callbacks, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry snobol_pattern_methods[] = {
    PHP_ME(Snobol_Pattern, compileFromAst, ai_compileFromAst, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
    PHP_ME(Snobol_Pattern, match, ai_match, ZEND_ACC_PUBLIC)
    PHP_ME(Snobol_Pattern, setEvalCallbacks, ai_setEval, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* module init */
PHP_MINIT_FUNCTION(snobol) {
    zend_class_entry ce;
    INIT_CLASS_ENTRY(ce, "Snobol\\Pattern", snobol_pattern_methods);
    snobol_pattern_ce = zend_register_internal_class(&ce);
    snobol_pattern_ce->create_object = snobol_pattern_create;

    /* Declare properties to avoid deprecation warnings */
    zend_declare_property_long(snobol_pattern_ce, "_bc_ptr", sizeof("_bc_ptr")-1, 0, ZEND_ACC_PRIVATE);
    zend_declare_property_long(snobol_pattern_ce, "_bc_len", sizeof("_bc_len")-1, 0, ZEND_ACC_PRIVATE);
    zend_declare_property_null(snobol_pattern_ce, "_callbacks", sizeof("_callbacks")-1, ZEND_ACC_PRIVATE);

    return SUCCESS;
}

/* Destructor */
PHP_METHOD(Snobol_Pattern, __destruct) {
    snobol_pattern_t *intern = php_snobol_fetch(Z_OBJ_P(ZEND_THIS));
    if (intern->bc) {
        compiler_free(intern->bc);
        intern->bc = NULL;
    }
    if (!Z_ISUNDEF(intern->eval_callbacks)) {
        zval_ptr_dtor(&intern->eval_callbacks);
        ZVAL_UNDEF(&intern->eval_callbacks);
    }
}

/* compiles AST (PHP zval array) to bytecode and returns Pattern object */
PHP_METHOD(Snobol_Pattern, compileFromAst) {
    zval *ast;
    ZEND_PARSE_PARAMETERS_START(1,1)
        Z_PARAM_ARRAY(ast)
    ZEND_PARSE_PARAMETERS_END();

    uint8_t *bc = NULL;
    size_t bc_len = 0;

    if (compile_ast_to_bytecode(ast, &bc, &bc_len) != 0) {
        zend_throw_exception(zend_ce_exception, "Failed to compile AST", 0);
        RETURN_NULL();
    }

    if (!bc || bc_len == 0) {
        zend_throw_exception(zend_ce_exception, "Compilation produced empty bytecode", 0);
        RETURN_NULL();
    }

    if (object_init_ex(return_value, snobol_pattern_ce) != SUCCESS) {
        if (bc) compiler_free(bc);
        zend_throw_exception(zend_ce_exception, "Failed to create Pattern object", 0);
        RETURN_NULL();
    }

    zend_update_property_long(snobol_pattern_ce, Z_OBJ_P(return_value), "_bc_ptr", sizeof("_bc_ptr")-1, (zend_long)(uintptr_t)bc);
    zend_update_property_long(snobol_pattern_ce, Z_OBJ_P(return_value), "_bc_len", sizeof("_bc_len")-1, (zend_long)bc_len);
}

/* Helper to call PHP callback from C; used by VM eval_fn */
typedef struct {
    zval callbacks; /* array */
} EvalBridge;

static bool eval_bridge_fn(int fn_id, const char *s, size_t start, size_t end, void *udata) {
    EvalBridge *eb = (EvalBridge *)udata;
    if (Z_ISUNDEF(eb->callbacks) || Z_TYPE(eb->callbacks) != IS_ARRAY) return true;
    zval *cb = zend_hash_index_find(Z_ARRVAL(eb->callbacks), (zend_ulong)fn_id);
    if (!cb || Z_TYPE_P(cb) != IS_ARRAY) {
        /* Alternatively allow string/closure */
        zval *cb_any = zend_hash_index_find(Z_ARRVAL(eb->callbacks), (zend_ulong)fn_id);
        if (!cb_any) return true;
    }
    /* cb may be callable; call it with the captured string */
    zval retval;
    zval params[1];
    ZVAL_STRINGL(&params[0], s + start, end - start);
    if (call_user_function(NULL, NULL, cb, &retval, 1, params) == SUCCESS) {
        bool ok = zend_is_true(&retval);
        zval_ptr_dtor(&retval);
        zval_ptr_dtor(&params[0]);
        return ok;
    } else {
        zval_ptr_dtor(&params[0]);
        return true;
    }
}

/* match method */
PHP_METHOD(Snobol_Pattern, match) {
    zend_string *input;
    ZEND_PARSE_PARAMETERS_START(1,1)
        Z_PARAM_STR(input)
    ZEND_PARSE_PARAMETERS_END();

    /* Get bc from properties */
    zval *bc_ptr_zv = zend_read_property(snobol_pattern_ce, Z_OBJ_P(ZEND_THIS), "_bc_ptr", sizeof("_bc_ptr")-1, 1, NULL);
    zval *bc_len_zv = zend_read_property(snobol_pattern_ce, Z_OBJ_P(ZEND_THIS), "_bc_len", sizeof("_bc_len")-1, 1, NULL);

    if (!bc_ptr_zv || Z_TYPE_P(bc_ptr_zv) != IS_LONG || Z_LVAL_P(bc_ptr_zv) == 0) {
        zend_throw_exception(zend_ce_exception, "Pattern not compiled", 0);
        RETURN_FALSE;
    }

    uint8_t *bc = (uint8_t *)(uintptr_t)Z_LVAL_P(bc_ptr_zv);
    size_t bc_len = (size_t)Z_LVAL_P(bc_len_zv);

    VM vm;
    memset(&vm, 0, sizeof(vm));
    vm.bc = bc;
    vm.bc_len = bc_len;
    vm.s = ZSTR_VAL(input);
    vm.len = ZSTR_LEN(input);
    vm.eval_fn = NULL;
    vm.eval_udata = NULL;

    bool ok = vm_exec(&vm);

    if (!ok) {
        RETURN_FALSE;
    }

    /* build associative array of var captures */
    array_init(return_value);
    for (size_t i = 0; i < vm.var_count; ++i) {
        size_t a = vm.var_start[i];
        size_t b = vm.var_end[i];
        char key[32];
        snprintf(key, sizeof(key), "v%u", (unsigned)i);

        if (b >= a && b <= vm.len) {
            zend_string *slice = zend_string_init(vm.s + a, b - a, 0);
            add_assoc_str(return_value, key, slice);
        } else {
            add_assoc_null(return_value, key);
        }
    }
}

/* setEvalCallbacks(array $callbacks) - store a PHP array of callables into the object */
PHP_METHOD(Snobol_Pattern, setEvalCallbacks) {
    zval *arr;
    ZEND_PARSE_PARAMETERS_START(1,1)
        Z_PARAM_ARRAY(arr)
    ZEND_PARSE_PARAMETERS_END();

    zend_update_property(snobol_pattern_ce, Z_OBJ_P(ZEND_THIS), "_callbacks", sizeof("_callbacks")-1, arr);
    RETURN_TRUE;
}
