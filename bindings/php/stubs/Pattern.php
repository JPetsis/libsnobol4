<?php

/**
 * IDE stub for the Snobol\Pattern class.
 *
 * The real implementation is provided by the libsnobol4 PHP extension
 * (bindings/php/src/snobol_pattern.c). This file exists only so that IDEs
 * (PHPStorm, etc.) can resolve the class and its methods; it is never loaded
 * at runtime because the extension already registers the class at startup.
 *
 * @generated from the extension method table — keep in sync with snobol_pattern.c
 */

namespace Snobol;

class Pattern
{
    /** @param mixed $ast @return static */
    public static function compileFromAst($ast): static { return new static(); }

    /** @param string $source @return static */
    public static function fromString($source): static { return new static(); }

    /** @param string $subject @param int|null $start @return array */
    public function match($subject, $start = null): array { return []; }

    /** @param string $subject @param string $replacement @return string */
    public function subst($subject, $replacement): string { return ''; }

    /** @param mixed ...$callbacks @return static */
    public function setEvalCallbacks(...$callbacks): static { return $this; }

    /** @param bool $enabled @return static */
    public function setJit($enabled): static { return $this; }

    /** @param string $subject @return array */
    public function searchAll($subject): array { return []; }

    /** @param string $subject @return array|null */
    public function matchLiteral($subject): ?array { return null; }

    /** @param string $subject @param int $limit @return array */
    public function searchSplit($subject, $limit = -1): array { return []; }

    /** @param string $subject @param int $limit @return array */
    public function searchSplitOffsets($subject, $limit = -1): array { return []; }

    /** @param string $subject @param string $replacement @return string */
    public function searchReplace($subject, $replacement): string { return ''; }
}
