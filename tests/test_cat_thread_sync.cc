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

#include <thread>

#if defined(CAT_THREAD_SAFE)

#include "cat_time.h"
#include "cat_thread_sync.h"

/*
 * A thread with a runtime of its own, for a case that has to cross a thread
 * boundary but is not about the thread API.
 */
struct plain_thread_s {
    uv_thread_t id;
    void (*run)(void *data);
    void *data;
};

static void plain_thread_entry(void *data)
{
    struct plain_thread_s *self = (struct plain_thread_s *) data;

    if (!cat_runtime_init_all() || !cat_run(CAT_RUN_EASY)) {
        return;
    }

    self->run(self->data);

    (void) cat_stop();
    (void) cat_runtime_shutdown_all();
    (void) cat_runtime_close_all();
}

static bool plain_thread_start(struct plain_thread_s *thread, void (*run)(void *), void *data)
{
    thread->run = run;
    thread->data = data;

    return uv_thread_create(&thread->id, plain_thread_entry, thread) == 0;
}

static void plain_thread_join(struct plain_thread_s *thread)
{
    (void) uv_thread_join(&thread->id);
}

static void mutex_case_init(cat_mutex_t *mutex)
{
    ASSERT_TRUE(cat_mutex_init(mutex));
}

static void mutex_case_close(cat_mutex_t *mutex)
{
    ASSERT_TRUE(cat_mutex_destroy(mutex));
}

static void sem_case_init(cat_sem_t *sem, unsigned int value)
{
    ASSERT_TRUE(cat_sem_init(sem, value));
}

static void sem_case_close(cat_sem_t *sem)
{
    cat_sem_destroy(sem);
}


/* Park a coroutine on a bare wait queue, so the queue itself can be tested
 * without a mutex or a semaphore in the way. */
struct queue_s {
    cat_thread_wait_t wait;
    int woken;
};

/* The uncontended path takes the lock and gives it back without suspending. */
TEST(cat_thread_sync, a_mutex_locks_and_unlocks_without_a_waiter)
{
    cat_mutex_t m;
    mutex_case_init(&m);

    ASSERT_FALSE(cat_thread_wait_is_parked(&m.wait));
    ASSERT_TRUE(cat_mutex_lock(&m, TEST_IO_TIMEOUT));
    cat_mutex_unlock(&m);
    /* a zero timeout still takes a free lock */
    ASSERT_TRUE(cat_mutex_lock(&m, 0));
    cat_mutex_unlock(&m);

    mutex_case_close(&m);
}

/* A held lock with a zero timeout reports a busy lock instead of waiting. */
TEST(cat_thread_sync, a_held_mutex_reports_busy_to_a_zero_timeout)
{
    cat_mutex_t m;
    mutex_case_init(&m);

    ASSERT_TRUE(cat_mutex_lock(&m, TEST_IO_TIMEOUT));
    ASSERT_FALSE(cat_mutex_lock(&m, 0));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBUSY);
    cat_mutex_unlock(&m);

    mutex_case_close(&m);
}

/* A second coroutine takes the lock only after the holder releases it. */
TEST(cat_thread_sync, a_waiter_takes_the_mutex_after_the_release)
{
    cat_mutex_t m;
    mutex_case_init(&m);

    bool finished = false;
    cat_bool_t second_ok = cat_false;

    ASSERT_TRUE(cat_mutex_lock(&m, TEST_IO_TIMEOUT));

    co([&] {
        second_ok = cat_mutex_lock(&m, TEST_IO_TIMEOUT);
        finished = true;
        cat_mutex_unlock(&m);
    });

    /* the second coroutine is parked on the held lock by now */
    ASSERT_FALSE(finished);
    ASSERT_TRUE(cat_thread_wait_is_parked(&m.wait));

    cat_mutex_unlock(&m);
    /* give the woken coroutine the thread to notice the release */
    ASSERT_TRUE(cat_time_delay(0));

    ASSERT_TRUE(finished);
    ASSERT_TRUE(second_ok);
    ASSERT_FALSE(cat_thread_wait_is_parked(&m.wait));

    mutex_case_close(&m);
}

/* Every coroutine that waits for the lock gets it in turn. */
TEST(cat_thread_sync, several_waiters_take_the_mutex_one_after_another)
{
    cat_mutex_t m;
    mutex_case_init(&m);

    enum { WAITERS = 4 };
    int taken = 0;
    int served = 0;
    cat_bool_t ok[WAITERS] = { cat_false, cat_false, cat_false, cat_false };

    ASSERT_TRUE(cat_mutex_lock(&m, TEST_IO_TIMEOUT));

    for (int index = 0; index < WAITERS; index++) {
        co([&, index] {
            ok[index] = cat_mutex_lock(&m, TEST_IO_TIMEOUT);
            taken++;
            /* hold it for a moment, so the next one has to wait for real */
            (void) cat_time_delay(1);
            cat_mutex_unlock(&m);
            /* count after the unlock: a reader that sees the last value knows
             * that no coroutine touches the mutex any more */
            served++;
        });
    }

    /* every coroutine is parked now */
    ASSERT_EQ(taken, 0);

    cat_mutex_unlock(&m);

    /* let every waiter run to the end */
    for (int round = 0; round < WAITERS * 2 && served < WAITERS; round++) {
        ASSERT_TRUE(cat_time_delay(10));
    }

    ASSERT_EQ(served, WAITERS);
    for (int index = 0; index < WAITERS; index++) {
        ASSERT_TRUE(ok[index]);
    }
    ASSERT_FALSE(cat_thread_wait_is_parked(&m.wait));

    mutex_case_close(&m);
}

/* A waiter that nobody releases gives up with a timeout. */
TEST(cat_thread_sync, a_mutex_waiter_times_out)
{
    cat_mutex_t m;
    mutex_case_init(&m);

    bool finished = false;
    cat_bool_t second_ok = cat_true;
    cat_errno_t second_error = 0;

    ASSERT_TRUE(cat_mutex_lock(&m, TEST_IO_TIMEOUT));

    co([&] {
        second_ok = cat_mutex_lock(&m, 100);
        second_error = cat_get_last_error_code();
        finished = true;
    });

    /* the waiter parks, and its timer runs out */
    ASSERT_TRUE(cat_time_delay(200));

    ASSERT_TRUE(finished);
    ASSERT_FALSE(second_ok);
    ASSERT_EQ(second_error, CAT_ETIMEDOUT);
    /* the timeout took the waiter out of the queue */
    ASSERT_FALSE(cat_thread_wait_is_parked(&m.wait));

    cat_mutex_unlock(&m);
    mutex_case_close(&m);
}

/* A BLOCK mutex needs no runtime: the waiter sleeps in the kernel. */
TEST(cat_thread_sync, a_blocking_mutex_holds_a_bare_thread)
{
    struct block_mutex_s {
        cat_mutex_t *mutex;
        std::atomic<int> state; /* 0: start, 1: in the wait, 2: held */
        cat_bool_t took;
    } state = {};

    cat_mutex_t m;
    ASSERT_TRUE(cat_mutex_init_ex(&m, CAT_SYNC_WAIT_BLOCK));
    ASSERT_TRUE(cat_mutex_lock(&m, TEST_IO_TIMEOUT));

    state.mutex = &m;
    uv_thread_t tid;
    ASSERT_EQ(uv_thread_create(&tid, [](void *data) {
        struct block_mutex_s *self = (struct block_mutex_s *) data;

        self->state = 1;
        self->took = cat_mutex_lock(self->mutex, CAT_TIMEOUT_FOREVER);
        if (self->took) {
            self->state = 2;
            cat_mutex_unlock(self->mutex);
        }
    }, &state), 0);

    /* the thread is in the wait by now, and no release has come */
    for (int round = 0; round < 5000 && state.state.load() == 0; round++) {
        uv_sleep(1);
    }
    ASSERT_EQ(state.state.load(), 1);
    uv_sleep(50);
    ASSERT_EQ(state.state.load(), 1);

    ASSERT_TRUE(cat_mutex_unlock(&m));
    ASSERT_EQ(uv_thread_join(&tid), 0);

    ASSERT_TRUE(state.took);
    ASSERT_EQ(state.state.load(), 2);

    ASSERT_TRUE(cat_mutex_destroy(&m));
}

/* A blocked waiter wakes one after another, so both threads take the lock. */
TEST(cat_thread_sync, a_blocking_mutex_serves_several_threads)
{
    struct block_mutex_count_s {
        cat_mutex_t *mutex;
        std::atomic<int> waiting;
        std::atomic<int> served;
    } state = {};

    cat_mutex_t m;
    ASSERT_TRUE(cat_mutex_init_ex(&m, CAT_SYNC_WAIT_BLOCK));
    ASSERT_TRUE(cat_mutex_lock(&m, TEST_IO_TIMEOUT));

    state.mutex = &m;
    uv_thread_t tid[2];
    for (int index = 0; index < 2; index++) {
        ASSERT_EQ(uv_thread_create(&tid[index], [](void *data) {
            struct block_mutex_count_s *self = (struct block_mutex_count_s *) data;

            self->waiting++;
            if (cat_mutex_lock(self->mutex, TEST_IO_TIMEOUT)) {
                self->served++;
                cat_mutex_unlock(self->mutex);
            }
        }, &state), 0);
    }

    for (int round = 0; round < 5000 && state.waiting.load() < 2; round++) {
        uv_sleep(1);
    }
    ASSERT_EQ(state.waiting.load(), 2);
    uv_sleep(50);

    ASSERT_TRUE(cat_mutex_unlock(&m));

    for (int index = 0; index < 2; index++) {
        ASSERT_EQ(uv_thread_join(&tid[index]), 0);
    }
    ASSERT_EQ(state.served.load(), 2);

    ASSERT_TRUE(cat_mutex_destroy(&m));
}

/* A blocked lock gives up on the timeout and says so. */
TEST(cat_thread_sync, a_blocking_mutex_waiter_times_out)
{
    struct block_mutex_timeout_s {
        cat_mutex_t *mutex;
        cat_bool_t took;
        cat_errno_t error;
    } state = {};

    cat_mutex_t m;
    ASSERT_TRUE(cat_mutex_init_ex(&m, CAT_SYNC_WAIT_BLOCK));
    ASSERT_TRUE(cat_mutex_lock(&m, TEST_IO_TIMEOUT));

    state.mutex = &m;

    struct plain_thread_s worker;
    ASSERT_TRUE(plain_thread_start(&worker, [](void *data) {
        struct block_mutex_timeout_s *self = (struct block_mutex_timeout_s *) data;

        self->took = cat_mutex_lock(self->mutex, 50);
        self->error = cat_get_last_error_code();
    }, &state));
    plain_thread_join(&worker);

    ASSERT_FALSE(state.took);
    ASSERT_EQ(state.error, CAT_ETIMEDOUT);

    ASSERT_TRUE(cat_mutex_unlock(&m));
    ASSERT_TRUE(cat_mutex_destroy(&m));
}

/* The signal of one thread ends the blocking wait of another. */
TEST(cat_thread_sync, a_blocking_condition_wakes_a_thread)
{
    struct block_cond_s {
        cat_mutex_t mutex;
        cat_cond_t cond;
        bool predicate;
    } state = {};

    ASSERT_TRUE(cat_mutex_init_ex(&state.mutex, CAT_SYNC_WAIT_BLOCK));
    ASSERT_TRUE(cat_cond_init(&state.cond));
    ASSERT_TRUE(cat_mutex_lock(&state.mutex, TEST_IO_TIMEOUT));

    struct plain_thread_s worker;
    ASSERT_TRUE(plain_thread_start(&worker, [](void *data) {
        struct block_cond_s *self = (struct block_cond_s *) data;

        if (cat_mutex_lock(&self->mutex, TEST_IO_TIMEOUT)) {
            self->predicate = true;
            cat_cond_signal(&self->cond);
            cat_mutex_unlock(&self->mutex);
        }
    }, &state));

    /* the wait releases the mutex, so the worker takes it and signals */
    ASSERT_TRUE(cat_cond_wait(&state.cond, &state.mutex, CAT_TIMEOUT_FOREVER));
    ASSERT_TRUE(state.predicate);
    cat_mutex_unlock(&state.mutex);

    plain_thread_join(&worker);

    cat_cond_destroy(&state.cond);
    ASSERT_TRUE(cat_mutex_destroy(&state.mutex));
}

/* A condition wait needs the caller to hold the mutex, in BLOCK mode too. */
TEST(cat_thread_sync, a_blocking_condition_wait_needs_the_holder)
{
    cat_mutex_t m;
    cat_cond_t c;
    ASSERT_TRUE(cat_mutex_init_ex(&m, CAT_SYNC_WAIT_BLOCK));
    ASSERT_TRUE(cat_cond_init(&c));

    ASSERT_FALSE(cat_cond_wait(&c, &m, TEST_IO_TIMEOUT));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);

    cat_cond_destroy(&c);
    ASSERT_TRUE(cat_mutex_destroy(&m));
}

/* A blocked condition wait gives up on the timeout and holds the mutex again. */
TEST(cat_thread_sync, a_blocking_condition_waiter_times_out)
{
    cat_mutex_t m;
    cat_cond_t c;
    ASSERT_TRUE(cat_mutex_init_ex(&m, CAT_SYNC_WAIT_BLOCK));
    ASSERT_TRUE(cat_cond_init(&c));

    ASSERT_TRUE(cat_mutex_lock(&m, TEST_IO_TIMEOUT));

    cat_msec_t start = cat_time_msec();
    ASSERT_FALSE(cat_cond_wait(&c, &m, 50));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
    ASSERT_GE(cat_time_msec() - start, 30);

    /* the caller holds the mutex again */
    ASSERT_TRUE(cat_mutex_unlock(&m));

    cat_cond_destroy(&c);
    ASSERT_TRUE(cat_mutex_destroy(&m));
}

TEST(cat_thread_sync, a_mutex_rejects_an_unknown_wait_mode)
{
    cat_mutex_t m;

    ASSERT_FALSE(cat_mutex_init_ex(&m, (cat_sync_wait_t) 42));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

/* A semaphore hands out its count, and an empty one waits for a post. */
TEST(cat_thread_sync, a_semaphore_waiter_wakes_on_a_post)
{
    cat_sem_t s;
    sem_case_init(&s, 0);

    bool waiter_finished = false;
    cat_bool_t acquired = cat_false;

    co([&] {
        acquired = cat_sem_acquire(&s, TEST_IO_TIMEOUT);
        waiter_finished = true;
    });

    ASSERT_FALSE(waiter_finished);

    cat_sem_post(&s);
    ASSERT_TRUE(cat_time_delay(0));

    ASSERT_TRUE(waiter_finished);
    ASSERT_TRUE(acquired);

    /* the waiter consumed the only count */
    ASSERT_FALSE(cat_sem_acquire(&s, 0));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBUSY);

    sem_case_close(&s);
}

/* A count that is already there needs no wait. */
TEST(cat_thread_sync, a_semaphore_with_a_count_does_not_wait)
{
    cat_sem_t s;
    sem_case_init(&s, 2);

    ASSERT_TRUE(cat_sem_acquire(&s, 0));
    ASSERT_TRUE(cat_sem_acquire(&s, 0));
    ASSERT_FALSE(cat_sem_acquire(&s, 0));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBUSY);

    sem_case_close(&s);
}

/* Several waiters on one semaphore are served by several posts. */
TEST(cat_thread_sync, several_semaphore_waiters_are_served_in_turn)
{
    cat_sem_t s;
    sem_case_init(&s, 0);

    enum { WAITERS = 3 };
    int acquired = 0;
    cat_bool_t ok[WAITERS] = { cat_false, cat_false, cat_false };

    for (int index = 0; index < WAITERS; index++) {
        co([&, index] {
            ok[index] = cat_sem_acquire(&s, TEST_IO_TIMEOUT);
            acquired++;
        });
    }

    ASSERT_EQ(acquired, 0);

    for (int index = 0; index < WAITERS; index++) {
        cat_sem_post(&s);
        ASSERT_TRUE(cat_time_delay(0));
    }

    ASSERT_EQ(acquired, WAITERS);
    for (int index = 0; index < WAITERS; index++) {
        ASSERT_TRUE(ok[index]);
    }
    ASSERT_FALSE(cat_thread_wait_is_parked(&s.wait));

    sem_case_close(&s);
}

/* A take and a release are visible in the value that the semaphore reports. */
TEST(cat_thread_sync, a_semaphore_reports_its_value)
{
    cat_sem_t s;
    sem_case_init(&s, 2);

    ASSERT_EQ(cat_sem_get_value(&s), 2);
    ASSERT_EQ(cat_sem_get_waiter_count(&s), 0);

    ASSERT_TRUE(cat_sem_acquire(&s, 0));
    ASSERT_EQ(cat_sem_get_value(&s), 1);

    ASSERT_TRUE(cat_sem_try_acquire(&s));
    ASSERT_EQ(cat_sem_get_value(&s), 0);

    ASSERT_FALSE(cat_sem_try_acquire(&s));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBUSY);

    cat_sem_release(&s, 3);
    ASSERT_EQ(cat_sem_get_value(&s), 3);

    /* a release of no count posts nothing */
    cat_sem_release(&s, 0);
    ASSERT_EQ(cat_sem_get_value(&s), 3);

    sem_case_close(&s);
}

/* The counts of one release serve the same number of waiters, and no more. */
TEST(cat_thread_sync, a_release_wakes_the_counted_waiters)
{
    cat_sem_t s;
    sem_case_init(&s, 0);

    enum { WAITERS = 3 };
    int acquired = 0;

    for (int index = 0; index < WAITERS; index++) {
        co([&] {
            if (cat_sem_acquire(&s, TEST_IO_TIMEOUT)) {
                acquired++;
            }
        });
    }

    ASSERT_EQ(acquired, 0);
    ASSERT_EQ(cat_sem_get_waiter_count(&s), WAITERS);

    cat_sem_release(&s, 2);
    ASSERT_TRUE(cat_time_delay(10));

    ASSERT_EQ(acquired, 2);
    ASSERT_EQ(cat_sem_get_waiter_count(&s), 1);
    ASSERT_EQ(cat_sem_get_value(&s), 0);

    cat_sem_release(&s, 1);
    ASSERT_TRUE(cat_time_delay(10));

    ASSERT_EQ(acquired, WAITERS);
    ASSERT_EQ(cat_sem_get_waiter_count(&s), 0);

    sem_case_close(&s);
}

/* An empty semaphore with nobody to post gives up with a timeout. */
TEST(cat_thread_sync, a_semaphore_waiter_times_out)
{
    cat_sem_t s;
    sem_case_init(&s, 0);

    cat_bool_t acquired = cat_true;
    cat_errno_t error = 0;
    bool finished = false;

    co([&] {
        acquired = cat_sem_acquire(&s, 100);
        error = cat_get_last_error_code();
        finished = true;
    });

    ASSERT_TRUE(cat_time_delay(200));

    ASSERT_TRUE(finished);
    ASSERT_FALSE(acquired);
    ASSERT_EQ(error, CAT_ETIMEDOUT);
    ASSERT_FALSE(cat_thread_wait_is_parked(&s.wait));

    sem_case_close(&s);
}

/* A thread that never took a runtime has nowhere to park a waiter. */
TEST(cat_thread_sync, a_thread_without_a_runtime_can_not_wait)
{
    cat_sem_t s;
    sem_case_init(&s, 0);

    struct bare_s {
        cat_sem_t *sem;
        cat_bool_t ok;
        cat_errno_t code;
    } bare = { &s, cat_true, 0 };

    uv_thread_t tid;
    ASSERT_EQ(uv_thread_create(&tid, [](void *data) {
        struct bare_s *self = (struct bare_s *) data;
        /* no cat_runtime_init here on purpose */
        self->ok = cat_sem_acquire(self->sem, 0);
        self->code = cat_get_last_error_code();
    }, &bare), 0);
    ASSERT_EQ(uv_thread_join(&tid), 0);

    ASSERT_FALSE(bare.ok);
    ASSERT_EQ(bare.code, CAT_EMISUSE);

    sem_case_close(&s);
}

/*
 * The release happens on one thread and the waiters park on another, which is
 * the case that needs the wakeup channel: a thread cannot resume a coroutine
 * that belongs to somebody else.
 */
TEST(cat_thread_sync, waiters_on_another_thread_wake_on_the_release)
{
    struct cross_s {
        cat_mutex_t mutex;
        cat_bool_t runtime_ok;
        int served;
        cat_bool_t ok[2];
    } cross = {};

    ASSERT_TRUE(cat_mutex_init(&cross.mutex));

    /* the main thread holds the lock, so the other one has to wait */
    ASSERT_TRUE(cat_mutex_lock(&cross.mutex, TEST_IO_TIMEOUT));

    std::thread worker([&cross] {
        cross.runtime_ok = cat_runtime_init_all() && cat_run(CAT_RUN_EASY);
        if (!cross.runtime_ok) {
            return;
        }

        /* the scheduler runs by now, so coroutines can wait here */
        for (int index = 0; index < 2; index++) {
            cat_coroutine_run(nullptr, [](cat_data_t *data) -> cat_data_t * {
                struct cross_s *self = (struct cross_s *) data;

                cat_bool_t taken = cat_mutex_lock(&self->mutex, TEST_IO_TIMEOUT);
                if (taken) {
                    self->ok[self->served] = cat_true;
                }
                self->served++;
                cat_mutex_unlock(&self->mutex);
                return nullptr;
            }, &cross);
        }

        /* cat_stop waits for every coroutine of this thread, so it returns
         * after both waiters are done */
        (void) cat_stop();
        (void) cat_runtime_shutdown_all();
        (void) cat_runtime_close_all();
    });

    /* wait for both coroutines to park, because a request for a loop that does
     * not run yet would be lost */
    while (cross.served == 0 && !cat_thread_wait_is_parked(&cross.mutex.wait)) {
        (void) cat_time_delay(1);
    }

    cat_mutex_unlock(&cross.mutex);
    worker.join();

    ASSERT_TRUE(cross.runtime_ok);
    ASSERT_EQ(cross.served, 2);
    ASSERT_TRUE(cross.ok[0]);
    ASSERT_TRUE(cross.ok[1]);

    cat_mutex_destroy(&cross.mutex);
}


/* ==========================================================================
 * Real usage: the shapes a program actually writes.
 * ========================================================================== */

/* A consumer waits for work and a producer hands it over, which is the reason
 * the semaphore exists. */
TEST(cat_thread_sync, a_producer_hands_work_to_a_consumer)
{
    cat_sem_t items;
    sem_case_init(&items, 0);

    int produced = 0;
    int consumed = 0;
    const int total = 100;

    co([&] {
        for (int index = 0; index < total; index++) {
            produced++;
            cat_sem_post(&items);
        }
    });

    co([&] {
        while (consumed < total) {
            if (cat_sem_acquire(&items, TEST_IO_TIMEOUT)) {
                consumed++;
            }
        }
    });

    /* let both run to the end */
    for (int round = 0; round < total && (produced < total || consumed < total); round++) {
        (void) cat_time_delay(1);
    }

    ASSERT_EQ(produced, total);
    ASSERT_EQ(consumed, total);
    ASSERT_FALSE(cat_thread_wait_is_parked(&items.wait));

    sem_case_close(&items);
}

/* A counter shared by several coroutines is only correct when the mutex really
 * keeps them apart. */
TEST(cat_thread_sync, a_shared_counter_under_the_mutex_is_exact)
{
    cat_mutex_t guard;
    mutex_case_init(&guard);

    long counter = 0;
    const int workers = 8;
    const int per_worker = 100;

    for (int index = 0; index < workers; index++) {
        co([&] {
            for (int step = 0; step < per_worker; step++) {
                ASSERT_TRUE(cat_mutex_lock(&guard, TEST_IO_TIMEOUT));
                counter++;
                cat_mutex_unlock(&guard);
            }
        });
    }

    for (int round = 0; round < 2000 && counter < workers * per_worker; round++) {
        (void) cat_time_delay(1);
    }

    ASSERT_EQ(counter, (long) workers * per_worker);
    ASSERT_FALSE(cat_thread_wait_is_parked(&guard.wait));

    mutex_case_close(&guard);
}

/* Several threads take the same mutex, which is the case that needs a wakeup
 * across threads. */
TEST(cat_thread_sync, several_threads_take_one_mutex_in_turn)
{
    struct shared_s {
        cat_mutex_t guard;
        long counter;
        int steps;
    } shared = { {}, 0, 50 };

    ASSERT_TRUE(cat_mutex_init(&shared.guard));

    const int threads = 3;

    for (int index = 0; index < threads; index++) {
        std::thread worker([&shared] {
            if (!cat_runtime_init_all() || !cat_run(CAT_RUN_EASY)) {
                return;
            }

            cat_coroutine_run(nullptr, [](cat_data_t *data) -> cat_data_t * {
                struct shared_s *self = (struct shared_s *) data;

                for (int step = 0; step < self->steps; step++) {
                    if (cat_mutex_lock(&self->guard, TEST_IO_TIMEOUT)) {
                        self->counter++;
                        cat_mutex_unlock(&self->guard);
                    }
                }
                return nullptr;
            }, &shared);

            (void) cat_stop();
            (void) cat_runtime_shutdown_all();
            (void) cat_runtime_close_all();
        });
        worker.join();
    }

    ASSERT_EQ(shared.counter, (long) threads * shared.steps);

    cat_mutex_destroy(&shared.guard);
}


/* ==========================================================================
 * Broad smoke: every entry point and every path, cheaply.
 * ========================================================================== */

/* init and destroy go together, repeatedly, and leave nothing behind. */
TEST(cat_thread_sync, init_and_destroy_repeat_cleanly)
{
    for (int round = 0; round < 20; round++) {
        cat_mutex_t m;
        cat_sem_t s;

        ASSERT_TRUE(cat_mutex_init(&m));
        ASSERT_FALSE(cat_thread_wait_is_parked(&m.wait));
        ASSERT_TRUE(cat_mutex_lock(&m, 0));
        cat_mutex_unlock(&m);
        cat_mutex_destroy(&m);

        ASSERT_TRUE(cat_sem_init(&s, 1));
        ASSERT_FALSE(cat_thread_wait_is_parked(&s.wait));
        ASSERT_TRUE(cat_sem_acquire(&s, 0));
        cat_sem_post(&s);
        cat_sem_destroy(&s);
    }
}

/* The three timeout kinds behave, for both the mutex and the semaphore. */
TEST(cat_thread_sync, the_timeout_kinds_behave)
{
    cat_mutex_t m;
    cat_sem_t s;

    mutex_case_init(&m);
    sem_case_init(&s, 0);

    /* zero never suspends */
    ASSERT_TRUE(cat_mutex_lock(&m, 0));
    ASSERT_FALSE(cat_sem_acquire(&s, 0));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBUSY);

    /* a small timeout runs out and reports it */
    ASSERT_FALSE(cat_sem_acquire(&s, 20));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);

    /* forever is accepted and the caller is woken by a post */
    co([&] {
        ASSERT_TRUE(cat_sem_acquire(&s, CAT_TIMEOUT_FOREVER));
    });
    cat_sem_post(&s);
    ASSERT_TRUE(cat_time_delay(0));

    cat_mutex_unlock(&m);

    sem_case_close(&s);
    mutex_case_close(&m);
}

/* is_parked follows the queue through zero, one and several waiters. */
TEST(cat_thread_sync, is_parked_follows_the_queue)
{
    struct queue_s q = {};

    ASSERT_TRUE(cat_thread_wait_init(&q.wait));
    ASSERT_FALSE(cat_thread_wait_is_parked(&q.wait));

    co([&] {
        if (cat_thread_wait_park(&q.wait, TEST_IO_TIMEOUT)) {
            q.woken++;
        }
    });

    ASSERT_TRUE(cat_thread_wait_is_parked(&q.wait));

    ASSERT_TRUE(cat_thread_wait_wake(&q.wait));
    ASSERT_TRUE(cat_time_delay(0));
    ASSERT_EQ(q.woken, 1);
    ASSERT_FALSE(cat_thread_wait_is_parked(&q.wait));

    cat_thread_wait_destroy(&q.wait);
}

/* A bare wait queue wakes one waiter per call, and the rest stay queued. */
TEST(cat_thread_sync, a_bare_queue_wakes_one_at_a_time)
{
    struct queue_s q = {};

    ASSERT_TRUE(cat_thread_wait_init(&q.wait));

    for (int index = 0; index < 3; index++) {
        co([&] {
            if (cat_thread_wait_park(&q.wait, TEST_IO_TIMEOUT)) {
                q.woken++;
            }
        });
    }

    for (int index = 0; index < 3; index++) {
        ASSERT_TRUE(cat_thread_wait_wake(&q.wait));
        ASSERT_TRUE(cat_time_delay(0));
        ASSERT_EQ(q.woken, index + 1);
    }

    /* nobody is left, so the next wake reports it */
    ASSERT_FALSE(cat_thread_wait_wake(&q.wait));

    cat_thread_wait_destroy(&q.wait);
}

/* broadcast takes everybody, including more waiters than one batch holds. */
TEST(cat_thread_sync, a_bare_queue_broadcast_takes_every_waiter)
{
    struct queue_s q = {};
    /* more than the batch of the wake loop, to exercise the repeat */
    const int waiters = 40;

    ASSERT_TRUE(cat_thread_wait_init(&q.wait));

    for (int index = 0; index < waiters; index++) {
        co([&] {
            if (cat_thread_wait_park(&q.wait, TEST_IO_TIMEOUT)) {
                q.woken++;
            }
        });
    }

    ASSERT_TRUE(cat_thread_wait_is_parked(&q.wait));

    ASSERT_TRUE(cat_thread_wait_wake_all(&q.wait));
    ASSERT_TRUE(cat_time_delay(0));

    ASSERT_EQ(q.woken, waiters);
    ASSERT_FALSE(cat_thread_wait_is_parked(&q.wait));

    /* an empty queue answers without touching anything */
    ASSERT_FALSE(cat_thread_wait_wake_all(&q.wait));

    cat_thread_wait_destroy(&q.wait);
}


/* ==========================================================================
 * Corner cases: the edges that a real program hits by accident.
 * ========================================================================== */

/* A waiter that timed out must leave the queue as empty as it found it, so the
 * next waiter is served normally. */
TEST(cat_thread_sync, a_queue_is_reusable_after_a_timeout)
{
    cat_sem_t s;
    int woken = 0;

    sem_case_init(&s, 0);

    co([&] {
        if (cat_sem_acquire(&s, 20)) {
            woken++;
        }
    });

    ASSERT_TRUE(cat_time_delay(100));
    ASSERT_EQ(woken, 0);
    ASSERT_FALSE(cat_thread_wait_is_parked(&s.wait));

    /* the same object serves a new waiter */
    co([&] {
        if (cat_sem_acquire(&s, TEST_IO_TIMEOUT)) {
            woken++;
        }
    });

    ASSERT_TRUE(cat_thread_wait_is_parked(&s.wait));
    cat_sem_post(&s);
    ASSERT_TRUE(cat_time_delay(0));

    ASSERT_EQ(woken, 1);
    ASSERT_FALSE(cat_thread_wait_is_parked(&s.wait));

    sem_case_close(&s);
}

/* One post serves one waiter. The other one keeps waiting, and its own timeout
 * then ends it without touching the served one. */
TEST(cat_thread_sync, a_post_serves_one_waiter_and_leaves_the_other)
{
    cat_sem_t s;
    int first_won = 0;
    int second_won = 0;

    sem_case_init(&s, 0);

    /* the first one waits long, so the post below serves it and not the other */
    co([&] {
        if (cat_sem_acquire(&s, TEST_IO_TIMEOUT)) {
            first_won++;
        }
    });

    co([&] {
        if (cat_sem_acquire(&s, 20)) {
            second_won++;
        }
    });

    ASSERT_EQ(first_won, 0);
    ASSERT_EQ(second_won, 0);

    cat_sem_post(&s);
    ASSERT_TRUE(cat_time_delay(0));

    /* the queue is first in, first served */
    ASSERT_EQ(first_won, 1);
    ASSERT_EQ(second_won, 0);

    /* the second one is still queued, and its own timeout takes it out */
    ASSERT_TRUE(cat_thread_wait_is_parked(&s.wait));
    ASSERT_TRUE(cat_time_delay(100));
    ASSERT_EQ(second_won, 0);
    ASSERT_FALSE(cat_thread_wait_is_parked(&s.wait));

    sem_case_close(&s);
}

/* A post that lands after the waiter already timed out must not resume it a
 * second time. */
TEST(cat_thread_sync, a_late_post_does_not_resume_a_waiter_twice)
{
    struct queue_s q = {};

    ASSERT_TRUE(cat_thread_wait_init(&q.wait));

    co([&] {
        if (cat_thread_wait_park(&q.wait, 20)) {
            q.woken++;
        }
    });

    ASSERT_TRUE(cat_time_delay(100));

    /* the queue is empty now, so this wake has nobody to wake */
    ASSERT_FALSE(cat_thread_wait_wake(&q.wait));
    ASSERT_TRUE(cat_time_delay(0));

    ASSERT_EQ(q.woken, 0);
    ASSERT_FALSE(cat_thread_wait_is_parked(&q.wait));

    cat_thread_wait_destroy(&q.wait);
}

/* A thread that never took a runtime cannot park, even when it asks for a
 * wait directly instead of going through a primitive. */
TEST(cat_thread_sync, park_on_a_thread_without_a_runtime_is_refused)
{
    struct bare_s {
        cat_thread_wait_t wait;
        cat_bool_t ok;
        cat_errno_t code;
    } bare = { {}, cat_true, 0 };

    uv_thread_t tid;
    ASSERT_EQ(uv_thread_create(&tid, [](void *data) {
        struct bare_s *self = (struct bare_s *) data;

        self->ok = cat_thread_wait_park(&self->wait, 0);
        self->code = cat_get_last_error_code();
    }, &bare), 0);
    ASSERT_EQ(uv_thread_join(&tid), 0);

    ASSERT_FALSE(bare.ok);
    ASSERT_EQ(bare.code, CAT_EMISUSE);
}

/*
 * A release with no holder is a mistake, and it is reported: the pthread call
 * accepts it and leaves a state that fails at destroy time, so the check here
 * keeps the mutex usable.
 */
TEST(cat_thread_sync, unlocking_a_free_mutex_is_refused)
{
    cat_mutex_t m;

    mutex_case_init(&m);

    ASSERT_FALSE(cat_mutex_unlock(&m));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);

    /* the refused release left the mutex untouched */
    ASSERT_TRUE(cat_mutex_lock(&m, 0));
    ASSERT_TRUE(cat_mutex_unlock(&m));

    mutex_case_close(&m);
}

/* Throwing a held mutex away is the same mistake, and libuv would abort on it. */
TEST(cat_thread_sync, destroying_a_held_mutex_is_refused)
{
    cat_mutex_t m;

    mutex_case_init(&m);

    ASSERT_TRUE(cat_mutex_lock(&m, 0));
    ASSERT_FALSE(cat_mutex_destroy(&m));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);

    /* the mutex survived the refusal, so it can still be released and dropped */
    ASSERT_TRUE(cat_mutex_unlock(&m));
    mutex_case_close(&m);
}

/* Many threads, many coroutines each, one mutex: the count must be exact and
 * nothing may be left waiting. */
TEST(cat_thread_sync, many_threads_and_coroutines_share_one_mutex)
{
    struct heavy_s {
        cat_mutex_t guard;
        long counter;
        int coroutines;
        int steps;
    } heavy = { {}, 0, 2, 200 };

    ASSERT_TRUE(cat_mutex_init(&heavy.guard));

    const int threads = 3;

    for (int index = 0; index < threads; index++) {
        std::thread worker([&heavy] {
            if (!cat_runtime_init_all() || !cat_run(CAT_RUN_EASY)) {
                return;
            }

            for (int which = 0; which < heavy.coroutines; which++) {
                cat_coroutine_run(nullptr, [](cat_data_t *data) -> cat_data_t * {
                    struct heavy_s *self = (struct heavy_s *) data;

                    for (int step = 0; step < self->steps; step++) {
                        if (cat_mutex_lock(&self->guard, TEST_IO_TIMEOUT)) {
                            self->counter++;
                            cat_mutex_unlock(&self->guard);
                        }
                    }
                    return nullptr;
                }, &heavy);
            }

            (void) cat_stop();
            (void) cat_runtime_shutdown_all();
            (void) cat_runtime_close_all();
        });
        worker.join();
    }

    ASSERT_EQ(heavy.counter, (long) threads * heavy.coroutines * heavy.steps);
    ASSERT_FALSE(cat_thread_wait_is_parked(&heavy.guard.wait));

    cat_mutex_destroy(&heavy.guard);
}


/* ==========================================================================
 * Condition variable: real usage first.
 * ========================================================================== */

/* A consumer waits for the producer to put something in, which is the shape
 * every queue has. */
TEST(cat_cond, a_consumer_waits_for_the_producer)
{
    struct shared_s {
        cat_mutex_t guard;
        cat_cond_t ready;
        int items;
        int taken;
    } shared = {};

    ASSERT_TRUE(cat_mutex_init(&shared.guard));
    ASSERT_TRUE(cat_cond_init(&shared.ready));

    const int total = 50;

    co([&] {
        ASSERT_TRUE(cat_mutex_lock(&shared.guard, TEST_IO_TIMEOUT));
        while (shared.items < total) {
            if (shared.taken >= total) {
                break;
            }
            if (cat_cond_wait(&shared.ready, &shared.guard, TEST_IO_TIMEOUT)) {
                shared.taken += shared.items;
                shared.items = 0;
            }
        }
        cat_mutex_unlock(&shared.guard);
    });

    co([&] {
        for (int index = 0; index < total; index++) {
            ASSERT_TRUE(cat_mutex_lock(&shared.guard, TEST_IO_TIMEOUT));
            shared.items++;
            cat_cond_signal(&shared.ready);
            cat_mutex_unlock(&shared.guard);
        }
    });

    for (int round = 0; round < 200 && shared.taken < total; round++) {
        (void) cat_time_delay(1);
    }

    ASSERT_EQ(shared.taken, total);
    ASSERT_FALSE(cat_thread_wait_is_parked(&shared.ready.wait));

    cat_cond_destroy(&shared.ready);
    cat_mutex_destroy(&shared.guard);
}

/* A broadcast tells every waiter that the state they watch has changed. */
TEST(cat_cond, a_broadcast_reaches_every_consumer)
{
    struct shared_s {
        cat_mutex_t guard;
        cat_cond_t changed;
        int generation;
        int seen_by[3];
    } shared = {};

    ASSERT_TRUE(cat_mutex_init(&shared.guard));
    ASSERT_TRUE(cat_cond_init(&shared.changed));

    for (int index = 0; index < 3; index++) {
        co([&, index] {
            ASSERT_TRUE(cat_mutex_lock(&shared.guard, TEST_IO_TIMEOUT));
            if (cat_cond_wait(&shared.changed, &shared.guard, TEST_IO_TIMEOUT)) {
                shared.seen_by[index] = shared.generation;
            }
            cat_mutex_unlock(&shared.guard);
        });
    }

    ASSERT_TRUE(cat_time_delay(0));

    ASSERT_TRUE(cat_mutex_lock(&shared.guard, TEST_IO_TIMEOUT));
    shared.generation = 7;
    ASSERT_TRUE(cat_cond_broadcast(&shared.changed));
    cat_mutex_unlock(&shared.guard);

    ASSERT_TRUE(cat_time_delay(0));

    for (int index = 0; index < 3; index++) {
        ASSERT_EQ(shared.seen_by[index], 7);
    }
    ASSERT_FALSE(cat_thread_wait_is_parked(&shared.changed.wait));

    cat_cond_destroy(&shared.changed);
    cat_mutex_destroy(&shared.guard);
}

/* ==========================================================================
 * Condition variable: broad smoke.
 * ========================================================================== */

/* A signal takes one waiter, not all of them. */
TEST(cat_cond, a_signal_wakes_one_of_several)
{
    struct shared_s {
        cat_mutex_t guard;
        cat_cond_t cond;
        int woken;
    } shared = {};

    ASSERT_TRUE(cat_mutex_init(&shared.guard));
    ASSERT_TRUE(cat_cond_init(&shared.cond));

    for (int index = 0; index < 3; index++) {
        co([&] {
            ASSERT_TRUE(cat_mutex_lock(&shared.guard, TEST_IO_TIMEOUT));
            if (cat_cond_wait(&shared.cond, &shared.guard, TEST_IO_TIMEOUT)) {
                shared.woken++;
            }
            cat_mutex_unlock(&shared.guard);
        });
    }

    ASSERT_TRUE(cat_time_delay(0));
    ASSERT_TRUE(cat_thread_wait_is_parked(&shared.cond.wait));

    ASSERT_TRUE(cat_cond_signal(&shared.cond));
    ASSERT_TRUE(cat_time_delay(0));
    ASSERT_EQ(shared.woken, 1);

    /* the other two must finish before the objects go away */
    ASSERT_TRUE(cat_cond_broadcast(&shared.cond));
    ASSERT_TRUE(cat_time_delay(0));
    ASSERT_EQ(shared.woken, 3);
    ASSERT_FALSE(cat_thread_wait_is_parked(&shared.cond.wait));

    cat_cond_destroy(&shared.cond);
    cat_mutex_destroy(&shared.guard);
}

/* A signal with nobody waiting is forgotten, and a broadcast with nobody
 * waiting is harmless. */
TEST(cat_cond, a_signal_without_a_waiter_is_forgotten)
{
    cat_cond_t cond;

    ASSERT_TRUE(cat_cond_init(&cond));

    ASSERT_FALSE(cat_cond_signal(&cond));
    ASSERT_FALSE(cat_cond_broadcast(&cond));
    ASSERT_FALSE(cat_thread_wait_is_parked(&cond.wait));

    cat_cond_destroy(&cond);
}

/* The wait gives the mutex back, or the producer could never take it. */
TEST(cat_cond, a_wait_gives_the_mutex_back)
{
    struct shared_s {
        cat_mutex_t guard;
        cat_cond_t cond;
        cat_bool_t other_took_it;
        cat_bool_t finished;
    } shared = {};

    ASSERT_TRUE(cat_mutex_init(&shared.guard));
    ASSERT_TRUE(cat_cond_init(&shared.cond));

    co([&] {
        ASSERT_TRUE(cat_mutex_lock(&shared.guard, TEST_IO_TIMEOUT));
        (void) cat_cond_wait(&shared.cond, &shared.guard, 20);
        shared.finished = cat_true;
        cat_mutex_unlock(&shared.guard);
    });

    ASSERT_TRUE(cat_time_delay(0));

    /* the waiting coroutine must not hold the mutex now */
    ASSERT_TRUE(cat_mutex_lock(&shared.guard, 0));
    shared.other_took_it = cat_true;
    cat_mutex_unlock(&shared.guard);

    ASSERT_TRUE(shared.other_took_it);

    /* the waiter has to finish before its objects go away */
    ASSERT_TRUE(cat_time_delay(100));
    ASSERT_TRUE(shared.finished);
    ASSERT_FALSE(cat_thread_wait_is_parked(&shared.cond.wait));

    cat_cond_destroy(&shared.cond);
    cat_mutex_destroy(&shared.guard);
}

/* The wait takes the mutex back before it returns, on the woken path. */
TEST(cat_cond, a_woken_wait_holds_the_mutex_again)
{
    struct shared_s {
        cat_mutex_t guard;
        cat_cond_t cond;
        cat_bool_t held_after;
        int stage;
    } shared = {};

    ASSERT_TRUE(cat_mutex_init(&shared.guard));
    ASSERT_TRUE(cat_cond_init(&shared.cond));

    co([&] {
        ASSERT_TRUE(cat_mutex_lock(&shared.guard, TEST_IO_TIMEOUT));
        (void) cat_cond_wait(&shared.cond, &shared.guard, TEST_IO_TIMEOUT);
        /* the mutex must be ours again, so nobody else can take it */
        shared.held_after = !cat_mutex_lock(&shared.guard, 0);
        shared.stage = 2;
        cat_mutex_unlock(&shared.guard);
    });

    ASSERT_TRUE(cat_time_delay(0));

    ASSERT_TRUE(cat_mutex_lock(&shared.guard, TEST_IO_TIMEOUT));
    cat_cond_signal(&shared.cond);
    cat_mutex_unlock(&shared.guard);

    ASSERT_TRUE(cat_time_delay(0));

    ASSERT_EQ(shared.stage, 2);
    ASSERT_TRUE(shared.held_after);

    cat_cond_destroy(&shared.cond);
    cat_mutex_destroy(&shared.guard);
}

/* The timeout takes the mutex back too. */
TEST(cat_cond, a_timed_out_wait_holds_the_mutex_again)
{
    struct shared_s {
        cat_mutex_t guard;
        cat_cond_t cond;
        cat_errno_t error;
        cat_bool_t held_after;
        int stage;
    } shared = {};

    ASSERT_TRUE(cat_mutex_init(&shared.guard));
    ASSERT_TRUE(cat_cond_init(&shared.cond));

    co([&] {
        ASSERT_TRUE(cat_mutex_lock(&shared.guard, TEST_IO_TIMEOUT));
        if (!cat_cond_wait(&shared.cond, &shared.guard, 20)) {
            shared.error = cat_get_last_error_code();
        }
        shared.held_after = !cat_mutex_lock(&shared.guard, 0);
        shared.stage = 2;
        cat_mutex_unlock(&shared.guard);
    });

    ASSERT_TRUE(cat_time_delay(100));

    ASSERT_EQ(shared.stage, 2);
    ASSERT_EQ(shared.error, CAT_ETIMEDOUT);
    ASSERT_TRUE(shared.held_after);
    ASSERT_FALSE(cat_thread_wait_is_parked(&shared.cond.wait));

    cat_cond_destroy(&shared.cond);
    cat_mutex_destroy(&shared.guard);
}

/* ==========================================================================
 * Condition variable: corners.
 * ========================================================================== */

/* Waiting without the mutex is a mistake, because the predicate it guards is
 * not protected then. */
TEST(cat_cond, a_wait_without_the_mutex_is_refused)
{
    cat_mutex_t guard;
    cat_cond_t cond;
    cat_bool_t ok = cat_true;
    cat_errno_t code = 0;

    ASSERT_TRUE(cat_mutex_init(&guard));
    ASSERT_TRUE(cat_cond_init(&cond));

    co([&] {
        ok = cat_cond_wait(&cond, &guard, 20);
        code = cat_get_last_error_code();
    });

    ASSERT_TRUE(cat_time_delay(0));

    ASSERT_FALSE(ok);
    ASSERT_EQ(code, CAT_EMISUSE);
    /* the refused wait did not touch the queue */
    ASSERT_FALSE(cat_thread_wait_is_parked(&cond.wait));

    cat_cond_destroy(&cond);
    cat_mutex_destroy(&guard);
}

/* A signal that lands between the predicate check and the wait is not lost,
 * because the mutex holds the signaller out until the waiter is queued. */
TEST(cat_cond, a_signal_in_the_gap_is_not_lost)
{
    struct shared_s {
        cat_mutex_t guard;
        cat_cond_t cond;
        int flag;
        int rounds;
        int seen;
    } shared = {};

    ASSERT_TRUE(cat_mutex_init(&shared.guard));
    ASSERT_TRUE(cat_cond_init(&shared.cond));

    shared.rounds = 50;

    co([&] {
        for (int round = 0; round < shared.rounds; round++) {
            ASSERT_TRUE(cat_mutex_lock(&shared.guard, TEST_IO_TIMEOUT));
            /* the loop around the wait is the whole point: the predicate is
             * re-checked under the mutex after every wakeup */
            while (shared.flag == 0) {
                (void) cat_cond_wait(&shared.cond, &shared.guard, TEST_IO_TIMEOUT);
            }
            shared.flag = 0;
            shared.seen++;
            cat_mutex_unlock(&shared.guard);
        }
    });

    co([&] {
        for (int round = 0; round < shared.rounds; round++) {
            ASSERT_TRUE(cat_mutex_lock(&shared.guard, TEST_IO_TIMEOUT));
            shared.flag = 1;
            cat_cond_signal(&shared.cond);
            cat_mutex_unlock(&shared.guard);
        }
    });

    for (int round = 0; round < 2000 && shared.seen < shared.rounds; round++) {
        (void) cat_time_delay(1);
    }

    ASSERT_EQ(shared.seen, shared.rounds);

    cat_cond_destroy(&shared.cond);
    cat_mutex_destroy(&shared.guard);
}

/* A waiter on another thread is reached through the wakeup channel. */
TEST(cat_cond, a_waiter_on_another_thread_wakes_on_a_signal)
{
    struct cross_s {
        cat_mutex_t guard;
        cat_cond_t cond;
        cat_bool_t runtime_ok;
        cat_bool_t woken;
    } cross = {};

    ASSERT_TRUE(cat_mutex_init(&cross.guard));
    ASSERT_TRUE(cat_cond_init(&cross.cond));

    /* the waiter takes the mutex on the other thread and waits there */
    std::thread worker([&cross] {
        cross.runtime_ok = cat_runtime_init_all() && cat_run(CAT_RUN_EASY);
        if (!cross.runtime_ok) {
            return;
        }

        cat_coroutine_run(nullptr, [](cat_data_t *data) -> cat_data_t * {
            struct cross_s *self = (struct cross_s *) data;

            if (!cat_mutex_lock(&self->guard, TEST_IO_TIMEOUT)) {
                return nullptr;
            }
            self->woken = cat_cond_wait(&self->cond, &self->guard, TEST_IO_TIMEOUT);
            cat_mutex_unlock(&self->guard);
            return nullptr;
        }, &cross);

        (void) cat_stop();
        (void) cat_runtime_shutdown_all();
        (void) cat_runtime_close_all();
    });

    /* let the other thread reach its wait */
    while (!cat_thread_wait_is_parked(&cross.cond.wait)) {
        (void) cat_time_delay(1);
    }

    ASSERT_TRUE(cat_mutex_lock(&cross.guard, TEST_IO_TIMEOUT));
    cat_cond_signal(&cross.cond);
    cat_mutex_unlock(&cross.guard);

    worker.join();

    ASSERT_TRUE(cross.runtime_ok);
    ASSERT_TRUE(cross.woken);

    cat_cond_destroy(&cross.cond);
    cat_mutex_destroy(&cross.guard);
}


/* ==========================================================================
 * Corner cases: a release that races a waiter must not be lost.
 *
 * Both primitives did the check and the park on the waiter side and the release
 * and the wakeup on the other side without a shared lock, so a waiter that found
 * the resource taken and had not queued itself yet could miss the release and
 * sleep through it. A bounded timeout turns that hang into a failure here.
 * ========================================================================== */

struct race_s {
    cat_sem_t sem;
    cat_mutex_t guard;
    cat_atomic_int64_t taken;
    cat_atomic_int64_t timed_out;
    /* how many times each waiter asks */
    int rounds;
};

static void race_waiter(cat_data_t *data)
{
    struct race_s *self = (struct race_s *) data;

    for (int round = 0; round < self->rounds; round++) {
        if (cat_sem_acquire(&self->sem, 200)) {
            (void) cat_atomic_int64_fetch_add(&self->taken, 1);
        } else {
            (void) cat_atomic_int64_fetch_add(&self->timed_out, 1);
        }
    }
}

TEST(cat_thread_sync, a_post_is_not_lost_while_a_waiter_is_about_to_sleep)
{
    struct race_s race = {};
    const int waiters = 3;
    const int posts_per_waiter = 400;
    struct plain_thread_s threads[3] = {};

    ASSERT_TRUE(cat_sem_init(&race.sem, 0));
    cat_atomic_int64_init(&race.taken, 0);
    cat_atomic_int64_init(&race.timed_out, 0);
    race.rounds = posts_per_waiter;

    for (int index = 0; index < waiters; index++) {
        ASSERT_TRUE(plain_thread_start(&threads[index], race_waiter, &race));
    }

    /* the posts come from this thread, which is what opens the window between a
     * waiter's check and its park */
    for (int index = 0; index < waiters * posts_per_waiter; index++) {
        cat_sem_post(&race.sem);
    }

    for (int index = 0; index < waiters; index++) {
        plain_thread_join(&threads[index]);
    }

    ASSERT_EQ(cat_atomic_int64_load(&race.timed_out), 0);
    ASSERT_EQ(cat_atomic_int64_load(&race.taken), (int64_t) waiters * posts_per_waiter);

    cat_sem_destroy(&race.sem);
}

static void race_locker(cat_data_t *data)
{
    struct race_s *self = (struct race_s *) data;

    for (int round = 0; round < self->rounds; round++) {
        if (cat_mutex_lock(&self->guard, 200)) {
            (void) cat_atomic_int64_fetch_add(&self->taken, 1);
            if (!cat_mutex_unlock(&self->guard)) {
                (void) cat_atomic_int64_fetch_add(&self->timed_out, 1);
            }
        } else {
            (void) cat_atomic_int64_fetch_add(&self->timed_out, 1);
        }
    }
}

TEST(cat_thread_sync, a_release_is_not_lost_while_a_locker_is_about_to_sleep)
{
    struct race_s race = {};
    const int lockers = 3;
    const int rounds = 400;
    struct plain_thread_s threads[3] = {};

    ASSERT_TRUE(cat_mutex_init(&race.guard));
    cat_atomic_int64_init(&race.taken, 0);
    cat_atomic_int64_init(&race.timed_out, 0);
    race.rounds = rounds;

    for (int index = 0; index < lockers; index++) {
        ASSERT_TRUE(plain_thread_start(&threads[index], race_locker, &race));
    }

    /* this thread takes and releases the lock while the others ask for it */
    for (int index = 0; index < lockers * rounds; index++) {
        if (cat_mutex_lock(&race.guard, TEST_IO_TIMEOUT)) {
            cat_mutex_unlock(&race.guard);
        }
    }

    for (int index = 0; index < lockers; index++) {
        plain_thread_join(&threads[index]);
    }

    ASSERT_EQ(cat_atomic_int64_load(&race.timed_out), 0);
    ASSERT_EQ(cat_atomic_int64_load(&race.taken), (int64_t) lockers * rounds);

    ASSERT_TRUE(cat_mutex_destroy(&race.guard));
}

TEST(cat_thread_sync, a_wait_starts_the_scheduler_of_a_thread_that_did_not_run)
{
    struct lazy_s {
        cat_bool_t has_runtime;
        cat_bool_t acquired;
        cat_bool_t poster_ok;
        int64_t value;
    } lazy = {};

    static cat_sem_t sem;

    uv_thread_t tid;
    ASSERT_EQ(uv_thread_create(&tid, [](void *data) {
        struct lazy_s *self = (struct lazy_s *) data;
        struct plain_thread_s poster = {};

        self->has_runtime = cat_runtime_init_all();
        if (!self->has_runtime) {
            return;
        }

        if (!cat_sem_init(&sem, 0)) {
            return;
        }

        /* The poster waits before it posts, so the acquire below has to suspend,
         * which needs a scheduler this thread never started. */
        self->poster_ok = plain_thread_start(&poster, [](void *data) {
            struct lazy_s *owner = (struct lazy_s *) data;

            (void) cat_time_delay(20);
            owner->value = 42;
            cat_sem_post(&sem);
        }, self);

        if (self->poster_ok) {
            self->acquired = cat_sem_acquire(&sem, TEST_IO_TIMEOUT);
            plain_thread_join(&poster);
        }

        (void) cat_stop();
        cat_sem_destroy(&sem);
        (void) cat_runtime_shutdown_all();
        (void) cat_runtime_close_all();
    }, &lazy), 0);
    ASSERT_EQ(uv_thread_join(&tid), 0);

    ASSERT_TRUE(lazy.has_runtime);
    ASSERT_TRUE(lazy.poster_ok);
    ASSERT_TRUE(lazy.acquired);
    ASSERT_EQ(lazy.value, 42);
}


TEST(cat_cond, a_wait_lets_a_coroutine_that_waits_for_the_mutex_in)
{
    struct share_s {
        cat_mutex_t mutex;
        cat_cond_t cond;
        cat_bool_t other_took_it;
        cat_bool_t release;
    } share = {};

    ASSERT_TRUE(cat_mutex_init(&share.mutex));
    ASSERT_TRUE(cat_cond_init(&share.cond));

    co([&share] {
        ASSERT_TRUE(cat_mutex_lock(&share.mutex, TEST_IO_TIMEOUT));

        /* The waiter is already asking for the mutex when the wait releases it. */
        co([&share] {
            if (cat_mutex_lock(&share.mutex, 1000)) {
                share.other_took_it = cat_true;
                share.release = cat_true;
                cat_mutex_unlock(&share.mutex);
            }
        });

        ASSERT_TRUE(cat_cond_wait(&share.cond, &share.mutex, 1000));
        ASSERT_TRUE(cat_mutex_unlock(&share.mutex));
    });

    /* The signal comes after the other coroutine left the mutex, so the wait
     * above only returns if the release let that coroutine in. */
    co([&share] {
        while (!share.release) {
            ASSERT_TRUE(cat_time_delay(1));
        }
        ASSERT_TRUE(cat_cond_signal(&share.cond));
    });

    ASSERT_TRUE(share.other_took_it);

    cat_cond_destroy(&share.cond);
    ASSERT_TRUE(cat_mutex_destroy(&share.mutex));
}

#endif /* CAT_THREAD_SAFE */
