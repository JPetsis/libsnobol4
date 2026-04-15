#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* phpize always builds shared extensions; ensure get_module() is compiled in.
   On macOS the autoconf-generated config.h sometimes omits COMPILE_DL_SNOBOL
   when --enable-snobol is passed without =shared.  This fallback is harmless
   for cmake builds because cmake defines it explicitly. */
#ifndef COMPILE_DL_SNOBOL
# define COMPILE_DL_SNOBOL 1
#endif

#include "php.h"
#include "php_snobol.h"
#include "ext/standard/info.h"

/* libsnobol4 core built-in function headers */
#include "snobol/string_fn.h"
#include "snobol/type_fn.h"
#include "snobol/vm.h"

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

/* Extern declaration from snobol_pattern.c */
void snobol_pattern_minit(void);

/* Extern declaration from snobol_table_php.c */
void snobol_table_php_minit(void);

/* Extern declaration from snobol_dynamic_pattern_php.c */
void snobol_dynamic_pattern_cache_php_minit(void);

PHP_MINFO_FUNCTION(snobol);
#ifdef SNOBOL_JIT
#include "snobol/jit.h"
#endif

PHP_MINFO_FUNCTION(snobol) {
    php_info_print_table_start();
    php_info_print_table_header(2, "snobol support", "enabled");
    php_info_print_table_row(2, "version", PHP_SNOBOL_VERSION);
#ifdef SNOBOL_JIT
    php_info_print_table_row(2, "micro-JIT", "enabled");
#else
    php_info_print_table_row(2, "micro-JIT", "disabled");
#endif
#ifdef SNOBOL_PROFILE
    php_info_print_table_row(2, "profiling", "enabled");
#else
    php_info_print_table_row(2, "profiling", "disabled");
#endif
    php_info_print_table_end();
}

PHP_MINIT_FUNCTION(snobol) {
    SNOBOL_LOG("PHP_MINIT_FUNCTION(snobol): START");
#ifdef SNOBOL_JIT
    snobol_jit_init();
#endif
    snobol_pattern_minit();
    snobol_table_php_minit();
    snobol_dynamic_pattern_cache_php_minit();
    SNOBOL_LOG("PHP_MINIT_FUNCTION(snobol): DONE");
    return SUCCESS;
}

#ifdef SNOBOL_JIT
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(ai_snobol_get_jit_stats, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(ai_snobol_reset_jit_stats, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_FUNCTION(snobol_get_jit_stats) {
    if (zend_parse_parameters_none() == FAILURE) {
        return;
    }

    SnobolJitStats *stats = snobol_jit_get_stats();
    array_init(return_value);
    /* Core counters */
    add_assoc_long(return_value, "jit_compilations_total",       (zend_long)stats->compilations_total);
    add_assoc_long(return_value, "jit_cache_hits_total",         (zend_long)stats->cache_hits_total);
    add_assoc_long(return_value, "jit_entries_total",            (zend_long)stats->entries_total);
    add_assoc_long(return_value, "jit_exits_total",              (zend_long)stats->exits_total);
    add_assoc_long(return_value, "jit_bailouts_total",           (zend_long)stats->bailouts_total);
    add_assoc_long(return_value, "jit_time_ns_total",            (zend_long)stats->time_ns_total);
    /* Backtracking counters */
    add_assoc_long(return_value, "choice_push_total",            (zend_long)stats->choice_push_total);
    add_assoc_long(return_value, "choice_pop_total",             (zend_long)stats->choice_pop_total);
    add_assoc_long(return_value, "choice_bytes_total",           (zend_long)stats->choice_bytes_total);
    /* Timing */
    add_assoc_long(return_value, "jit_compile_time_ns_total",    (zend_long)stats->compile_time_ns_total);
    add_assoc_long(return_value, "jit_exec_time_ns_total",       (zend_long)stats->exec_time_ns_total);
    add_assoc_long(return_value, "jit_interp_time_ns_total",     (zend_long)stats->interp_time_ns_total);
    /* Profitability / skip reasons */
    add_assoc_long(return_value, "jit_skipped_cold_total",       (zend_long)stats->skipped_cold_total);
    add_assoc_long(return_value, "jit_skipped_exit_rate_total",  (zend_long)stats->skipped_exit_rate_total);
    add_assoc_long(return_value, "jit_skipped_budget_total",     (zend_long)stats->skipped_budget_total);
    /* Bailout reasons */
    add_assoc_long(return_value, "jit_bailout_match_fail_total", (zend_long)stats->bailout_match_fail_total);
    add_assoc_long(return_value, "jit_bailout_partial_total",    (zend_long)stats->bailout_partial_total);
    /* Search-mode specific counters */
    add_assoc_long(return_value, "jit_search_entries_total",              (zend_long)stats->search_entries_total);
    add_assoc_long(return_value, "jit_search_candidate_rejects",          (zend_long)stats->search_candidate_rejects);
    add_assoc_long(return_value, "jit_skipped_search_cold_total",         (zend_long)stats->skipped_search_cold_total);
    add_assoc_long(return_value, "jit_bailout_search_candidate_total",    (zend_long)stats->bailout_search_candidate_total);
}

PHP_FUNCTION(snobol_reset_jit_stats) {
    if (zend_parse_parameters_none() == FAILURE) {
        return;
    }
    snobol_jit_reset_stats();
}
#endif

/* ============================================================
 * C function exports for libsnobol4 built-ins
 *
 * These PHP_FUNCTION implementations wrap the C built-in functions
 * from string_fn.h and type_fn.h, exposing them as global PHP
 * functions (snobol_text_*).  The Snobol\Text PHP class (Text.php)
 * uses these internally when the C extension is loaded.
 * ============================================================ */

/* --- STRING FUNCTIONS --- */

PHP_FUNCTION(snobol_text_size) {
    char  *s; size_t slen;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &s, &slen) == FAILURE) return;
    RETURN_LONG((zend_long)snobol_size(s, slen));
}

PHP_FUNCTION(snobol_text_trim) {
    char  *s; size_t slen;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &s, &slen) == FAILURE) return;
    snobol_buf b = {0}; snobol_buf_init(&b);
    if (snobol_trim(s, slen, &b)) {
        RETVAL_STRINGL(b.data, b.len);
    } else {
        RETVAL_STRINGL(s, slen);
    }
    snobol_buf_free(&b);
}

PHP_FUNCTION(snobol_text_dupl) {
    char *s; size_t slen; zend_long n;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "sl", &s, &slen, &n) == FAILURE) return;
    snobol_buf b = {0}; snobol_buf_init(&b);
    if (n > 0 && snobol_dupl(s, slen, (size_t)n, &b)) {
        RETVAL_STRINGL(b.data, b.len);
    } else {
        RETVAL_STRINGL("", 0);
    }
    snobol_buf_free(&b);
}

PHP_FUNCTION(snobol_text_reverse) {
    char *s; size_t slen;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &s, &slen) == FAILURE) return;
    snobol_buf b = {0}; snobol_buf_init(&b);
    if (snobol_reverse(s, slen, &b)) {
        RETVAL_STRINGL(b.data, b.len);
    } else {
        RETVAL_STRINGL(s, slen);
    }
    snobol_buf_free(&b);
}

PHP_FUNCTION(snobol_text_substr) {
    char *s; size_t slen; zend_long pos, len;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "sll", &s, &slen, &pos, &len) == FAILURE) return;
    snobol_buf b = {0}; snobol_buf_init(&b);
    if (pos >= 1 && snobol_substr(s, slen, (size_t)pos, (size_t)len, &b)) {
        RETVAL_STRINGL(b.data, b.len);
    } else {
        RETVAL_FALSE;
    }
    snobol_buf_free(&b);
}

PHP_FUNCTION(snobol_text_replace) {
    char *s, *f, *t; size_t slen, flen, tlen;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "sss", &s, &slen, &f, &flen, &t, &tlen) == FAILURE) return;
    snobol_buf b = {0}; snobol_buf_init(&b);
    if (snobol_replace(s, slen, f, flen, t, tlen, &b)) {
        RETVAL_STRINGL(b.data, b.len);
    } else {
        RETVAL_STRINGL(s, slen);
    }
    snobol_buf_free(&b);
}

PHP_FUNCTION(snobol_text_replace_char) {
    char *s, *f, *t; size_t slen, flen, tlen;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "sss", &s, &slen, &f, &flen, &t, &tlen) == FAILURE) return;
    snobol_buf b = {0}; snobol_buf_init(&b);
    if (snobol_replace_char(s, slen, f, flen, t, tlen, &b)) {
        RETVAL_STRINGL(b.data, b.len);
    } else {
        RETVAL_STRINGL(s, slen);
    }
    snobol_buf_free(&b);
}

PHP_FUNCTION(snobol_text_lpad) {
    char *s, *pad = " "; size_t slen, padlen = 1; zend_long width;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "sl|s", &s, &slen, &width, &pad, &padlen) == FAILURE) return;
    uint32_t pad_cp = (padlen > 0) ? (unsigned char)pad[0] : (uint32_t)' ';
    snobol_buf b = {0}; snobol_buf_init(&b);
    if (snobol_lpad(s, slen, (size_t)width, pad_cp, &b)) {
        RETVAL_STRINGL(b.data, b.len);
    } else {
        RETVAL_STRINGL(s, slen);
    }
    snobol_buf_free(&b);
}

PHP_FUNCTION(snobol_text_rpad) {
    char *s, *pad = " "; size_t slen, padlen = 1; zend_long width;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "sl|s", &s, &slen, &width, &pad, &padlen) == FAILURE) return;
    uint32_t pad_cp = (padlen > 0) ? (unsigned char)pad[0] : (uint32_t)' ';
    snobol_buf b = {0}; snobol_buf_init(&b);
    if (snobol_rpad(s, slen, (size_t)width, pad_cp, &b)) {
        RETVAL_STRINGL(b.data, b.len);
    } else {
        RETVAL_STRINGL(s, slen);
    }
    snobol_buf_free(&b);
}

PHP_FUNCTION(snobol_text_char) {
    zend_long cp;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "l", &cp) == FAILURE) return;
    snobol_buf b = {0}; snobol_buf_init(&b);
    if (cp >= 0 && snobol_char_fn((uint32_t)cp, &b)) {
        RETVAL_STRINGL(b.data, b.len);
    } else {
        RETVAL_FALSE;
    }
    snobol_buf_free(&b);
}

PHP_FUNCTION(snobol_text_ord) {
    char *s; size_t slen;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &s, &slen) == FAILURE) return;
    uint32_t cp = 0;
    if (snobol_ord(s, slen, &cp)) {
        RETURN_LONG((zend_long)cp);
    } else {
        RETURN_FALSE;
    }
}

PHP_FUNCTION(snobol_text_upper) {
    char *s; size_t slen;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &s, &slen) == FAILURE) return;
    snobol_buf b = {0}; snobol_buf_init(&b);
    if (snobol_upper(s, slen, &b)) {
        RETVAL_STRINGL(b.data, b.len);
    } else {
        RETVAL_STRINGL(s, slen);
    }
    snobol_buf_free(&b);
}

PHP_FUNCTION(snobol_text_lower) {
    char *s; size_t slen;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &s, &slen) == FAILURE) return;
    snobol_buf b = {0}; snobol_buf_init(&b);
    if (snobol_lower(s, slen, &b)) {
        RETVAL_STRINGL(b.data, b.len);
    } else {
        RETVAL_STRINGL(s, slen);
    }
    snobol_buf_free(&b);
}

/* --- COMPARISON / TYPE-CHECK FUNCTIONS --- */

PHP_FUNCTION(snobol_text_ident) {
    char *a, *b_s; size_t alen, blen;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "ss", &a, &alen, &b_s, &blen) == FAILURE) return;
    RETURN_BOOL(snobol_ident(a, alen, b_s, blen));
}

PHP_FUNCTION(snobol_text_differ) {
    char *a, *b_s; size_t alen, blen;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "ss", &a, &alen, &b_s, &blen) == FAILURE) return;
    RETURN_BOOL(snobol_differ(a, alen, b_s, blen));
}

PHP_FUNCTION(snobol_text_lexeq) {
    char *a, *b_s; size_t alen, blen;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "ss", &a, &alen, &b_s, &blen) == FAILURE) return;
    RETURN_BOOL(snobol_lexeq(a, alen, b_s, blen));
}

PHP_FUNCTION(snobol_text_lexlt) {
    char *a, *b_s; size_t alen, blen;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "ss", &a, &alen, &b_s, &blen) == FAILURE) return;
    RETURN_BOOL(snobol_lexlt(a, alen, b_s, blen));
}

PHP_FUNCTION(snobol_text_lexgt) {
    char *a, *b_s; size_t alen, blen;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "ss", &a, &alen, &b_s, &blen) == FAILURE) return;
    RETURN_BOOL(snobol_lexgt(a, alen, b_s, blen));
}

PHP_FUNCTION(snobol_text_integer) {
    char *s; size_t slen;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &s, &slen) == FAILURE) return;
    RETURN_BOOL(snobol_integer(s, slen));
}

PHP_FUNCTION(snobol_text_real) {
    char *s; size_t slen;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &s, &slen) == FAILURE) return;
    RETURN_BOOL(snobol_real(s, slen));
}

PHP_FUNCTION(snobol_text_numeric) {
    char *s; size_t slen;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &s, &slen) == FAILURE) return;
    RETURN_BOOL(snobol_numeric(s, slen));
}

static const zend_function_entry snobol_functions[] = {
#ifdef SNOBOL_JIT
    PHP_FE(snobol_get_jit_stats, ai_snobol_get_jit_stats)
    PHP_FE(snobol_reset_jit_stats, ai_snobol_reset_jit_stats)
#endif
    /* C function exports for string/comparison built-ins */
    PHP_FE(snobol_text_size,         NULL)
    PHP_FE(snobol_text_trim,         NULL)
    PHP_FE(snobol_text_dupl,         NULL)
    PHP_FE(snobol_text_reverse,      NULL)
    PHP_FE(snobol_text_substr,       NULL)
    PHP_FE(snobol_text_replace,      NULL)
    PHP_FE(snobol_text_replace_char, NULL)
    PHP_FE(snobol_text_lpad,         NULL)
    PHP_FE(snobol_text_rpad,         NULL)
    PHP_FE(snobol_text_char,         NULL)
    PHP_FE(snobol_text_ord,          NULL)
    PHP_FE(snobol_text_upper,        NULL)
    PHP_FE(snobol_text_lower,        NULL)
    PHP_FE(snobol_text_ident,        NULL)
    PHP_FE(snobol_text_differ,       NULL)
    PHP_FE(snobol_text_lexeq,        NULL)
    PHP_FE(snobol_text_lexlt,        NULL)
    PHP_FE(snobol_text_lexgt,        NULL)
    PHP_FE(snobol_text_integer,      NULL)
    PHP_FE(snobol_text_real,         NULL)
    PHP_FE(snobol_text_numeric,      NULL)
    PHP_FE_END
};

zend_module_entry snobol_module_entry = {
    STANDARD_MODULE_HEADER,
    "snobol",
    snobol_functions,
    PHP_MINIT(snobol),
    NULL,
    NULL,
    NULL,
    PHP_MINFO(snobol),
    PHP_SNOBOL_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_SNOBOL
ZEND_GET_MODULE(snobol)
#endif