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

    public function testMinorVersionIsTwelve(): void
    {
        $v = snobol_get_api_version();
        $minor = ($v >> 8) & 0xFF;
        $this->assertSame(12, $minor, 'Minor version component must be 12 (v0.12.0)');
    }

    public function testEncodingMatchesV0120(): void
    {
        // v0.12.0 encodes as (0 << 16) | (12 << 8) | 0 = 0x00000C00
        $expected = (0 << 16) | (12 << 8) | 0;
        $this->assertSame($expected, snobol_get_api_version());
    }

    public function testAbiVersionFunctionExists(): void
    {
        $this->assertTrue(
            function_exists('snobol_get_abi_version'),
            'snobol_get_abi_version() must be available'
        );
    }

    public function testAbiVersionReturnsOne(): void
    {
        $v = snobol_get_abi_version();
        $this->assertSame(1, $v, 'Initial ABI version must be 1');
    }
}

