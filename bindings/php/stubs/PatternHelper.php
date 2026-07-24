<?php

/**
 * IDE stub for the Snobol\PatternHelper class.
 *
 * The real implementation is provided by the libsnobol4 PHP extension
 * (bindings/php/src/snobol_pattern_helper_php.c). This file exists only so
 * that IDEs can resolve the class and its methods; it is never loaded at
 * runtime because the extension already registers the class at startup.
 *
 * @generated from the extension method table — keep in sync with snobol_pattern_helper_php.c
 */

namespace Snobol;

class PatternHelper
{
    /** @param string $pattern @return static */
    public static function fromString(string $pattern, ?array $options = null): static { return new static(); }

    /** @param array $ast @return static */
    public static function fromAst(array $ast, ?array $options = null): static { return new static(); }

    /** @param Pattern|array|string $patternOrAst @param string $subject @return array|null */
    public static function matchOnce($patternOrAst, string $subject, ?array $options = null): ?array { return null; }

    /** @param Pattern|array|string $patternOrAst @param string $subject @return array */
    public static function matchAll($patternOrAst, string $subject, ?array $options = null): array { return []; }

    /** @param Pattern|array|string $patternOrAst @param string $subject @return array */
    public static function split($patternOrAst, string $subject, ?array $options = null): array { return []; }

    /** @param Pattern|array|string $patternOrAst @param string $replacement @param string $subject @return string */
    public static function replace($patternOrAst, string $replacement, string $subject, ?array $options = null): string { return ''; }

    /** @return void */
    public static function clearCache(): void {}

    /** @param string $patternExpr @param string $subject @return mixed */
    public static function evalPattern(string $patternExpr, string $subject, ?array $options = null): mixed { return null; }

    /** @param Table $table @param string $keyPattern @param string $template @param string $subject @return string */
    public static function tableSubst(Table $table, string $keyPattern, string $template, string $subject): string { return ''; }

    /** @param Pattern|array|string $patternOrAst @param string $template @param string $subject @return string */
    public static function formattedSubst($patternOrAst, string $template, string $subject, ?array $options = null): string { return ''; }
}
