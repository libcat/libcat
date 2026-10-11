English | [中文](./README-CN.md)

<h1 align="center">CAT</h1>
<p align="center"><strong>Cooperative Async Technology</strong></p>
<p align="center"><strong>Synchronous code. High-performance async I/O.</strong></p>

[![license][license-badge]][license-link]
[![tests][ci-badge]][ci-link]
[![codecov][codecov-badge]][codecov-link]
![platform][platform-badge]
![architecture][architecture-badge]

libcat is the C coroutine and I/O core behind [Swow][swow-link]. Write with functions, loops and local variables; stackful coroutines and libuv handle asynchronous concurrency.

## Performance in the execution path

**Switch coroutines in user space.** The default context implementation saves and restores registers and stack state in assembly. Switching coroutines does not require waking another system thread.

**Wait on one connection. Serve the others.** The event loop schedules multiple connections without thread count growing in step with connection count. A socket call can suspend its coroutine before returning while other coroutines keep running.

**Move less data. Reuse allocations.** Plaintext reads fill caller-provided buffers directly. Vector writes can submit existing buffers together. Sockets reuse write requests when no other write occupies them.

<details>
<summary>Implementation references</summary>

- [Assembly for the default context implementation][context-source].
- [Read buffers][buffer-source], [vector writes and cached write requests][write-source].
- [Direct reads on supported Unix socket paths][inline-read-source]: return when data is available, then use the event-driven path when waiting is necessary.

Specific paths depend on the platform, socket type and encryption state. Default scheduling is cooperative: long CPU work or external blocking calls occupy the scheduler thread. Blocking tasks can use the worker interface.

</details>

## One C core for application I/O

### Coroutines and concurrency

| Capability | What you can do |
| --- | --- |
| [Stackful coroutines][coroutine-api] | Create, resume and suspend; preserve nested calls and local variables; configure stack size and inspect state, switch counts and elapsed time |
| [Channels][channel-api] | Buffered and unbuffered communication, producers and consumers, timed waits, multi-channel selection, closure and data cleanup |
| [Wait groups][sync-api] | Join coroutine tasks, wait for a group of concurrent operations and set a wait timeout |
| [Time and waiting][time-api] | Coroutine sleep and delay, timed waits, high-resolution time queries and cached time reads |

### Networking and protocols

| Capability | What you can do |
| --- | --- |
| [Sockets][socket-api] | TCP, UDP, IPv4/IPv6, Unix local streams and datagrams; connect, accept, read, write, vector writes and handle passing; operation-specific timeouts |
| [TLS / SSL][ssl-api] | OpenSSL integration, handshakes and encrypted transport, certificate-chain and peer-name verification, SNI, ALPN and protocol configuration |
| [HTTP and multipart][http-api] | Incremental request and response parsing; URL, status, header and body events; chunked messages and multipart data |
| [WebSocket][websocket-api] | Frame headers, opcodes, payload lengths, masking-key configuration, masking and unmasking utilities |
| [DNS][dns-api] | Wait for name resolution in a coroutine, query addresses and IPs, and configure lookup timeouts |
| [poll / select][poll-api] | Bring readable and writable waits for existing descriptors into coroutines while retaining poll/select-style calls |

### Clients and system I/O

| Capability | What you can do |
| --- | --- |
| [cURL][curl-api] | Keep `CURL *` handles, options and callbacks; wait for transfers and organize concurrent requests through coroutine-aware easy and multi interfaces |
| [PostgreSQL / libpq][pq-api] | Coroutine connections and queries, parameterized queries, prepared statements and execution; retain libpq connection and result objects |
| [Files and directories][fs-api] | Read, write, seek, sync, inspect metadata, rename, traverse directories and lock files; use the sendfile interface |
| [Processes][process-api] | Start child processes, wait for exit in a coroutine and inspect exit status; use [fork/waitpid interfaces][os-wait-api] on supported platforms |
| [Signals][signal-api] | Wait for signals in a coroutine, configure wait timeouts and send signals to processes |
| [Workers][work-api] and [asynchronous notifications][async-api] | Execute blocking tasks on workers while the caller waits; bring thread notifications into the event loop and arrange completion cleanup |

### Data and diagnostics

| Capability | What you can do |
| --- | --- |
| [Dynamic buffers][buffer-api] | Grow, append, format and export data; manage capacity and actual length |
| [Debug logs][log-api] and [watchdog][watchdog-api] | Module logs, coroutine state and switch statistics, scheduler deadlock diagnostics and blocking detection; build with ASan support |

OpenSSL, cURL and PostgreSQL are enabled through build configuration. System-specific capabilities depend on the platform.

## What the code looks like

### TCP: one connection, one coroutine

Each connection has its own stack and buffer. Read, send and close in order; other connections keep working while this one waits for data.

```c
static cat_data_t *echo_connection(cat_data_t *data)
{
    cat_socket_t *connection = (cat_socket_t *) data;
    char buffer[4096];
    ssize_t length;

    while ((length = cat_socket_recv(connection, buffer, sizeof(buffer))) > 0) {
        if (!cat_socket_send(connection, buffer, (size_t) length)) {
            break;
        }
    }

    cat_socket_close(connection);
    return NULL;
}
```

<details>
<summary>Starting the connection coroutine</summary>

Include `cat_api.h` and initialize the runtime and scheduler. Pass an accepted connection created with `cat_socket_create(NULL, ...)`:

```c
if (cat_coroutine_run(NULL, echo_connection, connection) == NULL) {
    cat_socket_close(connection);
}
```

The coroutine closes the connection after a successful start; the caller closes it if starting fails. Peer closure or a read/write error ends processing. Use `cat_get_last_error_message()` for error diagnostics. See the [HTTP echo example][http-example] for a complete listener entry point.

</details>

### HTTP: read, parse, respond

For a GET request that receives `Hello, CAT!`, the core flow after parser initialization is below. The coroutine handles an accepted connection; a request arriving in fragments continues through the same loop.

```c
while (!cat_http_parser_is_completed(&parser)) {
    ssize_t length = cat_socket_recv(connection, buffer, sizeof(buffer));
    if (length <= 0 || !cat_http_parser_execute(&parser, buffer, (size_t) length)) {
        goto _close;
    }
}

if (!cat_socket_send(connection, response, strlen(response))) {
    fprintf(stderr, "%s\n", cat_get_last_error_message());
}
```

<details>
<summary>Full handler: GET responds with Hello, CAT!</summary>

Include `cat_api.h` and `cat_http.h`. Ownership follows the TCP example; use `http_connection` as the entry point. The example closes each connection after one request; invalid requests or an early disconnect end processing.

```c
static cat_data_t *http_connection(cat_data_t *data)
{
    cat_socket_t *connection = (cat_socket_t *) data;
    cat_http_parser_t parser;
    char buffer[4096];

    cat_http_parser_init(&parser);
    if (!cat_http_parser_set_type(&parser, CAT_HTTP_PARSER_TYPE_REQUEST)) {
        goto _close;
    }
    cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENT_MESSAGE_COMPLETE);

    while (!cat_http_parser_is_completed(&parser)) {
        ssize_t length = cat_socket_recv(connection, buffer, sizeof(buffer));
        if (length <= 0 || !cat_http_parser_execute(&parser, buffer, (size_t) length)) {
            goto _close;
        }
    }

    const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: 12\r\n"
        "Connection: close\r\n\r\n"
        "Hello, CAT!\n";

    if (!cat_socket_send(connection, response, strlen(response))) {
        fprintf(stderr, "%s\n", cat_get_last_error_message());
    }

    _close:
    cat_socket_close(connection);
    return NULL;
}
```

</details>

## Build

Use CMake 3.18+, a C11 compiler and the assembler toolchain required by the default context implementation.

```sh
git clone https://github.com/libcat/libcat.git
cd libcat
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLIBCAT_BUILD_TESTS=OFF
cmake --build build --config Release --target cat --parallel
```

The target is the static library `cat`. Check CMake output for enabled OpenSSL, libcurl and libpq modules. For include paths, compile definitions and link dependencies when embedding the library, see the [CMake configuration][cmake-link] and [Swow integration][swow-integration].

## Explore further

- [Public APIs][headers-link] · [Tests][tests-link] · [Platform CI][ci-link].
- [HTTP echo server][http-example] · [DNS lookup][dns-example] · [HTTP benchmark entry point][http-benchmark].

The HTTP benchmark entry point provides workload settings. Report measurements with hardware, build configuration, throughput, latency and resource usage. With GoogleTest and a C++20 toolchain installed, run tests with `./run-tests.sh`; see the [CI configuration][workflow-link] for services and platform setup.

Send minimal reproductions through [Issues][issues-link], and contribute code, tests or documentation through [Pull Requests][pulls-link]. libcat uses the [Apache License 2.0][license-link]; third-party licenses are in [LICENSE.full][full-license-link].

[license-badge]: https://img.shields.io/badge/license-apache2-blue.svg
[license-link]: ./LICENSE
[full-license-link]: ./LICENSE.full
[ci-badge]: https://github.com/libcat/libcat/actions/workflows/tests.yml/badge.svg?branch=develop
[ci-link]: https://github.com/libcat/libcat/actions/workflows/tests.yml?query=branch%3Adevelop
[codecov-badge]: https://codecov.io/gh/libcat/libcat/branch/develop/graph/badge.svg
[codecov-link]: https://codecov.io/gh/libcat/libcat
[platform-badge]: https://img.shields.io/badge/platform-Win32%20%7C%20GNU%2FLinux%20%7C%20macOS%20%7C%20FreeBSD-gold
[architecture-badge]: https://img.shields.io/badge/architecture-x86--64%20%7C%20ARM64%20%7C%20mips64el-maroon
[swow-link]: https://github.com/swow/swow
[swow-integration]: https://github.com/swow/swow/blob/develop/ext/config.m4
[context-source]: ./deps/context/asm/jump_x86_64_sysv_elf_gas.S
[buffer-source]: ./src/cat_socket.c#L2800
[write-source]: ./src/cat_socket.c#L3547
[inline-read-source]: ./src/cat_socket.c#L2930
[socket-api]: ./include/cat_socket.h
[ssl-api]: ./include/cat_ssl.h
[curl-api]: ./include/cat_curl.h
[pq-api]: ./include/cat_pq.h
[coroutine-api]: ./include/cat_coroutine.h
[channel-api]: ./include/cat_channel.h
[http-example]: ./examples/socket/server/http_echo/main.c
[dns-example]: ./examples/dns/getaddrinfo/main.c
[headers-link]: ./include
[tests-link]: ./tests
[cmake-link]: ./CMakeLists.txt
[workflow-link]: ./.github/workflows/tests.yml
[http-benchmark]: ./examples/ab.sh
[issues-link]: https://github.com/libcat/libcat/issues
[pulls-link]: https://github.com/libcat/libcat/pulls
[sync-api]: ./include/cat_sync.h
[time-api]: ./include/cat_time.h
[http-api]: ./include/cat_http.h
[websocket-api]: ./include/cat_websocket.h
[dns-api]: ./include/cat_dns.h
[poll-api]: ./include/cat_poll.h
[fs-api]: ./include/cat_fs.h
[process-api]: ./include/cat_process.h
[os-wait-api]: ./include/cat_os_wait.h
[signal-api]: ./include/cat_signal.h
[work-api]: ./include/cat_work.h
[async-api]: ./include/cat_async.h
[buffer-api]: ./include/cat_buffer.h
[log-api]: ./include/cat_log.h
[watchdog-api]: ./include/cat_watchdog.h
