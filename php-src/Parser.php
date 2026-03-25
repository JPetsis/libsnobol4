<?php

namespace Snobol;

class Parser
{
    private Lexer $lexer;
    private array $tokens;
    private int $pos;
    private int $count;
    private array $labels = []; // Track defined labels for duplicate detection
    private array $gotoRefs = []; // Track goto references for validation

    public function __construct(string $input)
    {
        $this->lexer = new Lexer($input);
    }

    public function parse(): array
    {
        $this->tokens = $this->lexer->tokenize();
        $this->count = count($this->tokens);
        $this->pos = 0;
        $this->labels = [];
        $this->gotoRefs = [];

        $ast = $this->statement();

        if ($this->curr()['type'] !== Lexer::T_EOF) {
            throw new \Exception("Unexpected token after expression: ".$this->curr()['value']);
        }

        // Validate labels and gotos
        $this->validateLabelsAndGotos();

        return $ast;
    }

    private function validateLabelsAndGotos(): void
    {
        // Check for duplicate labels
        $labelCounts = [];
        foreach ($this->labels as $label) {
            $labelCounts[$label] = ($labelCounts[$label] ?? 0) + 1;
        }
        foreach ($labelCounts as $label => $count) {
            if ($count > 1) {
                throw new \Exception("Duplicate label '$label'");
            }
        }

        // Note: We don't validate goto references here because:
        // 1. Forward references are allowed in SNOBOL
        // 2. Labels may be defined in other statements/programs
        // Runtime validation will handle unresolved labels
    }

    private function statement(): array
    {
        // Check for label: IDENT ':'
        if ($this->curr()['type'] === Lexer::T_IDENT) {
            $lookahead = $this->peek(1);
            if ($lookahead['type'] === Lexer::T_COLON) {
                $labelName = $this->curr()['value'];
                $this->labels[] = $labelName; // Track defined label
                $this->next(); // consume IDENT
                $this->next(); // consume COLON
                $target = $this->statement();
                return Builder::label($labelName, $target);
            }
        }

        // Check for goto: ':' '(' IDENT ')'
        if ($this->curr()['type'] === Lexer::T_COLON) {
            $this->next();
            if ($this->match(Lexer::T_LPAREN)) {
                if ($this->curr()['type'] !== Lexer::T_IDENT) {
                    throw new \Exception("Expected label identifier after ':('");
                }
                $label = $this->curr()['value'];
                $this->gotoRefs[] = $label; // Track goto reference
                $this->next();
                $this->expect(Lexer::T_RPAREN);
                return Builder::goto($label);
            }
            throw new \Exception("Expected '(' after ':' in goto syntax");
        }

        return $this->expr();
    }

    private function expr(): array
    {
        $left = $this->term();

        while ($this->curr()['type'] === Lexer::T_PIPE) {
            $this->next();
            $right = $this->term();
            $left = Builder::alt($left, $right);
        }

        return $left;
    }

    private function term(): array
    {
        $factors = [];
        $factors[] = $this->factor();

        while ($this->isStartOfFactor()) {
            $factors[] = $this->factor();
        }

        // Check for goto at the end of a term: :(LABEL)
        if ($this->curr()['type'] === Lexer::T_COLON) {
            $this->next();
            if ($this->match(Lexer::T_LPAREN)) {
                if ($this->curr()['type'] !== Lexer::T_IDENT) {
                    throw new \Exception("Expected label identifier after ':('");
                }
                $label = $this->curr()['value'];
                $this->next();
                $this->expect(Lexer::T_RPAREN);
                $factors[] = Builder::goto($label);
            } else {
                throw new \Exception("Expected '(' after ':' in goto syntax");
            }
        }

        if (count($factors) === 1) {
            return $factors[0];
        }

        return Builder::concat($factors);
    }

    private function factor(): array
    {
        $atom = $this->atom();

        if ($this->curr()['type'] === Lexer::T_STAR) {
            $this->next();
            return Builder::arbno($atom);
        } elseif ($this->curr()['type'] === Lexer::T_PLUS) {
            $this->next();
            // A+ => A arbno(A)
            return Builder::concat([$atom, Builder::arbno($atom)]);
        } elseif ($this->curr()['type'] === Lexer::T_QUESTION) {
            $this->next();
            // A? => A | ""
            return Builder::alt($atom, Builder::lit(''));
        }

        return $atom;
    }

    // Expr -> Term { '|' Term }

    private function atom(): array
    {
        $t = $this->curr();

        if ($t['type'] === Lexer::T_LIT) {
            $this->next();
            return Builder::lit($t['value']);
        }

        if ($t['type'] === Lexer::T_CHARCLASS) {
            $this->next();
            // [^...] -> notany, [...] -> any
            $val = $t['value'];
            if (str_starts_with($val, '^')) {
                return Builder::notany($this->expandRanges(substr($val, 1)));
            } else {
                return Builder::any($this->expandRanges($val));
            }
        }

        if ($t['type'] === Lexer::T_IDENT) {
            // Builtin call or table access or variable ref?
            $name = $t['value'];
            $this->next();

            // Check for table access: TABLE[key]
            if ($this->match(Lexer::T_LBRACKET)) {
                $keyExpr = $this->atomForTableKey();
                $this->expect(Lexer::T_RBRACKET);

                // Check for table update: TABLE[key] = value
                if ($this->match(Lexer::T_EQUALS)) {
                    $valueExpr = $this->statement();
                    return Builder::tableUpdate($name, $keyExpr, $valueExpr);
                }

                return Builder::tableAccess($name, $keyExpr);
            }
            
            if ($this->match(Lexer::T_LPAREN)) {
                // Check for dynamic eval: EVAL(expr)
                if (strtoupper($name) === 'EVAL') {
                    // Check for empty EVAL()
                    if ($this->curr()['type'] === Lexer::T_RPAREN) {
                        throw new \Exception("EVAL requires a pattern expression argument");
                    }
                    // EVAL takes a full expression
                    $expr = $this->expr();
                    $this->expect(Lexer::T_RPAREN);
                    return Builder::dynamicEval($expr);
                }

                // Regular builtin - parse simple args
                $args = $this->parseArgs();
                $this->expect(Lexer::T_RPAREN);
                return $this->buildFunction($name, $args);
            }

            // Just IDENT? Fail for now unless we support variables
            throw new \Exception("Unexpected identifier '$name' (variable ref not supported yet)");
        }

        if ($t['type'] === Lexer::T_LPAREN) {
            $this->next();
            $e = $this->expr();
            $this->expect(Lexer::T_RPAREN);
            return $e;
        }

        if ($t['type'] === Lexer::T_ANCHOR_START) {
            $this->next();
            // Treat as empty literal for now or throw if strict
            // "Anchor Start" implies we are at start.
            // Since match is anchored, this is always true at 0.
            // But if used in middle "A ^ B", it should fail?
            // SNOBOL POS(0).
            // For now: return empty literal (matches nothing, consumes nothing)
            return Builder::lit('');
        }

        if ($t['type'] === Lexer::T_ANCHOR_END) {
            $this->next();
            // Treat as empty literal? No, this loses semantic.
            // But without Opcode, we can't do much.
            return Builder::lit('');
        }

        if ($t['type'] === Lexer::T_AT) {
            // Capture: @name(...)
            $this->next();
            if ($this->curr()['type'] !== Lexer::T_IDENT) {
                throw new \Exception("Expected identifier after @");
            }
            $name = $this->curr()['value']; // We assume capturing to variable named 'name'?
            // Builder::assign($var, $reg)? Builder::cap($reg, $sub)?
            // Builder::cap takes integer reg.
            // Builder::assign takes var index?
            // The VM uses indices.
            // We need a way to map names to indices if we use names.
            // For now, let's assume we use explicit register numbers or we map them.
            // Prompt said: "@rN(...)".
            // If name is r1, r2... parse number.
            $this->next();

            // Check for (...)
            if ($this->match(Lexer::T_LPAREN)) {
                $sub = $this->expr();
                $this->expect(Lexer::T_RPAREN);

                // parse 'rN' or just 'N'?
                // If input is @r1, name='r1'.
                if (preg_match('/^r(\d+)$/', $name, $m)) {
                    $reg = (int) $m[1];
                    return Builder::cap($reg, $sub);
                } else {
                    throw new \Exception("Capture must use register format @rN");
                }
            } else {
                throw new \Exception("Expected (...) after capture name");
            }
        }

        throw new \Exception("Unexpected token: ".$t['type']);
    }

    // Term -> Factor { Factor } (Implicit Concatenation)

    private function curr(): array
    {
        return $this->tokens[$this->pos];
    }

    private function next(): void
    {
        if ($this->pos < $this->count - 1) {
            $this->pos++;
        }
    }

    private function peek(int $offset): array
    {
        $idx = $this->pos + $offset;
        if ($idx < $this->count) {
            return $this->tokens[$idx];
        }
        return ['type' => Lexer::T_EOF, 'value' => ''];
    }

    // Factor -> Atom [ Quantifier ]

    private function expandRanges(string $s): string
    {
        $out = '';
        $len = strlen($s);
        $i = 0;

        while ($i < $len) {
            // Read char (start)
            $c = $s[$i];
            $charLen = 1;
            if ($c === '\\' && $i + 1 < $len) {
                $c = $s[$i + 1];
                $charLen = 2;
            }

            // Check ahead for range
            if ($i + $charLen < $len && $s[$i + $charLen] === '-') {
                // Check if there is an 'end' char
                $dashPos = $i + $charLen;
                if ($dashPos + 1 < $len) {
                    $nextPos = $dashPos + 1;
                    $endC = $s[$nextPos];
                    $endLen = 1;
                    if ($endC === '\\' && $nextPos + 1 < $len) {
                        $endC = $s[$nextPos + 1];
                        $endLen = 2;
                    }

                    // Now we have start=$c, end=$endC.
                    // Expand.
                    $startOrd = ord($c);
                    $endOrd = ord($endC);

                    if ($startOrd <= $endOrd) {
                        for ($k = $startOrd; $k <= $endOrd; $k++) {
                            $out .= chr($k);
                        }
                        $i = $nextPos + $endLen;
                        continue;
                    }
                }
            }

            $out .= $c;
            $i += $charLen;
        }
        return $out;
    }

    // Atom -> LIT | CHARCLASS | IDENT(...) | '(' Expr ')' | ANCHOR | CAPTURE

    private function match(string $type): bool
    {
        if ($this->curr()['type'] === $type) {
            $this->next();
            return true;
        }
        return false;
    }

    private function parseArgs(): array
    {
        $args = [];
        if ($this->curr()['type'] !== Lexer::T_RPAREN) {
            // For EVAL, we need to parse the full expression
            // For other builtins, we just need a literal value
            // For now, parse as a simple value (LIT or IDENT)
            
            $val = $this->curr()['value'];
            $this->next();
            $args[] = $val;

            // TODO: Comma support if needed.
        }
        return $args;
    }

    private function expect(string $type): void
    {
        if (!$this->match($type)) {
            throw new \Exception("Expected $type, got ".$this->curr()['type']);
        }
    }

    private function buildFunction(string $name, array $args): array
    {
        $arg0 = $args[0] ?? null;
        switch (strtoupper($name)) {
            case 'SPAN':
                return Builder::span($arg0);
            case 'BREAK':
                return Builder::brk($arg0);
            case 'LEN':
                return Builder::len((int) $arg0);
            case 'ANY':
                return Builder::any($arg0);
            case 'NOTANY':
                return Builder::notany($arg0);
            // TODO: EVAL
            default:
                throw new \Exception("Unknown builtin function: $name");
        }
    }

    private function isStartOfFactor(): bool
    {
        $t = $this->curr()['type'];
        return $t === Lexer::T_LIT ||
            $t === Lexer::T_IDENT ||
            $t === Lexer::T_CHARCLASS ||
            $t === Lexer::T_LPAREN ||
            $t === Lexer::T_ANCHOR_START ||
            $t === Lexer::T_ANCHOR_END ||
            $t === Lexer::T_AT;
    }

    // Parse an atom that can be a table key (literal or identifier)
    private function atomForTableKey(): array
    {
        $t = $this->curr();

        if ($t['type'] === Lexer::T_LIT) {
            $this->next();
            return Builder::lit($t['value']);
        }

        if ($t['type'] === Lexer::T_IDENT) {
            $this->next();
            return Builder::lit($t['value']);
        }

        throw new \Exception("Invalid table key: expected literal or identifier, got ".$t['type']);
    }
}
