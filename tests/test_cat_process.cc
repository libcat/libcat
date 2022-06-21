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
    stdio[1].data.fd = STDOUT_FILENO;
    stdio[2].flags = CAT_PROCESS_STDIO_FLAG_INHERIT_FD;
    stdio[2].data.fd = STDERR_FILENO;
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

#define GREETING_STRING "Hello libcat\n"

static void greeter()
{
    fprintf(stderr, GREETING_STRING);
}

static bool wait_for_greetings(cat_socket_t *pipe)
{
    char buffer[CAT_STRLEN(GREETING_STRING)];
    return (cat_socket_read(pipe, CAT_STRS(buffer))== sizeof(buffer)) &&
            (std::string(CAT_STRS(buffer)) == std::string(GREETING_STRING));
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
