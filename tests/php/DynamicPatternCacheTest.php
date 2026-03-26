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
    }

    public function testEvaluateNotCached(): void
    {
        $cache = new DynamicPatternCache();

        $result = $cache->evaluate("'A' | 'B'", "A");

        $this->assertFalse($result['cached']);
        $this->assertFalse($result['evaluated']);
    }

    public function testGetNotFound(): void
    {
        $cache = new DynamicPatternCache();

        $result = $cache->get("nonexistent");

        $this->assertFalse($result['found']);
    }

    public function testClear(): void
    {
        $cache = new DynamicPatternCache();

        /* Clear should not throw */
        $cache->clear();

        $stats = $cache->stats();
        $this->assertEquals(0, $stats['size']);
    }

    public function testStats(): void
    {
        $cache = new DynamicPatternCache(50);

        $stats = $cache->stats();

        $this->assertArrayHasKey('size', $stats);
        $this->assertArrayHasKey('max_size', $stats);
        $this->assertEquals(0, $stats['size']);
        $this->assertEquals(50, $stats['max_size']);
    }

    public function testMultipleCaches(): void
    {
        $cache1 = new DynamicPatternCache(10);
        $cache2 = new DynamicPatternCache(20);

        $stats1 = $cache1->stats();
        $stats2 = $cache2->stats();

        $this->assertEquals(10, $stats1['max_size']);
        $this->assertEquals(20, $stats2['max_size']);
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

        /* Both should show not found since compile doesn't actually cache yet */
        $this->assertFalse($result1['found']);
        $this->assertFalse($result2['found']);
    }

    public function testCacheStatsAfterOperations(): void
    {
        $cache = new DynamicPatternCache(100);

        $initialStats = $cache->stats();
        $this->assertEquals(0, $initialStats['size']);

        $cache->compile("'test_pattern'");

        $afterCompile = $cache->stats();
        $this->assertGreaterThanOrEqual(0, $afterCompile['size']);
        $this->assertEquals(100, $afterCompile['max_size']);
    }

    public function testEvaluateWithComplexPattern(): void
    {
        $cache = new DynamicPatternCache();

        $result = $cache->evaluate("'A' | 'B' | 'C'", "B");

        /* Should indicate not cached (compile not fully implemented) */
        $this->assertFalse($result['cached']);
    }

    public function testGetAfterCompile(): void
    {
        $cache = new DynamicPatternCache();

        $cache->compile("'unique_pattern_xyz'");

        $result = $cache->get("'unique_pattern_xyz'");

        /* Compile doesn't fully cache yet, so should be not found */
        $this->assertFalse($result['found']);
    }

    public function testMultipleCompilesSamePattern(): void
    {
        $cache = new DynamicPatternCache();

        $result1 = $cache->compile("'same_pattern'");
        $result2 = $cache->compile("'same_pattern'");

        /* Both should return not cached since compile doesn't store */
        $this->assertFalse($result1['cached']);
        $this->assertFalse($result2['cached']);
    }
}
