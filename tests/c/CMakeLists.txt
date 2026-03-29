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
    test_lexer.c
    test_parser.c
    test_ast.c
)

# Link against core library
target_link_libraries(snobol4_tests PRIVATE snobol4)

# Use C23 standard
target_compile_features(snobol4_tests PRIVATE c_std_23)

# Enable profiling and JIT for tests
target_compile_definitions(snobol4_tests PRIVATE
    SNOBOL_PROFILE=1
    SNOBOL_JIT=1
)

# Register with CTest
add_test(
    NAME snobol4_c_tests
    COMMAND snobol4_tests
)
