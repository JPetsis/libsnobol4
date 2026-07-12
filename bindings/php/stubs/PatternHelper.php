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
    /** @param string $source @return static */
    public static function fromString($source): static { return new static(); }

    /** @param mixed $ast @return static */
    public static function fromAst($ast): static { return new static(); }

    /** @param mixed $pattern @param string $subject @return array|null */
    public static function matchOnce($pattern, $subject): ?array { return null; }

    /** @param mixed $pattern @param string $subject @return array */
    public static function matchAll($pattern, $subject): array { return []; }

    /** @param string $subject @param mixed $pattern @param int $limit @return array */
    public static function split($subject, $pattern, $limit = -1): array { return []; }

    /** @param string $subject @param mixed $pattern @param string $replacement @return string */
    public static function replace($subject, $pattern, $replacement): string { return ''; }

    /** @return void */
    public static function clearCache(): void {}

    /** @param string $subject @param string $pattern @return mixed */
    public static function evalPattern($subject, $pattern): mixed { return null; }

    /** @param string $subject @param mixed $table @return string */
    public static function tableSubst($subject, $table): string { return ''; }

    /** @param string $subject @param mixed $pattern @param mixed ...$args @return string */
    public static function formattedSubst($subject, $pattern, ...$args): string { return ''; }
}
