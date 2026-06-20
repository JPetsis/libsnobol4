cmake_minimum_required(VERSION 3.16)

project(libsnobol4 VERSION 0.8.0 LANGUAGES C)

# Options
option(BUILD_TESTS "Build C test suite" ON)
option(BUILD_PHP "Build PHP binding" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(SNOBOL_PROFILE "Enable VM profiling" OFF)
option(SNOBOL_JIT "Enable micro-JIT" ON)
option(SNOBOL_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(SNOBOL_FUZZ "Build libFuzzer fuzz harnesses (requires Clang)" OFF)

# Sanitizer guard: not supported on MSVC
if(SNOBOL_SANITIZE AND MSVC)
    message(FATAL_ERROR "SNOBOL_SANITIZE=ON requires GCC or Clang. MSVC does not support -fsanitize=address,undefined.")
endif()

# Core library (always built)
add_subdirectory(core)

# Optional: C test suite
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/c)
endif()

# Optional: libFuzzer fuzz harnesses (Clang only)
if(SNOBOL_FUZZ)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        add_subdirectory(tests/fuzz)
    else()
        message(WARNING "SNOBOL_FUZZ=ON requires Clang; ignoring")
    endif()
endif()

# Optional: PHP binding
if(BUILD_PHP)
    add_subdirectory(bindings/php)
endif()

# Examples
add_subdirectory(examples/c)

# Optional: C microbenchmark suite (off by default)
option(BUILD_BENCH_C "Build C microbenchmark suite" OFF)
if(BUILD_BENCH_C)
    add_subdirectory(bench/c)
endif()

# Custom targets
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Removing all build artifacts"
)

# Sanitizer test target (only available when SNOBOL_SANITIZE=ON)
if(SNOBOL_SANITIZE)
    add_custom_target(test-asan
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running tests with AddressSanitizer + UndefinedBehaviorSanitizer"
    )
else()
    add_custom_target(test-asan
        COMMAND ${CMAKE_COMMAND} -E echo "Configure with -DSNOBOL_SANITIZE=ON to enable ASan/UBSan targets"
        COMMAND ${CMAKE_COMMAND} -E false
        COMMENT "test-asan requires SNOBOL_SANITIZE=ON"
    )
endif()

# Valgrind test target
find_program(VALGRIND valgrind)
if(VALGRIND)
    if(BUILD_TESTS)
        # Suppression file (optional — only passed if it exists)
        set(VALGRIND_SUPP_FILE "${CMAKE_SOURCE_DIR}/dev/valgrind.supp")
        if(EXISTS "${VALGRIND_SUPP_FILE}")
            set(VALGRIND_SUPP_ARG "--suppressions=${VALGRIND_SUPP_FILE}")
        else()
            set(VALGRIND_SUPP_ARG "")
        endif()

        # Plain terminal output target (interactive use)
        # --errors-for-leak-kinds=definite,indirect: only exit non-zero for
        # definite and indirect leaks; "possibly lost" / "still reachable"
        # (common in system/PHP allocators) are shown but do not fail the build.
        add_custom_target(test-valgrind
            COMMAND ${VALGRIND}
                    --error-exitcode=1
                    --leak-check=full
                    --show-leak-kinds=all
                    --track-origins=yes
                    --errors-for-leak-kinds=definite,indirect
                    ${VALGRIND_SUPP_ARG}
                    ${CMAKE_BINARY_DIR}/tests/c/snobol4_tests
            DEPENDS snobol4_tests
            COMMENT "Running tests under Valgrind memcheck"
        )

        # XML report target (CI / persistent report)
        add_custom_target(test-valgrind-report
            COMMAND ${VALGRIND}
                    --error-exitcode=1
                    --leak-check=full
                    --show-leak-kinds=all
                    --track-origins=yes
                    --errors-for-leak-kinds=definite,indirect
                    ${VALGRIND_SUPP_ARG}
                    --xml=yes
                    --xml-file=${CMAKE_BINARY_DIR}/valgrind-report.xml
                    ${CMAKE_BINARY_DIR}/tests/c/snobol4_tests
            DEPENDS snobol4_tests
            COMMENT "Running tests under Valgrind memcheck (XML report → build/valgrind-report.xml)"
        )
    endif()
else()
    message(WARNING "valgrind not found on PATH; 'test-valgrind' target will not be created. Install with: apt install valgrind (Linux) or brew install valgrind (macOS).")
endif()

# Benchmark target (if benchmark files exist)
if(EXISTS ${CMAKE_SOURCE_DIR}/bench)
    add_custom_target(bench
        COMMAND ${CMAKE_COMMAND} -E echo "Running benchmarks..."
        COMMAND ${CMAKE_COMMAND} -E echo "Benchmarks require PHP extension - use make bench instead"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Benchmark placeholder"
    )
endif()

# C benchmark target (runs snobol4_bench binary)
add_custom_target(bench-c
    COMMAND ${CMAKE_COMMAND} -E echo "==> Running C microbenchmarks..."
    COMMAND ${CMAKE_BINARY_DIR}/bench/c/snobol4_bench
    DEPENDS snobol4_bench
    COMMENT "Run C microbenchmark suite"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Installation summary
include(GNUInstallDirs)
install(CODE "
    message(STATUS \"libsnobol4 ${PROJECT_VERSION} installation complete\")
    message(STATUS \"  Headers:      \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/snobol/\")
    message(STATUS \"  Library:      \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/\")
    message(STATUS \"  CMake config: \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/cmake/libsnobol4/\")
    message(STATUS \"  pkg-config:   \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/pkgconfig/libsnobol4.pc\")
")
