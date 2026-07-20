cmake_minimum_required(VERSION 3.16)

# Core libsnobol4 C library
# This builds the language-agnostic C pattern matching engine

project(libsnobol4_core VERSION 0.12.0 LANGUAGES C)

# Core source files
set(_core_sources
    src/lexer.c
    src/parser.c
    src/ast.c
    src/compiler.c
    src/compiler_analysis.c
    src/compiler_codegen.c
    src/vm_choice.c
    src/vm_capture.c
    src/vm_exec.c
    src/table.c
    src/array.c
    src/dynamic_pattern.c
    src/version.c
    src/string_fn.c
    src/type_fn.c
    src/pattern_build.c
    src/search_meta.c
    src/search_tiers.c
    src/search_simd.c
    src/unicode_fold.c
    src/api.c
)

add_library(snobol4 STATIC ${_core_sources})

# Single source of version truth (see T5): generate version.h from the
# project() VERSION in the top-level CMakeLists.txt so the public header
# never hard-codes version literals.  The generated header is written into a
# build-local copy of the include tree (include/snobol/version.h) and added
# ahead of the source include dir, then installed alongside the rest.
set(SNOBOL_VERSION_MAJOR ${SNOBOL4_VERSION_MAJOR})
set(SNOBOL_VERSION_MINOR ${SNOBOL4_VERSION_MINOR})
set(SNOBOL_VERSION_PATCH ${SNOBOL4_VERSION_PATCH})
set(SNOBOL_VERSION_STRING "${SNOBOL4_VERSION_STRING}")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/snobol/version.h
    @ONLY
)

# Set C23 standard (MSVC uses /std:c17 set below in the warning-flags block)
if(NOT MSVC)
    target_compile_features(snobol4 PUBLIC c_std_23)
endif()

# Set up include directories
# Profiling counters are cheap and used by the catastrophic/trail C tests to
# assert bounded choice-point counts.
target_compile_definitions(snobol4 PRIVATE SNOBOL_PROFILE)

target_include_directories(snobol4
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
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

# Priority 6.8: Release-only aggressive optimization.
# Hidden visibility + no-semantic-interposition shrink the symbol table and let
# the compiler inline across the hot search/VM paths; _FORTIFY_SOURCE=3 hardens
# libc calls.  LTO (flto) is enabled project-wide for Release via
# CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE in the top-level CMakeLists.
if(NOT MSVC)
  target_compile_options(snobol4 PRIVATE
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-fvisibility=hidden>
    $<$<BOOL:${BUILD_SHARED_LIBS}>:-fno-semantic-interposition>
  )
  target_compile_definitions(snobol4 PRIVATE
    $<$<CONFIG:Release>:_FORTIFY_SOURCE=3>
  )
endif()

# Priority 6.9: Profile-Guided Optimization (off by default; see Makefile
# build-pgo-gen / pgo-train / build-pgo-use).  Applied on top of Release.
if(SNOBOL_PGO_GEN)
  target_compile_options(snobol4 PRIVATE -fprofile-generate)
  target_link_options(snobol4 PUBLIC -fprofile-generate)
elseif(SNOBOL_PGO_USE)
  if(SNOBOL_PGO_PROFILE)
    target_compile_options(snobol4 PRIVATE -fprofile-use=${SNOBOL_PGO_PROFILE} -fprofile-correction)
    target_link_options(snobol4 PUBLIC -fprofile-use=${SNOBOL_PGO_PROFILE} -fprofile-correction)
  else()
    target_compile_options(snobol4 PRIVATE -fprofile-use -fprofile-correction)
    target_link_options(snobol4 PUBLIC -fprofile-use -fprofile-correction)
  endif()
endif()

# SIMD compiler flags for accelerated search tiers (AVX2 on x86-64)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  target_compile_options(snobol4 PRIVATE -mavx2)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|ARM64)$")
  target_compile_options(snobol4 PRIVATE -march=armv8.2-a+simd)
endif()

# Optional: Enable VM profiling
option(SNOBOL_PROFILE "Enable VM profiling" OFF)
if(SNOBOL_PROFILE)
    target_compile_definitions(snobol4 PRIVATE SNOBOL_PROFILE=1)
endif()

option(ENABLE_PIKE_SCAN "Enable Pike single-pass scan (W1c)" OFF)
if(ENABLE_PIKE_SCAN)
    target_compile_definitions(snobol4 PRIVATE SNOBOL_PIKE_SCAN=1)
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

# Installation rules
include(GNUInstallDirs)

# Install headers to include/snobol/ (generated version.h installed explicitly
# since it lives in the build tree, not the source include tree)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/snobol/version.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/snobol
)
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

