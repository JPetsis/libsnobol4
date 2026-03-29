cmake_minimum_required(VERSION 3.16)

# Core libsnobol4 C library
# This builds the language-agnostic C23 pattern matching engine

project(libsnobol4_core VERSION 0.1.0 LANGUAGES C)

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
)

# Set C23 standard
target_compile_features(snobol4 PUBLIC c_std_23)

# Set up include directories
target_include_directories(snobol4
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Warning flags - disable unused variable warnings for now
# (Many variables are placeholders for future functionality)
# Also disable sign conversion warnings - these are intentional in bytecode/UTF-8 handling
target_compile_options(snobol4 PRIVATE
    -Wall -Wextra
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-unused-function
    -Wno-sign-conversion
    -Wno-implicit-int-conversion
    -Wno-shorten-64-to-32
)

# Optional: Enable VM profiling
option(SNOBOL_PROFILE "Enable VM profiling" OFF)
if(SNOBOL_PROFILE)
    target_compile_definitions(snobol4 PRIVATE SNOBOL_PROFILE=1)
endif()

# Enable micro-JIT by default
target_compile_definitions(snobol4 PRIVATE SNOBOL_JIT=1)

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
