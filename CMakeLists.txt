cmake_minimum_required(VERSION 3.16)

project(libsnobol4 VERSION 0.1.0 LANGUAGES C)

# Options
option(BUILD_TESTS "Build C test suite" ON)
option(BUILD_PHP "Build PHP binding" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(SNOBOL_PROFILE "Enable VM profiling" OFF)
option(SNOBOL_JIT "Enable micro-JIT" ON)

# Core library (always built)
add_subdirectory(core)

# Optional: C test suite
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/c)
endif()

# Optional: PHP binding
if(BUILD_PHP)
    add_subdirectory(bindings/php)
endif()

# Examples
add_subdirectory(examples/c)

# Custom targets
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Removing all build artifacts"
)

# Benchmark target (if benchmark files exist)
if(EXISTS ${CMAKE_SOURCE_DIR}/bench)
    add_custom_target(bench
        COMMAND ${CMAKE_COMMAND} -E echo "Running benchmarks..."
        COMMAND ${CMAKE_COMMAND} -E echo "Benchmarks require PHP extension - use make bench instead"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Benchmark placeholder"
    )
endif()

# Installation summary
install(CODE "
    message(STATUS \"libsnobol4 ${PROJECT_VERSION} installation complete\")
    message(STATUS \"  Headers: ${CMAKE_INSTALL_FULL_INCLUDEDIR}/snobol/\")
    message(STATUS \"  Library: ${CMAKE_INSTALL_FULL_LIBDIR}/\")
    message(STATUS \"  CMake config: ${CMAKE_INSTALL_FULL_LIBDIR}/cmake/libsnobol4/\")
")
