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
    std::atomic<int> entered;
    std::atomic<int> woken;
};

void futex_waiter_run(void *data)
{
    struct futex_waiter_s *self = (struct futex_waiter_s *) data;

    self->entered = 1;
    self->woken = cat_futex_wait(self->futex, self->expected, TEST_IO_TIMEOUT) ? 1 : 0;
}

void futex_waiter_start(struct futex_waiter_s *self, cat_futex_t *futex, int32_t expected)
{
    self->futex = futex;
    self->expected = expected;
    ASSERT_EQ(uv_thread_create(&self->thread, futex_waiter_run, self), 0);
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
