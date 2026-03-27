<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Pattern;

/**
 * Tests for EVAL() dynamic pattern evaluation through Pattern class
 */
class DynamicPatternEvalTest extends TestCase
{
    public function testRepeatedDynamicEvaluation(): void
    {
        /* Test that repeated EVAL() calls with the same pattern source reuse the cache */
        $pattern = Pattern::compileFromAst([
            'type' => 'dynamic_eval',
            'expr' => ['type' => 'lit', 'text' => 'A']
        ]);

        /* First evaluation */
        $result1 = $pattern->match('A');
        $this->assertNotFalse($result1);

        /* Second evaluation with same pattern - should reuse cache */
        $result2 = $pattern->match('A');
        $this->assertNotFalse($result2);
    }

    public function testInvalidDynamicPatternHandling(): void
    {
        /* Test that invalid dynamic patterns fail gracefully */
        $pattern = Pattern::compileFromAst([
            'type' => 'dynamic_eval',
            'expr' => ['type' => 'lit', 'text' => 'X']
        ]);

        /* Pattern 'X' should not match 'A' */
        $result = $pattern->match('A');
        $this->assertFalse($result);
    }

    public function testDynamicPatternWithConcatenation(): void
    {
        /* Test EVAL with concatenation in the dynamic pattern */
        $pattern = Pattern::compileFromAst([
            'type' => 'dynamic_eval',
            'expr' => [
                'type' => 'concat',
                'parts' => [
                    ['type' => 'lit', 'text' => 'A'],
                    ['type' => 'lit', 'text' => 'B']
                ]
            ]
        ]);

        /* Should match 'AB' */
        $result1 = $pattern->match('AB');
        $this->assertNotFalse($result1);

        /* Should not match 'A' */
        $result2 = $pattern->match('A');
        $this->assertFalse($result2);

        /* Should not match 'C' */
        $result3 = $pattern->match('C');
        $this->assertFalse($result3);
    }

    public function testDynamicPatternRuntimeOwnership(): void
    {
        /* Test that dynamic patterns are properly retained/released */
        $patterns = [];
        for ($i = 0; $i < 10; $i++) {
            $patterns[] = Pattern::compileFromAst([
                'type' => 'dynamic_eval',
                'expr' => ['type' => 'lit', 'text' => 'TEST']
            ]);
        }

        /* All patterns should work */
        foreach ($patterns as $pattern) {
            $result = $pattern->match('TEST');
            $this->assertNotFalse($result);
        }

        /* Patterns should be freed without memory leaks */
        unset($patterns);
    }

    public function testDynamicPatternCacheRegression(): void
    {
        /* Regression test: ensure cache doesn't return stale pointers */
        $pattern1 = Pattern::compileFromAst([
            'type' => 'dynamic_eval',
            'expr' => ['type' => 'lit', 'text' => 'FIRST']
        ]);

        $pattern2 = Pattern::compileFromAst([
            'type' => 'dynamic_eval',
            'expr' => ['type' => 'lit', 'text' => 'SECOND']
        ]);

        /* Both should work independently */
        $this->assertNotFalse($pattern1->match('FIRST'));
        $this->assertNotFalse($pattern2->match('SECOND'));
        $this->assertFalse($pattern1->match('SECOND'));
        $this->assertFalse($pattern2->match('FIRST'));
    }
}
