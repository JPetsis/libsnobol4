<?php

declare(strict_types=1);

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Builder;
use Snobol\Pattern;
use Snobol\Table;
use TypeError;

/**
 * Pattern-level table binding coverage for the feat-pattern-table-binding
 * change: Pattern::bindTables() with Snobol\Table objects, resolution in
 * match()/search* methods, the Builder twins, clearing and re-binding.
 *
 * Semantics note: a bound table read resolves when the key exists in the
 * table (the read succeeds zero-width); classic SNOBOL4 matches the stored
 * value against the subject instead, which is not implemented (see
 * docs/SNOBOL4_COMPATIBILITY.md).
 */
final class PatternTableBindingTest extends TestCase
{
    public function testBoundReadResolves(): void
    {
        $table = new Table();
        $table->set('k', 'value');

        $pattern = Pattern::fromString("T['k']");

        $this->assertFalse($pattern->match('k'), 'unbound read fails');

        $this->assertSame($pattern, $pattern->bindTables(['T' => $table]),
            'bindTables is fluent');

        $result = $pattern->match('k');
        $this->assertIsArray($result, 'bound read resolves');
        $this->assertSame(1, $result['_match_len']);
    }

    public function testMissingKeyFails(): void
    {
        $table = new Table();
        $pattern = Pattern::fromString("T['k']")->bindTables(['T' => $table]);

        $this->assertFalse($pattern->match('k'),
            'a missing key fails like an unbound read');
    }

    public function testBoundWriteIsObservable(): void
    {
        $table = new Table();
        $pattern = Pattern::fromString("T['k'] = 'v'")->bindTables(['T' => $table]);

        $result = $pattern->match('kv');

        $this->assertIsArray($result, 'bound write matches');
        $this->assertSame('v', $table->get('k'), 'the write landed in the table');
    }

    public function testBuilderTwinsResolveIdentically(): void
    {
        $table = new Table();
        $table->set('k', 'value');

        $read = Pattern::compileFromAst(Builder::tableAccess('T', Builder::lit('k')))
            ->bindTables(['T' => $table]);
        $writeTable = new Table();
        $write = Pattern::compileFromAst(
            Builder::tableUpdate('T', Builder::lit('k'), Builder::lit('v'))
        )->bindTables(['T' => $writeTable]);

        $this->assertIsArray($read->match('k'), 'builder read resolves');
        $this->assertIsArray($write->match('kv'), 'builder write matches');
        $this->assertSame('v', $writeTable->get('k'), 'builder write landed');
    }

    public function testSearchMethodsResolveBoundTables(): void
    {
        $table = new Table();
        $table->set('k', 'value');
        $pattern = Pattern::fromString("T['k']")->bindTables(['T' => $table]);

        $matches = $pattern->searchAll('kak');
        $this->assertCount(2, $matches, 'searchAll resolves both occurrences');

        $parts = $pattern->searchSplit('kak');
        $this->assertCount(3, $parts, 'searchSplit sees both delimiters');
        $this->assertSame('a', $parts[1]);
    }

    public function testSplitGeneratorResolvesBoundTables(): void
    {
        $table = new Table();
        $table->set('k', 'value');
        $pattern = Pattern::fromString("T['k']")->bindTables(['T' => $table]);

        /* The lazy iterator skips an empty trailing segment (unlike
         * searchSplit, which appends it). */
        $segments = iterator_to_array($pattern->searchSplitGenerator('kak'));
        $this->assertSame(['', 'a'], array_values($segments));
    }

    public function testClearingTheBinding(): void
    {
        $table = new Table();
        $table->set('k', 'value');
        $pattern = Pattern::fromString("T['k']");

        $this->assertIsArray($pattern->bindTables(['T' => $table])->match('k'));
        $this->assertSame($pattern, $pattern->bindTables([]));
        $this->assertFalse($pattern->match('k'), 'cleared binding fails again');
    }

    public function testRebindingReplacesTheTable(): void
    {
        $first = new Table();
        $second = new Table();
        $pattern = Pattern::fromString("T['k'] = 'v'");

        $this->assertIsArray($pattern->bindTables(['T' => $first])->match('kv'));
        $this->assertSame('v', $first->get('k'), 'the first binding wrote to the first table');

        $this->assertIsArray($pattern->bindTables(['T' => $second])->match('kv'));
        $this->assertSame('v', $second->get('k'),
            'the re-bound table received the next write');
    }

    public function testPatternRetainsTheTableObject(): void
    {
        $pattern = Pattern::fromString("T['k'] = 'v'");
        $table = new Table();
        $pattern->bindTables(['T' => $table]);
        unset($table); /* the pattern must keep it alive */

        $this->assertIsArray($pattern->match('kv'),
            'the retained table still resolves after the caller drops its reference');
    }

    public function testNonTableValueThrows(): void
    {
        $pattern = Pattern::fromString("T['k']");

        $this->expectException(TypeError::class);
        $pattern->bindTables(['T' => new \stdClass()]);
    }

    public function testUnbindableNameFailsWithoutCrash(): void
    {
        $table = new Table();
        $table->set('k', 'value');
        $pattern = Pattern::fromString("T['k']")->bindTables(['OTHER' => $table]);

        $this->assertFalse($pattern->match('k'),
            'a name not in the binding list fails like an unbound read');
    }
}
