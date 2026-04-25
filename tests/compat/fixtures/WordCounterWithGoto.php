<?php

/**
 * Compatibility Fixture 4: Word Counter using Labelled Control Flow
 *
 * Demonstrates that label definitions compile correctly, and that
 * forward gotos (compiled to OP_GOTO) execute as expected.
 */

namespace Snobol\Tests\Compat;

use Snobol\Builder;
use Snobol\PatternHelper;

class WordCounterWithGoto
{
    /**
     * Match the first word in the subject using a labelled span pattern.
     *
     * Pattern: found: SPAN('A-Za-z')
     * The label wraps the span; the VM traverses OP_LABEL + span body.
     *
     * @return array|false Match result (with _match_len) or false on no match
     */
    public function matchLabeledWord(string $subject)
    {
        $ast = Builder::label('found', Builder::span('A-Za-z'));

        return PatternHelper::matchOnce($ast, $subject);
    }

    /**
     * Count alphabetic words in $text using a labelled character-class pattern.
     *
     * @return int Number of non-overlapping word matches
     */
    public function countWords(string $text): int
    {
        $ast = Builder::label('word', Builder::span('A-Za-z'));
        $all = PatternHelper::matchAll($ast, $text);

        return count($all);
    }

    /**
     * Match a subject that begins with the prefix ">>" and is followed by
     * alphabetic characters.  Uses a forward-goto pattern to skip straight
     * to the character-span check:
     *
     *   concat( lit(">>"), goto("content"), label("content", span('A-Za-z')) )
     *
     * Bytecode order: LIT(">>"), GOTO(content), LABEL(content), SPAN, ACCEPT
     * On ">>" + letters: match ">>", GOTO jumps past LABEL to SPAN body → match.
     *
     * @return array|false Match result or false on no match
     */
    public function matchPrefixedWord(string $subject)
    {
        $ast = Builder::concat([
            Builder::lit('>>'),
            Builder::goto('content'),
            Builder::label('content', Builder::span('A-Za-z')),
        ]);

        return PatternHelper::matchOnce($ast, $subject);
    }
}

