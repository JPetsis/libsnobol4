#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_snobol.h"

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
PHP_MINFO_FUNCTION(snobol);
#ifdef SNOBOL_JIT
#include "snobol_jit.h"
#endif

PHP_MINIT_FUNCTION(snobol) {
    SNOBOL_LOG("PHP_MINIT_FUNCTION(snobol): START");
#ifdef SNOBOL_JIT
    snobol_jit_init();
#endif
    snobol_pattern_minit();
    SNOBOL_LOG("PHP_MINIT_FUNCTION(snobol): DONE");
    return SUCCESS;
}

zend_module_entry snobol_module_entry = {
    STANDARD_MODULE_HEADER,
    "snobol",
    NULL,
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