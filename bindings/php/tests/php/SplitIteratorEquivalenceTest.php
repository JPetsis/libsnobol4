<?php

declare(strict_types=1);

namespace Snobol\Tests;

use PHPUnit\Framework\Attributes\DataProvider;
use PHPUnit\Framework\TestCase;
use Snobol\Pattern;

/**
 * The lazy split iterator must yield exactly the segments the eager
 * searchSplit() returns, for every subject shape: no delimiter, a delimiter
 * at the end, only delimiters, and the empty subject. The segments must also
 * round-trip through implode() with the delimiter text.
 */
final class SplitIteratorEquivalenceTest extends TestCase
{
    /** @var string[] */
    private const SUBJECTS = ['kak', 'ka', 'ak', 'k', 'kk', ''];

    public function testGeneratorMatchesEagerSplitForEveryShape(): void
    {
        $pattern = Pattern::fromString("'k'");

        foreach (self::SUBJECTS as $subject) {
            $eager = $pattern->searchSplit($subject);
            $lazy = iterator_to_array($pattern->searchSplitGenerator($subject), false);

            $this->assertSame($eager, $lazy,
                'segments for subject ' . var_export($subject, true));
        }
    }

    public function testGeneratorKeepsSequentialKeys(): void
    {
        $pattern = Pattern::fromString("'k'");

        foreach (self::SUBJECTS as $subject) {
            $segments = iterator_to_array($pattern->searchSplitGenerator($subject));

            $this->assertSame(range(0, count($segments) - 1), array_keys($segments),
                'keys for subject ' . var_export($subject, true));
        }
    }

    public function testSegmentsReconstructTheSubject(): void
    {
        $pattern = Pattern::fromString("'k'");

        foreach (self::SUBJECTS as $subject) {
            $segments = iterator_to_array($pattern->searchSplitGenerator($subject), false);

            $this->assertSame($subject, implode('k', $segments),
                'implode round-trip for subject ' . var_export($subject, true));
        }
    }

    /**
     * @param string[] $expected
     */
    #[DataProvider('trailingShapes')]
    public function testTrailingEmptySegmentIsYielded(string $subject, array $expected): void
    {
        $pattern = Pattern::fromString("'k'");

        $this->assertSame($expected,
            iterator_to_array($pattern->searchSplitGenerator($subject), false));
    }

    /**
     * @return array<string, array{0: string, 1: string[]}>
     */
    public static function trailingShapes(): array
    {
        return [
            'delimiter at start and end' => ['kak', ['', 'a', '']],
            'delimiter at end' => ['ak', ['a', '']],
            'single delimiter' => ['k', ['', '']],
            'only delimiters' => ['kk', ['', '', '']],
            'empty subject' => ['', ['']],
        ];
    }

    public function testEarlyBreakStillStopsAfterN(): void
    {
        $pattern = Pattern::fromString("'k'");
        $seen = [];

        foreach ($pattern->searchSplitGenerator('kak') as $segment) {
            $seen[] = $segment;
            if (count($seen) === 2) {
                break;
            }
        }

        $this->assertSame(['', 'a'], $seen);
    }

    public function testNonLiteralDelimiterMatchesEagerSplit(): void
    {
        $pattern = Pattern::fromString("SPAN('0-9')");

        foreach (['abc123def', 'abc', '123', ''] as $subject) {
            $eager = $pattern->searchSplit($subject);
            $lazy = iterator_to_array($pattern->searchSplitGenerator($subject), false);

            $this->assertSame($eager, $lazy,
                'segments for subject ' . var_export($subject, true));
        }
    }
}
