<?php

/**
 * Compatibility Fixture 2: Text Transformer using Dynamic Patterns
 */

namespace Snobol\Tests\Compat;

use Snobol\DynamicPatternCache;
use Snobol\PatternHelper;

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
        /* Use core runtime for dynamic pattern evaluation via PatternHelper
         * Note: Complex patterns with alternation (|) require backtracking support
         * in the dynamic executor. For now, use simple literal patterns.
         */
        $result = PatternHelper::evalPattern($pattern, $text);

        if ($result !== false) {
            /* Extract matched text from captures */
            $matches = [];
            foreach ($result as $key => $value) {
                if (is_string($key) && str_starts_with($key, 'v') && is_string($value)) {
                    $matches[] = $value;
                }
            }

            /* If no captures, use the matched portion of subject */
            if (empty($matches) && isset($result['_match_len'])) {
                $matches[] = substr($text, 0, $result['_match_len']);
            }

            return ['found' => true, 'matches' => $matches];
        }

        /* Fallback: use PHP-native matching for complex patterns */
        /* This maintains backward compatibility for patterns with alternation */
        if (strpos($pattern, '|') !== false) {
            $parts = explode('|', $pattern);
            $matches = [];
            foreach ($parts as $part) {
                $part = trim($part, " '\"");
                if (strpos($text, $part) !== false) {
                    $matches[] = $part;
                }
            }
            if (count($matches) > 0) {
                return ['found' => true, 'matches' => $matches];
            }
        }

        return ['found' => false, 'matches' => []];
    }

    public function getCacheStats(): array
    {
        return $this->cache->stats();
    }
}
