<?php

namespace Snobol;

class Array_
{
    private $resource;

    public function __construct(int $size = 0)
    {
        $this->resource = snobol_array_create($size);
    }

    public function set(int $key, string $value): bool
    {
        return snobol_array_set($this->resource, $key, $value);
    }

    public function get(int $key): ?string
    {
        return snobol_array_get($this->resource, $key);
    }

    public function has(int $key): bool
    {
        return snobol_array_has($this->resource, $key);
    }

    public function delete(int $key): bool
    {
        return snobol_array_delete($this->resource, $key);
    }

    public function size(): int
    {
        return snobol_array_size($this->resource);
    }

    public function clear(): void
    {
        snobol_array_clear($this->resource);
    }

    public function keys(): array
    {
        return snobol_array_keys($this->resource);
    }

    public function values(): array
    {
        return snobol_array_values($this->resource);
    }
}
