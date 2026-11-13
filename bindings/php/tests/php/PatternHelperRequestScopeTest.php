<?php

declare(strict_types=1);

namespace Snobol\Tests;

use PHPUnit\Framework\TestCase;

/**
 * Regression guard for the PatternHelper slot cache (security finding
 * hsf_a805a673209b71b71f73ad3d): the cache is a file-scope static in the
 * extension while its entries are request-heap zend_string/zval values. The
 * module must empty it at request shutdown, or the next request on a reused
 * worker (php-fpm, mod_php) reads and releases memory that request shutdown
 * already freed.
 *
 * The built-in server serves every request in one process, so two sequential
 * requests reproduce the worker reuse without a web server stack.
 */
final class PatternHelperRequestScopeTest extends TestCase
{
    private const TIMEOUT_SECONDS = 0.1;
    private const READY_ATTEMPTS = 50;
    private const REQUEST_ATTEMPTS = 5;

    /** @var resource|false|null */
    private $server = null;

    private ?string $docroot = null;

    private ?string $logFile = null;

    private ?int $port = null;

    protected function setUp(): void
    {
        if (!extension_loaded('snobol')) {
            $this->markTestSkipped('snobol extension not loaded');
        }
        if (!function_exists('proc_open')) {
            $this->markTestSkipped('proc_open() is not available');
        }
    }

    protected function tearDown(): void
    {
        if (is_resource($this->server)) {
            $status = proc_get_status($this->server);
            if ($status['running']) {
                proc_terminate($this->server);
            }
            proc_close($this->server);
        }
        $this->server = null;

        if ($this->logFile !== null) {
            @unlink($this->logFile);
        }
        $this->logFile = null;

        if ($this->docroot !== null) {
            @unlink($this->docroot . '/probe.php');
            @rmdir($this->docroot);
        }
        $this->docroot = null;
        $this->port = null;
    }

    /**
     * The second request in the same process must compile its own Pattern
     * instead of reusing the first request's freed one.
     */
    public function testSecondRequestDoesNotReuseThePreviousRequestsPattern(): void
    {
        $this->startServer();

        $this->assertSame('OK', $this->request('/probe.php?p=%27aae%27'));
        $this->assertSame(
            'OK',
            $this->request('/probe.php?p=%27aae%27'),
            'the second request served a Pattern cached by the previous request'
        );
        $this->assertSame('OK', $this->request('/probe.php?p=%27aae%27'));

        $status = proc_get_status($this->server);
        $this->assertTrue(
            $status['running'],
            'the server process died while serving the cached Pattern'
        );
    }

    /** Start `php -S` on a free local port with a one-file document root. */
    private function startServer(): void
    {
        $this->docroot = sys_get_temp_dir() . '/snobol-phelper-' . bin2hex(random_bytes(8));
        self::assertTrue(mkdir($this->docroot, 0700, true), 'cannot create the probe document root');
        self::assertTrue(
            file_put_contents(
                $this->docroot . '/probe.php',
                "<?php\n"
                . "\$pattern = \$_GET['p'] ?? \"'aae'\";\n"
                . "try {\n"
                . "    \$result = \\Snobol\\PatternHelper::matchOnce(\$pattern, 'aae');\n"
                . "    echo is_array(\$result) ? 'OK' : 'NO_MATCH';\n"
                . "} catch (\\Throwable \$e) {\n"
                . "    echo 'EXC: ' . \$e->getMessage();\n"
                . "}\n"
            ) !== false,
            'cannot write the probe script'
        );

        $this->port = self::findFreePort();
        $this->logFile = $this->docroot . '.log';

        $descriptors = [
            0 => ['pipe', 'r'],
            1 => ['file', $this->logFile, 'a'],
            2 => ['file', $this->logFile, 'a'],
        ];
        $command = [
            PHP_BINARY,
            '-S',
            '127.0.0.1:' . $this->port,
            '-t',
            $this->docroot,
        ];
        $server = proc_open($command, $descriptors, $pipes, null, self::serverEnvironment());
        self::assertIsResource($server, 'cannot start the PHP built-in server');
        $this->server = $server;
        fclose($pipes[0]);

        $this->waitUntilListening();
    }

    /**
     * The inherited environment without PHP_CLI_SERVER_WORKERS: a value above
     * 1 makes the built-in server fork workers, and requests served by
     * different workers would each start with an empty cache.
     *
     * @return array<string, string>
     */
    private static function serverEnvironment(): array
    {
        $env = getenv();
        if (!is_array($env)) {
            return [];
        }
        unset($env['PHP_CLI_SERVER_WORKERS']);

        return $env;
    }

    /** @return int a port that was free a moment ago */
    private static function findFreePort(): int
    {
        $socket = stream_socket_server('tcp://127.0.0.1:0', $errno, $errstr);
        self::assertIsResource($socket, 'cannot reserve a local port: ' . $errstr);
        $name = stream_socket_get_name($socket, false);
        fclose($socket);
        self::assertIsString($name, 'cannot read the reserved port');

        return (int) substr($name, (int) strrpos($name, ':') + 1);
    }

    private function waitUntilListening(): void
    {
        for ($attempt = 0; $attempt < self::READY_ATTEMPTS; $attempt++) {
            $probe = @stream_socket_client(
                'tcp://127.0.0.1:' . $this->port,
                $errno,
                $errstr,
                self::TIMEOUT_SECONDS
            );
            if ($probe !== false) {
                fclose($probe);

                return;
            }
            $status = proc_get_status($this->server);
            if (!$status['running']) {
                self::fail('the PHP built-in server exited: ' . $this->serverLog());
            }
            usleep(100_000);
        }

        self::fail('the PHP built-in server did not start: ' . $this->serverLog());
    }

    /** Issue one request to the probe script and return its body. */
    private function request(string $path): string
    {
        $url = 'http://127.0.0.1:' . $this->port . $path;
        for ($attempt = 0; $attempt < self::REQUEST_ATTEMPTS; $attempt++) {
            $body = @file_get_contents(
                $url,
                false,
                stream_context_create(['http' => ['ignore_errors' => true, 'timeout' => 2]])
            );
            if ($body !== false) {
                return trim($body);
            }
            usleep(50_000);
        }

        self::fail('request to ' . $url . ' failed: ' . $this->serverLog());

        return '';
    }

    private function serverLog(): string
    {
        if ($this->logFile === null || !is_file($this->logFile)) {
            return '(no server log)';
        }

        return (string) file_get_contents($this->logFile);
    }
}
