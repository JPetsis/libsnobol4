<?php

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;
use Snobol\Table;

class TableTest extends TestCase
{
    public function testTableCreation(): void
    {
        $table = new Table();
        $this->assertInstanceOf(Table::class, $table);
        $this->assertEquals(0, $table->size());
    }

    public function testTableCreationWithName(): void
    {
        $table = new Table('mytable');
        $this->assertInstanceOf(Table::class, $table);
        $this->assertEquals(0, $table->size());
    }

    public function testTableSetAndGet(): void
    {
        $table = new Table();

        $result = $table->set('key1', 'value1');
        $this->assertTrue($result);
        $this->assertEquals(1, $table->size());

        $value = $table->get('key1');
        $this->assertEquals('value1', $value);
    }

    public function testTableHas(): void
    {
        $table = new Table();

        $this->assertFalse($table->has('key1'));

        $table->set('key1', 'value1');
        $this->assertTrue($table->has('key1'));
    }

    public function testTableDelete(): void
    {
        $table = new Table();

        $table->set('key1', 'value1');
        $this->assertTrue($table->has('key1'));

        $result = $table->delete('key1');
        $this->assertTrue($result);
        $this->assertFalse($table->has('key1'));
        $this->assertEquals(0, $table->size());
    }

    public function testTableDeleteNonExistent(): void
    {
        $table = new Table();

        $result = $table->delete('nonexistent');
        $this->assertFalse($result);
    }

    public function testTableGetNonExistent(): void
    {
        $table = new Table();

        $value = $table->get('nonexistent');
        $this->assertNull($value);
    }

    public function testTableSetNullDeletes(): void
    {
        $table = new Table();

        $table->set('key1', 'value1');
        $this->assertTrue($table->has('key1'));

        $table->set('key1', null);
        $this->assertFalse($table->has('key1'));
        $this->assertEquals(0, $table->size());
    }

    public function testTableUpdate(): void
    {
        $table = new Table();

        $table->set('key1', 'original');
        $table->set('key1', 'updated');

        $this->assertEquals(1, $table->size());
        $this->assertEquals('updated', $table->get('key1'));
    }

    public function testTableClear(): void
    {
        $table = new Table();

        $table->set('key1', 'value1');
        $table->set('key2', 'value2');
        $table->set('key3', 'value3');

        $this->assertEquals(3, $table->size());

        $table->clear();

        $this->assertEquals(0, $table->size());
        $this->assertFalse($table->has('key1'));
        $this->assertFalse($table->has('key2'));
        $this->assertFalse($table->has('key3'));
    }

    public function testTableClearAndReuse(): void
    {
        $table = new Table();

        $table->set('key1', 'value1');
        $table->clear();

        $table->set('key2', 'value2');

        $this->assertEquals(1, $table->size());
        $this->assertEquals('value2', $table->get('key2'));
        $this->assertFalse($table->has('key1'));
    }

    public function testTableSpecialCharacters(): void
    {
        $table = new Table();

        $table->set('hello world', 'foo bar');
        $this->assertEquals('foo bar', $table->get('hello world'));

        $table->set('key123', 'value456');
        $this->assertEquals('value456', $table->get('key123'));
    }

    public function testTableEmptyStringValue(): void
    {
        $table = new Table();

        $table->set('empty', '');
        $this->assertTrue($table->has('empty'));
        $this->assertEquals('', $table->get('empty'));
        $this->assertEquals(1, $table->size());
    }

    public function testTableMultipleTables(): void
    {
        $table1 = new Table('t1');
        $table2 = new Table('t2');

        $table1->set('key', 'from_t1');
        $table2->set('key', 'from_t2');

        $this->assertEquals('from_t1', $table1->get('key'));
        $this->assertEquals('from_t2', $table2->get('key'));
    }

    public function testTableSizeAfterMultipleOperations(): void
    {
        $table = new Table();

        for ($i = 0; $i < 10; $i++) {
            $table->set("key$i", "value$i");
        }

        $this->assertEquals(10, $table->size());

        for ($i = 0; $i < 5; $i++) {
            $table->delete("key$i");
        }

        $this->assertEquals(5, $table->size());
    }
}
