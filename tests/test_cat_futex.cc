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
  | Author: Yun Dou <dixyes@gmail.com>                                       |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

#include "cat_futex.h"
#include "cat_shm.h"
#include "cat_time.h"

#ifndef CAT_OS_WIN
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef CAT_OS_LINUX
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#endif

namespace {

/*
 * A waiter thread needs no runtime: the futex waits in the kernel and does not
 * touch the scheduler. The test therefore uses a bare thread and keeps the
 * results in atomics that the harness can read.
 */
struct futex_waiter_s {
    uv_thread_t thread;
    cat_futex_t *futex;
    int32_t expected;
    cat_timeout_t timeout;
    std::atomic<int> entered;
    std::atomic<int> woken;
};

void futex_waiter_run(void *data)
{
    struct futex_waiter_s *self = (struct futex_waiter_s *) data;

    self->entered = 1;
    self->woken = cat_futex_wait(self->futex, self->expected, self->timeout) ? 1 : 0;
}

void futex_waiter_start_with_timeout(struct futex_waiter_s *self, cat_futex_t *futex,
    int32_t expected, cat_timeout_t timeout)
{
    self->futex = futex;
    self->expected = expected;
    self->timeout = timeout;
    ASSERT_EQ(uv_thread_create(&self->thread, futex_waiter_run, self), 0);
}

void futex_waiter_start(struct futex_waiter_s *self, cat_futex_t *futex, int32_t expected)
{
    futex_waiter_start_with_timeout(self, futex, expected, TEST_IO_TIMEOUT);
}

/*
 * Wait until the thread reaches the futex. The thread sets the counter before
 * the call, so the delay below covers the step from the counter to the block.
 */
void futex_waiter_await_entered(struct futex_waiter_s *self)
{
    for (int round = 0; round < 5000 && self->entered.load() == 0; round++) {
        uv_sleep(1);
    }
    ASSERT_EQ(self->entered.load(), 1);
    uv_sleep(50);
}

void futex_waiter_join(struct futex_waiter_s *self)
{
    ASSERT_EQ(uv_thread_join(&self->thread), 0);
}

int futex_woken_count(struct futex_waiter_s *first, struct futex_waiter_s *second)
{
    return first->woken.load() + second->woken.load();
}

} // namespace

/* A value change with a wake ends the wait of one thread. */
TEST(cat_futex, a_waiter_wakes_when_the_value_changes)
{
    cat_futex_t word;
    cat_atomic_int32_init(&word, 0);

    struct futex_waiter_s waiter = {};
    futex_waiter_start(&waiter, &word, 0);
    futex_waiter_await_entered(&waiter);

    cat_atomic_int32_store(&word, 1);
    ASSERT_TRUE(cat_futex_wake(&word, 1));

    futex_waiter_join(&waiter);
    ASSERT_EQ(waiter.woken.load(), 1);
    ASSERT_EQ(cat_atomic_int32_load(&word), 1);
}

/* A value that is no longer the expected one fails the wait at the call. */
TEST(cat_futex, a_wait_reports_the_changed_value)
{
    cat_futex_t word;
    cat_atomic_int32_init(&word, 1);

    ASSERT_FALSE(cat_futex_wait(&word, 0, TEST_IO_TIMEOUT));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EAGAIN);
}

/* The value check comes before the timeout, so a changed value is the reason
 * even when the caller passed no timeout. */
TEST(cat_futex, a_wait_with_a_zero_timeout_reports_the_changed_value)
{
    cat_futex_t word;
    cat_atomic_int32_init(&word, 1);

    ASSERT_FALSE(cat_futex_wait(&word, 0, 0));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EAGAIN);
}

/* A wait without a timeout sleeps until a wake arrives. */
TEST(cat_futex, a_wait_without_a_timeout_sleeps_until_a_wake)
{
    cat_futex_t word;
    cat_atomic_int32_init(&word, 0);

    struct futex_waiter_s waiter = {};
    futex_waiter_start_with_timeout(&waiter, &word, 0, CAT_TIMEOUT_FOREVER);
    futex_waiter_await_entered(&waiter);
    ASSERT_EQ(waiter.woken.load(), 0);

    cat_atomic_int32_store(&word, 1);
    ASSERT_TRUE(cat_futex_wake(&word, 1));

    futex_waiter_join(&waiter);
    ASSERT_EQ(waiter.woken.load(), 1);
}

/* A value that stays the same makes the wait run into its timeout. */
TEST(cat_futex, a_wait_times_out)
{
    cat_futex_t word;
    cat_atomic_int32_init(&word, 0);

    cat_msec_t start = cat_time_msec();
    ASSERT_FALSE(cat_futex_wait(&word, 0, 20));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
    ASSERT_GE(cat_time_msec() - start, 10);

    /* a zero timeout checks the value and leaves without a sleep */
    start = cat_time_msec();
    ASSERT_FALSE(cat_futex_wait(&word, 0, 0));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
    ASSERT_LT(cat_time_msec() - start, 1000);
}

/* A wake succeeds when nobody waits, so a caller needs no waiter count. */
TEST(cat_futex, a_wake_without_a_waiter_succeeds)
{
    cat_futex_t word;
    cat_atomic_int32_init(&word, 0);

    ASSERT_TRUE(cat_futex_wake(&word, 1));
    ASSERT_TRUE(cat_futex_wake(&word, -1));
}

/* The count limits the wake, and a negative count releases the others. */
TEST(cat_futex, a_wake_releases_only_the_counted_waiters)
{
    cat_futex_t word;
    cat_atomic_int32_init(&word, 0);

    struct futex_waiter_s first = {};
    struct futex_waiter_s second = {};
    futex_waiter_start(&first, &word, 0);
    futex_waiter_start(&second, &word, 0);
    futex_waiter_await_entered(&first);
    futex_waiter_await_entered(&second);

    cat_atomic_int32_store(&word, 1);
    ASSERT_TRUE(cat_futex_wake(&word, 1));

    for (int round = 0; round < 5000 && futex_woken_count(&first, &second) == 0; round++) {
        uv_sleep(1);
    }
    ASSERT_EQ(futex_woken_count(&first, &second), 1);

    /* the second waiter is still asleep, so it needs the next wake */
    uv_sleep(100);
    ASSERT_EQ(futex_woken_count(&first, &second), 1);

    ASSERT_TRUE(cat_futex_wake(&word, -1));
    futex_waiter_join(&first);
    futex_waiter_join(&second);
    ASSERT_EQ(futex_woken_count(&first, &second), 2);
}

/* A count of zero wakes nobody, so the waiter keeps its sleep. */
TEST(cat_futex, a_wake_of_zero_leaves_the_waiter_asleep)
{
    cat_futex_t word;
    cat_atomic_int32_init(&word, 0);

    struct futex_waiter_s waiter = {};
    futex_waiter_start(&waiter, &word, 0);
    futex_waiter_await_entered(&waiter);

    ASSERT_TRUE(cat_futex_wake(&word, 0));
    uv_sleep(100);
    ASSERT_EQ(waiter.woken.load(), 0);

    cat_atomic_int32_store(&word, 1);
    ASSERT_TRUE(cat_futex_wake(&word, 1));

    futex_waiter_join(&waiter);
    ASSERT_EQ(waiter.woken.load(), 1);
}

/* The add returns the value before it, and an overflow wraps. */
TEST(cat_futex, add_returns_the_old_value_and_wraps)
{
    cat_futex_t word;
    cat_atomic_int32_init(&word, 10);

    ASSERT_EQ(cat_futex_add(&word, 5), 10);
    ASSERT_EQ(cat_atomic_int32_load(&word), 15);
    ASSERT_EQ(cat_futex_add(&word, -20), 15);
    ASSERT_EQ(cat_atomic_int32_load(&word), -5);

    cat_atomic_int32_store(&word, INT32_MAX);
    ASSERT_EQ(cat_futex_add(&word, 1), INT32_MAX);
    ASSERT_EQ(cat_atomic_int32_load(&word), INT32_MIN);

    cat_atomic_int32_store(&word, INT32_MIN);
    ASSERT_EQ(cat_futex_add(&word, -1), INT32_MIN);
    ASSERT_EQ(cat_atomic_int32_load(&word), INT32_MAX);
}

/* The exchange returns the value before it. */
TEST(cat_futex, exchange_returns_the_old_value)
{
    cat_futex_t word;
    cat_atomic_int32_init(&word, 7);

    ASSERT_EQ(cat_futex_exchange(&word, 9), 7);
    ASSERT_EQ(cat_atomic_int32_load(&word), 9);
    ASSERT_EQ(cat_futex_exchange(&word, -1), 9);
    ASSERT_EQ(cat_atomic_int32_load(&word), -1);
}

/* A strong compare-exchange stores only against the expected value, so the
 * second attempt fails and leaves the value that the first one stored. */
TEST(cat_futex, compare_exchange_exchanges_once_and_keeps_the_value)
{
    cat_futex_t word;
    cat_atomic_int32_init(&word, 3);

    ASSERT_TRUE(cat_futex_compare_exchange(&word, 3, 4));
    ASSERT_EQ(cat_atomic_int32_load(&word), 4);

    ASSERT_FALSE(cat_futex_compare_exchange(&word, 3, 5));
    ASSERT_EQ(cat_atomic_int32_load(&word), 4);

    ASSERT_TRUE(cat_futex_compare_exchange(&word, 4, 4));
    ASSERT_EQ(cat_atomic_int32_load(&word), 4);
}

#ifndef CAT_OS_WIN

/*
 * A forked child and the parent share the page, so a wake crosses the process
 * boundary. A private futex would leave the child asleep until its timeout.
 */
TEST(cat_futex, a_wake_reaches_a_forked_child)
{
    cat_shm_t *shm = cat_shm_create_anonymous(sizeof(cat_futex_t) * 2);
    ASSERT_NE(shm, nullptr);
    DEFER(cat_shm_detach(shm));

    cat_futex_t *word = (cat_futex_t *) cat_shm_get_address(shm);
    cat_futex_t *ready = word + 1;
    cat_atomic_int32_init(word, 0);
    cat_atomic_int32_init(ready, 0);

    pid_t pid = fork();
    ASSERT_NE(pid, -1);
    if (pid == 0) {
        cat_atomic_int32_store(ready, 1);
        _exit(cat_futex_wait(word, 0, 1000) ? 0 : 1);
    }

    for (int round = 0; round < 5000 && cat_atomic_int32_load(ready) == 0; round++) {
        uv_sleep(1);
    }
    ASSERT_EQ(cat_atomic_int32_load(ready), 1);
    uv_sleep(50);

    cat_atomic_int32_store(word, 1);
    ASSERT_TRUE(cat_futex_wake(word, 1));

    int status = 0;
    ASSERT_EQ(waitpid(pid, &status, 0), pid);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
}

#endif

#ifdef CAT_OS_LINUX

namespace {

std::atomic<int> futex_signal_hits;

void futex_signal_handler(int signal)
{
    (void) signal;
    futex_signal_hits.fetch_add(1);
}

} // namespace

/*
 * A signal ends the syscall but not the wait. The handler has no SA_RESTART,
 * so the syscall returns EINTR and the loop enters the kernel again, and the
 * wake that follows still reaches the thread.
 */
TEST(cat_futex, a_signal_does_not_end_the_wait)
{
    struct sigaction action = {};
    struct sigaction previous;

    action.sa_handler = futex_signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    ASSERT_EQ(sigaction(SIGUSR1, &action, &previous), 0);
    DEFER(sigaction(SIGUSR1, &previous, nullptr));

    futex_signal_hits = 0;
    cat_futex_t word;
    cat_atomic_int32_init(&word, 0);

    struct futex_waiter_s waiter = {};
    futex_waiter_start(&waiter, &word, 0);
    futex_waiter_await_entered(&waiter);

    for (int round = 0; round < 3; round++) {
        ASSERT_EQ(pthread_kill(waiter.thread, SIGUSR1), 0);
        uv_sleep(5);
    }
    ASSERT_GE(futex_signal_hits.load(), 1);
    ASSERT_EQ(waiter.woken.load(), 0);

    cat_atomic_int32_store(&word, 1);
    ASSERT_TRUE(cat_futex_wake(&word, 1));

    futex_waiter_join(&waiter);
    ASSERT_EQ(waiter.woken.load(), 1);
}

/* An address that no mapping holds fails the wake, and the error is the one
 * that the syscall reports for it. */
TEST(cat_futex, a_wake_reports_an_address_that_no_mapping_holds)
{
    cat_futex_t *word = (cat_futex_t *) mmap(NULL, 4096, PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(word, MAP_FAILED);
    ASSERT_EQ(munmap(word, 4096), 0);

    ASSERT_FALSE(cat_futex_wake(word, 1));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EFAULT);

    ASSERT_FALSE(cat_futex_wake(word, -1));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EFAULT);
}

#endif /* CAT_OS_LINUX */
