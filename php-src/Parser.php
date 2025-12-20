<?php

namespace Snobol;

class Parser
{
    private Lexer $lexer;
    private array $tokens;
    private int $pos;
    private int $count;

    public function __construct(string $input)
    {
        $this->lexer = new Lexer($input);
    }

    public function parse(): array
    {
        $this->tokens = $this->lexer->tokenize();
        $this->count = count($this->tokens);
        $this->pos = 0;

        $ast = $this->expr();

        if ($this->curr()['type'] !== Lexer::T_EOF) {
            throw new \Exception("Unexpected token after expression: ".$this->curr()['value']);
        }

        return $ast;
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
            // Builtin call?
            $name = $t['value'];
            $this->next();
            if ($this->match(Lexer::T_LPAREN)) {
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
        // Simple comma separated args?
        // Currently builtins take 1 arg usually.
        // EVAL takes fn, reg.
        // Let's support expression as arg.
        $args = [];
        if ($this->curr()['type'] !== Lexer::T_RPAREN) {
            // Args are usually strings (LIT) or numbers for LEN.
            // But EVAL takes a function index (int).
            // Let's parse generic 'Value'.
            // For simplicity, parse LIT or NUMBER (LIT) or EXPR?
            // SPAN('...') -> LIT.
            // LEN(5) -> LIT (value 5).

            // We need to consume tokens until RPAREN or COMMA.
            // Actually, let's just parse a single literal or integer for now as that's what builtins take.
            // Except EVAL?

            // If the next token is LIT, take it.
            // If IDENT, maybe?
            // If NUMBER? Lexer doesn't have T_NUMBER yet, treats digits as IDENT or LIT?
            // Lexer treats digits as IDENT currently (ctype_alnum).

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
}
