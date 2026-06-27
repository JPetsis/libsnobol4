cmake_minimum_required(VERSION 3.16)

# Core libsnobol4 C library
# This builds the language-agnostic C pattern matching engine

project(libsnobol4_core VERSION 0.11.0 LANGUAGES C)

# Detect SLJIT backend early so we can conditionally include sources
set(_using_sljit FALSE)
if(SNOBOL_JIT_BACKEND STREQUAL "sljit")
    set(_using_sljit TRUE)
endif()

# Core source files — exclude custom backends when using SLJIT
set(_core_sources
    src/lexer.c
    src/parser.c
    src/ast.c
    src/compiler.c
    src/vm.c
    src/table.c
    src/array.c
    src/dynamic_pattern.c
    src/jit.c
    src/jit_ir.c
    src/version.c
    src/string_fn.c
    src/type_fn.c
    src/pattern_build.c
    src/search.c
    src/unicode_fold.c
    src/api.c
)

# SLJIT is the only JIT backend. The 4 arch-specific backends
# (arm64/arm32/riscv64/x86_64) have been retired.
list(APPEND _core_sources
    src/jit_backend_sljit.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../deps/sljit/sljitLir.c
)

add_library(snobol4 STATIC ${_core_sources})

# Set C23 standard (MSVC uses /std:c17 set below in the warning-flags block)
if(NOT MSVC)
    target_compile_features(snobol4 PUBLIC c_std_23)
endif()

# Set up include directories
target_include_directories(snobol4
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# SLJIT include path (needed for sljitLir.c to #include sljitNative*.c)
if(_using_sljit)
    target_include_directories(snobol4 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../deps/sljit
    )
endif()

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

# Diagnostic option: disable MSVC /GS buffer security check entirely.
# Use this to determine whether a STATUS_STACK_BUFFER_OVERRUN crash originates
# from the /GS cookie check or from another source.
option(SNOBOL_JIT_NO_GS "Disable /GS buffer security check on MSVC (diagnostic)" OFF)
if(MSVC AND SNOBOL_JIT_NO_GS)
    target_compile_options(snobol4 PRIVATE /GS-)
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

# Enable micro-JIT on supported architectures.  Backend source files have their
# own arch guards so the compiler only sees code for the active target.
# On Windows JIT is opt-in and requires SNOBOL_JIT=ON (only x86_64 supported).
set(_jit_arch_ok FALSE)
# ARM64
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(_jit_arch_ok TRUE)
endif()
# ARM32 (armv7l, armv6l, arm, etc. — but not arm64)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|ARM64|aarch64")
    set(_jit_arch_ok TRUE)
endif()
# RISC-V 64-bit
if(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64|riscv64.*")
    set(_jit_arch_ok TRUE)
endif()
# x86-64 (AMD64)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64|amd64|i686|x86")
    set(_jit_arch_ok TRUE)
endif()
# SLJIT: cross-platform, works on any architecture
if(_using_sljit)
    set(_jit_arch_ok TRUE)
endif()
if(_jit_arch_ok)
    target_compile_definitions(snobol4 PRIVATE SNOBOL_JIT=1)

    # Platform detection for JIT code paths
    if(WIN32)
        target_compile_definitions(snobol4 PRIVATE SNOBOL_JIT_PLATFORM_WINDOWS=1)
        target_compile_definitions(snobol4 PRIVATE SNOBOL_JIT_WIN64_ABI=1)
    elseif(APPLE)
        target_compile_definitions(snobol4 PRIVATE SNOBOL_JIT_PLATFORM_MACOS=1)
    else()
        target_compile_definitions(snobol4 PRIVATE SNOBOL_JIT_PLATFORM_LINUX=1)
    endif()
endif()
unset(_jit_arch_ok)

# JIT backend selection (compile-time; only one backend per binary).
# Valid values: "arm64" (default, requires ARM64 host)
#               "arm32" (requires ARMv7-A Thumb-2 host)
#               "riscv64" (requires RV64GC host)
#               "x86_64" (requires x86-64 host)
#               "sljit"  (uses SLJIT — cross-platform, replaces all custom backends)
set(SNOBOL_JIT_BACKEND "sljit" CACHE STRING "JIT backend to compile in (sljit only; arch-specific backends retired)")

set_property(CACHE SNOBOL_JIT_BACKEND PROPERTY STRINGS "sljit")
set(_valid_jit_backends "arm64;arm32;riscv64;x86_64;sljit")
if(NOT SNOBOL_JIT_BACKEND IN_LIST _valid_jit_backends)
    message(FATAL_ERROR
        "SNOBOL_JIT_BACKEND='${SNOBOL_JIT_BACKEND}' is not a recognised backend.\n"
        "Valid backends: ${_valid_jit_backends}"
    )
endif()
target_compile_definitions(snobol4 PRIVATE
    SNOBOL_JIT_BACKEND="${SNOBOL_JIT_BACKEND}"
)

# SLJIT-specific: disable the arch-detection-based JIT auto-enable since
# SLJIT handles cross-compilation. The existing SNOBOL_JIT=1 will be set
# by the arch detection block above; for sljit backend we keep it.
if(_using_sljit)
    target_compile_definitions(snobol4 PRIVATE SNOBOL_JIT_BACKEND_SLJIT=1)
endif()

# Optional: enable method JIT (whole-pattern compilation via SLJIT).
# Enabled by default when the SLJIT backend is used; the method JIT
# compiles a single trace starting at ip=0 and caches it for reuse.
# Disable with -DSNOBOL_JIT_METHOD=OFF to use only the tracing JIT.
option(SNOBOL_JIT_METHOD "Enable method JIT (whole-pattern compilation)" ON)
if(SNOBOL_JIT_METHOD)
    target_compile_definitions(snobol4 PRIVATE SNOBOL_JIT_METHOD=1)
endif()

# Optional: enable RV64C compressed instructions (RISC-V 64-bit only)
option(SNOBOL_JIT_RV64C "Enable RV64C compressed instructions on RISC-V 64-bit" OFF)
if(SNOBOL_JIT_RV64C AND CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
    target_compile_definitions(snobol4 PRIVATE SNOBOL_JIT_RV64C=1)
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

