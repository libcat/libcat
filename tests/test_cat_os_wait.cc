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

#ifdef CAT_OS_WAIT

TEST(cat_os_wait, fork_restores_child_event_loop)
{
#ifdef CAT_COROUTINE_USE_THREAD_CONTEXT
    SKIP_IF_(1, "Unable to fork with thread-context");
#endif
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

#endif
