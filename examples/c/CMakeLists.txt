# C examples for libsnobol4

# Simple example - basic pattern matching
add_executable(example_basic basic.c)
target_link_libraries(example_basic PRIVATE snobol4)
target_compile_features(example_basic PRIVATE c_std_23)

# Example with captures
add_executable(example_captures captures.c)
target_link_libraries(example_captures PRIVATE snobol4)
target_compile_features(example_captures PRIVATE c_std_23)

# Install examples
install(TARGETS example_basic example_captures
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
