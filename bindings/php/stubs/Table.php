<?php

/**
 * IDE stub for the Snobol\Table class.
 *
 * The real implementation is provided by the libsnobol4 PHP extension
 * (bindings/php/src/snobol_table_php.c). This file exists only so that IDEs
 * can resolve the class and its methods. When the extension is missing, a
 * call fails loudly through the shared guard instead of returning a value, so
 * a stub call can never masquerade as a result.
 *
 * @generated from the extension method table — keep in sync with snobol_table_php.c
 */

namespace Snobol;

require_once __DIR__ . '/_extension_guard.php';

class Table
{
    /** @param string|null $name */
    public function __construct(?string $name = null) { stub_extension_required(__METHOD__); }

    /** @param string $key @return string|null Value, or null for an unset key */
    public function get(string $key): ?string { stub_extension_required(__METHOD__); }

    /** @param string $key @param string|null $value Setting null deletes the key (SNOBOL semantics) @return bool */
    public function set(string $key, ?string $value): bool { stub_extension_required(__METHOD__); }

    /** @param string $key @return bool */
    public function has(string $key): bool { stub_extension_required(__METHOD__); }

    /** @param string $key @return bool true when the key existed and was removed */
    public function delete(string $key): bool { stub_extension_required(__METHOD__); }

    /** @return void */
    public function clear(): void { stub_extension_required(__METHOD__); }

    /** @return int Number of set keys */
    public function size(): int { stub_extension_required(__METHOD__); }
}
