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
  | Author: Twosee <twosee@php.net>                                          |
  +--------------------------------------------------------------------------+
 */

#include "test.h"
#include <algorithm>

#ifdef CAT_OS_UNIX_LIKE

TEST(cat_os_wait, fork_restores_child_event_loop)
{
#ifdef CAT_COROUTINE_USE_THREAD_CONTEXT
    SKIP_IF_(1, "Unable to fork with thread-context");
#endif
    SKIP_IF_TSAN("ThreadSanitizer does not support fork with threads");
    cat_pid_t pid = cat_os_fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        // The public fork wrapper must leave coroutine timers usable in the child.
        cat_socket_close_all();
        // Inherited fixture destructors would remove the parent's certificate files.
        _exit(cat_time_delay(0) ? 0 : 1);
    }
    bool reaped = false;
    DEFER(if (!reaped) { kill(pid, SIGTERM); waitpid(pid, nullptr, 0); });
    int status = 0;
    cat_pid_t result = cat_os_waitpid_ex(pid, &status, 0, TEST_IO_TIMEOUT);
    reaped = result == pid;
    ASSERT_EQ(result, pid);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
}

#if defined(CAT_OS_LINUX) && GTEST_HAS_DEATH_TEST && !defined(CAT_COROUTINE_USE_THREAD_CONTEXT)
TEST(cat_os_wait, fork_process_limit_failure_and_recovery)
{
    SKIP_IF_(getuid() == 0, "RLIMIT_NPROC does not restrict root");
    ASSERT_EXIT({
        struct rlimit original;
        if (getrlimit(RLIMIT_NPROC, &original) != 0) {
            std::exit(1);
        }
        struct rlimit restricted = original;
        restricted.rlim_cur = 0;
        if (setrlimit(RLIMIT_NPROC, &restricted) != 0) {
            std::exit(2);
        }
        cat_pid_t pid = cat_os_fork();
        if (pid == 0) {
            _exit(3);
        }
        cat_errno_t error = cat_get_last_error_code();
        // Restore only this test child's limit before testing a successful fork.
        if (setrlimit(RLIMIT_NPROC, &original) != 0) {
            std::exit(4);
        }
        if (pid != -1 || error != CAT_EAGAIN) {
            if (pid > 0) {
                waitpid(pid, nullptr, 0);
            }
            std::exit(5);
        }
        pid = cat_os_fork();
        if (pid == 0) {
            _exit(0);
        }
        if (pid < 0) {
            std::exit(6);
        }
        int status = 0;
        if (cat_os_waitpid_ex(pid, &status, 0, TEST_IO_TIMEOUT) != pid ||
            !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            std::exit(7);
        }
        std::exit(0);
    }, testing::ExitedWithCode(0), "");
}
#endif

static pid_t new_child_process(int exit_status = 0)
{
    pid_t pid = fork();
    if (pid == 0) {
        usleep(1000);
        _exit(exit_status);
    } else if (pid < 0) {
        CAT_NEVER_HERE("Should never happend");
    }
    return pid;
}

TEST(cat_os_wait, timeout)
{
    int status;
    pid_t w_pid = cat_os_wait_ex(&status, 1);
    ASSERT_EQ(w_pid, -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
}

/* A child that is still running is not a result for WNOHANG: the call returns
 * zero at once and leaves the child waitable. */
TEST(cat_os_wait, wnohang_returns_zero_while_the_child_runs)
{
    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        usleep(200 * 1000);
        _exit(0);
    }

    bool reaped = false;
    DEFER(if (!reaped) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
    });

    int status = 0;
    pid_t ret = cat_os_waitpid_ex(pid, &status, WNOHANG, TEST_IO_TIMEOUT);
    ASSERT_EQ(ret, 0);

    /* the child is still there, so a blocking call waits for it */
    ret = cat_os_waitpid_ex(pid, &status, 0, TEST_IO_TIMEOUT);
    reaped = ret == pid;
    ASSERT_EQ(ret, pid);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
}

/* WNOHANG reports a child that already exited, with its status. */
TEST(cat_os_wait, wnohang_returns_an_exited_child)
{
    pid_t pid = new_child_process(7);
    int status = 0;

    bool reaped = false;
    DEFER(if (!reaped) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
    });

    /* A sanitizer can slow the child down, so poll instead of guessing a delay.
     * A call that does not block returns zero until the child exits. */
    pid_t ret = 0;
    for (cat_timeout_t waited = 0; waited < TEST_IO_TIMEOUT && ret == 0; waited += 10) {
        ret = cat_os_waitpid_ex(pid, &status, WNOHANG, 0);
        if (ret == 0) {
            ASSERT_TRUE(cat_time_delay(10));
        }
    }
    reaped = ret == pid;
    ASSERT_EQ(ret, pid);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 7);
}

/* A timed out wait leaves the child waitable, and the second wait gets it. */
TEST(cat_os_wait, a_timed_out_wait_leaves_the_child_waitable)
{
    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        usleep(100 * 1000);
        _exit(3);
    }

    bool reaped = false;
    DEFER(if (!reaped) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
    });

    int status = 0;
    ASSERT_EQ(cat_os_waitpid_ex(pid, &status, 0, 10), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);

    ASSERT_EQ(cat_os_waitpid_ex(pid, &status, 0, TEST_IO_TIMEOUT), pid);
    reaped = true;
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 3);
}

/* wait4 takes the same options, and WNOHANG means the same there. */
TEST(cat_os_wait, wnohang_returns_zero_for_wait4)
{
#ifdef CAT_OS_WAIT_HAVE_RUSAGE
    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        usleep(200 * 1000);
        _exit(0);
    }

    bool reaped = false;
    DEFER(if (!reaped) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
    });

    int status = 0;
    struct rusage rusage;
    memset(&rusage, 0, sizeof(rusage));
    ASSERT_EQ(cat_os_wait4_ex(pid, &status, WNOHANG, &rusage, TEST_IO_TIMEOUT), 0);
    ASSERT_EQ(cat_os_wait4_ex(pid, &status, 0, &rusage, TEST_IO_TIMEOUT), pid);
    reaped = true;
    ASSERT_TRUE(WIFEXITED(status));
#else
    SKIP_IF_(1, "rusage is not available");
#endif
}

TEST(cat_os_wait, reject_non_child_pids)
{
    for (pid_t pid : {getpid(), getppid()}) {
        SCOPED_TRACE(pid);
        ASSERT_GT(pid, 0);
        int status = 0;
        errno = 0;
        ASSERT_EQ(waitpid(pid, &status, WNOHANG), -1);
        ASSERT_EQ(errno, ECHILD);
        ASSERT_EQ(cat_os_waitpid_ex(pid, &status, 0, 0), -1);
        ASSERT_EQ(cat_get_last_error_code(), CAT_ECHILD);
    }
}

TEST(cat_os_wait, wait)
{
#ifdef CAT_OS_WAIT_HAVE_RUSAGE
    for (auto get_rusage : std::array<bool, 2>{ false, true }) {
#endif
    for (auto exit_status : std::array<int, 2>{ 0, 1 })  {
        pid_t pid = new_child_process(exit_status);

        int status;
        pid_t w_pid;
#ifdef CAT_OS_WAIT_HAVE_RUSAGE
        struct rusage rusage;
        memset(&rusage, 0, sizeof(rusage));

        if (get_rusage) {
            w_pid = cat_os_wait3(&status, 0, &rusage);
        } else
#endif
        {
            w_pid = cat_os_wait(&status);
        }

        ASSERT_EQ(w_pid, pid);
        ASSERT_EQ(WIFEXITED(status), 1);
        ASSERT_EQ(WEXITSTATUS(status), exit_status);
        ASSERT_EQ(WTERMSIG(status), 0);
#ifdef CAT_OS_WAIT_HAVE_RUSAGE
        if (get_rusage) {
            ASSERT_GT(rusage.ru_utime.tv_sec + rusage.ru_utime.tv_usec + rusage.ru_stime.tv_sec + rusage.ru_stime.tv_usec, 0);
        }
#endif
    }
#ifdef CAT_OS_WAIT_HAVE_RUSAGE
    }
#endif
}

TEST(cat_os_wait, waitpid)
{
#ifdef CAT_OS_WAIT_HAVE_RUSAGE
    for (auto get_rusage : std::array<bool, 2>{ false, true }) {
#endif
    for (auto exit_status : std::array<int, 2>{ 0, 1 })  {
        pid_t pid = new_child_process(exit_status);

        int status;
        pid_t w_pid;
#ifdef CAT_OS_WAIT_HAVE_RUSAGE
        struct rusage rusage;
        memset(&rusage, 0, sizeof(rusage));

        if (get_rusage) {
            w_pid = cat_os_wait4(pid, &status, 0, &rusage);
        } else
#endif
        {
            w_pid = cat_os_waitpid(pid, &status, 0);
        }

        ASSERT_EQ(w_pid, pid);
        ASSERT_EQ(WIFEXITED(status), 1);
        ASSERT_EQ(WEXITSTATUS(status), exit_status);
        ASSERT_EQ(WTERMSIG(status), 0);
#ifdef CAT_OS_WAIT_HAVE_RUSAGE
        if (get_rusage) {
            ASSERT_GT(rusage.ru_utime.tv_sec + rusage.ru_utime.tv_usec + rusage.ru_stime.tv_sec + rusage.ru_stime.tv_usec, 0);
        }
#endif
    }
    /* error cases */
    {
        int status;
        pid_t w_pid;
        w_pid = cat_os_waitpid(0, &status, 0);

        ASSERT_EQ(w_pid, -1);
        ASSERT_EQ(cat_get_last_error_code(), CAT_ENOTSUP);
    }
#ifdef CAT_OS_WAIT_HAVE_RUSAGE
    }
#endif
}

TEST(cat_os_wait, concurrent_waiters_match_child_status)
{
    std::array<std::array<int, 2>, 6> gates;
    for (auto &gate : gates) {
        gate = {-1, -1};
    }
    DEFER(for (auto &gate : gates) {
        for (int fd : gate) { if (fd >= 0) { close(fd); } }
    });
    for (auto &gate : gates) {
        ASSERT_EQ(pipe(gate.data()), 0);
    }
    std::array<pid_t, 6> pids = {};
    std::array<bool, 6> released = {}, reaped = {};
    wait_group wg;
    // Release every child before joining waiters, including assertion-return paths.
    DEFER(
        for (size_t i = 0; i < pids.size(); i++) {
            if (pids[i] > 0 && !released[i]) { write(gates[i][1], "x", 1); }
        }
        wg();
        for (size_t i = 0; i < pids.size(); i++) {
            if (pids[i] > 0 && !reaped[i]) { waitpid(pids[i], nullptr, 0); }
        }
    );
    for (size_t i = 0; i < pids.size(); i++) {
        pid_t pid = fork();
        ASSERT_GE(pid, 0);
        if (pid == 0) {
            for (size_t j = 0; j < gates.size(); j++) {
                close(gates[j][1]);
                if (j != i) { close(gates[j][0]); }
            }
            char permit;
            const ssize_t n = read(gates[i][0], &permit, 1);
            _exit(n == 1 ? static_cast<int>(17 + i) : 127);
        }
        pids[i] = pid;
    }
    std::array<size_t, 6> order = {0, 1, 2, 3, 4, 5};
    std::sort(order.begin(), order.end(), [&](size_t first, size_t second) {
        return pids[first] > pids[second];
    });
    auto start_waiter = [&](size_t i, bool with_usage) {
        co([&, i, with_usage] {
            wg++;
            DEFER(wg--);
            int status = 0;
            pid_t pid;
#ifdef CAT_OS_WAIT_HAVE_RUSAGE
            struct rusage usage = {};
            usage.ru_utime.tv_sec = usage.ru_stime.tv_sec = -1;
            if (with_usage) {
                pid = cat_os_wait4_ex(pids[i], &status, 0, &usage, TEST_IO_TIMEOUT);
            } else
#else
            (void) with_usage;
#endif
            {
                pid = cat_os_waitpid_ex(pids[i], &status, 0, TEST_IO_TIMEOUT);
            }
            reaped[i] = pid == pids[i];
            ASSERT_EQ(pid, pids[i]);
            ASSERT_TRUE(WIFEXITED(status));
            ASSERT_EQ(WEXITSTATUS(status), 17 + i);
#ifdef CAT_OS_WAIT_HAVE_RUSAGE
            if (with_usage) {
                ASSERT_GE(usage.ru_utime.tv_sec, 0);
                ASSERT_GE(usage.ru_stime.tv_sec, 0);
            }
#endif
        });
    };
    const size_t pending = order.size() / 2;
    for (size_t n = 0; n < pending; n++) {
        start_waiter(order[n], false);
        ASSERT_FALSE(reaped[order[n]]);
    }
    // Pending sibling waiters keep SIGCHLD active while these exited children are cached.
    for (size_t n = pending; n < order.size(); n++) {
        const size_t i = order[n];
        ASSERT_EQ(write(gates[i][1], "x", 1), 1);
        released[i] = true;
        const cat_msec_t start = cat_time_msec();
        while (cat_kill(pids[i], 0)) {
            ASSERT_LT(cat_time_msec() - start, TEST_IO_TIMEOUT);
            ASSERT_EQ(cat_time_msleep(1), 0);
        }
        ASSERT_EQ(cat_get_last_error_code(), CAT_ESRCH);
    }
    for (size_t n = pending; n < order.size(); n++) {
        start_waiter(order[n], true);
        ASSERT_TRUE(reaped[order[n]]);
    }
    for (size_t i = 0; i < pids.size(); i++) {
        if (released[i]) { continue; }
        ASSERT_EQ(write(gates[i][1], "x", 1), 1);
        released[i] = true;
    }
    ASSERT_TRUE(wg());
    for (bool child_reaped : reaped) {
        ASSERT_TRUE(child_reaped);
    }
}

TEST(cat_os_wait, shutdown_discards_unused_child_status)
{
    using children_t = std::array<pid_t, 3>;
    ASSERT_EXIT({
        int gate[2];
        if (pipe(gate) != 0) { std::exit(1); }
        const pid_t held = fork();
        if (held < 0) { std::exit(1); }
        if (held == 0) {
            close(gate[1]);
            char permit;
            _exit(read(gate[0], &permit, 1) == 1 ? 0 : 1);
        }
        close(gate[0]);
        bool completed = false;
        bool success = false;
        wait_group wg;
        co([&] {
            wg++;
            DEFER(wg--);
            int status = 0;
            success = cat_os_waitpid_ex(held, &status, 0, TEST_IO_TIMEOUT) == held &&
                WIFEXITED(status) && WEXITSTATUS(status) == 0;
            completed = true;
        });
        if (completed) { std::exit(1); }
        children_t children;
        for (size_t i = 0; i < children.size(); i++) {
            children[i] = new_child_process(static_cast<int>(17 + i));
            const cat_msec_t start = cat_time_msec();
            while (cat_kill(children[i], 0)) {
                if (cat_time_msec() - start >= TEST_IO_TIMEOUT || cat_time_msleep(1) != 0) { std::exit(1); }
            }
            if (cat_get_last_error_code() != CAT_ESRCH) { std::exit(1); }
        }
        // Consume one cached result, leaving the other records for aggregate shutdown.
        int status = 0;
        if (cat_os_waitpid_ex(children[0], &status, 0, 0) != children[0] ||
            !WIFEXITED(status) || WEXITSTATUS(status) != 17) { std::exit(1); }
        if (write(gate[1], "x", 1) != 1) { std::exit(1); }
        close(gate[1]);
        if (!wg(TEST_IO_TIMEOUT) || !completed || !success) { std::exit(1); }
        if (!cat_stop() || !cat_shutdown_all() || !cat_init_all() || !cat_run(CAT_RUN_EASY)) { std::exit(1); }
        for (size_t i = 1; i < children.size(); i++) {
            if (cat_os_waitpid_ex(children[i], &status, 0, 0) != -1 ||
                cat_get_last_error_code() != CAT_ECHILD) { std::exit(1); }
        }
        if (!cat_time_delay(0) || !cat_stop() || !cat_shutdown_all()) { std::exit(1); }
        std::exit(0);
    }, testing::ExitedWithCode(0), "");
}

TEST(cat_os_wait, wait_after_sigchld)
{
#ifdef CAT_OS_WAIT_HAVE_RUSAGE
    for (auto get_rusage : std::array<bool, 2>{ false, true }) {
#endif
    for (auto type : std::array<int, 2>{ 0, 1 })  {
        pid_t pid = new_child_process();

        while (cat_kill(pid, 0) == 0) {
            (void) cat_time_msleep(1);
        }
        (void) cat_time_msleep(100);

        int status;
        pid_t w_pid;
#ifdef CAT_OS_WAIT_HAVE_RUSAGE
        struct rusage rusage;
        memset(&rusage, 0, sizeof(rusage));
#endif
        if (type == 0) {
#ifdef CAT_OS_WAIT_HAVE_RUSAGE
            if (get_rusage) {
                w_pid = cat_os_wait3(&status, 0, &rusage);
            } else
#endif
            {
                w_pid = cat_os_wait(&status);
            }
        } else {
#ifdef CAT_OS_WAIT_HAVE_RUSAGE
            if (get_rusage) {
                w_pid = cat_os_wait4(pid, &status, 0, &rusage);
            } else
#endif
            {
                w_pid = cat_os_waitpid(pid, &status, 0);
            }
        }
        ASSERT_EQ(w_pid, pid);
        ASSERT_EQ(WIFEXITED(status), 1);
        ASSERT_EQ(WEXITSTATUS(status), 0);
        ASSERT_EQ(WTERMSIG(status), 0);
#ifdef CAT_OS_WAIT_HAVE_RUSAGE
        if (get_rusage) {
            ASSERT_GT(rusage.ru_utime.tv_sec + rusage.ru_utime.tv_usec + rusage.ru_stime.tv_sec + rusage.ru_stime.tv_usec, 0);
        }
#endif
    }
#ifdef CAT_OS_WAIT_HAVE_RUSAGE
    }
#endif
}

TEST(cat_os_wait, kill_then_wait)
{
    pid_t pid = fork();
    if (pid == 0) {
        usleep(1000 * 1000); // 1s
        _exit(0);
    } else if (pid < 0) {
        ASSERT_TRUE(false);
    }
    ASSERT_TRUE(cat_kill(pid, CAT_SIGTERM));
    int status;
    pid_t w_pid = cat_os_wait(&status);
    ASSERT_EQ(w_pid, pid);
    ASSERT_EQ(WIFEXITED(status), 0);
    ASSERT_EQ(WEXITSTATUS(status), 0);
    ASSERT_EQ(WIFSIGNALED(status), 1);
    ASSERT_EQ(WTERMSIG(status), CAT_SIGTERM);
}

TEST(cat_os_wait, waitpid_after_wait)
{
    pid_t pid = new_child_process();
    int status;
    pid_t w_pid = cat_os_wait(&status);
    ASSERT_EQ(w_pid, pid);
    ASSERT_EQ(WIFEXITED(status), 1);
    ASSERT_EQ(WEXITSTATUS(status), 0);
    w_pid = cat_os_waitpid(pid, &status, 0);
    ASSERT_EQ(w_pid, -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ECHILD);
}

/* The status decoders agree with the wait macros. */
TEST(cat_os_wait, status_decoders_match_the_wait_macros)
{
    pid_t pid = new_child_process(3);
    int status = 0;

    ASSERT_EQ(cat_os_waitpid_ex(pid, &status, 0, TEST_IO_TIMEOUT), pid);
    ASSERT_EQ(cat_os_wait_status_exited(status), WIFEXITED(status) != 0 ? cat_true : cat_false);
    ASSERT_EQ(cat_os_wait_status_exit_code(status), WEXITSTATUS(status));
    ASSERT_EQ(cat_os_wait_status_signaled(status), WIFSIGNALED(status) != 0 ? cat_true : cat_false);
    ASSERT_EQ(cat_os_wait_status_term_signal(status), -1);
    ASSERT_EQ(cat_os_wait_status_exit_code(status), 3);
}

#endif

#ifdef CAT_OS_WIN

#include "cat_process.h"

/* A child process that the tests below wait for. */
TEST(cat_os_wait, windows_child)
{
    if (!cat_env_is_true("TEST_CAT_OS_WAIT_CHILD", cat_false)) {
        GTEST_SKIP() << "Only runs as a child process";
    }

    if (cat_env_is_true("TEST_CAT_OS_WAIT_CHILD_SLEEP", cat_false)) {
        cat_sys_sleep(60 * 1000);
    }

    exit(7);
}

static cat_process_t *spawn_windows_wait_child(bool sleep)
{
    const cat_const_string_t *exepath = cat_exepath();
    if (exepath == nullptr) {
        return nullptr;
    }

    const char *args[] = {
        exepath->data,
        "--gtest_filter=cat_os_wait.windows_child",
        NULL,
    };
    const char *env_awake[] = {
        "TEST_CAT_OS_WAIT_CHILD=1",
        NULL,
    };
    const char *env_sleep[] = {
        "TEST_CAT_OS_WAIT_CHILD=1",
        "TEST_CAT_OS_WAIT_CHILD_SLEEP=1",
        NULL,
    };
    cat_process_stdio_container_t stdio[3] = { { 0 } };
    stdio[0].flags = CAT_PROCESS_STDIO_FLAG_IGNORE;
    stdio[1].flags = CAT_PROCESS_STDIO_FLAG_IGNORE;
    stdio[2].flags = CAT_PROCESS_STDIO_FLAG_IGNORE;

    cat_process_options_t options = { nullptr };
    options.file = args[0];
    options.args = args;
    options.env = sleep ? env_sleep : env_awake;
    options.stdio = stdio;
    options.stdio_count = CAT_ARRAY_SIZE(stdio);

    return cat_process_run(&options);
}

/* Windows waits for a known pid and reports the exit code as the status. */
TEST(cat_os_wait, windows_waitpid_reports_the_exit_code)
{
    cat_process_t *process;
    ASSERT_NE(process = spawn_windows_wait_child(false), nullptr);
    DEFER(cat_process_close(process));

    cat_pid_t pid = cat_process_get_pid(process);
    int status = 0;
    ASSERT_EQ(cat_os_waitpid(pid, &status, 0), pid);
    ASSERT_TRUE(cat_os_wait_status_exited(status));
    ASSERT_FALSE(cat_os_wait_status_signaled(status));
    ASSERT_EQ(cat_os_wait_status_exit_code(status), 7);
    ASSERT_EQ(cat_os_wait_status_term_signal(status), -1);

    /* let the uv process handle take its own exit before the close */
    ASSERT_TRUE(cat_process_wait_ex(process, TEST_IO_TIMEOUT));
}

/* WNOHANG returns zero while the process still runs. */
TEST(cat_os_wait, windows_wnohang_returns_zero_while_the_process_runs)
{
    cat_process_t *process;
    ASSERT_NE(process = spawn_windows_wait_child(true), nullptr);
    DEFER(cat_process_close(process));

    cat_pid_t pid = cat_process_get_pid(process);
    int status = 0;
    ASSERT_EQ(cat_os_waitpid_ex(pid, &status, WNOHANG, 0), 0);

    ASSERT_TRUE(cat_process_kill(process, CAT_SIGKILL));
    ASSERT_EQ(cat_os_waitpid_ex(pid, &status, 0, TEST_IO_TIMEOUT), pid);
    ASSERT_TRUE(cat_os_wait_status_exited(status));
    ASSERT_TRUE(cat_process_wait_ex(process, TEST_IO_TIMEOUT));
}

/* A timed out wait leaves the process waitable. */
TEST(cat_os_wait, windows_waitpid_times_out)
{
    cat_process_t *process;
    ASSERT_NE(process = spawn_windows_wait_child(true), nullptr);
    DEFER(cat_process_close(process));

    cat_pid_t pid = cat_process_get_pid(process);
    int status = 0;
    ASSERT_EQ(cat_os_waitpid_ex(pid, &status, 0, 10), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);

    ASSERT_TRUE(cat_process_kill(process, CAT_SIGKILL));
    ASSERT_EQ(cat_os_waitpid_ex(pid, &status, 0, TEST_IO_TIMEOUT), pid);
    ASSERT_TRUE(cat_process_wait_ex(process, TEST_IO_TIMEOUT));
}

/* Windows has no child process relation, so waiting for any child is refused. */
TEST(cat_os_wait, windows_wait_for_any_child_is_refused)
{
    int status = 0;
    ASSERT_EQ(cat_os_wait(&status), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOTSUP);

    ASSERT_EQ(cat_os_wait_ex(&status, 10), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOTSUP);
}

/* A pid that belongs to no process is reported, not guessed. */
TEST(cat_os_wait, windows_waitpid_rejects_a_missing_process)
{
    int status = 0;
    ASSERT_EQ(cat_os_waitpid_ex((cat_pid_t) INT_MAX, &status, 0, TEST_IO_TIMEOUT), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ESRCH);
}

#endif /* CAT_OS_WIN */
