cmake_minimum_required(VERSION 3.16)

# PHP binding for libsnobol4
# Requires PHP 8.0+ development headers

project(libsnobol4_php VERSION 0.1.0 LANGUAGES C)

# Find php-config executable
find_program(PHP_CONFIG php-config
    HINTS
        $ENV{PHP_CONFIG}
        /usr/bin
        /usr/local/bin
        /opt/homebrew/bin
    DOC "php-config executable"
)

if(NOT PHP_CONFIG)
    message(FATAL_ERROR 
        "php-config not found. Please install PHP development package:\n"
        "  - Ubuntu/Debian: php-dev or phpX.X-dev\n"
        "  - macOS (brew): brew install php\n"
        "  - Windows: Use phpize from PHP SDK"
    )
endif()

message(STATUS "Found php-config: ${PHP_CONFIG}")

# Get PHP configuration using php-config
execute_process(
    COMMAND ${PHP_CONFIG} --includes
    OUTPUT_VARIABLE PHP_INCLUDES
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND ${PHP_CONFIG} --extension-dir
    OUTPUT_VARIABLE PHP_EXTENSION_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND ${PHP_CONFIG} --version
    OUTPUT_VARIABLE PHP_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

message(STATUS "PHP version: ${PHP_VERSION}")
message(STATUS "PHP extension dir: ${PHP_EXTENSION_DIR}")

# Parse include flags
string(REPLACE " " ";" PHP_INCLUDE_LIST "${PHP_INCLUDES}")
set(PHP_INCLUDE_DIRS "")
foreach(flag ${PHP_INCLUDE_LIST})
    if(flag MATCHES "^-I(.+)$")
        list(APPEND PHP_INCLUDE_DIRS "${CMAKE_MATCH_1}")
    endif()
endforeach()

# PHP extension module target
add_library(snobol_php MODULE
    src/php_snobol.c
    src/snobol_table_php.c
    src/snobol_dynamic_pattern_php.c
)

# Link against core library
target_link_libraries(snobol_php PRIVATE snobol4)

# PHP extension configuration
target_include_directories(snobol_php PRIVATE
    ${PHP_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/core/include
)

# Set PHP extension properties
set_target_properties(snobol_php PROPERTIES
    PREFIX ""
    OUTPUT_NAME "snobol"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/php"
)

# Add compile definitions for PHP extension
target_compile_definitions(snobol_php PRIVATE
    HAVE_CONFIG_H
    PHP_BUILD=1
    ZEND_COMPILE_DL_EXT=1
)

# Installation
install(TARGETS snobol_php
    LIBRARY DESTINATION ${PHP_EXTENSION_DIR}
)

message(STATUS "PHP include dirs: ${PHP_INCLUDE_DIRS}")
