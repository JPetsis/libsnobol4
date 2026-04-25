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
require_once __DIR__.'/fixtures/WordCounterWithGoto.php';
require_once __DIR__.'/fixtures/TextTransformerWithGoto.php';
require_once __DIR__.'/fixtures/TemplateEngineWithGoto.php';

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

    /*
     * Test 5: Labelled Control Flow – WordCounterWithGoto
     */
    public function testLabeledWordMatch(): void
    {
        $counter = new WordCounterWithGoto();
        $result = $counter->matchLabeledWord("hello world");

        $this->assertNotFalse($result, 'labeled span matches a word');
    }

    public function testLabeledWordCount(): void
    {
        $counter = new WordCounterWithGoto();
        $count = $counter->countWords("one two three 42");

        /* Three alphabetic word spans: "one", "two", "three" */
        $this->assertEquals(3, $count);
    }

    public function testForwardGotoPrefix(): void
    {
        $counter = new WordCounterWithGoto();

        /* ">>" prefix followed by letters: should match */
        $this->assertNotFalse($counter->matchPrefixedWord(">>hello"));

        /* No ">>" prefix: should NOT match */
        $this->assertFalse($counter->matchPrefixedWord("hello"));
    }

    /*
     * Test 6: Labelled Control Flow – TextTransformerWithGoto
     */
    public function testClassifyNumber(): void
    {
        $transformer = new TextTransformerWithGoto();
        $this->assertEquals('number', $transformer->classifyFirstToken('123abc'));
    }

    public function testClassifyWord(): void
    {
        $transformer = new TextTransformerWithGoto();
        $this->assertEquals('word', $transformer->classifyFirstToken('hello'));
    }

    public function testTaggedSegmentMatch(): void
    {
        $transformer = new TextTransformerWithGoto();
        /* "[intro]" tag followed by alpha content */
        $result = $transformer->matchTaggedSegment('[intro]hello world', 'intro');
        $this->assertNotFalse($result, 'tagged segment forward-goto matches');
    }

    public function testTaggedSegmentNoMatch(): void
    {
        $transformer = new TextTransformerWithGoto();
        /* Wrong tag */
        $result = $transformer->matchTaggedSegment('[other]hello', 'intro');
        $this->assertFalse($result, 'tagged segment does not match wrong tag');
    }

    /*
     * Test 7: Labelled Control Flow – TemplateEngineWithGoto
     */
    public function testDetectTemplateVariable(): void
    {
        $engine = new TemplateEngineWithGoto();
        $result = $engine->detectVariable('{name} rest');

        $this->assertNotFalse($result);
        $this->assertEquals('name', $result['v0']);
    }

    public function testFindTemplateVariables(): void
    {
        $engine = new TemplateEngineWithGoto();
        $vars = $engine->findVariables('{first} and {second} and {third}');

        $this->assertCount(3, $vars);
        $this->assertContains('first', $vars);
        $this->assertContains('second', $vars);
        $this->assertContains('third', $vars);
    }

    public function testValidateVariableWithGoto(): void
    {
        $engine = new TemplateEngineWithGoto();

        /* Valid variable tokens */
        $this->assertTrue($engine->validateVariable('{hello}'));
        $this->assertTrue($engine->validateVariable('{ABC}'));

        /* Invalid: no closing brace - should not match */
        $this->assertFalse($engine->validateVariable('{missing'));
    }

    public function testCountTemplateVariables(): void
    {
        $engine = new TemplateEngineWithGoto();
        $this->assertEquals(2, $engine->countVariables('{a} plain text {b}'));
        $this->assertEquals(0, $engine->countVariables('no variables here'));
    }
}
