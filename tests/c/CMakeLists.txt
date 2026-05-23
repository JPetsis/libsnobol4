# C test suite for libsnobol4 core

# Test executable
add_executable(snobol4_tests
    test_runner.c
    test_vm.c
    test_tables.c
    test_dynamic_patterns.c
    test_table_ops.c
    test_template_ops.c
    test_control_flow.c
    test_backtracking.c
    test_stress_backtracking.c
    test_catastrophic.c
    test_jit_observability.c
    test_jit_cache.c
    test_jit_branches.c
    test_jit_profitability.c
    test_jit_cfg.c
    test_jit_opcode_coverage.c
    test_lexer.c
    test_parser.c
    test_ast.c
    # Built-in functions test suites
    test_pattern_breakx.c
    test_pattern_bal.c
    test_pattern_fence.c
    test_string_size.c
    test_string_transform.c
    test_string_ops.c
    test_string_char.c
    test_string_case.c
    test_comparison.c
    test_builtin_dispatch.c
    test_search_runtime.c
    test_search_jit.c
    test_fusion.c
    test_compact_choice.c
    # New: Unicode fold, case-insensitive patterns, API version
    test_unicode_fold.c
    test_pattern_case.c
    test_api_version.c
)

# Link against core library
target_link_libraries(snobol4_tests PRIVATE snobol4)

# Use C23 standard
target_compile_features(snobol4_tests PRIVATE c_std_23)

# Enable profiling for tests
target_compile_definitions(snobol4_tests PRIVATE SNOBOL_PROFILE=1)

# Propagate sanitizer flags to test binaries
if(SNOBOL_SANITIZE)
    target_compile_options(snobol4_tests PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(snobol4_tests PUBLIC
        -fsanitize=address,undefined
    )
endif()

# Enable JIT for tests only on ARM64 (micro-JIT emits ARM64 machine code)
if(NOT WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    target_compile_definitions(snobol4_tests PRIVATE SNOBOL_JIT=1)
endif()

# Warning flags for test sources - mirror core library settings
if(MSVC)
    target_compile_options(snobol4_tests PRIVATE
        /W4
        # Use C17 so nullptr is not a keyword (C23), allowing snobol_internal.h
        # compat #define nullptr NULL to work on MSVC 2022+.
        /std:c17
    )
    # Suppress MSVC safe-CRT deprecation warnings in test sources
    target_compile_definitions(snobol4_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
else ()
    target_compile_options(snobol4_tests PRIVATE
            -Wall -Wextra
            -Wno-unused-variable
            -Wno-unused-but-set-variable
            -Wno-unused-function
            -Wno-sign-conversion
            -Wno-implicit-int-conversion
            -Wno-shorten-64-to-32
    )
endif()

# Register with CTest
add_test(
    NAME snobol4_c_tests
    COMMAND snobol4_tests
)
