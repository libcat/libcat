/*
  +--------------------------------------------------------------------------+
  | libcat                                                                   |
  +--------------------------------------------------------------------------+
  | Licensed under the Apache License, Version 2.0 (the "License");          |
  | you may not use this file except in compliance with the License.         |
  | You may obtain a copy of the License at                                  |
  | http://www.apache.org/licenses/LICENSE-2.0                               |
  | Unless required by applicable law or agreed to in writing, software      |
  | distributed under the License is distributed on an "AS IS" BASIS,        |
  | WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. |
  | See the License for the specific language governing permissions and      |
  | limitations under the License. See accompanying LICENSE file.            |
  +--------------------------------------------------------------------------+
  | Author: Twosee <twose@qq.com>                                            |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

TEST(cat_process, echo)
{
    SKIP_IF(!file_exists("/bin/echo"));
    cat_process_t *process;
    cat_process_options_t options = { nullptr };
    const char *args[] = { "/bin/echo", "Hello libcat", NULL };
    cat_process_stdio_container_t stdio[3] = { { 0 } };

    options.file = "/bin/echo";
    options.args = args;

    stdio[0].flags = CAT_PROCESS_STDIO_FLAG_IGNORE;
    stdio[1].flags = CAT_PROCESS_STDIO_FLAG_INHERIT_FD;
    stdio[1].data.fd = CAT_STDOUT_FILENO;
    stdio[2].flags = CAT_PROCESS_STDIO_FLAG_INHERIT_FD;
    stdio[2].data.fd = CAT_STDERR_FILENO;
    options.stdio = stdio;
    options.stdio_count = CAT_ARRAY_SIZE(stdio);

    testing::internal::CaptureStdout();
    ([&] {
        ASSERT_NE((process = cat_process_run(&options)), nullptr);
        DEFER(cat_process_close(process));
        ASSERT_TRUE(cat_process_wait_ex(process, TEST_IO_TIMEOUT));
    })();
    std::string output = testing::internal::GetCapturedStdout();
    ASSERT_NE(output.find("Hello libcat"), std::string::npos);
}

/* uv_spawn() removes the handle from the loop queue when it fails, so a failed
 * cat_process_run() must not close that handle: closing it makes the loop remove the node
 * a second time and leaves the queue pointing at freed memory, which the next socket that
 * is created and closed walks into */

TEST(cat_process, spawn_failure_leaves_the_loop_queue_intact)
{
    for (cat_socket_type_t type : {
        CAT_SOCKET_TYPE_UDP,
        CAT_SOCKET_TYPE_UDP4,
        CAT_SOCKET_TYPE_TCP,
        CAT_SOCKET_TYPE_TCP4,
        CAT_SOCKET_TYPE_PIPE,
    }) {
        SCOPED_TRACE(type);

        /* the socket stays alive while the process fails, because its close releases the
         * memory that a corrupted loop handle queue keeps pointing at */
        cat_socket_t *socket = cat_socket_create(nullptr, type);
        ASSERT_NE(socket, nullptr);

        cat_process_options_t options = { nullptr };
        const char *args[] = { "/asdfghjkl", NULL };
        options.file = args[0];
        options.args = args;
        options.stdio_count = 1;
        cat_process_stdio_container_t stdio[1] = { { 0 } };
        stdio[0].flags = CAT_PROCESS_STDIO_FLAG_INHERIT_FD;
        stdio[0].data.fd = CAT_OS_INVALID_FD;
        options.stdio = stdio;

        ASSERT_EQ(cat_process_run(&options), nullptr);
        ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
        ASSERT_TRUE(cat_time_delay(0));
        ASSERT_TRUE(cat_socket_close(socket));

        /* a second round proves the queue stayed usable for the handles after it */
        cat_socket_t *again = cat_socket_create(nullptr, type);
        ASSERT_NE(again, nullptr);
        ASSERT_TRUE(cat_time_delay(0));
        ASSERT_TRUE(cat_socket_close(again));
    }
}

/* A stdio stream that the process owns must not stay registered in the loop when the
 * spawn fails: the stream outlives the freed cat_process_t and the loop walks it during
 * shutdown, and the failed spawn also leaves the process handle in the queue unless the
 * exec failure removes it */

TEST(cat_process, failed_spawn_leaves_no_stdio_stream_behind)
{
    for (auto stdio_flags : {
        (cat_process_stdio_flags_t) (CAT_PROCESS_STDIO_FLAG_CREATE_PIPE | CAT_PROCESS_STDIO_FLAG_READABLE_PIPE),
        (cat_process_stdio_flags_t) (CAT_PROCESS_STDIO_FLAG_CREATE_PIPE | CAT_PROCESS_STDIO_FLAG_WRITABLE_PIPE),
        (cat_process_stdio_flags_t) CAT_PROCESS_STDIO_FLAG_INHERIT_STREAM,
    }) {
        SCOPED_TRACE(stdio_flags);

        cat_socket_t *stream = cat_socket_create(nullptr, CAT_SOCKET_TYPE_PIPE);
        ASSERT_NE(stream, nullptr);

        cat_process_options_t options = { nullptr };
        const char *args[] = { "/asdfghjkl", NULL };
        options.file = args[0];
        options.args = args;
        options.stdio_count = 1;
        cat_process_stdio_container_t stdio[1] = { { 0 } };
        stdio[0].flags = stdio_flags;
        stdio[0].data.stream = stream;
        options.stdio = stdio;

        ASSERT_EQ(cat_process_run(&options), nullptr);
        /* a stream that is not a stream is rejected before spawn, and the spawn itself
         * fails on the missing executable */
        ASSERT_EQ(cat_get_last_error_code(),
            stdio_flags == CAT_PROCESS_STDIO_FLAG_INHERIT_STREAM ? CAT_EINVAL : CAT_ENOENT);
        ASSERT_TRUE(cat_time_delay(0));

        /* the stream is still owned by the caller and must still be usable */
        ASSERT_TRUE(cat_socket_is_available(stream));
        ASSERT_TRUE(cat_socket_close(stream));

        /* a handle created after the failure proves the queue is still intact */
        cat_socket_t *again = cat_socket_create(nullptr, CAT_SOCKET_TYPE_PIPE);
        ASSERT_NE(again, nullptr);
        ASSERT_TRUE(cat_time_delay(0));
        ASSERT_TRUE(cat_socket_close(again));
    }
}

TEST(cat_process, invalid_stream)
{
    cat_process_options_t options = { nullptr };
    const char *args[] = { "/asdfghjkl", NULL };
    options.file = args[0];
    options.args = args;

    cat_socket_t *non_stream;
    ASSERT_NE(non_stream = cat_socket_create(nullptr, CAT_SOCKET_TYPE_UDP), nullptr);
    DEFER(cat_socket_close(non_stream));

    cat_process_stdio_container_t stdio[1];
    stdio[0].flags = CAT_PROCESS_STDIO_FLAG_INHERIT_STREAM;
    stdio[0].data.stream = non_stream;
    options.stdio = stdio;
    options.stdio_count = 1;

    ASSERT_EQ(cat_process_run(&options), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

TEST(cat_process, run_failed)
{
    cat_process_options_t options = { nullptr };
    options.file = "/asdfghjkl";
    options.args = NULL;

    cat_process_stdio_container_t stdio[1];
    stdio[0].flags = CAT_PROCESS_STDIO_FLAG_INHERIT_FD;
    stdio[0].data.fd = CAT_OS_INVALID_FD;
    options.stdio = stdio;
    options.stdio_count = 1;

    ASSERT_EQ(cat_process_run(&options), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

#define TEST_PROCESS_START(test_suite_name, test_name, child) \
TEST(test_suite_name, test_name) { \
    if (cat_env_is_true("TEST_CAT_PROCESS_IS_CHILD", cat_false)) { \
        child(); \
        return; \
    } \
    \
    const cat_const_string_t *exepath; \
    ASSERT_NE(exepath = cat_exepath(), nullptr); \
    \
    cat_socket_t *pipe_stdin; \
    ASSERT_NE(pipe_stdin = cat_socket_create(nullptr, CAT_SOCKET_TYPE_PIPE), nullptr); \
    DEFER2(cat_socket_close(pipe_stdin), 1); \
    \
    cat_socket_t *pipe_stdout; \
    ASSERT_NE(pipe_stdout = cat_socket_create(nullptr, CAT_SOCKET_TYPE_PIPE), nullptr); \
    DEFER2(cat_socket_close(pipe_stdout), 2); \
    \
    cat_socket_t *pipe_stderr; \
    ASSERT_NE(pipe_stderr = cat_socket_create(nullptr, CAT_SOCKET_TYPE_PIPE), nullptr); \
    DEFER2(cat_socket_close(pipe_stderr), 3); \
    \
    cat_process_t *process; \
    cat_process_options_t options = { nullptr }; \
    const char *args[] = { exepath->data, "--gtest_filter=cat_process." #test_name, NULL }; \
    const char *env[] = { "TEST_CAT_PROCESS_IS_CHILD=1", NULL }; \
    cat_process_stdio_container_t stdio[3] = { { 0 } }; \
    \
    options.file = args[0]; \
    options.args = args; \
    options.env = env; \
    \
    stdio[0].flags = CAT_PROCESS_STDIO_FLAG_CREATE_PIPE | CAT_PROCESS_STDIO_FLAG_READABLE_PIPE; \
    stdio[0].data.stream = pipe_stdin; \
    stdio[1].flags = CAT_PROCESS_STDIO_FLAG_CREATE_PIPE | CAT_PROCESS_STDIO_FLAG_WRITABLE_PIPE; \
    stdio[1].data.stream = pipe_stdout; \
    stdio[2].flags = CAT_PROCESS_STDIO_FLAG_CREATE_PIPE | CAT_PROCESS_STDIO_FLAG_WRITABLE_PIPE; \
    stdio[2].data.stream = pipe_stderr; \
    options.stdio = stdio; \
    options.stdio_count = CAT_ARRAY_SIZE(stdio); \
    \
    ASSERT_NE((process = cat_process_run(&options)), nullptr); \
    DEFER2(cat_process_close(process), 4); \

#define TEST_PROCESS_END() \
}

#define GREETING_STRING "Hello libcat"

static void greeter()
{
    fprintf(stderr, GREETING_STRING);
}

static bool wait_for_greetings(cat_socket_t *pipe)
{
    bool ret = false;
    ([&] {
        char buffer[CAT_STRLEN(GREETING_STRING)];
        ssize_t nread = cat_socket_read(pipe, CAT_STRS(buffer));
        ASSERT_EQ(nread, sizeof(buffer));
        ASSERT_EQ(std::string(buffer, nread), std::string(GREETING_STRING));
        ret = true;
    })();
    return ret;
}

TEST_PROCESS_START(cat_process, greeter, greeter) {
    ASSERT_TRUE(wait_for_greetings(pipe_stderr));
    ASSERT_GT(cat_process_get_pid(process), 0);
    ASSERT_TRUE(cat_process_wait_ex(process, TEST_IO_TIMEOUT));
    ASSERT_TRUE(cat_process_has_exited(process));
    ASSERT_EQ(cat_process_get_exit_status(process), 0);
    ASSERT_EQ(cat_process_get_term_signal(process), 0);
} TEST_PROCESS_END()

TEST_PROCESS_START(cat_process, multi_wait, greeter) {
    ASSERT_TRUE(wait_for_greetings(pipe_stderr));
    wait_group wg;
    for (int n = 10; n--;) {
        co([&] {
            wg++;
            DEFER(wg--);
            ASSERT_TRUE(cat_process_wait_ex(process, TEST_IO_TIMEOUT));
            ASSERT_TRUE(cat_process_has_exited(process));
        });
    }
} TEST_PROCESS_END()

TEST_PROCESS_START(cat_process, kill, [] {
    greeter();
    cat_sys_sleep(TEST_IO_TIMEOUT * 10);
}) {
    ASSERT_TRUE(wait_for_greetings(pipe_stderr));
#ifdef CAT_OS_UNIX_LIKE
    {
        cat_pid_t pid = process->u.process.pid;
        process->u.process.pid = (cat_pid_t) INT_MAX;
        ASSERT_FALSE(cat_process_kill(process, CAT_SIGTERM));
        ASSERT_EQ(cat_get_last_error_code(), CAT_ESRCH);
        process->u.process.pid = pid;
    }
#endif
    ASSERT_FALSE(cat_process_has_exited(process));
    ASSERT_TRUE(cat_process_kill(process, CAT_SIGTERM));
    ASSERT_TRUE(cat_process_wait_ex(process, TEST_IO_TIMEOUT));
    ASSERT_FALSE(cat_process_kill(process, CAT_SIGTERM));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ESRCH);
#ifndef CAT_OS_WIN
    ASSERT_EQ(cat_process_get_exit_status(process), 0);
#else
    /* it seems that exit status will be 1
     * if the process was terminated on Windows */
    ASSERT_EQ(cat_process_get_exit_status(process), 1);
#endif
    ASSERT_EQ(cat_process_get_term_signal(process), CAT_SIGTERM);
} TEST_PROCESS_END();

/* The Windows process group flag is accepted on every platform. A Unix-like
 * platform ignores it, and the child still runs. */
TEST(cat_process, new_process_group_flag_spawns_the_child)
{
    if (cat_env_is_true("TEST_CAT_PROCESS_IS_CHILD", cat_false)) {
        exit(7);
        return;
    }

    const cat_const_string_t *exepath;
    ASSERT_NE(exepath = cat_exepath(), nullptr);

    const char *args[] = {
        exepath->data,
        "--gtest_filter=cat_process.new_process_group_flag_spawns_the_child",
        NULL,
    };
    const char *env[] = { "TEST_CAT_PROCESS_IS_CHILD=1", NULL };
    cat_process_stdio_container_t stdio[3] = { { 0 } };
    stdio[0].flags = CAT_PROCESS_STDIO_FLAG_IGNORE;
    stdio[1].flags = CAT_PROCESS_STDIO_FLAG_IGNORE;
    stdio[2].flags = CAT_PROCESS_STDIO_FLAG_IGNORE;

    cat_process_options_t options = { nullptr };
    options.file = args[0];
    options.args = args;
    options.env = env;
    options.stdio = stdio;
    options.stdio_count = CAT_ARRAY_SIZE(stdio);
    options.flags = CAT_PROCESS_FLAG_WINDOWS_NEW_PROCESS_GROUP;

    cat_process_t *process;
    ASSERT_NE(process = cat_process_run(&options), nullptr);
    DEFER(cat_process_close(process));

    ASSERT_TRUE(cat_process_wait_ex(process, TEST_IO_TIMEOUT));
    ASSERT_TRUE(cat_process_has_exited(process));
    ASSERT_EQ(cat_process_get_exit_status(process), 7);
}

/* A normal exit reports the exit code and no signal. */
TEST_PROCESS_START(cat_process, status_of_a_normal_exit, [] {
    exit(7);
}) {
    ASSERT_TRUE(cat_process_wait_ex(process, TEST_IO_TIMEOUT));
    cat_process_status_t status = cat_process_get_status(process);
    ASSERT_TRUE(status.exited);
    ASSERT_FALSE(status.signaled);
    ASSERT_EQ(status.exit_code, 7);
    ASSERT_EQ(status.term_signal, -1);
} TEST_PROCESS_END();

/* A killed process reports the signal and no exit code. */
TEST_PROCESS_START(cat_process, status_of_a_killed_process, [] {
    greeter();
    cat_sys_sleep(TEST_IO_TIMEOUT * 10);
}) {
    ASSERT_TRUE(wait_for_greetings(pipe_stderr));
    ASSERT_TRUE(cat_process_kill(process, CAT_SIGKILL));
    ASSERT_TRUE(cat_process_wait_ex(process, TEST_IO_TIMEOUT));
    cat_process_status_t status = cat_process_get_status(process);
    ASSERT_TRUE(status.exited);
    ASSERT_TRUE(status.signaled);
    ASSERT_EQ(status.exit_code, -1);
    ASSERT_EQ(status.term_signal, CAT_SIGKILL);
} TEST_PROCESS_END();

/* A process that still runs reports neither a code nor a signal. */
TEST_PROCESS_START(cat_process, status_of_a_running_process, [] {
    greeter();
    cat_sys_sleep(TEST_IO_TIMEOUT * 10);
}) {
    ASSERT_TRUE(wait_for_greetings(pipe_stderr));
    cat_process_status_t status = cat_process_get_status(process);
    ASSERT_FALSE(status.exited);
    ASSERT_FALSE(status.signaled);
    ASSERT_EQ(status.exit_code, -1);
    ASSERT_EQ(status.term_signal, -1);
    /* the kill only cleans up, and SIGKILL is the one signal both platforms map */
    ASSERT_TRUE(cat_process_kill(process, CAT_SIGKILL));
    ASSERT_TRUE(cat_process_wait_ex(process, TEST_IO_TIMEOUT));
} TEST_PROCESS_END();

TEST_PROCESS_START(cat_process, cancel_wait, [] {
    do {
        char buffer[CAT_STRLEN(GREETING_STRING) + 1];
        char *line = fgets(CAT_STRS(buffer), stdin);
        ASSERT_EQ(line, buffer);
        ASSERT_EQ(strlen(line), CAT_STRLEN(GREETING_STRING));
        ASSERT_STREQ(line, GREETING_STRING);
    } while (0);
    greeter();
}) {
    cat_coroutine_t *coroutine = co([&] {
        ASSERT_FALSE(cat_process_wait_ex(process, TEST_IO_TIMEOUT));
        ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
    });
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
    ASSERT_TRUE(cat_socket_send(pipe_stdin, CAT_STRS(GREETING_STRING)));
    ASSERT_TRUE(wait_for_greetings(pipe_stderr));
    ASSERT_TRUE(cat_process_wait(process));
    ASSERT_TRUE(cat_process_has_exited(process));
} TEST_PROCESS_END();

TEST_PROCESS_START(cat_process, exit_1, [] {
    exit(1);
}) {
    ASSERT_TRUE(cat_process_wait_ex(process, TEST_IO_TIMEOUT));
    ASSERT_TRUE(cat_process_has_exited(process));
    ASSERT_EQ(cat_process_get_exit_status(process), 1);
    ASSERT_EQ(cat_process_get_term_signal(process), 0);
} TEST_PROCESS_END();
