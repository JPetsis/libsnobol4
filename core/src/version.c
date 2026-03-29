/**
 * @file version.c
 * @brief libsnobol4 version information
 */

#include "snobol/snobol.h"

void snobol_version(int* major, int* minor, int* patch) {
    if (major) *major = SNOBOL_VERSION_MAJOR;
    if (minor) *minor = SNOBOL_VERSION_MINOR;
    if (patch) *patch = SNOBOL_VERSION_PATCH;
}
