<?php
require '/var/www/html/vendor/autoload.php';

use Snobol\Builder as B;
use Snobol\Pattern;
use Snobol\PatternHelper as PH;

echo "=== PrimitivesTest-style: span('a') + fence + lit('b') ===\n";
$ast = B::concat([B::span('a'), B::fence(), B::lit('b')]);
var_dump(PH::matchOnce($ast, 'aaab'));

echo "\n=== span 'a' only ===\n";
var_dump(PH::matchOnce(B::span('a'), 'aaa'));

echo "\n=== span 'A-Za-z' only ===\n";
var_dump(PH::matchOnce(B::span('A-Za-z'), 'hello'));

echo "\n=== span '0-9' only ===\n";
var_dump(PH::matchOnce(B::span('0-9'), '123'));

echo "\n=== label + span('a') ===\n";
var_dump(PH::matchOnce(B::label('x', B::span('a')), 'aaa'));

echo "\n=== label + span('A-Za-z') ===\n";
var_dump(PH::matchOnce(B::label('x', B::span('A-Za-z')), 'hello'));

echo "\n=== lit('hello') ===\n";
var_dump(PH::matchOnce(B::lit('hello'), 'hello world'));

echo "\n=== cap(0, span) in label - detectVariable ===\n";
$ast = B::label('var', B::concat([
    B::lit('{'),
    B::cap(0, B::span('A-Za-z')),
    B::lit('}'),
]));
$r = PH::matchOnce($ast, '{name} rest');
var_dump($r);

echo "\n=== cap(0, span) without label ===\n";
$ast2 = B::concat([
    B::lit('{'),
    B::cap(0, B::span('A-Za-z')),
    B::lit('}'),
]);
$r2 = PH::matchOnce($ast2, '{name} rest');
var_dump($r2);

echo "\n=== cap(0, span('0123456789')) - explicit chars, passing PatternTest style ===\n";
$ast3 = B::cap(0, B::span('0123456789'));
var_dump(PH::matchOnce($ast3, '12345'));

echo "\n=== cap(0, span('0-9')) - range notation ===\n";
$ast4 = B::cap(0, B::span('0-9'));
var_dump(PH::matchOnce($ast4, '12345'));

echo "\n=== cap(0, span('A-Za-z')) in concat with lit ===\n";
$ast5 = B::concat([
    B::lit('{'),
    B::cap(0, B::span('A-Za-z')),
    B::lit('}'),
]);
var_dump(PH::matchOnce($ast5, '{name} rest'));

echo "\n=== no cache: Pattern::compileFromAst directly ===\n";
$ast6 = B::concat([
    B::lit('{'),
    B::cap(0, B::span('A-Za-z')),
    B::lit('}'),
]);
$p = Pattern::compileFromAst($ast6);
var_dump($p->match('{name} rest'));

