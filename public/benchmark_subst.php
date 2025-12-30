<?php
require_once __DIR__.'/../vendor/autoload.php';

use Snobol\Builder;
use Snobol\Pattern;

$patternAst = Builder::concat([
    Builder::cap(1, Builder::span("abcdefghijklmnopqrstuvwxyz")),
    Builder::assign(1, 1)
]);
$pattern = Pattern::compileFromAst($patternAst);

$subject = str_repeat("abc def ghi ", 10000); // 120,000 chars
$template = "\${v1.upper()}";

echo "Benchmarking Substitution...\n";
echo "Subject length: ".strlen($subject)." bytes\n";

// 1. Old way (Simulated via manual loop like old PatternHelper::replace)
$start = microtime(true);
$oldResult = '';
$offset = 0;
$subjectLen = strlen($subject);
while ($offset < $subjectLen) {
    $remaining = substr($subject, $offset);
    $res = $pattern->match($remaining);
    if ($res === false) {
        $oldResult .= $subject[$offset];
        $offset++;
        continue;
    }
    $matchLen = $res['_match_len'];
    $val = $res['v1'] ?? '';
    $oldResult .= strtoupper((string) $val);
    $offset += max(1, $matchLen);
}
$end = microtime(true);
$oldTime = $end - $start;
echo "Old PHP Loop: ".number_format($oldTime, 4)."s\n";

// 2. New way (C-level subst)
$start = microtime(true);
$newResult = $pattern->subst($subject, $template);
$end = microtime(true);
$newTime = $end - $start;
echo "New C subst:  ".number_format($newTime, 4)."s\n";

if ($oldResult === $newResult) {
    echo "Results match!\n";
} else {
    echo "RESULTS DISCREPANCY!\n";
    echo "Old (start): ".substr($oldResult, 0, 50)."\n";
    echo "New (start): ".substr($newResult, 0, 50)."\n";
}

echo "Speedup: ".number_format($oldTime / $newTime, 2)."x\n";

