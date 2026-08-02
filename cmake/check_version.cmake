# Verify that the generated snobol/version.h matches the declared project()
# version in the top-level CMakeLists.txt.  Run via the check-version target.
#
# Required -D variables:
#   HEADER    path to the generated core/include/snobol/version.h
#   EXPECTED  expected version string, e.g. "1.0.0"

if(NOT DEFINED HEADER OR NOT DEFINED EXPECTED)
    message(FATAL_ERROR "check_version.cmake requires -DHEADER and -DEXPECTED")
endif()

if(NOT EXISTS "${HEADER}")
    message(FATAL_ERROR "Generated version header not found: ${HEADER}")
endif()

file(READ "${HEADER}" _contents)

string(REGEX MATCH "SNOBOL_VERSION_MAJOR[ \t]+([0-9]+)" _unused "${_contents}")
set(_major "${CMAKE_MATCH_1}")
string(REGEX MATCH "SNOBOL_VERSION_MINOR[ \t]+([0-9]+)" _unused "${_contents}")
set(_minor "${CMAKE_MATCH_1}")
string(REGEX MATCH "SNOBOL_VERSION_PATCH[ \t]+([0-9]+)" _unused "${_contents}")
set(_patch "${CMAKE_MATCH_1}")

set(_actual "${_major}.${_minor}.${_patch}")

if(NOT _actual STREQUAL EXPECTED)
    message(FATAL_ERROR
        "Version mismatch: snobol/version.h reports ${_actual}, "
        "but CMakeLists.txt declares ${EXPECTED}. "
        "Edit project(VERSION) in the top-level CMakeLists.txt, not the header.")
endif()

message(STATUS "Version check passed: snobol/version.h == ${EXPECTED}")
