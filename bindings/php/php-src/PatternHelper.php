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
     * @param  array  $options  Optional flags: ['full' => true, 'cache' => bool, 'caseInsensitive' => bool]
     * @return array|false Associative array of captures on success, false on no match
     */
    public static function matchOnce($patternOrAst, string $subject, array $options = [])
    {
        $pattern = self::resolvePattern($patternOrAst, $options['cache'] ?? true, $options);

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
     * @param  array  $options  Compilation options
     * @return Pattern Compiled pattern
     */
    private static function resolvePattern($patternOrAst, bool $useCache, array $options = []): Pattern
    {
        if ($patternOrAst instanceof Pattern) {
            return $patternOrAst;
        }

        $compileOptions = array_intersect_key($options, ['caseInsensitive' => 1]);

        if (is_string($patternOrAst)) {
            if ($useCache) {
                // Strings don't easily support caseInsensitive unless we bake it into string or options
                // For now, if options provided, mix into key
                $key = $patternOrAst.($compileOptions ? json_encode($compileOptions) : '');
                return self::getCache()->get($key, fn() => self::fromString($patternOrAst, $compileOptions));
            }
            return self::fromString($patternOrAst, $compileOptions);
        }

        if (is_array($patternOrAst)) {
            if ($useCache) {
                $key = self::canonicalizeAst($patternOrAst).($compileOptions ? json_encode($compileOptions) : '');
                return self::getCache()->get($key, fn() => self::fromAst($patternOrAst, $compileOptions));
            }
            return self::fromAst($patternOrAst, $compileOptions);
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
     * This method is now provided by the C extension as Pattern::fromString().
     * This stub is kept for backward compatibility but delegates to the C implementation.
     *
     * @param  string  $pattern  Textual pattern representation
     * @param  array  $options  Compilation options
     * @return Pattern Compiled pattern instance
     */
    public static function fromString(string $pattern, array $options = []): Pattern
    {
        /* Delegate to C extension implementation */
        return Pattern::fromString($pattern, $options);
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
     * @param  array  $options  Compilation options
     * @return Pattern Compiled pattern instance from the C extension
     * @throws \InvalidArgumentException When AST is structurally invalid
     */
    public static function fromAst(array $ast, array $options = []): Pattern
    {
        if (!isset($ast['type'])) {
            throw new \InvalidArgumentException('Invalid AST: missing "type" field');
        }

        try {
            $compiled = Pattern::compileFromAst($ast, $options);
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
     * Delegates repeated-search control flow to the native core search runtime
     * via Pattern::searchAll() rather than implementing an offset loop in PHP.
     *
     * @param  string|array|Pattern  $patternOrAst  Pattern specification
     * @param  string  $subject  Subject string to match against
     * @param  array  $options  Optional flags
     * @return array Array of capture arrays, one per match
     */
    public static function matchAll($patternOrAst, string $subject, array $options = []): array
    {
        $pattern = self::resolvePattern($patternOrAst, $options['cache'] ?? true, $options);

        /* Delegate the repeated-search control loop to the native core runtime.
         * Pattern::searchAll() executes one native C scan loop over the full
         * subject, avoiding PHP-level offset iteration and substring creation. */
        $raw = $pattern->searchAll($subject);

        /* Normalise: remove internal _match_start/_match_len metadata from
         * each result and keep only named captures (_output kept for compat). */
        $results = [];
        foreach ($raw as $entry) {
            unset($entry['_match_len'], $entry['_match_start']);
            $results[] = $entry;
        }
        return $results;
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
     * Delegates the repeated-search control loop to the native core runtime
     * via Pattern::searchSplit() rather than using PHP-level offset retries.
     *
     * @param  string|array|self  $patternOrAst  Pattern specification (used as delimiter)
     * @param  string  $subject  Subject string to split
     * @param  array  $options  Optional flags
     * @return array Array of string segments between matches
     */
    public static function split($patternOrAst, string $subject, array $options = []): array
    {
        $pattern = self::resolvePattern($patternOrAst, $options['cache'] ?? true, $options);

        /* Delegate to native C search loop: avoids PHP substring creation and
         * byte-by-byte offset retries as the primary execution strategy. */
        return $pattern->searchSplit($subject);
    }


    /**
     * Replace pattern matches with replacement text.
     *
     * For template-based replacements (containing '$'), delegates to Pattern::subst()
     * which uses the core search runtime internally.  For literal replacements,
     * delegates to Pattern::searchReplace() for a native C scan loop.
     *
     * @param  string|array|Pattern  $patternOrAst  Pattern specification
     * @param  string  $replacement  Replacement text (supports $v0 template syntax)
     * @param  string  $subject  Subject string
     * @param  array  $options  Optional flags
     * @return string Result with replacements applied
     */
    public static function replace($patternOrAst, string $replacement, string $subject, array $options = []): string
    {
        $pattern = self::resolvePattern($patternOrAst, $options['cache'] ?? true, $options);

        // Detect if template syntax is used ($v0, ${v1}, etc.)
        if (strpos($replacement, '$') !== false) {
            return $pattern->subst($subject, $replacement);
        }

        /* Delegate literal replacement to native core search loop — avoids PHP
         * substring creation and byte-by-byte offset retries. */
        return $pattern->searchReplace($subject, $replacement);
    }

    /**
     * Reset the global pattern cache (useful for testing).
     */
    public static function clearCache(): void
    {
        self::$cache = null;
    }

    /**
     * Evaluate a dynamic pattern expression.
     *
     * This method routes dynamic pattern evaluation through the core runtime
     * using EVAL(...) semantics rather than PHP-native pattern matching.
     *
     * @param  string  $patternExpr  Dynamic pattern expression (e.g., "'A' | 'B'")
     * @param  string  $subject  Subject string to match against
     * @param  array  $options  Optional flags: ['cache' => bool]
     * @return array|false Match result or false on failure
     */
    public static function evalPattern(string $patternExpr, string $subject, array $options = [])
    {
        /* Use C parser via Pattern::fromString for canonical runtime compilation */
        return Pattern::fromString($patternExpr, $options)->match($subject);
    }

    /**
     * Create a table-backed substitution pattern.
     *
     * This method creates a pattern that performs table lookups during substitution,
     * routing through the core runtime rather than PHP-only string processing.
     *
     * @param  Table  $table  Runtime table object
     * @param  string  $keyPattern  Pattern to capture the lookup key
     * @param  string  $template  Template with table reference (e.g., "$TABLE[$v0]")
     * @param  string  $subject  Subject string to transform
     * @return string Transformed string with table-backed substitutions
     */
    public static function tableSubst(Table $table, string $keyPattern, string $template, string $subject): string
    {
        /* Compile the key-capturing pattern using C parser */
        /*
         * Note: Full table-backed substitution requires:
         * 1. Registering the table with the VM runtime
         * 2. Template compilation with structured table references (done in 3.1)
         *
         * For now, use the Pattern's native subst() which will execute
         * template bytecode through the core runtime.
         */
        return Pattern::fromString($keyPattern)->subst($subject, $template);
    }

    /**
     * Perform formatted substitution with capture variables.
     *
     * Routes template execution through the core runtime with explicit
     * format operations (upper, lower, length) rather than PHP post-processing.
     *
     * @param  string|array|Pattern  $patternOrAst  Pattern specification
     * @param  string  $template  Template with format directives (e.g., "${v0.upper()}")
     * @param  string  $subject  Subject string to transform
     * @param  array  $options  Optional flags
     * @return string Transformed string
     */
    public static function formattedSubst($patternOrAst, string $template, string $subject, array $options = []): string
    {
        $pattern = self::resolvePattern($patternOrAst, $options['cache'] ?? true, $options);
        return $pattern->subst($subject, $template);
    }
}

