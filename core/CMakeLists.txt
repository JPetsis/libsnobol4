cmake_minimum_required(VERSION 3.16)

# Core libsnobol4 C library
# This builds the language-agnostic C pattern matching engine

project(libsnobol4_core VERSION 0.3.0 LANGUAGES C)

# Library target for all core source files
add_library(snobol4 STATIC
    src/lexer.c
    src/parser.c
    src/ast.c
    src/compiler.c
    src/vm.c
    src/table.c
    src/dynamic_pattern.c
    src/jit.c
    src/jit_ir.c
    src/jit_backend_arm64.c
    src/version.c
    src/string_fn.c
    src/type_fn.c
    src/pattern_build.c
    src/search.c
    src/unicode_fold.c
    src/api.c
)

# Set C23 standard
target_compile_features(snobol4 PUBLIC c_std_23)

# Set up include directories
target_include_directories(snobol4
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Warning flags - compiler-specific
if(MSVC)
    # MSVC warning flags
    target_compile_options(snobol4 PRIVATE
        /W4
        /wd4100  # unreferenced formal parameter
        /wd4101  # unreferenced local variable
        /wd4189  # local variable initialized but not referenced
        /wd4244  # conversion (possible loss of data)
        /wd4267  # conversion from 'size_t' to smaller type
        # Use C17 so that nullptr is not a keyword (C23 feature), allowing our
        # snobol_internal.h compat #define nullptr NULL to take effect on MSVC 2022+.
        /std:c17
    )
    # Suppress deprecation warnings for POSIX names (strcpy, getenv, etc.)
    target_compile_definitions(snobol4 PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    # GCC/Clang warning flags
    # Disable unused variable warnings (placeholders for future functionality)
    # Disable sign conversion warnings (intentional in bytecode/UTF-8 handling)
    target_compile_options(snobol4 PRIVATE
        -Wall -Wextra
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        -Wno-unused-function
        -Wno-sign-conversion
        -Wno-implicit-int-conversion
        -Wno-shorten-64-to-32
    )
endif()

# Optional: Enable VM profiling
option(SNOBOL_PROFILE "Enable VM profiling" OFF)
if(SNOBOL_PROFILE)
    target_compile_definitions(snobol4 PRIVATE SNOBOL_PROFILE=1)
endif()

# Optional: apply sanitizer flags
if(SNOBOL_SANITIZE)
    target_compile_options(snobol4 PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(snobol4 PUBLIC
        -fsanitize=address,undefined
    )
endif()

# Enable micro-JIT only on ARM64 (emits ARM64 machine code; requires sys/mman.h).
# Enabling it on x86-64 would compile in ARM64 trace execution and crash at runtime.
# Explicitly force JIT off on Windows regardless of user input.
if(WIN32)
    set(SNOBOL_JIT OFF CACHE BOOL "" FORCE)
endif()
if(NOT WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    target_compile_definitions(snobol4 PRIVATE SNOBOL_JIT=1)

    # Platform detection for JIT code paths
    if(APPLE)
        target_compile_definitions(snobol4 PRIVATE SNOBOL_JIT_PLATFORM_MACOS=1)
    else()
        target_compile_definitions(snobol4 PRIVATE SNOBOL_JIT_PLATFORM_LINUX=1)
    endif()
endif()

# JIT backend selection (compile-time; only one backend per binary).
# Valid values: "arm64"  (default, requires ARM64 host)
# Adding a new backend: add the source file and a new valid value here.
set(SNOBOL_JIT_BACKEND "arm64" CACHE STRING "JIT backend to compile in (arm64)")
set_property(CACHE SNOBOL_JIT_BACKEND PROPERTY STRINGS "arm64")

set(_valid_jit_backends "arm64")
if(NOT SNOBOL_JIT_BACKEND IN_LIST _valid_jit_backends)
    message(FATAL_ERROR
        "SNOBOL_JIT_BACKEND='${SNOBOL_JIT_BACKEND}' is not a recognised backend.\n"
        "Valid backends: ${_valid_jit_backends}"
    )
endif()
target_compile_definitions(snobol4 PRIVATE
    SNOBOL_JIT_BACKEND="${SNOBOL_JIT_BACKEND}"
)

# Installation rules
include(GNUInstallDirs)

# Install headers to include/snobol/
install(DIRECTORY include/snobol/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/snobol
    FILES_MATCHING PATTERN "*.h"
)

# Install library to lib/
install(TARGETS snobol4
    EXPORT libsnobol4Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Generate CMake package config for find_package(libsnobol4)
include(CMakePackageConfigHelpers)

install(EXPORT libsnobol4Targets
    FILE libsnobol4Targets.cmake
    NAMESPACE libsnobol4::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libsnobol4
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/libsnobol4Config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/libsnobol4Config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libsnobol4
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/libsnobol4ConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/libsnobol4Config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/libsnobol4ConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libsnobol4
)

# Generate and install pkg-config file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/libsnobol4.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/libsnobol4.pc
    @ONLY
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/libsnobol4.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

