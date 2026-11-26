<?php

/**
 * IDE stub for the Snobol\Pattern class.
 *
 * The real implementation is provided by the libsnobol4 PHP extension
 * (bindings/php/src/snobol_pattern.c). This file exists only so that IDEs can
 * resolve the class and its methods. When the extension is missing, a call
 * fails loudly through the shared guard instead of returning a value, so a
 * stub call can never masquerade as a result.
 *
 * @generated from the extension method table — keep in sync with snobol_pattern.c
 */

namespace Snobol;

require_once __DIR__ . '/_extension_guard.php';

class Pattern
{
    /** @param array<string, mixed> $ast @param array<string, mixed>|null $options @return Pattern */
    public static function compileFromAst(array $ast, ?array $options = null): Pattern { stub_extension_required(__METHOD__); }

    /** @param string $source @param array<string, mixed>|null $options @return Pattern */
    public static function fromString(string $source, ?array $options = null): Pattern { stub_extension_required(__METHOD__); }

    /** @param string $input @param array<string, mixed>|null $options @return array|false Match result, false when nothing matched */
    public function match(string $input, ?array $options = null): array|false { stub_extension_required(__METHOD__); }

    /** @param string $subject @param string $template @param array<string, Table>|null $tables @return string|false */
    public function subst(string $subject, string $template, ?array $tables = null): string|false { stub_extension_required(__METHOD__); }

    /** @param array<string, mixed> $callbacks @return true */
    public function setEvalCallbacks(array $callbacks): true { stub_extension_required(__METHOD__); }

    /** @param bool $enabled @return true */
    public function setJit(bool $enabled): true { stub_extension_required(__METHOD__); }

    /** @param array<string, Table> $tables name => Table; an empty array clears the binding @return static */
    public function bindTables(array $tables): static { stub_extension_required(__METHOD__); }

    /** @param string $subject @param array<string, mixed>|null $options @return array|false */
    public function searchAll(string $subject, ?array $options = null): array|false { stub_extension_required(__METHOD__); }

    /** @param string $input @return array|false */
    public function matchLiteral(string $input): array|false { stub_extension_required(__METHOD__); }

    /** @param string $subject @param array<string, mixed>|null $options @return array|false */
    public function searchSplit(string $subject, ?array $options = null): array|false { stub_extension_required(__METHOD__); }

    /** @param string $subject @param array<string, mixed>|null $options @return array|false */
    public function searchSplitOffsets(string $subject, ?array $options = null): array|false { stub_extension_required(__METHOD__); }

    /** @param string $subject @return array|false */
    public function searchSplitCuts(string $subject): array|false { stub_extension_required(__METHOD__); }

    /** @param string $subject @param string $replacement @param array<string, mixed>|null $options @return string|false */
    public function searchReplace(string $subject, string $replacement, ?array $options = null): string|false { stub_extension_required(__METHOD__); }

    /** @param string $subject @return SearchIterator */
    public function searchAllGenerator(string $subject): SearchIterator { stub_extension_required(__METHOD__); }

    /** @param string $subject @return SplitIterator */
    public function searchSplitGenerator(string $subject): SplitIterator { stub_extension_required(__METHOD__); }
}
