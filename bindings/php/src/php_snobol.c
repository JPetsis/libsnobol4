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
}

PHP_FUNCTION(snobol_reset_jit_stats) {
    if (zend_parse_parameters_none() == FAILURE) {
        return;
    }
    snobol_jit_reset_stats();
}
#endif

static const zend_function_entry snobol_functions[] = {
#ifdef SNOBOL_JIT
    PHP_FE(snobol_get_jit_stats, ai_snobol_get_jit_stats)
    PHP_FE(snobol_reset_jit_stats, ai_snobol_reset_jit_stats)
#endif
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