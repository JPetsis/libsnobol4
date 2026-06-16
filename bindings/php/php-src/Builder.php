<?php

namespace Snobol;

final class Builder
{
    public static function concat(array $parts): array
    {
        return ['type' => 'concat', 'parts' => $parts];
    }

    public static function alt($left, $right): array
    {
        $l = is_array($left) ? $left : self::lit((string) $left);
        $r = is_array($right) ? $right : self::lit((string) $right);
        return ['type' => 'alt', 'left' => $l, 'right' => $r];
    }

    public static function lit(string $s): array
    {
        return ['type' => 'lit', 'text' => $s];
    }

    public static function span(string $set): array
    {
        return ['type' => 'span', 'set' => $set];
    }

    public static function brk(string $set): array
    {
        return ['type' => 'break', 'set' => $set];
    }

    public static function any(?string $set = null): array
    {
        if ($set !== null) {
            return ['type' => 'any', 'set' => $set];
        }
        return ['type' => 'any'];
    }

    public static function notany(string $set): array
    {
        return ['type' => 'notany', 'set' => $set];
    }

    public static function arbno(array $sub): array
    {
        return ['type' => 'arbno', 'sub' => $sub];
    }

    public static function cap(int $reg, array $sub): array
    {
        return ['type' => 'cap', 'reg' => $reg, 'sub' => $sub];
    }

    public static function assign(int $var, int $reg): array
    {
        return ['type' => 'assign', 'var' => $var, 'reg' => $reg];
    }

    public static function len(int $n): array
    {
        return ['type' => 'len', 'n' => $n];
    }

    public static function eval(int $fn, int $reg): array
    {
        return ['type' => 'eval', 'fn' => $fn, 'reg' => $reg];
    }

    public static function anchor(string $type): array
    {
        return ['type' => 'anchor', 'atype' => $type];
    }

    public static function repeat(array $sub, int $min, int $max = -1): array
    {
        return ['type' => 'repeat', 'sub' => $sub, 'min' => $min, 'max' => $max];
    }

    public static function emit(string $text): array
    {
        return ['type' => 'emit', 'text' => $text];
    }

    public static function emitRef(int $reg): array
    {
        return ['type' => 'emit', 'reg' => $reg];
    }

    // Label and control-flow nodes
    public static function label(string $name, array $target): array
    {
        return ['type' => 'label', 'name' => $name, 'target' => $target];
    }

    public static function goto(string $label): array
    {
        return ['type' => 'goto', 'label' => $label];
    }

    // Dynamic evaluation node
    public static function dynamicEval(array $expr): array
    {
        return ['type' => 'dynamic_eval', 'expr' => $expr];
    }

    // Table access/update nodes
    public static function tableAccess(string $tableName, array $keyExpr): array
    {
        return ['type' => 'table_access', 'table' => $tableName, 'key' => $keyExpr];
    }

    public static function tableUpdate(string $tableName, array $keyExpr, array $valueExpr): array
    {
        return ['type' => 'table_update', 'table' => $tableName, 'key' => $keyExpr, 'value' => $valueExpr];
    }

    // Array access/update nodes
    public static function arrayAccess(string $arrayName, array $indexExpr): array
    {
        return ['type' => 'array_access', 'array' => $arrayName, 'index' => $indexExpr];
    }

    public static function arrayUpdate(string $arrayName, array $indexExpr, array $valueExpr): array
    {
        return ['type' => 'array_update', 'array' => $arrayName, 'index' => $indexExpr, 'value' => $valueExpr];
    }

    public static function arrayCreate(string $arrayName, int $size = 0): array
    {
        return ['type' => 'array_create', 'array' => $arrayName, 'size' => $size];
    }

    // -----------------------------------------------------------------------
    // Pattern primitives
    // -----------------------------------------------------------------------

    /**
     * BREAKX – like BREAK but pushes a retry choice so the engine can resume
     * scanning after a match, enabling O(n) tokenization.
     *
     * @param  string  $set  Characters that stop the scan (delimiter set)
     */
    public static function breakx(string $set): array
    {
        return ['type' => 'breakx', 'set' => $set];
    }

    /**
     * BAL – match a balanced pair of delimiters (e.g. '(' and ')').
     * Succeeds on the smallest balanced substring starting at the current
     * position, including the outer delimiters.
     *
     * @param  string  $open  Opening delimiter character (first codepoint used)
     * @param  string  $close  Closing delimiter character (first codepoint used)
     */
    public static function bal(string $open = '(', string $close = ')'): array
    {
        return ['type' => 'bal', 'open' => $open, 'close' => $close];
    }

    /**
     * FENCE – cut the choice stack at the current position.
     * Prevents the engine from backtracking past this point, implementing
     * possessive / atomic matching semantics.
     */
    public static function fence(): array
    {
        return ['type' => 'fence'];
    }

    /**
     * REM – consume the remainder of the subject string unconditionally.
     * The cursor is advanced to the end, succeeding immediately.
     */
    public static function rem(): array
    {
        return ['type' => 'rem'];
    }

    /**
     * RPOS(n) – succeed only when the cursor is exactly n codepoints from
     * the end of the subject.  RPOS(0) is equivalent to an end-of-string
     * anchor.
     *
     * @param  int  $n  Distance from end (0 = at end)
     */
    public static function rpos(int $n): array
    {
        return ['type' => 'rpos', 'n' => $n];
    }

    /**
     * RTAB(n) – advance the cursor until it is exactly n codepoints from
     * the end of the subject.  RTAB(0) consumes to the end (equivalent to
     * REM for matching purposes).
     *
     * @param  int  $n  Final distance from end (0 = consume to end)
     */
    public static function rtab(int $n): array
    {
        return ['type' => 'rtab', 'n' => $n];
    }

    /**
     * ARB – match an arbitrary number of characters (0 or more), trying
     * the shortest match first.  Equivalent to RTAB(0) but provides the
     * traditional SNOBOL4 ARB semantics as a convenience alias.
     */
    public static function arb(): array
    {
        // Implemented as ARBNO(LEN(1)) which tries 0, 1, 2, … chars.
        return self::arbno(self::len(1));
    }

    /**
     * POS(n) – succeed only when the cursor is exactly n codepoints from
     * the beginning of the subject.  POS(0) is equivalent to a start-of-
     * string anchor.
     *
     * @param  int  $n  Position from start (0 = at beginning)
     */
    public static function pos(int $n): array
    {
        return ['type' => 'pos', 'n' => $n];
    }

    /**
     * TAB(n) – advance the cursor until it is exactly n codepoints from
     * the beginning of the subject.  TAB(0) consumes nothing (no-op).
     *
     * @param  int  $n  Target position from start
     */
    public static function tab(int $n): array
    {
        return ['type' => 'tab', 'n' => $n];
    }

    /**
     * ABORT – terminate the entire match immediately as a failure,
     * without any possibility of backtracking to try alternatives.
     */
    public static function abort(): array
    {
        return ['type' => 'abort'];
    }

    /**
     * FAIL – force the current match attempt to fail, triggering
     * backtracking.  If no alternatives remain the overall match fails.
     */
    public static function fail(): array
    {
        return ['type' => 'fail'];
    }

    /**
     * SUCCEED – force the current match to succeed immediately,
     * committing the result and preventing further backtracking.
     */
    public static function succeed(): array
    {
        return ['type' => 'succeed'];
    }
}