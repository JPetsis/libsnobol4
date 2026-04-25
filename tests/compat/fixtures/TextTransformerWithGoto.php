<?php

/**
 * Compatibility Fixture 5: Text Transformer using Labelled Control Flow
 *
 * Demonstrates label-wrapped classification patterns and forward gotos
 * that skip directly to the interesting part of a pattern.
 */

namespace Snobol\Tests\Compat;

use Snobol\Builder;
use Snobol\PatternHelper;

class TextTransformerWithGoto
{
    /**
     * Classify the first token type in $input.
     *
     * Uses individually-labelled patterns to try digit and word spans.
     * Labels serve as named pattern entry-points in the compiled bytecode.
     *
     * @return string 'number' | 'word' | 'other'
     */
    public function classifyFirstToken(string $input): string
    {
        /* Label wraps the span - demonstrates AST_LABEL compilation */
        $numberAst = Builder::label('number', Builder::span('0-9'));
        if (PatternHelper::matchOnce($numberAst, $input) !== false) {
            return 'number';
        }

        $wordAst = Builder::label('word', Builder::span('A-Za-z'));
        if (PatternHelper::matchOnce($wordAst, $input) !== false) {
            return 'word';
        }

        return 'other';
    }

    /**
     * Match a tagged segment: "[tag]" followed by alpha content.
     *
     * Uses a forward-goto to jump over the LABEL instruction straight
     * to the body span, demonstrating compiled GOTO → LABEL transfer:
     *
     *   concat( lit("[tag]"), goto("body"), label("body", span('A-Za-z ')) )
     *
     * @return array|false Match result or false
     */
    public function matchTaggedSegment(string $subject, string $tag)
    {
        $ast = Builder::concat([
            Builder::lit("[{$tag}]"),
            Builder::goto('body'),
            Builder::label('body', Builder::span('A-Za-z ')),
        ]);

        return PatternHelper::matchOnce($ast, $subject);
    }

    /**
     * Split subject on spaces and return segments whose first word
     * is found by a labelled span pattern.
     *
     * @return array List of non-empty words
     */
    public function extractWords(string $text): array
    {
        $ast = Builder::label('tok', Builder::span('A-Za-z0-9'));
        $matches = PatternHelper::matchAll($ast, $text);

        $words = [];
        foreach ($matches as $m) {
            if (isset($m['_match_len']) && $m['_match_len'] > 0) {
                /* _match_start is the position matched in the subject */
                $words[] = substr($text, $m['_match_start'] ?? 0, $m['_match_len']);
            }
        }

        return $words;
    }
}

