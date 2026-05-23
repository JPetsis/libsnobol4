<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;

/**
 * PHP test for snobol_get_api_version().
 *
 * Verifies the version encoding (MAJOR << 16 | MINOR << 8 | PATCH)
 * and that the major component matches SNOBOL_VERSION_MAJOR = 0.
 */
class ApiVersionTest extends TestCase
{
    public function testFunctionExists(): void
    {
        $this->assertTrue(
            function_exists('snobol_get_api_version'),
            'snobol_get_api_version() must be available'
        );
    }

    public function testReturnsInteger(): void
    {
        $v = snobol_get_api_version();
        $this->assertIsInt($v);
    }

    public function testMajorVersionIsZero(): void
    {
        $v = snobol_get_api_version();
        $major = ($v >> 16) & 0xFF;
        $this->assertSame(0, $major, 'Major version component must be 0');
    }

    public function testMinorVersionIsEight(): void
    {
        $v = snobol_get_api_version();
        $minor = ($v >> 8) & 0xFF;
        $this->assertSame(8, $minor, 'Minor version component must be 8 (v0.8.0)');
    }

    public function testEncodingMatchesV080(): void
    {
        // v0.8.0 encodes as (0 << 16) | (8 << 8) | 0 = 0x00000800
        $expected = (0 << 16) | (8 << 8) | 0;
        $this->assertSame($expected, snobol_get_api_version());
    }
}

