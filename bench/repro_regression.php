<?php
require __DIR__.'/../vendor/autoload.php';

use Snobol\Builder;
use Snobol\Pattern;

$altDepth = 100;
$alt = Builder::lit('a');
for ($i = 1; $i < $altDepth; $i++) {
    $ch = chr(ord('a') + ($i % 26));
    $alt = Builder::alt($alt, Builder::lit($ch));
}
$alt = Builder::alt($alt, Builder::lit('!'));
$patternAlt = Pattern::compileFromAst($alt);

$subjectAlt = '!';

echo "Running reproduction with JIT ON...\n";
$patternAlt->setJit(true);
$start = hrtime(true);
for ($i = 0; $i < 1000; $i++) {
    $patternAlt->match($subjectAlt);
}
$end = hrtime(true);
$jitOn = ($end - $start) / 1e6;
echo "Duration JIT ON: ".$jitOn."ms\n";
if (function_exists('snobol_get_jit_stats')) {
    print_r(snobol_get_jit_stats());
}

echo "Running reproduction with JIT OFF...\n";
$patternAlt->setJit(false);
$start = hrtime(true);
for ($i = 0; $i < 1000; $i++) {
    $patternAlt->match($subjectAlt);
}
$end = hrtime(true);
$jitOff = ($end - $start) / 1e6;
echo "Duration JIT OFF: ".$jitOff."ms\n";

echo "Speedup: ".($jitOff / $jitOn)."x\n";
