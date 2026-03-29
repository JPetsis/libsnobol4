<?php

namespace Snobol;

/**
 * Associative table for SNOBOL4 pattern matching
 *
 * Provides key-value storage with table-backed pattern substitutions.
 */
class Table
{
    /** @var resource Table resource from C extension */
    private $resource;

    /**
     * Create a new table
     *
     * @param  string  $name  Optional table name for debugging
     */
    public function __construct(string $name = '')
    {
        $this->resource = snobol_table_create($name);
    }

    /**
     * Set a key-value pair
     *
     * @param  string  $key
     * @param  string  $value
     * @return bool Success
     */
    public function set(string $key, string $value): bool
    {
        return snobol_table_set($this->resource, $key, $value);
    }

    /**
     * Get value by key
     *
     * @param  string  $key
     * @return string|null Value or null if not found
     */
    public function get(string $key): ?string
    {
        return snobol_table_get($this->resource, $key);
    }

    /**
     * Check if key exists
     *
     * @param  string  $key
     * @return bool
     */
    public function has(string $key): bool
    {
        return snobol_table_has($this->resource, $key);
    }

    /**
     * Delete a key
     *
     * @param  string  $key
     * @return bool Success
     */
    public function delete(string $key): bool
    {
        return snobol_table_delete($this->resource, $key);
    }

    /**
     * Get table size
     *
     * @return int Number of entries
     */
    public function size(): int
    {
        return snobol_table_size($this->resource);
    }

    /**
     * Get table name
     *
     * @return string
     */
    public function name(): string
    {
        return snobol_table_name($this->resource);
    }

    /**
     * Clear all entries
     *
     * @return void
     */
    public function clear(): void
    {
        snobol_table_clear($this->resource);
    }

    /**
     * Get all keys
     *
     * @return array
     */
    public function keys(): array
    {
        return snobol_table_keys($this->resource);
    }

    /**
     * Get all values
     *
     * @return array
     */
    public function values(): array
    {
        return snobol_table_values($this->resource);
    }

    /**
     * Convert to associative array
     *
     * @return array
     */
    public function toArray(): array
    {
        return snobol_table_to_array($this->resource);
    }
}
