cmake_minimum_required(VERSION 3.16)

# Core libsnobol4 C library
# This builds the language-agnostic C pattern matching engine

project(libsnobol4_core VERSION 0.2.0 LANGUAGES C)

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
    src/version.c
    src/string_fn.c
    src/type_fn.c
    src/pattern_build.c
    src/search.c
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

# Enable micro-JIT only on ARM64 (emits ARM64 machine code; requires sys/mman.h).
# Enabling it on x86-64 would compile in ARM64 trace execution and crash at runtime.
if(NOT WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    target_compile_definitions(snobol4 PRIVATE SNOBOL_JIT=1)
endif()

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
