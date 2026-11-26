<?php

/**
 * IDE stub for the Snobol\Builder class.
 *
 * The real implementation is provided by the libsnobol4 PHP extension
 * (bindings/php/src/snobol_builder_php.c). This file exists only so that IDEs
 * can resolve the class and its methods. When the extension is missing, a
 * call fails loudly through the shared guard instead of returning a value, so
 * a stub call can never masquerade as a result.
 *
 * Every method is static and returns an AST node array that the Pattern and
 * PatternHelper constructors accept.
 *
 * @generated from the extension method table — keep in sync with snobol_builder_php.c
 */

namespace Snobol;

require_once __DIR__ . '/_extension_guard.php';

class Builder
{
    /** @param string $s @return array<string, mixed> AST node */
    public static function lit(string $s): array { stub_extension_required(__METHOD__); }

    /** @param string $set @return array<string, mixed> AST node */
    public static function span(string $set): array { stub_extension_required(__METHOD__); }

    /** @param string $set @return array<string, mixed> AST node */
    public static function brk(string $set): array { stub_extension_required(__METHOD__); }

    /** @param string|null $set @return array<string, mixed> AST node */
    public static function any(?string $set = null): array { stub_extension_required(__METHOD__); }

    /** @param string $set @return array<string, mixed> AST node */
    public static function notany(string $set): array { stub_extension_required(__METHOD__); }

    /** @param int $n @return array<string, mixed> AST node */
    public static function len(int $n): array { stub_extension_required(__METHOD__); }

    /** @param array<string, mixed> $sub @return array<string, mixed> AST node */
    public static function arbno(array $sub): array { stub_extension_required(__METHOD__); }

    /** @param int $reg Register in [0, 63]; ValueError outside @param array<string, mixed> $sub @return array<string, mixed> AST node */
    public static function cap(int $reg, array $sub): array { stub_extension_required(__METHOD__); }

    /** @param int $reg Register in [0, 63]; ValueError outside @param array<string, mixed> $sub @return array<string, mixed> AST node */
    public static function name(int $reg, array $sub): array { stub_extension_required(__METHOD__); }

    /** @param int $var @param int $reg Register in [0, 63]; ValueError outside @return array<string, mixed> AST node */
    public static function assign(int $var, int $reg): array { stub_extension_required(__METHOD__); }

    /** @param array<int, array<string, mixed>> $parts @return array<string, mixed> AST node */
    public static function concat(array $parts): array { stub_extension_required(__METHOD__); }

    /** @param array<string, mixed> $left @param array<string, mixed> $right @return array<string, mixed> AST node */
    public static function alt($left, $right): array { stub_extension_required(__METHOD__); }

    /** SNOBOL EVAL node factory; not PHP's eval(). @param int $fn @param int $reg @return array<string, mixed> AST node */
    public static function eval(int $fn, int $reg): array { stub_extension_required(__METHOD__); }

    /** @param string $atype @return array<string, mixed> AST node */
    public static function anchor(string $atype): array { stub_extension_required(__METHOD__); }

    /** @param array<string, mixed> $sub @param int $min @param int|null $max @return array<string, mixed> AST node */
    public static function repeat(array $sub, int $min, ?int $max = null): array { stub_extension_required(__METHOD__); }

    /** @param string $text @return array<string, mixed> AST node */
    public static function emit(string $text): array { stub_extension_required(__METHOD__); }

    /** @param int $reg @return array<string, mixed> AST node */
    public static function emitRef(int $reg): array { stub_extension_required(__METHOD__); }

    /** @param string $name @param array<string, mixed> $target @return array<string, mixed> AST node */
    public static function label(string $name, array $target): array { stub_extension_required(__METHOD__); }

    /** @param string $label @return array<string, mixed> AST node */
    public static function goto(string $label): array { stub_extension_required(__METHOD__); }

    /** @param array<string, mixed> $expr @return array<string, mixed> AST node */
    public static function dynamicEval(array $expr): array { stub_extension_required(__METHOD__); }

    /** @param string $tableName @param array<string, mixed> $keyExpr @return array<string, mixed> AST node */
    public static function tableAccess(string $tableName, array $keyExpr): array { stub_extension_required(__METHOD__); }

    /** @param string $tableName @param array<string, mixed> $keyExpr @param array<string, mixed> $valueExpr @return array<string, mixed> AST node */
    public static function tableUpdate(string $tableName, array $keyExpr, array $valueExpr): array { stub_extension_required(__METHOD__); }

    /** @param string $arrayName @param array<string, mixed> $indexExpr @return array<string, mixed> AST node */
    public static function arrayAccess(string $arrayName, array $indexExpr): array { stub_extension_required(__METHOD__); }

    /** @param string $arrayName @param array<string, mixed> $indexExpr @param array<string, mixed> $valueExpr @return array<string, mixed> AST node */
    public static function arrayUpdate(string $arrayName, array $indexExpr, array $valueExpr): array { stub_extension_required(__METHOD__); }

    /** @param string $arrayName @param int|null $size @return array<string, mixed> AST node */
    public static function arrayCreate(string $arrayName, ?int $size = null): array { stub_extension_required(__METHOD__); }

    /** @param string $set @return array<string, mixed> AST node */
    public static function breakx(string $set): array { stub_extension_required(__METHOD__); }

    /** @param string|null $open @param string|null $close @return array<string, mixed> AST node */
    public static function bal(?string $open = null, ?string $close = null): array { stub_extension_required(__METHOD__); }

    /** @return array<string, mixed> AST node */
    public static function fence(): array { stub_extension_required(__METHOD__); }

    /** @return array<string, mixed> AST node */
    public static function rem(): array { stub_extension_required(__METHOD__); }

    /** @param int $n @return array<string, mixed> AST node */
    public static function rpos(int $n): array { stub_extension_required(__METHOD__); }

    /** @param int $n @return array<string, mixed> AST node */
    public static function rtab(int $n): array { stub_extension_required(__METHOD__); }

    /** @return array<string, mixed> AST node */
    public static function arb(): array { stub_extension_required(__METHOD__); }

    /** @param int $n @return array<string, mixed> AST node */
    public static function pos(int $n): array { stub_extension_required(__METHOD__); }

    /** @param int $n @return array<string, mixed> AST node */
    public static function tab(int $n): array { stub_extension_required(__METHOD__); }

    /** @return array<string, mixed> AST node */
    public static function abort(): array { stub_extension_required(__METHOD__); }

    /** @return array<string, mixed> AST node */
    public static function fail(): array { stub_extension_required(__METHOD__); }

    /** @return array<string, mixed> AST node */
    public static function succeed(): array { stub_extension_required(__METHOD__); }
}
