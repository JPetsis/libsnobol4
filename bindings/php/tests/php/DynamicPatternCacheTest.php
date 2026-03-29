<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\DynamicPatternCache;

class DynamicPatternCacheTest extends TestCase
{
    public function testCacheCreation(): void
    {
        $cache = new DynamicPatternCache();
        $this->assertInstanceOf(DynamicPatternCache::class, $cache);

        $stats = $cache->stats();
        $this->assertEquals(0, $stats['size']);
        $this->assertGreaterThan(0, $stats['max_size']);
    }

    public function testCacheCreationWithCustomSize(): void
    {
        $cache = new DynamicPatternCache(100);
        $stats = $cache->stats();
        $this->assertEquals(100, $stats['max_size']);
    }

    public function testCacheCreationWithZeroSize(): void
    {
        $cache = new DynamicPatternCache(0);
        $stats = $cache->stats();
        $this->assertGreaterThan(0, $stats['max_size']);
    }

    public function testCompileNotCached(): void
    {
        $cache = new DynamicPatternCache();

        $result = $cache->compile("'A' | 'B'");

        $this->assertFalse($result['cached']);
        $this->assertEquals("'A' | 'B'", $result['pattern']);
        $this->assertTrue($result['compiled']);
    }

    public function testCompileCached(): void
    {
        $cache = new DynamicPatternCache();

        /* First compile - not cached */
        $result1 = $cache->compile("'test_cached_pattern'");
        $this->assertFalse($result1['cached']);

        /* Second compile - should be cached */
        $result2 = $cache->compile("'test_cached_pattern'");
        $this->assertTrue($result2['cached']);
    }

    public function testEvaluateNotCached(): void
    {
        $cache = new DynamicPatternCache();

        $result = $cache->evaluate("'A' | 'B'", "A");

        $this->assertFalse($result['cached']);
        $this->assertTrue($result['evaluated']);
        $this->assertNotEmpty($result['matches']);
    }

    public function testEvaluateCached(): void
    {
        $cache = new DynamicPatternCache();

        /* First evaluate - not cached */
        $result1 = $cache->evaluate("'X' | 'Y'", "X");
        $this->assertFalse($result1['cached']);
        $this->assertTrue($result1['evaluated']);

        /* Second evaluate - should be cached */
        $result2 = $cache->evaluate("'X' | 'Y'", "Y");
        $this->assertTrue($result2['cached']);
        $this->assertTrue($result2['evaluated']);
    }

    public function testEvaluateWithAlternation(): void
    {
        $cache = new DynamicPatternCache();

        /* Test alternation pattern - should work with full backtracking */
        $result = $cache->evaluate("'hello' | 'world' | 'test'", "world");

        $this->assertTrue($result['evaluated']);
        $this->assertNotEmpty($result['matches']);
    }

    public function testEvaluateWithConcatenation(): void
    {
        $cache = new DynamicPatternCache();

        $result = $cache->evaluate("'hello' ' ' 'world'", "hello world");

        $this->assertTrue($result['evaluated']);
        $this->assertNotEmpty($result['matches']);
    }

    public function testGetNotFound(): void
    {
        $cache = new DynamicPatternCache();

        $result = $cache->get("nonexistent");

        $this->assertFalse($result['found']);
        $this->assertNull($result['pattern']);
    }

    public function testGetFound(): void
    {
        $cache = new DynamicPatternCache();

        $cache->compile("'findable_pattern'");
        $result = $cache->get("'findable_pattern'");

        $this->assertTrue($result['found']);
        $this->assertInstanceOf(\Snobol\Pattern::class, $result['pattern']);
    }

    public function testClear(): void
    {
        $cache = new DynamicPatternCache();

        /* Compile something */
        $cache->compile("'to_clear'");

        /* Clear should work without throwing */
        $cache->clear();

        $stats = $cache->stats();
        $this->assertEquals(0, $stats['size']);

        /* After clear, pattern should not be found */
        $result = $cache->get("'to_clear'");
        $this->assertFalse($result['found']);
    }

    public function testStats(): void
    {
        $cache = new DynamicPatternCache(50);

        $stats = $cache->stats();

        $this->assertArrayHasKey('size', $stats);
        $this->assertArrayHasKey('max_size', $stats);
        $this->assertArrayHasKey('compile_count', $stats);
        $this->assertArrayHasKey('evaluate_count', $stats);
        $this->assertEquals(0, $stats['size']);
        $this->assertEquals(50, $stats['max_size']);
    }

    public function testStatsAfterOperations(): void
    {
        $cache = new DynamicPatternCache(100);

        $cache->compile("'pattern1'");
        $cache->compile("'pattern2'");
        $cache->evaluate("'pattern3'", "test");

        $stats = $cache->stats();
        $this->assertEquals(3, $stats['size']);
        $this->assertEquals(3, $stats['compile_count']);  /* evaluate() calls compile() internally */
        $this->assertEquals(1, $stats['evaluate_count']);
    }

    public function testMultipleCaches(): void
    {
        $cache1 = new DynamicPatternCache(10);
        $cache2 = new DynamicPatternCache(20);

        $stats1 = $cache1->stats();
        $stats2 = $cache2->stats();

        $this->assertEquals(10, $stats1['max_size']);
        $this->assertEquals(20, $stats2['max_size']);

        /* Caches are isolated */
        $cache1->compile("'cache1_pattern'");
        $result = $cache2->get("'cache1_pattern'");
        $this->assertFalse($result['found']);
    }

    public function testCompileAndClear(): void
    {
        $cache = new DynamicPatternCache();

        $cache->compile("'test'");
        $cache->clear();

        $stats = $cache->stats();
        $this->assertEquals(0, $stats['size']);
    }

    public function testEvaluateWithEmptyString(): void
    {
        $cache = new DynamicPatternCache();

        $result = $cache->evaluate("", "");

        $this->assertFalse($result['cached']);
    }

    public function testGetWithEmptyString(): void
    {
        $cache = new DynamicPatternCache();

        $result = $cache->get("");

        $this->assertFalse($result['found']);
    }

    public function testCacheIsolation(): void
    {
        $cache1 = new DynamicPatternCache();
        $cache2 = new DynamicPatternCache();

        $cache1->compile("'from_cache1'");

        $result1 = $cache1->get("'from_cache1'");
        $result2 = $cache2->get("'from_cache1'");

        $this->assertTrue($result1['found']);
        $this->assertFalse($result2['found']);
    }

    public function testCacheStatsAfterOperations(): void
    {
        $cache = new DynamicPatternCache(100);

        $initialStats = $cache->stats();
        $this->assertEquals(0, $initialStats['size']);

        $cache->compile("'test_pattern'");

        $afterCompile = $cache->stats();
        $this->assertEquals(1, $afterCompile['size']);
        $this->assertEquals(100, $afterCompile['max_size']);
    }

    public function testEvaluateWithComplexPattern(): void
    {
        $cache = new DynamicPatternCache();

        $result = $cache->evaluate("'A' | 'B' | 'C'", "B");

        $this->assertFalse($result['cached']);
        $this->assertTrue($result['evaluated']);
    }

    public function testGetAfterCompile(): void
    {
        $cache = new DynamicPatternCache();

        $cache->compile("'unique_pattern_xyz'");

        $result = $cache->get("'unique_pattern_xyz'");

        $this->assertTrue($result['found']);
        $this->assertInstanceOf(\Snobol\Pattern::class, $result['pattern']);
    }

    public function testMultipleCompilesSamePattern(): void
    {
        $cache = new DynamicPatternCache();

        $result1 = $cache->compile("'same_pattern'");
        $result2 = $cache->compile("'same_pattern'");

        $this->assertFalse($result1['cached']);
        $this->assertTrue($result2['cached']);
    }

    public function testCacheEviction(): void
    {
        $cache = new DynamicPatternCache(3);

        /* Add more patterns than capacity */
        $cache->compile("'evict_1'");
        $cache->compile("'evict_2'");
        $cache->compile("'evict_3'");
        $cache->compile("'evict_4'");  /* Should evict evict_1 */

        $stats = $cache->stats();
        $this->assertEquals(3, $stats['size']);

        /* First pattern should be evicted */
        $result = $cache->get("'evict_1'");
        $this->assertFalse($result['found']);

        /* Last pattern should exist */
        $result = $cache->get("'evict_4'");
        $this->assertTrue($result['found']);
    }

    public function testEvaluateReturnsMatches(): void
    {
        $cache = new DynamicPatternCache();

        $result = $cache->evaluate("'hello'", "hello");

        $this->assertTrue($result['evaluated']);
        $this->assertIsArray($result['matches']);
    }

    public function testEvaluateFailure(): void
    {
        $cache = new DynamicPatternCache();

        $result = $cache->evaluate("'hello'", "goodbye");

        $this->assertFalse($result['evaluated']);
        $this->assertEmpty($result['matches']);
    }
}
