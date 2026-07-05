#pragma once

#define _POSIX_C_SOURCE 199309L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include <time.h>
#include <snobol/snobol.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef HAVE_PCRE2
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#endif

#if defined(__APPLE__)
#include <mach/mach_time.h>
#endif

/* Number of match iterations per benchmark (compile once, match N times) */
#define BENCH_ITERATIONS 100000

/* Result for one scenario */
typedef struct {
    const char *name;
    const char *label;
    int64_t snobol_ns;        /* total ns for snobol4 match (VM path) */
    int64_t literal_ns;       /* total ns for snobol_pattern_match_literal (-1=unavail) */
    int64_t pcre2_ns;         /* total ns for pcre2 matches (-1 = unavailable) */
    int64_t search_ns;        /* total ns for snobol4 search-mode matches */
} bench_results_t;

/* Timer: nanosecond-scale using clock_gettime or mach_absolute_time */
static inline int64_t bench_ns(void) {
#if defined(_WIN32)
    return 0; /* not implemented for Windows */
#elif defined(__APPLE__)
    static mach_timebase_info_data_t info = {0};
    if (info.denom == 0) mach_timebase_info(&info);
    uint64_t raw = mach_absolute_time();
    return (int64_t)(raw * info.numer / info.denom);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000L + (int64_t)ts.tv_nsec;
#endif
}

/* Suite declaration macro */
#define BENCH_SUITE(name) void bench_##name##_suite(bench_results_t *)
