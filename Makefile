# libsnobol4 Makefile
# Simple wrapper around CMake build system
#
# Usage:
#   make          - Configure and build
#   make test     - Run tests
#   make clean    - Clean build artifacts
#   make help     - Show all targets

.PHONY: all build test clean distclean install help \
        build-debug build-release build-jit build-asan \
        test-verbose test-valgrind bench docs format lint warnings

# Default build type
BUILD_TYPE ?= Release
CMAKE_EXTRA_FLAGS ?=

# Detect if we're in DDEV
IN_DDEV := $(shell [ -n "$$IS_DDEV_PROJECT" ] && echo 1 || echo 0)

# DDEV wrapper
ifneq ($(IN_DDEV),1)
    DDEV := $(shell command -v ddev 2> /dev/null)
    ifneq ($(DDEV),)
        # DDEV available but not in project - wrap commands
        DDEV_EXEC = ddev exec
    endif
endif

# CMake command
CMAKE = cmake
CMAKE_BUILD = $(CMAKE) --build build
CMAKE_TEST = ctest --test-dir build

# Default target
all: build

# Configure and build
build:
	@echo "==> Configuring libsnobol4 ($(BUILD_TYPE))..."
	$(CMAKE) -B build \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DBUILD_TESTS=ON \
		-DBUILD_PHP=OFF \
		-DSNOBOL_JIT=ON \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		$(CMAKE_EXTRA_FLAGS)
	@echo "==> Building libsnobol4..."
	$(CMAKE_BUILD)
	@echo "==> Build complete!"
	@echo "==> compile_commands.json generated for IDE support"

# Build with different configurations
build-debug:
	@$(MAKE) build BUILD_TYPE=Debug

build-release:
	@$(MAKE) build BUILD_TYPE=Release

build-jit:
	@$(MAKE) build BUILD_TYPE=Release CMAKE_EXTRA_FLAGS="-DSNOBOL_JIT=ON -DSNOBOL_PROFILE=ON"

build-asan:
	@$(MAKE) build BUILD_TYPE=Debug \
		CMAKE_EXTRA_FLAGS="-DCMAKE_C_FLAGS='-fsanitize=address -fno-omit-frame-pointer -g'"

# Run tests
test:
	@echo "==> Running tests..."
	@if [ -d build ]; then \
		$(CMAKE_TEST) --output-on-failure; \
	else \
		echo "Build directory not found. Run 'make build' first."; \
		exit 1; \
	fi

test-verbose:
	@echo "==> Running tests (verbose)..."
	@if [ -d build ]; then \
		$(CMAKE_TEST) --verbose --output-on-failure; \
	else \
		echo "Build directory not found. Run 'make build' first."; \
		exit 1; \
	fi

# Run tests under Valgrind
test-valgrind:
	@echo "==> Running tests under Valgrind..."
	@if command -v valgrind >/dev/null 2>&1; then \
		if [ -f build/tests/c/snobol4_tests ]; then \
			valgrind \
				--leak-check=full \
				--show-leak-kinds=all \
				--track-origins=yes \
				--verbose \
				--error-exitcode=1 \
				./build/tests/c/snobol4_tests; \
		else \
			echo "Test executable not found. Run 'make build' first."; \
			exit 1; \
		fi; \
	else \
		echo "Valgrind not found. Install with:"; \
		echo "  macOS: brew install valgrind (may require --HEAD on Apple Silicon)"; \
		echo "  Linux: apt install valgrind (Debian/Ubuntu) or yum install valgrind (RHEL/CentOS)"; \
		exit 1; \
	fi

# Clean targets
clean:
	@echo "==> Cleaning build artifacts..."
	rm -rf build/
	@echo "==> Cleaning benchmark results..."
	find bench -maxdepth 1 -type f -name 'results_*.json' ! -name 'results_example.json' -delete 2>/dev/null || true
	@echo "==> Clean complete!"

distclean: clean
	@echo "==> Removing generated config files..."
	rm -f snobol4-core/config.h.in~
	rm -f snobol4-core/configure~
	@echo "==> Deep clean complete!"

# Install
install:
	@echo "==> Installing libsnobol4..."
	$(CMAKE) --install build

# Benchmark (requires PHP extension)
bench:
	@echo "==> Running benchmark suite..."
	@if command -v php >/dev/null 2>&1; then \
		for bench in bench/tokenize.php bench/replace.php bench/dates.php bench/backtracking.php; do \
			if [ -f "$$bench" ]; then \
				echo "Running $$bench..."; \
				php "$$bench"; \
			fi; \
		done; \
	else \
		echo "PHP not found. Benchmarks require PHP extension."; \
		echo "Run 'make build-php' to build PHP binding first."; \
	fi

# Documentation
docs:
	@echo "==> Generating documentation..."
	@if command -v doxygen >/dev/null 2>&1; then \
		if [ -f Doxyfile ]; then \
			doxygen Doxyfile; \
			echo "Documentation generated in html/"; \
		else \
			echo "Doxyfile not found. Creating basic documentation..."; \
		fi; \
	else \
		echo "Doxygen not installed. Install with: brew install doxygen (macOS) or apt install doxygen (Linux)"; \
	fi

# Code formatting
format:
	@echo "==> Formatting code..."
	@if command -v clang-format >/dev/null 2>&1; then \
		find core/include core/src tests/c -name '*.c' -o -name '*.h' | xargs clang-format -i; \
		echo "Code formatted!"; \
	else \
		echo "clang-format not found. Install with: brew install clang-format (macOS) or apt install clang-format (Linux)"; \
	fi

# Linting
lint:
	@echo "==> Running linter..."
	@if command -v clang-tidy >/dev/null 2>&1; then \
		if [ -d build ]; then \
			find core/include core/src tests/c -name '*.c' -o -name '*.h' | xargs clang-tidy; \
		else \
			echo "Build directory not found. Run 'make build' first."; \
		fi; \
	else \
		echo "clang-tidy not found. Options:"; \
		echo ""; \
		echo "  Option 1: Install LLVM toolchain with clang-tidy"; \
		echo "    macOS:  brew install llvm"; \
		echo "            (clang-tidy is included in LLVM)"; \
		echo "    Then ensure PATH includes LLVM bin:"; \
		echo "    export PATH=\"\$$PATH:/opt/homebrew/opt/llvm/bin\""; \
		echo ""; \
		echo "  Option 2: Use compiler warnings instead (no extra install)"; \
		echo "    make warnings"; \
		echo "    (Builds with -Wall -Wextra -Wpedantic -Werror)"; \
		echo ""; \
		echo "  Option 3: Run clang-tidy manually"; \
		echo "    clang-tidy core/src/*.c -- -Icore/include"; \
		echo "    (Uses compile_commands.json from build/)"; \
		exit 0; \
	fi

# Build with strict warnings (alternative to clang-tidy)
warnings:
	@echo "==> Building with strict warnings..."
	$(CMAKE) -B build \
		-DCMAKE_BUILD_TYPE=Debug \
		-DBUILD_TESTS=ON \
		-DBUILD_PHP=OFF \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		-DCMAKE_C_FLAGS="-Wall -Wextra -Wpedantic"
	$(CMAKE_BUILD)
	@echo "==> Build with strict warnings complete!"
	@echo "Note: -Werror omitted to allow unused placeholders in active development"

# PHP binding build
build-php:
	@echo "==> Building with PHP binding..."
	$(CMAKE) -B build \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DBUILD_TESTS=ON \
		-DBUILD_PHP=ON \
		-DSNOBOL_JIT=ON \
		$(CMAKE_EXTRA_FLAGS)
	$(CMAKE_BUILD)
	@echo "==> PHP binding build complete!"

# Help
help:
	@echo "libsnobol4 Build System"
	@echo "======================="
	@echo ""
	@echo "Build targets:"
	@echo "  all (default)  - Configure and build the project"
	@echo "  build          - Build with default configuration (Release)"
	@echo "  build-debug    - Build with Debug configuration"
	@echo "  build-release  - Build with Release configuration"
	@echo "  build-jit      - Build with JIT and profiling enabled"
	@echo "  build-asan     - Build with AddressSanitizer"
	@echo "  build-php      - Build with PHP binding (requires PHP dev headers)"
	@echo ""
	@echo "Test targets:"
	@echo "  test           - Run C test suite"
	@echo "  test-verbose   - Run tests with verbose output"
	@echo "  test-valgrind  - Run tests under Valgrind (memory leak detection)"
	@echo ""
	@echo "Clean targets:"
	@echo "  clean          - Remove build artifacts and benchmark results"
	@echo "  distclean      - Remove all generated files"
	@echo ""
	@echo "Other targets:"
	@echo "  install        - Install system-wide (requires sudo)"
	@echo "  bench          - Run benchmark suite (requires PHP)"
	@echo "  docs           - Generate documentation (requires Doxygen)"
	@echo "  format         - Format code (requires clang-format)"
	@echo "  lint           - Run clang-tidy (requires LLVM toolchain)"
	@echo "  warnings       - Build with strict warnings (alternative to lint)"
	@echo "  help           - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Build the project"
	@echo "  make build-debug        # Debug build"
	@echo "  make test               # Run tests"
	@echo "  make test-verbose       # Verbose test output"
	@echo "  make test-valgrind      # Run tests under Valgrind"
	@echo "  make clean              # Clean build"
	@echo "  make build-php          # Build with PHP binding"
	@echo "  make install            # Install system-wide"
	@echo ""
	@echo "Variables:"
	@echo "  BUILD_TYPE     - CMake build type (default: Release)"
	@echo "  CMAKE_EXTRA_FLAGS - Additional CMake flags"
	@echo ""
	@echo "Examples with variables:"
	@echo "  make build BUILD_TYPE=Debug"
	@echo "  make build CMAKE_EXTRA_FLAGS='-DBUILD_PHP=ON'"
