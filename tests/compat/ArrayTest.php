<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Array_;

class ArrayTest extends TestCase
{
    public function testArrayCreation(): void
    {
        $arr = new Array_();
        $this->assertInstanceOf(Array_::class, $arr);
        $this->assertEquals(0, $arr->size());
    }

    public function testArrayCreationWithSize(): void
    {
        $arr = new Array_(10);
        $this->assertInstanceOf(Array_::class, $arr);
        $this->assertEquals(0, $arr->size());
    }

    public function testArraySetAndGet(): void
    {
        $arr = new Array_();

        $result = $arr->set(1, 'hello');
        $this->assertTrue($result);
        $this->assertEquals(1, $arr->size());

        $value = $arr->get(1);
        $this->assertEquals('hello', $value);
    }

    public function testArrayGetUnset(): void
    {
        $arr = new Array_();
        $value = $arr->get(42);
        $this->assertNull($value);
    }

    public function testArrayHas(): void
    {
        $arr = new Array_();
        $this->assertFalse($arr->has(1));

        $arr->set(1, 'hello');
        $this->assertTrue($arr->has(1));
    }

    public function testArrayDelete(): void
    {
        $arr = new Array_();

        $arr->set(1, 'value1');
        $this->assertTrue($arr->has(1));

        $result = $arr->delete(1);
        $this->assertTrue($result);
        $this->assertFalse($arr->has(1));
        $this->assertEquals(0, $arr->size());
    }

    public function testArrayDeleteNonExistent(): void
    {
        $arr = new Array_();
        $result = $arr->delete(99);
        $this->assertFalse($result);
    }

    public function testArrayUpdate(): void
    {
        $arr = new Array_();

        $arr->set(5, 'original');
        $arr->set(5, 'updated');

        $this->assertEquals(1, $arr->size());
        $this->assertEquals('updated', $arr->get(5));
    }

    public function testArrayClear(): void
    {
        $arr = new Array_();

        $arr->set(1, 'a');
        $arr->set(2, 'b');
        $arr->set(3, 'c');

        $this->assertEquals(3, $arr->size());

        $arr->clear();

        $this->assertEquals(0, $arr->size());
        $this->assertFalse($arr->has(1));
        $this->assertFalse($arr->has(2));
        $this->assertFalse($arr->has(3));
    }

    public function testArraySparseAccess(): void
    {
        $arr = new Array_();

        $arr->set(1, 'first');
        $arr->set(100, 'hundredth');
        $arr->set(1000, 'thousandth');

        $this->assertEquals(3, $arr->size());

        $this->assertEquals('first', $arr->get(1));
        $this->assertEquals('hundredth', $arr->get(100));
        $this->assertEquals('thousandth', $arr->get(1000));

        $this->assertNull($arr->get(50));
    }

    public function testArrayKeys(): void
    {
        $arr = new Array_();

        $arr->set(3, 'three');
        $arr->set(1, 'one');
        $arr->set(2, 'two');

        $keys = $arr->keys();
        $this->assertCount(3, $keys);
        sort($keys);
        $this->assertEquals([1, 2, 3], $keys);
    }

    public function testArrayValues(): void
    {
        $arr = new Array_();

        $arr->set(1, 'one');
        $arr->set(2, 'two');

        $values = $arr->values();
        $this->assertCount(2, $values);
        sort($values);
        $this->assertEquals(['one', 'two'], $values);
    }

    public function testArrayMultipleInstances(): void
    {
        $arr1 = new Array_();
        $arr2 = new Array_();

        $arr1->set(1, 'from_arr1');
        $arr2->set(1, 'from_arr2');

        $this->assertEquals('from_arr1', $arr1->get(1));
        $this->assertEquals('from_arr2', $arr2->get(1));
    }

    public function testArrayBulkOperations(): void
    {
        $arr = new Array_();

        for ($i = 0; $i < 10; $i++) {
            $arr->set($i, "value$i");
        }

        $this->assertEquals(10, $arr->size());

        for ($i = 0; $i < 5; $i++) {
            $arr->delete($i);
        }

        $this->assertEquals(5, $arr->size());

        $this->assertFalse($arr->has(0));
        $this->assertTrue($arr->has(9));
    }
}
