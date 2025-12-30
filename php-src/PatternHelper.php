<?php

namespace Snobol;

/**
 * High-level helper APIs for SNOBOL4-style pattern matching.
 *
 * This class provides convenience methods that wrap the native Snobol\Pattern class
 * from the C extension for common text processing tasks.
 *
 * Note: The native Snobol\Pattern class is registered by the C extension.
 * This PatternHelper class provides static helper methods.
 */
class PatternHelper
{
    private static ?PatternCache $cache = null;

    /**
     * Match a pattern once against a subject string.
     *
     * @param  string|array|Pattern  $patternOrAst  Pattern specification (string/AST/compiled Pattern)
     * @param  string  $subject  Subject string to match against
     * @param  array  $options  Optional flags: ['full' => true] for full-string match, ['cache' => false] to bypass cache
     * @return array|false Associative array of captures on success, false on no match
     */
    public static function matchOnce($patternOrAst, string $subject, array $options = [])
    {
        $pattern = self::resolvePattern($patternOrAst, $options['cache'] ?? true);

        $result = $pattern->match($subject);

        if ($result !== false && ($options['full'] ?? false)) {
            $matchLen = $result['_match_len'] ?? 0;
            if ($matchLen !== strlen($subject)) {
                return false;
            }
        }

        return $result;
    }

    /**
     * Resolve a pattern specification to a compiled Pattern instance.
     *
     * @param  string|array|self  $patternOrAst  Pattern specification
     * @param  bool  $useCache  Whether to use the cache
     * @return self Compiled pattern
     */
    private static function resolvePattern($patternOrAst, bool $useCache): Pattern
    {
        if ($patternOrAst instanceof Pattern) {
            return $patternOrAst;
        }

        if (is_string($patternOrAst)) {
            if ($useCache) {
                return self::getCache()->get($patternOrAst, fn() => self::fromString($patternOrAst));
            }
            return self::fromString($patternOrAst);
        }

        if (is_array($patternOrAst)) {
            if ($useCache) {
                $key = self::canonicalizeAst($patternOrAst);
                return self::getCache()->get($key, fn() => self::fromAst($patternOrAst));
            }
            return self::fromAst($patternOrAst);
        }

        throw new \InvalidArgumentException(
            'Pattern must be a string, array (AST), or Pattern instance'
        );
    }

    /**
     * Get the global pattern cache instance.
     *
     * @return PatternCache
     */
    private static function getCache(): PatternCache
    {
        if (self::$cache === null) {
            self::$cache = new PatternCache();
        }
        return self::$cache;
    }

    /**
     * Compile a pattern from a textual SNOBOL-like pattern string.
     *
     * Note: Textual parsing is not yet fully implemented. This is a stub for future compatibility.
     *
     * @param  string  $pattern  Textual pattern representation
     * @return Pattern Compiled pattern instance
     * @throws \LogicException When textual parsing is not yet available
     */
    public static function fromString(string $pattern): Pattern
    {
        $parser = new Parser($pattern);
        return self::fromAst($parser->parse());
    }

    /**
     * Create a canonical string representation of an AST for cache keying.
     *
     * @param  array  $ast  AST array
     * @return string Canonical key
     */
    private static function canonicalizeAst(array $ast): string
    {
        return json_encode($ast, JSON_THROW_ON_ERROR);
    }

    /**
     * Compile a pattern from an AST produced by Snobol\Builder.
     *
     * @param  array  $ast  Structured array from Builder methods
     * @return Pattern Compiled pattern instance from the C extension
     * @throws \InvalidArgumentException When AST is structurally invalid
     */
    public static function fromAst(array $ast): Pattern
    {
        if (!isset($ast['type'])) {
            throw new \InvalidArgumentException('Invalid AST: missing "type" field');
        }

        try {
            $compiled = Pattern::compileFromAst($ast);
            if (!is_object($compiled) || !($compiled instanceof Pattern)) {
                throw new \RuntimeException('Extension compileFromAst did not return a Pattern object');
            }

            return $compiled;
        } catch (\InvalidArgumentException $e) {
            throw $e;
        } catch (\Throwable $e) {
            throw new \InvalidArgumentException(
                'Failed to compile pattern from AST: '.$e->getMessage(),
                0,
                $e
            );
        }
    }


    /**
     * Find all non-overlapping matches of a pattern in a subject string.
     *
     * @param  string|array|Pattern  $patternOrAst  Pattern specification
     * @param  string  $subject  Subject string to match against
     * @param  array  $options  Optional flags
     * @return array Array of capture arrays, one per match
     */
    public static function matchAll($patternOrAst, string $subject, array $options = []): array
    {
        $pattern = self::resolvePattern($patternOrAst, $options['cache'] ?? true);

        $matches = [];
        $offset = 0;
        $subjectLen = strlen($subject);

        // Manual scan because Pattern::match is anchored
        while ($offset <= $subjectLen) { // Allow matching empty string at end
            $remaining = substr($subject, $offset);
            $result = $pattern->match($remaining);

            if ($result === false) {
                // No match at current offset, advance by 1
                $offset++;
                continue;
            }

            // If match returned boolean true, use estimate
            if ($result === true) {
                $matchLen = self::estimateMatchLength($remaining, []);
                $matches[] = []; // Placeholder
            } else {
                // Clean up metadata
                $matchLen = 0;
                if (isset($result['_match_len'])) {
                    $matchLen = $result['_match_len'];
                    unset($result['_match_len']);
                } else {
                    // Fallback estimate if metadata not available
                    $matchLen = self::estimateMatchLength($remaining, $result);
                }
                $matches[] = $result;
            }

            // Advance by match length (min 1 to avoid infinite loop on empty match)
            $offset += max(1, $matchLen);
        }

        return $matches;
    }

    /**
     * Estimate the length of a match from the match result.
     * This is a heuristic; ideally the VM would tell us the match length.
     *
     * @param  string  $subject  Subject that was matched
     * @param  array  $result  Match result with captures
     * @return int Estimated match length
     */
    private static function estimateMatchLength(string $subject, array $result): int
    {
        // Simplified: look at the longest capture value
        // A proper implementation would track match positions in the VM
        $maxLen = 0; // Default to 0, let caller handle min 1 if needed
        foreach ($result as $value) {
            if (is_string($value)) {
                $maxLen = max($maxLen, strlen($value));
            }
        }
        return $maxLen;
    }

    /**
     * Split a subject string by pattern matches.
     *
     * @param  string|array|self  $patternOrAst  Pattern specification (used as delimiter)
     * @param  string  $subject  Subject string to split
     * @param  array  $options  Optional flags
     * @return array Array of string segments between matches
     */
    public static function split($patternOrAst, string $subject, array $options = []): array
    {
        $pattern = self::resolvePattern($patternOrAst, $options['cache'] ?? true);

        $segments = [];
        $offset = 0;
        $lastMatchEnd = 0;
        $subjectLen = strlen($subject);

        while ($offset <= $subjectLen) {
            $remaining = substr($subject, $offset);
            $result = $pattern->match($remaining);

            if ($result === false) {
                $offset++;
                continue;
            }

            // Match found at $offset
            // Add segment before this match
            $segments[] = substr($subject, $lastMatchEnd, $offset - $lastMatchEnd);

            $matchLen = 0;
            if ($result === true) {
                $matchLen = self::estimateMatchLength($remaining, []);
            } else {
                if (isset($result['_match_len'])) {
                    $matchLen = $result['_match_len'];
                    unset($result['_match_len']);
                } else {
                    $matchLen = self::estimateMatchLength($remaining, $result);
                }
            }

            $offset += max(1, $matchLen);
            $lastMatchEnd = $offset;
        }

        // Add remaining segment
        $segments[] = substr($subject, $lastMatchEnd);

        return $segments;
    }


    /**
     * Replace pattern matches with replacement text.
     *
     * @param  string|array|Pattern  $patternOrAst  Pattern specification
     * @param  string  $replacement  Replacement text (simple string for now)
     * @param  string  $subject  Subject string
     * @param  array  $options  Optional flags
     * @return string Result with replacements applied
     */
    public static function replace($patternOrAst, string $replacement, string $subject, array $options = []): string
    {
        $pattern = self::resolvePattern($patternOrAst, $options['cache'] ?? true);

        // Detect if template syntax is used ($v0, ${v1}, etc.)
        if (strpos($replacement, '$') !== false) {
            return $pattern->subst($subject, $replacement);
        }

        $result = '';
        $offset = 0;
        $lastMatchEnd = 0;

        while ($offset < strlen($subject)) {
            $remaining = substr($subject, $offset);
            $matchResult = $pattern->match($remaining);

            if ($matchResult === false) {
                $offset++;
                continue;
            }

            // Add segment before this match
            $result .= substr($subject, $lastMatchEnd, $offset - $lastMatchEnd);

            // Add replacement
            $result .= $replacement;

            $matchLen = 0;
            if ($matchResult === true) {
                $matchLen = self::estimateMatchLength($remaining, []);
            } elseif (isset($matchResult['_match_len'])) {
                $matchLen = $matchResult['_match_len'];
                unset($matchResult['_match_len']);
            } else {
                $matchLen = self::estimateMatchLength($remaining, $matchResult);
            }

            $offset += max(1, $matchLen);
            $lastMatchEnd = $offset;
        }

        // Add remaining segment
        $result .= substr($subject, $lastMatchEnd);

        return $result;
    }

    /**
     * Reset the global pattern cache (useful for testing).
     */
    public static function clearCache(): void
    {
        self::$cache = null;
    }
}

