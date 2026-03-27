#ifndef SNOBOL_INTERNAL_H
#define SNOBOL_INTERNAL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef STANDALONE_BUILD
#include "php.h"
#else
#include <stdlib.h>
#include <string.h>
#endif

/* Memory management macros to abstract away PHP's allocator */
#ifdef STANDALONE_BUILD
#define snobol_malloc  malloc
#define snobol_free    free
#define snobol_realloc realloc
#define snobol_calloc  calloc
#else
#define snobol_malloc  emalloc
#define snobol_free    efree
#define snobol_realloc erealloc
#define snobol_calloc  ecalloc
#endif

/* Debug logging */
/* #define SNOBOL_TRACE 1 */
#ifdef SNOBOL_TRACE
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
static inline void snobol_log_impl(const char *file, int line, const char *fmt, ...) {
    FILE *f = fopen("/tmp/snobol_debug.log", "a");
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
#define SNOBOL_LOG(fmt, ...) snobol_log_impl(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define SNOBOL_LOG(fmt, ...) ((void)0)
#endif

#endif /* SNOBOL_INTERNAL_H */
