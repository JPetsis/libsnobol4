# Contributing to SNOBOL4 for PHP

Thank you for considering contributing to the SNOBOL4 for PHP extension! This document provides guidelines and
instructions for setting up your development environment, building the extension, and submitting changes.

## Development Environment

We recommend using **DDEV** for a consistent development environment that mirrors the production build process.

### Prerequisites

* [Docker Desktop](https://www.docker.com/products/docker-desktop) (or Colima/OrbStack on macOS)
* [DDEV](https://ddev.com/get-started/)

### Setup

1. **Fork and Clone** the repository:
   ```bash
   git clone https://github.com/YOUR_USERNAME/snobol4-ddev.git
   cd snobol4-ddev
   ```

2. **Start the Environment:**
   ```bash
   ddev start
   ```
   This command will:
    * Start the web and database containers.
    * Automatically compile the C extension from `snobol4-php/`.
    * Configure PHP to enable the `snobol.so` extension.

## Development Workflow

### Project Structure

* `snobol4-php/`: **(Core C Code)** This is where the magic happens. Modify `.c` and `.h` files here.
* `php-src/`: **(PHP Helpers)** Userland PHP classes (like the `Builder`) that interact with the extension.
* `public/`: **(Tests/Examples)** Entry points for testing the extension within the container.
* `.ddev/`: **(Config)** DDEV configuration files. The build logic is primarily in `.ddev/config.yaml`.

### Making Changes to C Code

If you modify files in `snobol4-php/`, you must **rebuild the extension** for changes to take effect.

The easiest way to rebuild is to restart DDEV, as the `post-start` hook handles the compilation:

```bash
ddev restart
```

For faster iteration, you can `ddev ssh` into the container and run the build commands manually:

```bash
ddev ssh
# Inside the container:
mkdir -p /tmp/snobol_build
cp -r /var/www/html/snobol4-php/* /tmp/snobol_build/
cd /tmp/snobol_build
phpize
./configure
make
sudo make install
# No need to restart php-fpm if you are running CLI scripts, otherwise:
sudo kill -USR2 $(pidof php-fpm8.4)
```

### Running Tests

We currently use a simple PHP script for testing.

1. **Run via DDEV exec:**
   ```bash
   ddev exec php public/test.php
   ```

2. **Or inside SSH:**
   ```bash
   ddev ssh
   php public/test.php
   ```

*Note: We plan to migrate to `PHPUnit` or `phpt` tests in the future. Contributions in this area are welcome!*

## Coding Standards

* **C Code:** Follow the style of existing files. Generally, we adhere to standard PHP extension development
  conventions (Zend API).
* **PHP Code:** Follow PSR-12 coding standards.

## Submitting Pull Requests

1. Create a new branch for your feature or fix:
   ```bash
   git checkout -b feature/my-new-feature
   ```
2. Commit your changes with clear, descriptive messages.
3. Push to your fork and submit a Pull Request (PR).
4. Ensure your PR description clearly explains the changes and any testing performed.

## License

By contributing, you agree that your contributions will be licensed under the [Apache License, Version 2.0](LICENSE).
