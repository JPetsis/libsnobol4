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

# Get PHP library path and libs
execute_process(
    COMMAND ${PHP_CONFIG} --ldflags
    OUTPUT_VARIABLE PHP_LDFLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND ${PHP_CONFIG} --libs
    OUTPUT_VARIABLE PHP_LIBS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Parse include flags
string(REPLACE " " ";" PHP_INCLUDE_LIST "${PHP_INCLUDES}")
set(PHP_INCLUDE_DIRS "")
foreach(flag ${PHP_INCLUDE_LIST})
    if(flag MATCHES "^-I(.+)$")
        list(APPEND PHP_INCLUDE_DIRS "${CMAKE_MATCH_1}")
    endif()
endforeach()

# Parse library flags for linking
set(PHP_LINK_LIBS "")
foreach(flag ${PHP_LDFLAGS} ${PHP_LIBS})
    if(flag MATCHES "^-L(.+)$")
        list(APPEND PHP_LINK_DIRS "${CMAKE_MATCH_1}")
    elseif(flag MATCHES "^-l(.+)$")
        list(APPEND PHP_LINK_LIBS "${CMAKE_MATCH_1}")
    endif()
endforeach()

# Add library search paths
if(PHP_LINK_DIRS)
    link_directories(${PHP_LINK_DIRS})
endif()

# Generate config.h for PHP extension
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
)

# PHP extension module target
add_library(snobol_php MODULE
    src/php_snobol.c
    src/snobol_table_php.c
    src/snobol_dynamic_pattern_php.c
)

# Link against core library and PHP
target_link_libraries(snobol_php PRIVATE snobol4 ${PHP_LINK_LIBS})

# PHP extension configuration
target_include_directories(snobol_php PRIVATE
    ${PHP_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_CURRENT_BINARY_DIR}
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
