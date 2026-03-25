<?php

/**
 * Compatibility Fixture 3: Template Engine with Table-Backed Substitution
 */

namespace Snobol\Tests\Compat;

use Snobol\Table;

class TemplateEngine
{
    private Table $variables;

    public function __construct()
    {
        $this->variables = new Table('template_vars');
    }

    public function set(string $name, string $value): void
    {
        $this->variables->set($name, $value);
    }

    public function renderWithFormat(string $template, string $format): string
    {
        $result = $this->render($template);

        switch ($format) {
            case 'upper':
                return strtoupper($result);
            case 'lower':
                return strtolower($result);
            case 'trim':
                return trim($result);
            default:
                return $result;
        }
    }

    public function render(string $template): string
    {
        /* Simple template rendering: {VAR} -> table lookup */
        return preg_replace_callback('/\{([A-Za-z_][A-Za-z0-9_]*)\}/', function ($matches) {
            $varName = $matches[1];
            $value = $this->variables->get($varName);
            return $value ?? '';  /* Graceful degradation: empty for missing */
        }, $template);
    }

    public function get(string $name): ?string
    {
        return $this->variables->get($name);
    }

    public function clear(): void
    {
        $this->variables->clear();
    }

    public function getVariableCount(): int
    {
        return $this->variables->size();
    }
}
