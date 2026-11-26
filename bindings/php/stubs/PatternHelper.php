<?php

/**
 * IDE stub for the Snobol\PatternHelper class.
 *
 * The real implementation is provided by the libsnobol4 PHP extension
 * (bindings/php/src/snobol_pattern_helper_php.c). This file exists only so
 * that IDEs can resolve the class and its methods. When the extension is
 * missing, a call fails loudly through the shared guard instead of returning
 * a value, so a stub call can never masquerade as a result.
 *
 * @generated from the extension method table — keep in sync with snobol_pattern_helper_php.c
 */

namespace Snobol;

require_once __DIR__ . '/_extension_guard.php';

class PatternHelper
{
    /** @param string $pattern @param array<string, mixed>|null $options @return Pattern */
    public static function fromString(string $pattern, ?array $options = null): Pattern { stub_extension_required(__METHOD__); }

    /** @param array<string, mixed> $ast @param array<string, mixed>|null $options @return Pattern */
    public static function fromAst(array $ast, ?array $options = null): Pattern { stub_extension_required(__METHOD__); }

    /** @param Pattern|array<string, mixed>|string $patternOrAst @param string $subject @param array<string, mixed>|null $options @return array|false|null Match result, null when nothing matched, false on an uncompiled pattern */
    public static function matchOnce($patternOrAst, string $subject, ?array $options = null): array|false|null { stub_extension_required(__METHOD__); }

    /** @param Pattern|array<string, mixed>|string $patternOrAst @param string $subject @param array<string, mixed>|null $options @return array */
    public static function matchAll($patternOrAst, string $subject, ?array $options = null): array { stub_extension_required(__METHOD__); }

    /** @param Pattern|array<string, mixed>|string $patternOrAst @param string $subject @param array<string, mixed>|null $options @return array */
    public static function split($patternOrAst, string $subject, ?array $options = null): array { stub_extension_required(__METHOD__); }

    /** @param Pattern|array<string, mixed>|string $patternOrAst @param string $replacement @param string $subject @param array<string, mixed>|null $options @return string */
    public static function replace($patternOrAst, string $replacement, string $subject, ?array $options = null): string { stub_extension_required(__METHOD__); }

    /** @return void */
    public static function clearCache(): void { stub_extension_required(__METHOD__); }

    /** @param string $patternExpr @param string $subject @param array<string, mixed>|null $options @return mixed */
    public static function evalPattern(string $patternExpr, string $subject, ?array $options = null): mixed { stub_extension_required(__METHOD__); }

    /** @param Table $table @param string $keyPattern @param string $template @param string $subject @return string */
    public static function tableSubst(Table $table, string $keyPattern, string $template, string $subject): string { stub_extension_required(__METHOD__); }

    /** @param Pattern|array<string, mixed>|string $patternOrAst @param string $template @param string $subject @param array<string, mixed>|null $options @return string */
    public static function formattedSubst($patternOrAst, string $template, string $subject, ?array $options = null): string { stub_extension_required(__METHOD__); }
}
