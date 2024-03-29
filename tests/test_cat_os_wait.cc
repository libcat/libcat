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

#ifdef CAT_OS_WAIT

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
