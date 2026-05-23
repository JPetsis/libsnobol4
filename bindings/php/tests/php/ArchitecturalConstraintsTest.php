<?php

declare(strict_types=1);

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;

/**
 * Architectural constraints for the PHP binding layer.
 *
 * These tests enforce that the "C core owns all parsing" invariant
 * is never accidentally broken by PHP-side code additions.
 *
 */
class ArchitecturalConstraintsTest extends TestCase
{
    private static string $phpSrcDir = __DIR__ . '/../../php-src';

    /** Return all *.php file paths under bindings/php/php-src/ */
    private function phpSrcFiles(): array
    {
        $files = [];
        $iterator = new \RecursiveIteratorIterator(
            new \RecursiveDirectoryIterator(
                self::$phpSrcDir,
                \RecursiveDirectoryIterator::SKIP_DOTS
            )
        );
        foreach ($iterator as $file) {
            if ($file->isFile() && $file->getExtension() === 'php') {
                $files[] = $file->getPathname();
            }
        }
        return $files;
    }

    /**
     * @test
     * Requirement: PHP binding SHALL NOT contain PHP-native pattern parsing code
     * Scenario: No PHP-native Lexer instantiation
     */
    public function testNoPhpNativeLexerInstantiation(): void
    {
        $occurrences = [];
        foreach ($this->phpSrcFiles() as $path) {
            $contents = file_get_contents($path);
            if ($contents === false) {
                continue;
            }
            $lines = explode("\n", $contents);
            foreach ($lines as $lineNo => $line) {
                if (str_contains($line, 'new Lexer(')) {
                    $occurrences[] = sprintf('%s:%d: %s', basename($path), $lineNo + 1, trim($line));
                }
            }
        }

        $this->assertCount(
            0,
            $occurrences,
            "PHP-native Lexer instantiation found under bindings/php/php-src/ — "
            . "all SNOBOL4 parsing must go through the C extension.\n"
            . implode("\n", $occurrences)
        );
    }

    /**
     * @test
     * Requirement: PHP binding SHALL NOT contain PHP-native pattern parsing code
     * Scenario: No PHP-native Parser instantiation
     */
    public function testNoPhpNativeParserInstantiation(): void
    {
        $occurrences = [];
        foreach ($this->phpSrcFiles() as $path) {
            $contents = file_get_contents($path);
            if ($contents === false) {
                continue;
            }
            $lines = explode("\n", $contents);
            foreach ($lines as $lineNo => $line) {
                if (str_contains($line, 'new Parser(')) {
                    $occurrences[] = sprintf('%s:%d: %s', basename($path), $lineNo + 1, trim($line));
                }
            }
        }

        $this->assertCount(
            0,
            $occurrences,
            "PHP-native Parser instantiation found under bindings/php/php-src/ — "
            . "all SNOBOL4 parsing must go through the C extension.\n"
            . implode("\n", $occurrences)
        );
    }

    /**
     * @test
     * Requirement: PHP binding SHALL NOT contain PHP-native pattern parsing code
     * Scenario: Lexer.php and Parser.php do not exist
     */
    public function testLexerPhpFileDoesNotExist(): void
    {
        $lexerPath = self::$phpSrcDir . '/Lexer.php';
        $this->assertFileDoesNotExist(
            $lexerPath,
            'bindings/php/php-src/Lexer.php must not exist — '
            . 'SNOBOL4 lexing belongs in the C core (snobol_lexer_create()).'
        );
    }

    /**
     * @test
     * Requirement: PHP binding SHALL NOT contain PHP-native pattern parsing code
     * Scenario: Lexer.php and Parser.php do not exist
     */
    public function testParserPhpFileDoesNotExist(): void
    {
        $parserPath = self::$phpSrcDir . '/Parser.php';
        $this->assertFileDoesNotExist(
            $parserPath,
            'bindings/php/php-src/Parser.php must not exist — '
            . 'SNOBOL4 parsing belongs in the C core (snobol_parser_parse()).'
        );
    }
}

