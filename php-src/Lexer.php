<?php

namespace Snobol;

class Lexer
{
    const T_EOF = 'EOF';
    const T_LIT = 'LIT';
    const T_IDENT = 'IDENT';
    const T_CHARCLASS = 'CHARCLASS';
    const T_PIPE = 'PIPE';
    const T_LPAREN = 'LPAREN';
    const T_RPAREN = 'RPAREN';
    const T_STAR = 'STAR';
    const T_PLUS = 'PLUS';
    const T_QUESTION = 'QUESTION';
    const T_ANCHOR_START = 'ANCHOR_START';
    const T_ANCHOR_END = 'ANCHOR_END';
    const T_AT = 'AT';

    private string $input;
    private int $pos;
    private int $len;

    public function __construct(string $input)
    {
        $this->input = $input;
        $this->len = strlen($input);
        $this->pos = 0;
    }

    public function tokenize(): array
    {
        $tokens = [];
        while ($this->pos < $this->len) {
            $char = $this->input[$this->pos];

            if (ctype_space($char)) {
                $this->pos++;
                continue;
            }

            if ($char === '|') {
                $tokens[] = ['type' => self::T_PIPE, 'value' => '|'];
                $this->pos++;
            } elseif ($char === '(') {
                $tokens[] = ['type' => self::T_LPAREN, 'value' => '('];
                $this->pos++;
            } elseif ($char === ')') {
                $tokens[] = ['type' => self::T_RPAREN, 'value' => ')'];
                $this->pos++;
            } elseif ($char === '*') {
                $tokens[] = ['type' => self::T_STAR, 'value' => '*'];
                $this->pos++;
            } elseif ($char === '+') {
                $tokens[] = ['type' => self::T_PLUS, 'value' => '+'];
                $this->pos++;
            } elseif ($char === '?') {
                $tokens[] = ['type' => self::T_QUESTION, 'value' => '?'];
                $this->pos++;
            } elseif ($char === '^') {
                $tokens[] = ['type' => self::T_ANCHOR_START, 'value' => '^'];
                $this->pos++;
            } elseif ($char === '$') {
                $tokens[] = ['type' => self::T_ANCHOR_END, 'value' => '$'];
                $this->pos++;
            } elseif ($char === '@') {
                $tokens[] = ['type' => self::T_AT, 'value' => '@'];
                $this->pos++;
            } elseif ($char === "'" || $char === '"') {
                $tokens[] = $this->readQuoted($char);
            } elseif ($char === '[') {
                $tokens[] = $this->readCharClass();
            } elseif (ctype_alnum($char) || $char === '_') {
                $tokens[] = $this->readIdent();
            } else {
                // Unknown char, maybe treat as literal or error?
                // For now, error
                throw new \Exception("Unexpected character '$char' at position ".$this->pos);
            }
        }
        $tokens[] = ['type' => self::T_EOF, 'value' => ''];
        return $tokens;
    }

    private function readQuoted(string $quote): array
    {
        $start = $this->pos;
        $this->pos++; // skip open quote
        $str = '';
        while ($this->pos < $this->len) {
            $c = $this->input[$this->pos];
            if ($c === $quote) {
                $this->pos++; // skip close quote
                return ['type' => self::T_LIT, 'value' => $str];
            }
            if ($c === '\\' && $this->pos + 1 < $this->len) {
                $this->pos++;
                $str .= $this->input[$this->pos]; // simplistic escape
            } else {
                $str .= $c;
            }
            $this->pos++;
        }
        throw new \Exception("Unclosed string starting at $start");
    }

    private function readCharClass(): array
    {
        $start = $this->pos;
        $this->pos++; // skip [
        $str = '';
        // Check negation
        if ($this->pos < $this->len && $this->input[$this->pos] === '^') {
            $str .= '^';
            $this->pos++;
        }

        // Handle closing bracket as first char (standard regex behavior)?
        // For simplicity, assume standard `]` closes.
        while ($this->pos < $this->len) {
            $c = $this->input[$this->pos];
            if ($c === ']') {
                $this->pos++;
                return ['type' => self::T_CHARCLASS, 'value' => $str];
            }
            if ($c === '\\' && $this->pos + 1 < $this->len) {
                // Preserve the backslash and the next char
                $str .= $c;
                $this->pos++;
                $str .= $this->input[$this->pos];
            } else {
                $str .= $c;
            }
            $this->pos++;
        }
        throw new \Exception("Unclosed character class starting at $start");
    }

    private function readIdent(): array
    {
        $start = $this->pos;
        while ($this->pos < $this->len) {
            $c = $this->input[$this->pos];
            if (ctype_alnum($c) || $c === '_') {
                $this->pos++;
            } else {
                break;
            }
        }
        return ['type' => self::T_IDENT, 'value' => substr($this->input, $start, $this->pos - $start)];
    }
}