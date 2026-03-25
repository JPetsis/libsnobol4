<?php

/**
 * Compatibility Fixture 1: Word Counter using Tables
 */

namespace Snobol\Tests\Compat;

use Snobol\Table;

class WordCounter
{
    private Table $table;

    public function __construct()
    {
        $this->table = new Table('wordcount');
    }

    public function countWords(string $input): array
    {
        $words = preg_split('/\s+/', trim($input));

        foreach ($words as $word) {
            $word = strtolower(trim($word));
            if ($word === '') {
                continue;
            }

            $current = $this->table->get($word);
            if ($current === null) {
                $this->table->set($word, '1');
            } else {
                $this->table->set($word, (string) ((int) $current + 1));
            }
        }

        $result = [];
        foreach ($words as $word) {
            $word = strtolower(trim($word));
            if ($word !== '' && !isset($result[$word])) {
                $result[$word] = (int) $this->table->get($word);
            }
        }

        return $result;
    }
}
