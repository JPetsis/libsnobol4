<?php

/**
 * Guard shared by the IDE stubs in this directory.
 *
 * Every stub here describes a class or function the libsnobol4 extension
 * registers from C. The `Snobol\` PSR-4 mapping points at this directory, so a
 * stub can be autoloaded in a PHP process where the extension is missing. In
 * that case it must fail at the first call with the real cause instead of
 * returning neutral values that look like application bugs.
 */

namespace Snobol;

/**
 * Fail a stub call because the extension that implements it is not loaded.
 *
 * Declared `never` so every stub body can satisfy its own return type by
 * calling this. It is only reachable when the extension is absent: with the
 * extension loaded, PHP resolves the real class or function and never loads
 * the stub file.
 *
 * @param string $member Stub method or function that was called
 * @throws \Error Always
 */
function stub_extension_required(string $member): never
{
    throw new \Error(
        $member . ' requires the libsnobol4 PHP extension (ext-snobol), which is not loaded. '
        . 'Install it with `pie install libsnobol4/snobol` and restart PHP.'
    );
}
