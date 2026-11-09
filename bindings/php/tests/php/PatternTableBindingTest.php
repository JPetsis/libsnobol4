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
 * Semantics: a bound table read matches the STORED VALUE against the subject
 * at the current position (classic SNOBOL4).  The key is data: it is never
 * matched against or consumed from the subject.  The value is consumed on
 * success; an absent key fails.
 */
final class PatternTableBindingTest extends TestCase
{
    public function testBoundReadResolves(): void
    {
        $table = new Table();
        $table->set('k', 'value');

        $pattern = Pattern::fromString("T['k']");

        $this->assertFalse($pattern->match('value'), 'unbound read fails');

        $this->assertSame($pattern, $pattern->bindTables(['T' => $table]),
            'bindTables is fluent');

        $result = $pattern->match('value');
        $this->assertIsArray($result, 'bound read resolves');
        $this->assertSame(5, $result['_match_len'], 'the value is consumed');
    }

    public function testReadMatchesTheStoredValue(): void
    {
        $table = new Table();
        $table->set('k', 'value');
        $pattern = Pattern::fromString("T['k']")->bindTables(['T' => $table]);

        $this->assertIsArray($pattern->match('value'), 'stored value matches');
        $this->assertFalse($pattern->match('k'),
            'the key text alone does not match');

        $table->set('k', 'other');
        $this->assertFalse($pattern->match('value'),
            'a replaced value no longer matches');
        $this->assertIsArray($pattern->match('other'),
            'the new stored value matches');
    }

    public function testReadMatchesAtItsPosition(): void
    {
        $table = new Table();
        $table->set('k', 'value');
        $pattern = Pattern::fromString("'k' T['k']")->bindTables(['T' => $table]);

        $result = $pattern->match('kvalue');
        $this->assertIsArray($result, 'the value matches after the prefix');
        $this->assertSame(6, $result['_match_len']);
        $this->assertFalse($pattern->match('value'),
            'the value at the wrong position does not match');
    }

    public function testEmptyValueMatchesZeroWidth(): void
    {
        $table = new Table();
        $table->set('k', '');
        $pattern = Pattern::fromString("T['k']")->bindTables(['T' => $table]);

        $result = $pattern->match('');
        $this->assertIsArray($result, 'empty value matches an empty subject');
        $this->assertSame(0, $result['_match_len']);

        $result = $pattern->match('abc');
        $this->assertIsArray($result, 'empty value matches zero-width');
        $this->assertSame(0, $result['_match_len']);
    }

    public function testCaseInsensitiveValue(): void
    {
        $table = new Table();
        $table->set('k', 'Hello');
        $pattern = Pattern::fromString("T['k']", ['caseInsensitive' => true])
            ->bindTables(['T' => $table]);

        $this->assertIsArray($pattern->match('hello'),
            'case-insensitive value matches');
        $this->assertIsArray($pattern->match('HELLO'),
            'opposite case matches');

        $sensitive = Pattern::fromString("T['k']")
            ->bindTables(['T' => $table]);
        $this->assertFalse($sensitive->match('hello'),
            'case-sensitive read rejects a folded match');
    }

    public function testMissingKeyFails(): void
    {
        $table = new Table();
        $pattern = Pattern::fromString("T['k']")->bindTables(['T' => $table]);

        $this->assertFalse($pattern->match('value'),
            'a missing key fails like an unbound read');
    }

    public function testBoundWriteIsObservable(): void
    {
        $table = new Table();
        $pattern = Pattern::fromString("T['k'] = 'v'")->bindTables(['T' => $table]);

        $result = $pattern->match('v');

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

        $this->assertIsArray($read->match('value'), 'builder read resolves');
        $this->assertIsArray($write->match('v'), 'builder write matches');
        $this->assertSame('v', $writeTable->get('k'), 'builder write landed');
    }

    public function testReadValueIsCapturable(): void
    {
        $table = new Table();
        $table->set('k', 'size');

        /* Source form: $v1 names the capture register explicitly. */
        $source = Pattern::fromString("T['k'] \$v1")
            ->bindTables(['T' => $table]);
        $result = $source->match('size');
        $this->assertIsArray($result, 'source naming read matches');
        $this->assertSame('size', $result['v1'], 'v1 holds the matched value');

        /* Builder twin: cap(reg, sub) is the same construct. */
        $built = Pattern::compileFromAst(
            Builder::cap(1, Builder::tableAccess('T', Builder::lit('k')))
        )->bindTables(['T' => $table]);
        $result = $built->match('size');
        $this->assertIsArray($result, 'builder naming read matches');
        $this->assertSame('size', $result['v1'],
            'builder v1 holds the matched value');
    }

    public function testRegisterKeyWriteStoresUnderTheKey(): void
    {
        $table = new Table();
        $pattern = Pattern::fromString("('z' \$v1) 'q' T[\$v1] = 'x'")
            ->bindTables(['T' => $table]);

        $this->assertIsArray($pattern->match('zqx'),
            'register-key write matches');
        $this->assertSame('x', $table->get('z'),
            'the captured key addresses the write');
        $this->assertFalse($table->has('x'),
            'the value did not become the key');
    }

    public function testEmptyValueWriteStoresAnEmptyValue(): void
    {
        $table = new Table();
        $pattern = Pattern::fromString("T['k'] = ''")->bindTables(['T' => $table]);

        $this->assertIsArray($pattern->match(''),
            'empty-value write matches zero-width');
        $this->assertTrue($table->has('k'), 'the entry exists');
        $this->assertSame('', $table->get('k'), 'the value is empty');
    }

    public function testNonLiteralBuilderKeyThrowsADescriptiveError(): void
    {
        $this->expectException(\Exception::class);
        $this->expectExceptionMessage('table key');
        Pattern::compileFromAst(
            Builder::tableAccess('T', Builder::concat([
                Builder::lit('a'),
                Builder::lit('b'),
            ]))
        );
    }

    public function testSearchMethodsResolveBoundTables(): void
    {
        $table = new Table();
        $table->set('k', 'value');
        $pattern = Pattern::fromString("T['k']")->bindTables(['T' => $table]);

        $matches = $pattern->searchAll('valueXvalue');
        $this->assertCount(2, $matches, 'searchAll resolves both occurrences');

        $parts = $pattern->searchSplit('valueXvalue');
        $this->assertCount(3, $parts, 'searchSplit sees both values');
        $this->assertSame('X', $parts[1]);
    }

    public function testSplitGeneratorResolvesBoundTables(): void
    {
        $table = new Table();
        $table->set('k', 'value');
        $pattern = Pattern::fromString("T['k']")->bindTables(['T' => $table]);

        /* The iterator yields the same segments as searchSplit(), including
         * the empty trailing segment. */
        $segments = iterator_to_array($pattern->searchSplitGenerator('valueXvalue'), false);
        $this->assertSame(['', 'X', ''], $segments);
        $this->assertSame($pattern->searchSplit('valueXvalue'), $segments);
    }

    public function testClearingTheBinding(): void
    {
        $table = new Table();
        $table->set('k', 'value');
        $pattern = Pattern::fromString("T['k']");

        $this->assertIsArray($pattern->bindTables(['T' => $table])->match('value'));
        $this->assertSame($pattern, $pattern->bindTables([]));
        $this->assertFalse($pattern->match('value'), 'cleared binding fails again');
    }

    public function testRebindingReplacesTheTable(): void
    {
        $first = new Table();
        $second = new Table();
        $pattern = Pattern::fromString("T['k'] = 'v'");

        $this->assertIsArray($pattern->bindTables(['T' => $first])->match('v'));
        $this->assertSame('v', $first->get('k'), 'the first binding wrote to the first table');

        $this->assertIsArray($pattern->bindTables(['T' => $second])->match('v'));
        $this->assertSame('v', $second->get('k'),
            'the re-bound table received the next write');
    }

    public function testPatternRetainsTheTableObject(): void
    {
        $pattern = Pattern::fromString("T['k'] = 'v'");
        $table = new Table();
        $pattern->bindTables(['T' => $table]);
        unset($table); /* the pattern must keep it alive */

        $this->assertIsArray($pattern->match('v'),
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

        $this->assertFalse($pattern->match('value'),
            'a name not in the binding list fails like an unbound read');
    }
}
