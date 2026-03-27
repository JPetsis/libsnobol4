<?php

namespace Snobol;

/**
 * Dynamic Pattern Cache for runtime-compiled patterns.
 *
 * This class provides a PHP interface to the core runtime's dynamic pattern
 * caching system. It enables caching and reuse of dynamically compiled patterns
 * across multiple EVAL(...) operations.
 *
 * The cache uses source text as the cache key, enabling repeated dynamic pattern
 * evaluations with the same source to reuse previously compiled bytecode.
 *
 * @see PatternHelper::evalPattern() For high-level dynamic pattern evaluation
 */
class DynamicPatternCache
{
    private const DEFAULT_CAPACITY = 128;

    private array $cache = [];
    private array $accessOrder = [];
    private int $capacity;
    private int $compileCount = 0;
    private int $evaluateCount = 0;

    /**
     * Create a new dynamic pattern cache.
     *
     * @param  int  $capacity  Maximum number of patterns to cache (0 = use default)
     */
    public function __construct(int $capacity = self::DEFAULT_CAPACITY)
    {
        if ($capacity < 0) {
            throw new \InvalidArgumentException('Cache capacity must be non-negative');
        }
        $this->capacity = ($capacity > 0) ? $capacity : self::DEFAULT_CAPACITY;
    }

    /**
     * Evaluate a dynamic pattern against a subject string.
     *
     * @param  string  $patternSource  Pattern source text (e.g., "'A' | 'B'")
     * @param  string  $subject  Subject string to match against
     * @return array Evaluation result with keys:
     *   - 'cached': bool - Whether pattern was already cached
     *   - 'evaluated': bool - Whether evaluation succeeded
     *   - 'matches': array - Match captures on success
     */
    public function evaluate(string $patternSource, string $subject): array
    {
        $this->evaluateCount++;

        /* Check if already cached */
        $cached = isset($this->cache[$patternSource]);
        $pattern = $cached ? $this->cache[$patternSource] : null;

        if ($cached) {
            $this->touch($patternSource);
        } else {
            /* Compile and cache */
            $compileResult = $this->compile($patternSource);
            if (!$compileResult['compiled']) {
                return [
                    'cached' => false,
                    'evaluated' => false,
                    'matches' => [],
                    'error' => $compileResult['error'] ?? 'Compilation failed'
                ];
            }
            $pattern = $this->cache[$patternSource];
        }

        /* Execute the pattern */
        try {
            $result = $pattern->match($subject);
            return [
                'cached' => $cached,
                'evaluated' => ($result !== false),
                'matches' => ($result !== false) ? $result : []
            ];
        } catch (\Throwable $e) {
            return [
                'cached' => $cached,
                'evaluated' => false,
                'matches' => [],
                'error' => $e->getMessage()
            ];
        }
    }

    /**
     * Update the access timestamp for a key.
     *
     * @param  string  $key  Cache key
     */
    private function touch(string $key): void
    {
        /* Remove old position if exists */
        if (($oldPos = array_search($key, $this->accessOrder, true)) !== false) {
            unset($this->accessOrder[$oldPos]);
        }

        /* Add to end (most recent) */
        $this->accessOrder[] = $key;
    }

    /**
     * Compile a pattern source and cache it.
     *
     * @param  string  $patternSource  Pattern source text (e.g., "'A' | 'B'")
     * @return array Compilation result with keys:
     *   - 'cached': bool - Whether pattern was already cached
     *   - 'pattern': string - The pattern source
     *   - 'compiled': bool - Whether compilation succeeded
     */
    public function compile(string $patternSource): array
    {
        $this->compileCount++;

        /* Check if already cached */
        if (isset($this->cache[$patternSource])) {
            $this->touch($patternSource);
            return [
                'cached' => true,
                'pattern' => $patternSource,
                'compiled' => true
            ];
        }

        /* Parse and compile the pattern through the core runtime */
        try {
            $parser = new Parser($patternSource);
            $ast = $parser->parse();
            $pattern = PatternHelper::fromAst($ast);

            /* Cache the compiled pattern */
            $this->put($patternSource, $pattern);

            return [
                'cached' => false,
                'pattern' => $patternSource,
                'compiled' => true
            ];
        } catch (\Throwable $e) {
            return [
                'cached' => false,
                'pattern' => $patternSource,
                'compiled' => false,
                'error' => $e->getMessage()
            ];
        }
    }

    /**
     * Store a pattern in the cache.
     *
     * @param  string  $key  Pattern source text (cache key)
     * @param  Pattern  $pattern  Compiled pattern
     */
    private function put(string $key, Pattern $pattern): void
    {
        /* Evict if at capacity */
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

        /* Reindex access order array to ensure we get the first element */
        $this->accessOrder = array_values($this->accessOrder);
        $lruKey = array_shift($this->accessOrder);
        unset($this->cache[$lruKey]);
    }

    /**
     * Get a cached pattern by source.
     *
     * @param  string  $patternSource  Pattern source text
     * @return array Result with keys:
     *   - 'found': bool - Whether pattern was found
     *   - 'pattern': Pattern|null - The cached pattern if found
     */
    public function get(string $patternSource): array
    {
        if (isset($this->cache[$patternSource])) {
            $this->touch($patternSource);
            return [
                'found' => true,
                'pattern' => $this->cache[$patternSource]
            ];
        }

        return [
            'found' => false,
            'pattern' => null
        ];
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
     * Get cache statistics.
     *
     * @return array Statistics with keys:
     *   - 'size': int - Current number of cached patterns
     *   - 'max_size': int - Maximum capacity
     *   - 'compile_count': int - Total compile operations
     *   - 'evaluate_count': int - Total evaluate operations
     */
    public function stats(): array
    {
        return [
            'size' => count($this->cache),
            'max_size' => $this->capacity,
            'compile_count' => $this->compileCount,
            'evaluate_count' => $this->evaluateCount
        ];
    }
}
