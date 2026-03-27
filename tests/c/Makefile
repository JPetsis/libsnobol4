# C Test Runner for SNOBOL4

CC = gcc
# Add defines to map PHP memory functions to standard C functions
CFLAGS = -std=c11 -Wall -Wextra -I$(CORE_DIR) -g -O0 \
         -Derealloc=realloc -Defree=free -Demalloc=malloc -Destrndup=strndup \
         -DSTANDALONE_BUILD -DSNOBOL_PROFILE -DSNOBOL_JIT

# Test sources
TEST_SOURCES = test_runner.c test_vm.c test_tables.c test_dynamic_patterns.c test_table_ops.c test_template_ops.c test_control_flow.c test_backtracking.c test_stress_backtracking.c test_catastrophic.c test_jit_observability.c test_jit_cache.c test_jit_branches.c test_jit_profitability.c test_lexer.c test_parser.c test_ast.c
TEST_OBJECTS = $(TEST_SOURCES:.c=.o)
TEST_BINARY = test_runner

# SNOBOL4 core sources (relative to snobol4-core/)
CORE_DIR = ../../snobol4-core
CORE_SOURCES = snobol_vm.c snobol_jit.c snobol_table.c snobol_dynamic_pattern.c snobol_ast.c snobol_lexer.c snobol_parser.c
# Note: snobol_compiler.c excluded from standalone tests (requires PHP headers)
# Build object files in the current directory to avoid read-only errors
CORE_OBJECTS = $(CORE_SOURCES:.c=.o)

.PHONY: all test clean

all: $(TEST_BINARY)

$(TEST_BINARY): $(TEST_OBJECTS) $(CORE_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^

# Rule for test sources
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Rule for core sources
%.o: $(CORE_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_BINARY)
	@echo "Running C tests..."
	@./$(TEST_BINARY)

clean:
	# Remove binaries + objects, including any stale host-built artifacts accidentally mounted into the container
	rm -f $(TEST_OBJECTS) $(TEST_BINARY)
	rm -f $(CORE_OBJECTS)
