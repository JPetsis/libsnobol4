<?php

/**
 * IDE stub for the Snobol\Array_ class.
 *
 * The real implementation is provided by the libsnobol4 PHP extension
 * (bindings/php/src/snobol_array_php.c). This file exists only so that IDEs
 * can resolve the class and its methods. When the extension is missing, a
 * call fails loudly through the shared guard instead of returning a value, so
 * a stub call can never masquerade as a result.
 *
 * @generated from the extension method table — keep in sync with snobol_array_php.c
 */

namespace Snobol;

require_once __DIR__ . '/_extension_guard.php';

class Array_
{
    /** @param int $size Initial capacity */
    public function __construct(int $size = 0) { stub_extension_required(__METHOD__); }

    /** @param int $key @return string|null Value, or null for an unset index */
    public function get(int $key): ?string { stub_extension_required(__METHOD__); }

    /** @param int $key @param string $value @return bool */
    public function set(int $key, string $value): bool { stub_extension_required(__METHOD__); }

    /** @param int $key @return bool */
    public function has(int $key): bool { stub_extension_required(__METHOD__); }

    /** @param int $key @return bool true when the index existed and was removed */
    public function delete(int $key): bool { stub_extension_required(__METHOD__); }

    /** @return void */
    public function clear(): void { stub_extension_required(__METHOD__); }

    /** @return int Number of set indices */
    public function size(): int { stub_extension_required(__METHOD__); }

    /** @return list<int> Set indices */
    public function keys(): array { stub_extension_required(__METHOD__); }

    /** @return list<string> Values in index order */
    public function values(): array { stub_extension_required(__METHOD__); }
}
