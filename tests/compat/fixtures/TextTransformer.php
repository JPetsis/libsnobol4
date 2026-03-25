<?php

/**
 * Compatibility Fixture 2: Text Transformer using Dynamic Patterns
 */

namespace Snobol\Tests\Compat;

use Snobol\DynamicPatternCache;

class TextTransformer
{
    private DynamicPatternCache $cache;

    public function __construct()
    {
        $this->cache = new DynamicPatternCache(32);
    }

    public function transform(string $text, string $mode): string
    {
        switch ($mode) {
            case 'upper':
                return strtoupper($text);
            case 'lower':
                return strtolower($text);
            case 'reverse':
                return strrev($text);
            case 'title':
                return ucwords(strtolower($text));
            default:
                return $text;
        }
    }

    public function transformWithPattern(string $text, string $pattern): array
    {
        /* Use dynamic pattern cache for repeated patterns */
        $compileResult = $this->cache->compile($pattern);

        /* Apply transformation based on pattern type */
        if (strpos($pattern, '|') !== false) {
            /* Alternation pattern - find matches */
            $parts = explode('|', $pattern);
            $matches = [];
            foreach ($parts as $part) {
                $part = trim($part, " '\"");
                if (strpos($text, $part) !== false) {
                    $matches[] = $part;
                }
            }
            return ['found' => count($matches) > 0, 'matches' => $matches];
        }

        return ['found' => false, 'matches' => []];
    }

    public function getCacheStats(): array
    {
        return $this->cache->stats();
    }
}
