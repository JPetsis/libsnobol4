<?php
// php-src/builder.php
namespace Snobol;

class Builder {
    public static function lit(string $s) {
        return ['type' => 'lit', 'text' => $s];
    }
    public static function concat(array $parts) {
        return ['type' => 'concat', 'parts' => $parts];
    }

    public static function alt($left, $right)
    {
        $l = is_array($left) ? $left : self::lit((string) $left);
        $r = is_array($right) ? $right : self::lit((string) $right);
        return ['type' => 'alt', 'left' => $l, 'right' => $r];
    }
    public static function span(string $set) {
        return ['type' => 'span', 'set' => $set];
    }
    public static function brk(string $set) {
        return ['type' => 'break', 'set' => $set];
    }

    public static function any(?string $set = null)
    {
        if ($set !== null) {
            return ['type' => 'any', 'set' => $set];
        }
        return ['type' => 'any'];
    }
    public static function notany(string $set) {
        return ['type' => 'notany', 'set' => $set];
    }
    public static function arbno(array $sub) {
        return ['type' => 'arbno', 'sub' => $sub];
    }
    public static function cap(int $reg, array $sub) {
        return ['type' => 'cap', 'reg' => $reg, 'sub' => $sub];
    }
    public static function assign(int $var, int $reg) {
        return ['type' => 'assign', 'var' => $var, 'reg' => $reg];
    }
    public static function len(int $n) {
        return ['type' => 'len', 'n' => $n];
    }
    public static function eval(int $fn, int $reg) {
        return ['type' => 'eval', 'fn' => $fn, 'reg' => $reg];
    }

    public static function anchor(string $type)
    {
        return ['type' => 'anchor', 'atype' => $type];
    }

    public static function repeat(array $sub, int $min, int $max = -1)
    {
        return ['type' => 'repeat', 'sub' => $sub, 'min' => $min, 'max' => $max];
    }

    public static function emit(string $text)
    {
        return ['type' => 'emit', 'text' => $text];
    }

    public static function emitRef(int $reg)
    {
        return ['type' => 'emit', 'reg' => $reg];
    }
}
