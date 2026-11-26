<?php

/**
 * IDE stub for the Snobol\SplitIterator class.
 *
 * The real implementation is provided by the libsnobol4 PHP extension
 * (bindings/php/src/snobol_split_iterator_php.c), which also registers the
 * class as an \Iterator. This file exists only so that IDEs can resolve the
 * class and its methods. When the extension is missing, a call fails loudly
 * through the shared guard instead of returning a value, so a stub call can
 * never masquerade as a result.
 *
 * @generated from the extension method table — keep in sync with snobol_split_iterator_php.c
 */

namespace Snobol;

require_once __DIR__ . '/_extension_guard.php';

class SplitIterator implements \Iterator
{
    /** @param Pattern $pattern Compiled pattern @param string $subject Subject to split @return static */
    public static function fromPattern(Pattern $pattern, string $subject): static { stub_extension_required(__METHOD__); }

    /** @return string|null Segment, or null before rewind() and on exhaustion */
    public function current(): mixed { stub_extension_required(__METHOD__); }

    /** @return int Sequence number of the current segment */
    public function key(): mixed { stub_extension_required(__METHOD__); }

    /** @return void */
    public function next(): void { stub_extension_required(__METHOD__); }

    /** @return bool */
    public function valid(): bool { stub_extension_required(__METHOD__); }

    /** @return void */
    public function rewind(): void { stub_extension_required(__METHOD__); }
}
