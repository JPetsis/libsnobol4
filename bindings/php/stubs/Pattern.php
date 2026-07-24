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
    public static function compileFromAst($ast, ?array $options = null): static { return new static(); }

    /** @param string $source @return static */
    public static function fromString(string $source, ?array $options = null): static { return new static(); }

    /** @param string $subject @param array $options @return array */
    public function match(string $subject, array $options = []): array|false { return []; }

    /** @param string $subject @param string $replacement @return string */
    public function subst(string $subject, string $replacement): string { return ''; }

    /** @param mixed ...$callbacks @return static */
    public function setEvalCallbacks(...$callbacks): static { return $this; }

    /** @param bool $enabled @return static */
    public function setJit(bool $enabled): static { return $this; }

    /** @param string $subject @param array $options @return array */
    public function searchAll(string $subject, array $options = []): array { return []; }

    /** @param string $subject @return array|null */
    public function matchLiteral(string $subject): ?array { return null; }

    /** @param string $subject @param array $options @return array */
    public function searchSplit(string $subject, array $options = []): array { return []; }

    /** @param string $subject @param array $options @return array */
    public function searchSplitOffsets(string $subject, array $options = []): array { return []; }

    /** @param string $subject @return array */
    public function searchSplitCuts(string $subject): array { return []; }

    /** @param string $subject @param string $replacement @param array $options @return string */
    public function searchReplace(string $subject, string $replacement, array $options = []): string { return ''; }

    /** @param string $subject @return SearchIterator */
    public function searchAllGenerator(string $subject): SearchIterator {
        return new SearchIterator($this, $subject);
    }
}
