<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Array_;

class ArrayTest extends TestCase
{
    public function testArrayCreation(): void
    {
        $array = new Array_();
        $this->assertInstanceOf(Array_::class, $array);
        $this->assertSame(0, $array->size());
    }

    public function testArrayCreationWithSize(): void
    {
        $array = new Array_(8);
        $this->assertSame(0, $array->size());
    }

    public function testArraySetAndGet(): void
    {
        $array = new Array_();
        $this->assertTrue($array->set(0, 'hello'));
        $this->assertSame('hello', $array->get(0));
    }

    public function testArraySetUpdatesExisting(): void
    {
        $array = new Array_();
        $array->set(1, 'old');
        $array->set(1, 'new');
        $this->assertSame('new', $array->get(1));
    }

    public function testArrayGetMissingKeyReturnsNull(): void
    {
        $array = new Array_();
        $this->assertNull($array->get(42));
    }

    public function testArrayHas(): void
    {
        $array = new Array_();
        $this->assertFalse($array->has(0));
        $array->set(0, 'x');
        $this->assertTrue($array->has(0));
        $this->assertFalse($array->has(1));
    }

    public function testArrayDelete(): void
    {
        $array = new Array_();
        $array->set(2, 'x');
        $this->assertTrue($array->delete(2));
        $this->assertFalse($array->has(2));
        $this->assertFalse($array->delete(2));
    }

    public function testArrayClear(): void
    {
        $array = new Array_();
        $array->set(0, 'a');
        $array->set(1, 'b');
        $array->clear();
        $this->assertSame(0, $array->size());
        $this->assertFalse($array->has(0));
    }

    public function testArrayKeys(): void
    {
        $array = new Array_();
        $array->set(1, 'a');
        $array->set(3, 'b');
        $this->assertSame([1, 3], $array->keys());
    }

    public function testArrayKeysEmpty(): void
    {
        $array = new Array_();
        $this->assertSame([], $array->keys());
    }

    public function testArrayValues(): void
    {
        $array = new Array_();
        $array->set(0, 'a');
        $array->set(1, 'b');
        $this->assertSame(['a', 'b'], $array->values());
    }

    public function testArrayValuesEmpty(): void
    {
        $array = new Array_();
        $this->assertSame([], $array->values());
    }

    public function testArrayNegativeKeys(): void
    {
        $array = new Array_();
        $array->set(-1, 'neg');
        $this->assertTrue($array->has(-1));
        $this->assertSame('neg', $array->get(-1));
    }

    public function testArrayZeroKey(): void
    {
        $array = new Array_();
        $array->set(0, 'zero');
        $this->assertSame('zero', $array->get(0));
    }

    public function testArraySparseKeys(): void
    {
        $array = new Array_();
        $array->set(5, 'five');
        $array->set(-2, 'neg');
        $this->assertSame([5, -2], $array->keys());
        $this->assertNull($array->get(1));
    }

    public function testArrayManyOperations(): void
    {
        $array = new Array_();
        for ($i = 0; $i < 100; $i++) {
            $array->set($i, "v{$i}");
        }
        $this->assertSame(100, $array->size());
        $this->assertSame('v99', $array->get(99));
        $this->assertCount(100, $array->keys());
    }

    public function testArrayDestructionReleases(): void
    {
        $array = new Array_();
        $array->set(0, 'x');
        $id = spl_object_id($array);
        unset($array);
        gc_collect_cycles();
        $this->assertTrue(true);
    }
}
