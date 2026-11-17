<?php

declare(strict_types=1);

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Pattern;

/**
 * Parser depth limits through Pattern::fromString: deep nesting must be
 * rejected with a parse error instead of exhausting the process stack.
 */
final class ParserDepthTest extends TestCase
{
    public function testDeepParenthesesAreRejected(): void
    {
        $source = str_repeat('(', 20000) . "'x'" . str_repeat(')', 20000);

        $this->expectException(\Exception::class);
        $this->expectExceptionMessage('nesting too deep');

        Pattern::fromString($source);
    }

    public function testDeepLabelPrefixesAreRejected(): void
    {
        $source = '';
        for ($i = 0; $i < 20000; $i++) {
            $source .= 'l' . $i . ': ';
        }
        $source .= "'x'";

        $this->expectException(\Exception::class);
        $this->expectExceptionMessage('nesting too deep');

        Pattern::fromString($source);
    }

    public function testDeepEvalNestingIsRejectedByTheStackBudget(): void
    {
        $source = str_repeat('EVAL(', 255) . "'x'" . str_repeat(')', 255);

        $this->expectException(\Exception::class);
        $this->expectExceptionMessage('parser stack limit');

        Pattern::fromString($source);
    }

    public function testNestingUnderTheLimitStillMatches(): void
    {
        $source = str_repeat('(', 100) . "'x'" . str_repeat(')', 100);
        $pattern = Pattern::fromString($source);

        $result = $pattern->match('x');

        $this->assertIsArray($result);
        $this->assertSame(1, $result['_match_len']);
    }
}
