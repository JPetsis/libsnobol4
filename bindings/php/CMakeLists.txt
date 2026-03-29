cmake_minimum_required(VERSION 3.16)

# PHP binding for libsnobol4
# Requires PHP 8.4+ development headers

project(libsnobol4_php VERSION 0.1.0 LANGUAGES C)

# Find PHP package
find_package(PHP REQUIRED COMPONENTS Phar)

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

message(STATUS "PHP extension directory: ${PHP_EXTENSION_DIR}")
message(STATUS "PHP include dirs: ${PHP_INCLUDE_DIRS}")
