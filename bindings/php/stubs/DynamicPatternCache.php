<?php

/**
 * IDE stub for the Snobol\DynamicPatternCache class.
 *
 * The real implementation is provided by the libsnobol4 PHP extension
 * (bindings/php/src/snobol_dynamic_pattern_php.c). This file exists only so
 * that IDEs can resolve the class and its methods. When the extension is
 * missing, a call fails loudly through the shared guard instead of returning
 * a value, so a stub call can never masquerade as a result.
 *
 * @generated from the extension method table — keep in sync with snobol_dynamic_pattern_php.c
 */

namespace Snobol;

require_once __DIR__ . '/_extension_guard.php';

class DynamicPatternCache
{
    /** @param int|null $capacity Negative raises ValueError; null/0 use the default */
    public function __construct(?int $capacity = null) { stub_extension_required(__METHOD__); }

    /** @param string $pattern_source @return array{cached: bool, pattern: string, compiled: bool, error?: string} */
    public function compile(string $pattern_source): array { stub_extension_required(__METHOD__); }

    /** @param string $pattern_source @param string $subject @return array{cached: bool, evaluated: bool, matches: array|null} */
    public function evaluate(string $pattern_source, string $subject): array { stub_extension_required(__METHOD__); }

    /** @param string $pattern_source @return array{found: bool, pattern: Pattern|null} */
    public function get(string $pattern_source): array { stub_extension_required(__METHOD__); }

    /** @return void */
    public function clear(): void { stub_extension_required(__METHOD__); }

    /** @return array{size: int, max_size: int, compile_count: int, evaluate_count: int} */
    public function stats(): array { stub_extension_required(__METHOD__); }
}
