<?php
// Resolve and require the PHP binding's Composer autoloader.
// Works both on the host (bindings/php/vendor) and inside the ddev
// container (where bindings/php is mounted at /var/www/html).
$autoloadCandidates = [
    __DIR__ . '/../vendor/autoload.php',              // repo-root composer, if present
    __DIR__ . '/../bindings/php/vendor/autoload.php', // host layout
    '/var/www/html/vendor/autoload.php',              // ddev layout
];
foreach ($autoloadCandidates as $candidate) {
    if (is_file($candidate)) {
        require $candidate;
        return;
    }
}
fwrite(STDERR, "Could not locate the PHP binding autoloader. Tried:\n  " . implode("\n  ", $autoloadCandidates) . "\n");
exit(1);
