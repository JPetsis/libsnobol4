<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Builder;
use Snobol\Pattern;
use Snobol\PatternCache;
use Snobol\PatternHelper;

class PatternCacheTest extends TestCase
{
    public function testCacheCreation(): void
    {
        $cache = new PatternCache();
        $this->assertEquals(0, $cache->size());
    }

    public function testCacheWithCustomCapacity(): void
    {
        $cache = new PatternCache(10);
        $this->assertEquals(0, $cache->size());
    }

    public function testCacheCapacityMustBePositive(): void
    {
        $this->expectException(\InvalidArgumentException::class);
        new PatternCache(0);
    }

    public function testCacheStoresAndRetrieves(): void
    {
        $cache = new PatternCache();
        $ast = Builder::lit("test");

        $pattern = $cache->get('test-key', fn() => PatternHelper::fromAst($ast));
        $this->assertInstanceOf(Pattern::class, $pattern);
        $this->assertEquals(1, $cache->size());
    }

    public function testCacheHitReusesPattern(): void
    {
        $cache = new PatternCache();
        $ast = Builder::lit("test");

        $pattern1 = $cache->get('test-key', fn() => PatternHelper::fromAst($ast));
        $pattern2 = $cache->get('test-key', fn() => PatternHelper::fromAst($ast));

        // Should be the same instance
        $this->assertSame($pattern1, $pattern2);
        $this->assertEquals(1, $cache->size());
    }

    public function testCacheEvictionAtCapacity(): void
    {
        $cache = new PatternCache(3);

        // Fill cache to capacity
        $cache->get('key1', fn() => PatternHelper::fromAst(Builder::lit("a")));
        $cache->get('key2', fn() => PatternHelper::fromAst(Builder::lit("b")));
        $cache->get('key3', fn() => PatternHelper::fromAst(Builder::lit("c")));

        $this->assertEquals(3, $cache->size());

        // Add one more - should evict LRU (key1)
        $cache->get('key4', fn() => PatternHelper::fromAst(Builder::lit("d")));

        $this->assertEquals(3, $cache->size());
        $this->assertFalse($cache->has('key1'));
        $this->assertTrue($cache->has('key2'));
        $this->assertTrue($cache->has('key3'));
        $this->assertTrue($cache->has('key4'));
    }

    public function testCacheLruEvictionOrder(): void
    {
        $cache = new PatternCache(2);

        // Add two entries
        $cache->get('key1', fn() => PatternHelper::fromAst(Builder::lit("a")));
        $cache->get('key2', fn() => PatternHelper::fromAst(Builder::lit("b")));

        // Access key1 to make it more recent
        $cache->get('key1', fn() => PatternHelper::fromAst(Builder::lit("a")));

        // Add key3 - should evict key2 (least recently used)
        $cache->get('key3', fn() => PatternHelper::fromAst(Builder::lit("c")));

        $this->assertTrue($cache->has('key1'));
        $this->assertFalse($cache->has('key2'));
        $this->assertTrue($cache->has('key3'));
    }

    public function testCacheClear(): void
    {
        $cache = new PatternCache();

        $cache->get('key1', fn() => PatternHelper::fromAst(Builder::lit("a")));
        $cache->get('key2', fn() => PatternHelper::fromAst(Builder::lit("b")));

        $this->assertEquals(2, $cache->size());

        $cache->clear();

        $this->assertEquals(0, $cache->size());
        $this->assertFalse($cache->has('key1'));
        $this->assertFalse($cache->has('key2'));
    }

    public function testCacheHasMethod(): void
    {
        $cache = new PatternCache();

        $this->assertFalse($cache->has('nonexistent'));

        $cache->get('test', fn() => PatternHelper::fromAst(Builder::lit("test")));

        $this->assertTrue($cache->has('test'));
    }
}

