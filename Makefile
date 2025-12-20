.PHONY: help build test clean install test-valgrind build-asan test-asan

# Detect if we're in DDEV
DDEV := $(shell command -v ddev 2> /dev/null)
IN_DDEV := $(if $(IS_DDEV_PROJECT),1,0)

# ASan detection for PHP extension
ifeq ($(IN_DDEV),1)
    PHP_EXT_DIR := $(shell php-config --extension-dir 2>/dev/null)
    SNOBOL_SO := $(PHP_EXT_DIR)/snobol.so
    HAS_ASAN := $(shell if [ -f "$(SNOBOL_SO)" ] && ldd "$(SNOBOL_SO)" | grep -q libasan; then echo 1; else echo 0; fi)
    LIBASAN_PATH := $(shell find /usr/lib -name "libasan.so.[0-9]" | head -n 1)
    ifeq ($(HAS_ASAN),1)
        ASAN_ENV := USE_ZEND_ALLOC=0 ASAN_OPTIONS=detect_leaks=0 LD_PRELOAD=$(LIBASAN_PATH)
    endif
endif

help:
	@echo "SNOBOL4 Development Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  help     - Show this help message"
	@echo "  build    - Build the C core and PHP extension"
	@echo "  test     - Run C and PHP unit tests"
	@echo "  clean    - Remove build artifacts and test binaries"
	@echo "  install  - Install and enable the snobol extension"
	@echo "  test-valgrind - Run PHP tests under Valgrind"
	@echo "  build-asan - Build extension with AddressSanitizer"
	@echo "  test-asan - Run tests with AddressSanitizer enabled"
	@echo ""
	@echo "Environment detection:"
ifeq ($(IN_DDEV),1)
	@echo "  Running inside DDEV container"
else ifdef DDEV
	@echo "  DDEV available (use 'ddev exec make <target>')"
else
	@echo "  Native environment (no DDEV detected)"
endif

build:
ifeq ($(IN_DDEV),1)
	@echo "Building inside DDEV container..."
	@rm -rf /tmp/snobol_build
	@mkdir -p /tmp/snobol_build
	@cp -rf /var/www/html/snobol4-php/. /tmp/snobol_build/
	@cd /tmp/snobol_build && \
		phpize && \
		./configure --enable-snobol && \
		$(MAKE)
	@echo "Extension built successfully in /tmp/snobol_build/"
	@echo "Run 'make install' to install the extension"
	@echo "Build complete!"
else ifdef DDEV
	@echo "Running build inside DDEV..."
	@ddev exec make build
else
	@echo "Building in native environment..."
	@echo "Installing valgrind for native builds..."
	@sudo apt-get update && sudo apt-get install -y valgrind || \
		yum install -y valgrind || \
		brew install valgrind || \
		echo "Warning: Could not install valgrind automatically. Please install manually."
	@cd snobol4-php && \
		phpize && \
		./configure --enable-snobol && \
		$(MAKE)
	@echo "Build complete!"
endif

test:
	@echo "Running tests..."
ifeq ($(IN_DDEV),1)
	@echo "Running C tests..."
	@if [ -d tests/c ]; then \
		cd tests/c && $(MAKE) test || exit 1; \
	else \
		echo "No C tests found (tests/c/ does not exist)"; \
	fi
	@echo "Running PHP tests..."
	@if [ -f vendor/bin/phpunit ]; then \
		$(ASAN_ENV) vendor/bin/phpunit tests/php || exit 1; \
	else \
		echo "PHPUnit not found (run 'composer install' to enable PHP tests)"; \
	fi
else ifdef DDEV
	@echo "Running tests inside DDEV..."
	@ddev exec make test
else
	@echo "Running C tests..."
	@if [ -d tests/c ]; then \
		cd tests/c && $(MAKE) test || exit 1; \
	else \
		echo "No C tests found (tests/c/ does not exist)"; \
	fi
	@echo "Running PHP tests..."
	@if [ -f vendor/bin/phpunit ]; then \
		php vendor/bin/phpunit tests/php || exit 1; \
	else \
		echo "PHPUnit not found (run 'composer install' to enable PHP tests)"; \
	fi
endif
	@echo "All tests passed!"

clean:
	@echo "Cleaning build artifacts..."
ifeq ($(IN_DDEV),1)
	@rm -rf /tmp/snobol_build
endif
	@cd snobol4-php && \
		if [ -f Makefile ]; then $(MAKE) clean 2>/dev/null || true; fi && \
		phpize --clean 2>/dev/null || true
	@rm -rf snobol4-php/.libs snobol4-php/modules snobol4-php/*.lo snobol4-php/*.o
	@if [ -d tests/c ]; then \
		cd tests/c && $(MAKE) clean 2>/dev/null || true; \
	fi
	@echo "Clean complete!"

install:
ifeq ($(IN_DDEV),1)
	@echo "Installing extension inside DDEV..."
	@if [ -f /tmp/snobol_build/modules/snobol.so ]; then \
		sudo cp /tmp/snobol_build/modules/snobol.so /usr/lib/php/20240924/; \
		echo "Enabling extension for CLI and FPM..."; \
		echo "extension=snobol.so" | sudo tee /etc/php/8.4/mods-available/snobol.ini > /dev/null; \
		sudo ln -sf /etc/php/8.4/mods-available/snobol.ini /etc/php/8.4/cli/conf.d/20-snobol.ini; \
		sudo ln -sf /etc/php/8.4/mods-available/snobol.ini /etc/php/8.4/fpm/conf.d/20-snobol.ini; \
		sudo service php8.4-fpm reload 2>/dev/null || true; \
		echo "Extension installed and enabled!"; \
	else \
		echo "Error: Extension not built. Run 'make build' first."; \
		exit 1; \
	fi
else ifdef DDEV
	@echo "Installing extension via DDEV..."
	@ddev exec sudo make install
else
	@echo "Installing extension in native environment..."
	@cd snobol4-php && sudo $(MAKE) install
	@echo "Extension installed! Add 'extension=snobol.so' to your php.ini to enable."
endif

test-valgrind:
ifeq ($(IN_DDEV),1)
	@./dev/valgrind_phpunit.sh
else ifdef DDEV
	@ddev exec make test-valgrind
else
	@./dev/valgrind_phpunit.sh
endif

build-asan:
ifeq ($(IN_DDEV),1)
	@echo "Building with ASan inside DDEV..."
	@rm -rf /tmp/snobol_build
	@mkdir -p /tmp/snobol_build
	@cp -rf /var/www/html/snobol4-php/. /tmp/snobol_build/
	@cd /tmp/snobol_build && \
		phpize && \
		./configure --enable-snobol CFLAGS="-fsanitize=address -fno-omit-frame-pointer -g" LDFLAGS="-fsanitize=address" && \
		$(MAKE)
	@echo "ASan build complete. Run 'make install' to install."
else ifdef DDEV
	@ddev exec make build-asan
else
	@echo "Building with ASan natively..."
	@cd snobol4-php && \
		phpize && \
		./configure --enable-snobol CFLAGS="-fsanitize=address -fno-omit-frame-pointer -g" LDFLAGS="-fsanitize=address" && \
		$(MAKE)
endif

test-asan:
ifeq ($(IN_DDEV),1)
	@echo "Running tests with ASan..."
	@if [ -z "$(LIBASAN_PATH)" ]; then \
		echo "Error: libasan.so not found. Make sure gcc is installed."; \
		exit 1; \
	fi
	@USE_ZEND_ALLOC=0 ASAN_OPTIONS=detect_leaks=0 LD_PRELOAD=$(LIBASAN_PATH) $(MAKE) test
else ifdef DDEV
	@ddev exec make test-asan
else
	@echo "Running tests with ASan..."
	@USE_ZEND_ALLOC=0 $(MAKE) test
endif