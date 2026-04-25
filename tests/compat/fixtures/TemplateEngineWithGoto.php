<?php

/**
 * Compatibility Fixture 6: Template Engine using Labelled Control Flow
 *
 * Demonstrates label + goto in template-variable detection patterns:
 * - Builder::label wraps complex sub-patterns (AST_LABEL compilation)
 * - Forward goto skips directly to the content matcher (OP_GOTO transfer)
 */

namespace Snobol\Tests\Compat;

use Snobol\Builder;
use Snobol\PatternHelper;

class TemplateEngineWithGoto
{
    /**
     * Detect a "{VAR}" template variable using a labelled pattern.
     *
     * Pattern: label("var", concat( lit("{"), cap(0, span('A-Za-z')), lit("}") ))
     * The label wraps the full variable-detection sub-pattern; successful
     * compilation proves AST_LABEL + AST_CAP are integrated correctly.
     *
     * @return array|false Match result with 'v0' capture, or false
     */
    public function detectVariable(string $template)
    {
        $ast = Builder::label('var', Builder::concat([
            Builder::lit('{'),
            Builder::cap(0, Builder::span('A-Za-z')),
            Builder::assign(0, 0),
            Builder::lit('}'),
        ]));

        return PatternHelper::matchOnce($ast, $template);
    }

    /**
     * Validate that $input is a well-formed "{VAR}" token using a forward-goto.
     *
     * Pattern:  concat( lit("{"), goto("check"), label("check", concat(span, lit("}"))) )
     *
     * Bytecode: LIT("{"), GOTO(check), LABEL(check), SPAN('A-Za-z'), LIT("}"), ACCEPT
     * The GOTO skips the LABEL instruction itself and jumps directly into the
     * span body, verifying that OP_GOTO → OP_LABEL transfer executes correctly.
     *
     * @return bool true if $input matches "{ALPHA...}"
     */
    public function validateVariable(string $input): bool
    {
        $ast = Builder::concat([
            Builder::lit('{'),
            Builder::goto('check'),
            Builder::label('check', Builder::concat([
                Builder::span('A-Za-z'),
                Builder::lit('}'),
            ])),
        ]);

        return PatternHelper::matchOnce($ast, $input) !== false;
    }

    /**
     * Count the number of variables in a template string.
     *
     * @return int Number of {VAR} tokens found
     */
    public function countVariables(string $template): int
    {
        return count($this->findVariables($template));
    }

    /**
     * Find all "{VAR}" variables in the template.
     *
     * Re-uses the labelled detection pattern across PatternHelper::matchAll.
     *
     * @return string[] Ordered list of variable names
     */
    public function findVariables(string $template): array
    {
        $ast = Builder::label('var', Builder::concat([
            Builder::lit('{'),
            Builder::cap(0, Builder::span('A-Za-z')),
            Builder::assign(0, 0),
            Builder::lit('}'),
        ]));

        $results = PatternHelper::matchAll($ast, $template);

        $vars = [];
        foreach ($results as $r) {
            if (isset($r['v0'])) {
                $vars[] = $r['v0'];
            }
        }

        return $vars;
    }
}



