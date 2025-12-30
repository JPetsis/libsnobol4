<?php

namespace Snobol;

/**
 * SNOBOL4 Pattern class (Native C Extension).
 *
 * This class is registered by the native C extension (snobol4-php/snobol_pattern.c)
 * and provides the core pattern matching functionality. This PHP file serves as
 * a type hint and documentation stub for IDEs and static analysis tools.
 *
 * The actual implementation is in C and provides:
 * - Pattern compilation from AST
 * - Pattern matching against subject strings
 * - Capture and assignment handling
 *
 * @see PatternHelper For high-level convenience methods
 * @see Builder For constructing pattern ASTs
 */
class Pattern
{
    /**
     * Private constructor - use Pattern::compileFromAst() to create instances.
     */
    private function __construct()
    {
        // Native implementation provided by C extension
    }

    /**
     * Compile a pattern from an AST produced by Snobol\Builder.
     *
     * This is a static factory method that compiles an AST representation
     * of a pattern into a Pattern object with internal bytecode.
     *
     * @param  array  $ast  Pattern AST from Builder methods
     * @return Pattern Compiled pattern object
     * @throws \Exception When AST compilation fails
     */
    public static function compileFromAst(array $ast): Pattern
    {
        // Native implementation in C extension
        // This method is never actually called - the C extension provides the real implementation
        throw new \LogicException('This is a stub - the C extension provides the actual implementation');
    }

    /**
     * Match this pattern against a subject string.
     *
     * Attempts to match the compiled pattern against the beginning of the subject string.
     * Returns an associative array of captured variables on success, or false on failure.
     *
     * Captured variables are returned as keys like 'v0', 'v1', etc., corresponding to
     * the variable indices used in assign() operations in the pattern AST.
     *
     * @param  string  $subject  The string to match against
     * @return array|false Associative array of captures ['v0' => 'value', ...] or false
     */
    public function match(string $subject)
    {
        // Native implementation in C extension
        throw new \LogicException('This is a stub - the C extension provides the actual implementation');
    }

    /**
     * Set evaluation callbacks for dynamic pattern matching.
     *
     * Allows PHP callbacks to be invoked during pattern matching for
     * advanced use cases (e.g., conditional matching logic).
     *
     * @param  array  $callbacks  Array of callback functions indexed by callback ID
     * @return void
     */
    public function setEvalCallbacks(array $callbacks): void
    {
        // Native implementation in C extension
        throw new \LogicException('This is a stub - the C extension provides the actual implementation');
    }

    /**
     * Perform streaming substitution using a template string.
     *
     * @param  string  $subject
     * @param  string  $template
     * @return string
     */
    public function subst(string $subject, string $template): string
    {
        // Native implementation in C extension
        throw new \LogicException('This is a stub - the C extension provides the actual implementation');
    }
}

