<?php

namespace Snobol;

/**
 * In-memory cache for compiled SNOBOL4 patterns.
 *
 * Implements a simple LRU (Least Recently Used) eviction policy with a fixed capacity.
 */
class PatternCache
{
    private const DEFAULT_CAPACITY = 128;

    private array $cache = [];
    private array $accessOrder = [];
    private int $capacity;

    public function __construct(int $capacity = self::DEFAULT_CAPACITY)
    {
        if ($capacity < 1) {
            throw new \InvalidArgumentException('Cache capacity must be at least 1');
        }
        $this->capacity = $capacity;
    }

    /**
     * Get a pattern from cache or compute it using the provided factory.
     *
     * @param  string  $key  Canonical pattern key
     * @param  callable  $factory  Factory function to create the pattern if not cached: fn(): Pattern
     * @return Pattern Cached or newly created pattern
     */
    public function get(string $key, callable $factory): Pattern
    {
        if (isset($this->cache[$key])) {
            // Cache hit - update access order
            $this->touch($key);
            return $this->cache[$key];
        }

        // Cache miss - create new pattern
        $pattern = $factory();
        $this->put($key, $pattern);
        return $pattern;
    }

    /**
     * Update the access timestamp for a key.
     *
     * @param  string  $key  Cache key
     */
    private function touch(string $key): void
    {
        // Remove old position if exists
        if (($oldPos = array_search($key, $this->accessOrder, true)) !== false) {
            unset($this->accessOrder[$oldPos]);
        }

        // Add to end (most recent)
        $this->accessOrder[] = $key;
    }

    /**
     * Store a pattern in the cache.
     *
     * @param  string  $key  Canonical pattern key
     * @param  Pattern  $pattern  Pattern to cache
     */
    private function put(string $key, Pattern $pattern): void
    {
        // Evict if at capacity
        if (count($this->cache) >= $this->capacity && !isset($this->cache[$key])) {
            $this->evictLru();
        }

        $this->cache[$key] = $pattern;
        $this->touch($key);
    }

    /**
     * Evict the least recently used entry.
     */
    private function evictLru(): void
    {
        if (empty($this->accessOrder)) {
            return;
        }

        // Reindex access order array to ensure we get the first element
        $this->accessOrder = array_values($this->accessOrder);
        $lruKey = array_shift($this->accessOrder);
        unset($this->cache[$lruKey]);
    }

    /**
     * Clear all cached patterns.
     */
    public function clear(): void
    {
        $this->cache = [];
        $this->accessOrder = [];
    }

    /**
     * Get the current number of cached patterns.
     *
     * @return int Number of entries in cache
     */
    public function size(): int
    {
        return count($this->cache);
    }

    /**
     * Check if a key exists in the cache.
     *
     * @param  string  $key  Cache key
     * @return bool True if key exists
     */
    public function has(string $key): bool
    {
        return isset($this->cache[$key]);
    }
}

