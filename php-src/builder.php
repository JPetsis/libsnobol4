<?php

namespace Snobol;

final class Builder
{
    public static function lit(string $s): array
    {
        return ['type' => 'lit', 'text' => $s];
    }

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
}
