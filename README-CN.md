[English](./README.md) | 中文

<h1 align="center">CAT</h1>
<p align="center"><strong>Cooperative Async Technology</strong></p>
<p align="center"><strong>同步的写法，异步的高性能。</strong></p>

[![license][license-badge]][license-link]
[![tests][ci-badge]][ci-link]
[![codecov][codecov-badge]][codecov-link]
![platform][platform-badge]
![architecture][architecture-badge]

libcat 是 [Swow][swow-link] 背后的 C 协程与 I/O 核心。用函数、循环和局部变量组织代码，由有栈协程和 libuv 完成异步并发。

## 性能，落实到执行路径

**协程在用户态切换。** 默认上下文用汇编保存、恢复寄存器和栈状态；切换协程无需唤醒另一个系统线程。

**等待一个连接，继续服务其他连接。** 多个连接由事件循环调度，线程数量无需随连接数量同步增长。Socket 调用返回前，当前协程可以挂起，其他协程继续执行。

**减少数据搬运和重复分配。** 明文读取直接填入调用方缓冲区；向量写入可提交现有的多个缓冲区；没有其他写入占用时，Socket 复用写请求对象。

<details>
<summary>查看实现依据</summary>

- [默认上下文的汇编实现][context-source]。
- [读取缓冲区][buffer-source]、[向量写入与写请求缓存][write-source]。
- [支持的 Unix Socket 直接读取路径][inline-read-source]：数据可读时直接返回，需要等待时再进入事件驱动路径。

具体路径取决于平台、Socket 类型和加密状态。默认调度为协作式：直接执行长时间 CPU 计算或外部阻塞调用，会占用调度线程；阻塞任务可以通过工作线程接口执行。

</details>

## 一个 C 核心，覆盖应用的 I/O

### 协程与并发

| 能力 | 可以做什么 |
| --- | --- |
| [有栈协程][coroutine-api] | 创建、恢复、挂起；保留嵌套调用与局部变量；配置栈大小，查询状态、切换次数与运行时间 |
| [Channel][channel-api] | 有缓冲与无缓冲通信、生产与消费、超时等待、多 Channel 选择、关闭与数据清理 |
| [WaitGroup][sync-api] | 汇合多个协程任务，等待一组并发操作完成，设置等待超时 |
| [时间与等待][time-api] | 协程 sleep 与 delay、超时等待、高精度时间查询和缓存时间读取 |

### 网络与协议

| 能力 | 可以做什么 |
| --- | --- |
| [Socket][socket-api] | TCP、UDP、IPv4/IPv6、Unix 本地流与数据报；连接、接受、读写、向量写入和句柄传递；按操作配置超时 |
| [TLS / SSL][ssl-api] | OpenSSL 集成、握手与加密传输、证书链及对端名称验证、SNI、ALPN 与协议配置 |
| [HTTP 与 multipart][http-api] | 增量解析请求和响应；URL、状态、消息头与正文事件；chunked 消息和 multipart 数据解析 |
| [WebSocket][websocket-api] | 帧头、操作码、负载长度、掩码字段配置，以及数据掩码与去掩码工具 |
| [DNS][dns-api] | 在协程中等待域名解析，查询地址与 IP，配置解析超时 |
| [poll / select][poll-api] | 将已有描述符的可读、可写等待接入协程，复用基于 poll/select 的调用方式 |

### 客户端与系统 I/O

| 能力 | 可以做什么 |
| --- | --- |
| [cURL][curl-api] | 沿用 `CURL *`、选项与回调；使用协程版 easy 和 multi 接口等待传输，组织并发请求 |
| [PostgreSQL / libpq][pq-api] | 协程连接与查询、参数查询、预处理语句及执行；继续使用 libpq 的连接和结果对象 |
| [文件与目录][fs-api] | 文件读写、定位、同步、状态查询、重命名、目录遍历与文件锁；提供 sendfile 接口 |
| [进程][process-api] | 启动子进程、协程等待退出、获取退出状态；提供受支持平台上的 [fork/waitpid 接口][os-wait-api] |
| [信号][signal-api] | 在协程中等待信号、配置等待超时，向进程发送信号 |
| [工作线程][work-api]与[异步通知][async-api] | 将阻塞任务交给工作线程，调用协程等待结果；将线程通知接入事件循环，安排完成后的清理 |

### 数据与诊断

| 能力 | 可以做什么 |
| --- | --- |
| [动态缓冲区][buffer-api] | 扩容、追加、格式化与导出数据，管理容量和实际长度 |
| [调试日志][log-api]与[Watchdog][watchdog-api] | 分模块日志、协程状态与切换统计、调度死锁诊断、阻塞监测；提供 ASan 构建支持 |

OpenSSL、cURL 和 PostgreSQL 按构建配置启用；系统相关能力以对应平台为准。

## 写起来是什么样

### TCP：一个连接，一个协程

每个连接拥有自己的栈和缓冲区。处理函数按顺序读取、发送、关闭；等待数据时，其他连接继续工作。

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
<summary>如何启动这个连接协程</summary>

包含 `cat_api.h`，初始化运行时和调度器后，将用 `cat_socket_create(NULL, ...)` 创建、已经接受的连接传给它：

```c
if (cat_coroutine_run(NULL, echo_connection, connection) == NULL) {
    cat_socket_close(connection);
}
```

成功启动后由协程关闭连接，启动失败由调用方关闭。对端关闭或读写错误会结束处理，可通过 `cat_get_last_error_message()` 获取错误诊断。完整监听入口见 [HTTP Echo 示例][http-example]。

</details>

### HTTP：读请求、解析、响应

以 GET 请求返回 `Hello, CAT!` 为例。在已接受连接的协程中，解析器初始化后，核心流程如下。请求分多次到达，也沿着同一个循环继续处理。

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
<summary>展开完整处理函数：GET 返回 Hello, CAT!</summary>

包含 `cat_api.h` 和 `cat_http.h`。连接归属与 TCP 例子一致，将入口换为 `http_connection`。示例每个连接处理一次请求后关闭；无效请求或提前断开结束处理。

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

## 构建

需要 CMake 3.18+、C11 编译器，以及默认上下文实现所需的汇编工具链。

```sh
git clone https://github.com/libcat/libcat.git
cd libcat
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLIBCAT_BUILD_TESTS=OFF
cmake --build build --config Release --target cat --parallel
```

构建目标为静态库 `cat`。检查 CMake 输出，确认所需的 OpenSSL、libcurl 和 libpq 已启用。嵌入应用时的头文件、编译定义与链接依赖，参见 [CMake 配置][cmake-link] 和 [Swow 集成][swow-integration]。

## 继续探索

- [公开 API][headers-link] · [测试用例][tests-link] · [平台 CI][ci-link]。
- [HTTP Echo 服务器][http-example] · [DNS 查询][dns-example] · [HTTP 压测入口][http-benchmark]。

HTTP 压测入口提供负载配置；测量结果应注明硬件、构建方式、吞吐、延迟和资源占用。安装 GoogleTest 与 C++20 工具链后，可通过 `./run-tests.sh` 运行测试；服务和平台设置见 [CI 配置][workflow-link]。

欢迎通过 [Issues][issues-link] 提供最小复现，通过 [Pull Requests][pulls-link] 贡献实现、测试和文档。libcat 使用 [Apache License 2.0][license-link]；第三方协议见 [LICENSE.full][full-license-link]。

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
