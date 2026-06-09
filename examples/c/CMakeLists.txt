# C examples for libsnobol4

# Simple example - basic pattern matching
add_executable(example_basic basic.c)
target_link_libraries(example_basic PRIVATE snobol4)
if(MSVC)
    target_compile_options(example_basic PRIVATE /std:c17 /W4)
    target_compile_definitions(example_basic PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_features(example_basic PRIVATE c_std_23)
endif()

# Example with captures
add_executable(example_captures captures.c)
target_link_libraries(example_captures PRIVATE snobol4)
if(MSVC)
    target_compile_options(example_captures PRIVATE /std:c17 /W4)
    target_compile_definitions(example_captures PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_features(example_captures PRIVATE c_std_23)
endif()

# Install examples
install(TARGETS example_basic example_captures
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
