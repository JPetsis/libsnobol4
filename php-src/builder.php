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
    public static function alt(array $left, array $right) {
        return ['type' => 'alt', 'left' => $left, 'right' => $right];
    }
    public static function span(string $set) {
        return ['type' => 'span', 'set' => $set];
    }
    public static function brk(string $set) {
        return ['type' => 'break', 'set' => $set];
    }

    public static function any(string $set = null)
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
}
