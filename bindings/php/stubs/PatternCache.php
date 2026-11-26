<?php

/**
 * IDE stub for the Snobol\PatternCache class.
 *
 * The real implementation is provided by the libsnobol4 PHP extension
 * (bindings/php/src/snobol_pattern_cache_php.c). This file exists only so
 * that IDEs can resolve the class and its methods. When the extension is
 * missing, a call fails loudly through the shared guard instead of returning
 * a value, so a stub call can never masquerade as a result.
 *
 * @generated from the extension method table — keep in sync with snobol_pattern_cache_php.c
 */

namespace Snobol;

require_once __DIR__ . '/_extension_guard.php';

class PatternCache
{
    /** @param int|null $capacity Capacities below 1 raise ValueError */
    public function __construct(?int $capacity = null) { stub_extension_required(__METHOD__); }

    /** @param string $key @param callable $factory Builds the value on a miss @return mixed Cached or factory-built value */
    public function get(string $key, callable $factory): mixed { stub_extension_required(__METHOD__); }

    /** @param string $key @return bool */
    public function has(string $key): bool { stub_extension_required(__METHOD__); }

    /** @return void */
    public function clear(): void { stub_extension_required(__METHOD__); }

    /** @return int Number of cached entries */
    public function size(): int { stub_extension_required(__METHOD__); }
}
