<?php
require __DIR__ . '/../../vendor/autoload.php';
$segments = \Snobol\PatternHelper::split("'|' | ','", "a,b|c,d");
print_r($segments);
echo "OK\n";
