<?php

/**
 * Compatibility Test Suite
 *
 * Runs reference SNOBOL-style programs and verifies output equivalence.
 * Tests combine tables, dynamic evaluation, formatted substitutions, and labelled control flow.
 */

namespace Snobol\Tests\Compat;

use PHPUnit\Framework\TestCase;

/* Load fixture classes */
require_once __DIR__.'/fixtures/WordCounter.php';
require_once __DIR__.'/fixtures/TextTransformer.php';
require_once __DIR__.'/fixtures/TemplateEngine.php';

class CompatibilityTest extends TestCase
{
    /*
     * Test 1: Word Counter with Tables
     */
    public function testWordCounterWithTables(): void
    {
        $counter = new WordCounter();
        $input = "hello world hello SNOBOL world hello";
        $result = $counter->countWords($input);

        $this->assertEquals(3, $result['hello']);
        $this->assertEquals(2, $result['world']);
        $this->assertEquals(1, $result['snobol']);
        $this->assertCount(3, $result);
    }

    public function testWordCounterEmptyInput(): void
    {
        $counter = new WordCounter();
        $result = $counter->countWords("");
        $this->assertEmpty($result);
    }

    public function testWordCounterSingleWord(): void
    {
        $counter = new WordCounter();
        $result = $counter->countWords("single");
        $this->assertEquals(1, $result['single']);
        $this->assertCount(1, $result);
    }

    /*
     * Test 2: Text Transformer with Dynamic Patterns
     */
    public function testTextTransformerUpper(): void
    {
        $transformer = new TextTransformer();
        $result = $transformer->transform("hello world", "upper");
        $this->assertEquals("HELLO WORLD", $result);
    }

    public function testTextTransformerLower(): void
    {
        $transformer = new TextTransformer();
        $result = $transformer->transform("HELLO WORLD", "lower");
        $this->assertEquals("hello world", $result);
    }

    public function testTextTransformerTitle(): void
    {
        $transformer = new TextTransformer();
        $result = $transformer->transform("hello world", "title");
        $this->assertEquals("Hello World", $result);
    }

    public function testTextTransformerPatternMatch(): void
    {
        $transformer = new TextTransformer();
        $result = $transformer->transformWithPattern("hello world", "'hello' | 'world'");
        $this->assertTrue($result['found']);
        /* Alternation matches ONE of the alternatives - 'hello' matches first */
        $this->assertContains('hello', $result['matches']);
    }

    public function testTextTransformerCacheStats(): void
    {
        $transformer = new TextTransformer();
        $stats = $transformer->getCacheStats();
        $this->assertArrayHasKey('size', $stats);
        $this->assertArrayHasKey('max_size', $stats);
        $this->assertEquals(0, $stats['size']);
        $this->assertEquals(32, $stats['max_size']);
    }

    /*
     * Test 3: Template Engine with Table-Backed Substitution
     */
    public function testTemplateEngineBasicSubstitution(): void
    {
        $engine = new TemplateEngine();
        $engine->set('name', 'Alice');
        $engine->set('city', 'Boston');
        $result = $engine->render("Hello {name} from {city}!");
        $this->assertEquals("Hello Alice from Boston!", $result);
    }

    public function testTemplateEngineMissingVariable(): void
    {
        $engine = new TemplateEngine();
        $engine->set('name', 'Bob');
        $result = $engine->render("Hello {name} from {city}!");
        $this->assertEquals("Hello Bob from !", $result);
    }

    public function testTemplateEngineWithFormat(): void
    {
        $engine = new TemplateEngine();
        $engine->set('greeting', 'hello');
        $result = $engine->renderWithFormat("{greeting}", "upper");
        $this->assertEquals("HELLO", $result);
    }

    public function testTemplateEngineClear(): void
    {
        $engine = new TemplateEngine();
        $engine->set('key', 'value');
        $this->assertEquals(1, $engine->getVariableCount());
        $engine->clear();
        $this->assertEquals(0, $engine->getVariableCount());
    }

    public function testTemplateEngineMultipleVariables(): void
    {
        $engine = new TemplateEngine();
        for ($i = 0; $i < 10; $i++) {
            $engine->set("var$i", "value$i");
        }
        $this->assertEquals(10, $engine->getVariableCount());
        $template = "";
        for ($i = 0; $i < 10; $i++) {
            $template .= "{var$i} ";
        }
        $result = $engine->render(trim($template));
        $expected = "value0 value1 value2 value3 value4 value5 value6 value7 value8 value9";
        $this->assertEquals($expected, $result);
    }

    /*
     * Test 4: Combined Features Test
     */
    public function testCombinedFeatures(): void
    {
        $counter = new WordCounter();
        $transformer = new TextTransformer();
        $engine = new TemplateEngine();

        $wordCounts = $counter->countWords("hello world hello");
        foreach ($wordCounts as $word => $count) {
            $engine->set($word, (string) $count);
        }
        $result = $engine->render("hello:{hello} world:{world}");
        $this->assertEquals("hello:2 world:1", $result);
        $transformed = $transformer->transform($result, "upper");
        $this->assertEquals("HELLO:2 WORLD:1", $transformed);
    }

    public function testCombinedFeaturesWithMissingData(): void
    {
        $engine = new TemplateEngine();
        $transformer = new TextTransformer();
        $engine->set('known', 'value');
        $result = $engine->render("{known} {unknown}");
        $this->assertEquals("value ", $result);
        $transformed = $transformer->transform($result, "upper");
        $this->assertEquals("VALUE ", $transformed);
    }
}
