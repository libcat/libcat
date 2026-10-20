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
 */

#include "test.h"

#include "cat_thread.h"

#ifdef CAT_THREAD_SAFE

#include <thread>

/*
 * A thread keeps a pointer to the object it registers, so the storage of every
 * registration below outlives the thread. Each case owns its own storage.
 *
 * A thread body writes what it observed and the case asserts after the join,
 * because a fatal assertion inside a thread does not stop that thread and its
 * output interleaves with the rest.
 */

TEST(cat_thread, the_main_thread_has_an_object)
{
    cat_thread_t *thread = cat_thread_get_current();
    ASSERT_NE(thread, nullptr);

    ASSERT_TRUE(cat_thread_is_main(thread));
    ASSERT_TRUE(cat_thread_is_running(thread));
    ASSERT_EQ(cat_thread_get_parent(thread), nullptr);
    ASSERT_TRUE(cat_thread_equal(thread, cat_thread_get_current()));

    cat_thread_id_t id = cat_thread_get_id(thread);
    cat_thread_id_t current_id = cat_thread_get_current_id();
    ASSERT_TRUE(uv_thread_equal(&id, &current_id));
}

TEST(cat_thread, the_main_thread_registers_again_silently)
{
    cat_thread_t *thread = cat_thread_get_current();

    /* the same object asks again, which a module may do without checking */
    ASSERT_TRUE(cat_thread_register_main(thread));
    ASSERT_EQ(cat_thread_get_current(), thread);
    ASSERT_TRUE(cat_thread_is_main(thread));
}

TEST(cat_thread, a_second_root_is_rejected)
{
    cat_thread_t other = {};

    /* the process already has a root, so this declaration is a misuse */
    ASSERT_FALSE(cat_thread_register_main(&other));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);
    ASSERT_TRUE(cat_thread_is_main(cat_thread_get_current()));
}

TEST(cat_thread, a_null_object_is_rejected)
{
    ASSERT_FALSE(cat_thread_register_main(nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_FALSE(cat_thread_register_external(nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

TEST(cat_thread, another_thread_can_not_claim_the_root)
{
    static cat_thread_t child_storage;
    cat_thread_t *main_thread = cat_thread_get_current();

    struct claim_s {
        cat_thread_t *root;
        cat_bool_t register_rejected;
        cat_bool_t root_still_main;
        cat_bool_t root_id_intact;
    } claim = {};

    /* a host hands the object of one thread to another, so this case does the
     * same and the child tries to declare that object as its own root */
    claim.root = cat_thread_get_current();

    uv_thread_t tid;
    ASSERT_EQ(uv_thread_create(&tid, [](void *data) {
        struct claim_s *self = (struct claim_s *) data;
        if (!cat_runtime_init()) {
            return;
        }
        cat_thread_id_t root_id = cat_thread_get_id(self->root);
        self->register_rejected = !cat_thread_register_main(self->root);
        self->root_still_main = cat_thread_is_main(self->root);
        cat_thread_id_t after_id = cat_thread_get_id(self->root);
        self->root_id_intact = uv_thread_equal(&root_id, &after_id);
        (void) cat_thread_register_external(&child_storage);
        (void) cat_thread_runtime_shutdown();
        (void) cat_runtime_close();
    }, &claim), 0);
    ASSERT_EQ(uv_thread_join(&tid), 0);

    ASSERT_TRUE(claim.register_rejected);
    ASSERT_TRUE(claim.root_still_main);
    ASSERT_TRUE(claim.root_id_intact);
    ASSERT_EQ(main_thread, cat_thread_get_current());
}

TEST(cat_thread, a_second_object_for_one_thread_is_rejected)
{
    static cat_thread_t first;
    static cat_thread_t second;

    /* a pointer that no registration would ever write, so the case can tell
     * whether the rejected object was left alone */
    cat_thread_t *const sentinel = (cat_thread_t *) &second.name;

    struct replace_s {
        cat_bool_t first_ok;
        cat_bool_t second_rejected;
        cat_bool_t still_first;
        cat_thread_t *parent_after;
        cat_thread_id_t id_after;
    } replace = {};

    uv_thread_t tid;
    ASSERT_EQ(uv_thread_create(&tid, [](void *data) {
        struct replace_s *self = (struct replace_s *) data;
        if (!cat_runtime_init()) {
            return;
        }
        self->first_ok = cat_thread_register_external(&first);
        /* mark the second object, then ask for it while the first one holds
         * this thread */
        memset(&second, 0, sizeof(second));
        second.parent = (cat_thread_t *) &second.name;
        /* one thread holds one object, so the second one must not replace it */
        self->second_rejected = !cat_thread_register_external(&second);
        self->still_first = cat_thread_get_current() == &first;
        self->parent_after = second.parent;
        self->id_after = second.id;
        (void) cat_thread_runtime_shutdown();
        (void) cat_runtime_close();
    }, &replace), 0);
    ASSERT_EQ(uv_thread_join(&tid), 0);

    ASSERT_TRUE(replace.first_ok);
    ASSERT_TRUE(replace.second_rejected);
    ASSERT_TRUE(replace.still_first);
    /* a rejected object stays untouched, marks included */
    ASSERT_EQ(replace.parent_after, sentinel);
    ASSERT_EQ(replace.id_after, (cat_thread_id_t) 0);
    ASSERT_EQ(second.parent, (cat_thread_t *) &second.name);
}

/* TODO: open this when the contract of destroy is settled */
#if 0
TEST(cat_thread, a_thread_object_outlives_a_destroy_call)
{
    static cat_thread_t child_storage;
    cat_thread_t *main_thread = cat_thread_get_current();

    struct destroy_s {
        cat_atomic_uint32_t ready;
        cat_atomic_uint32_t go;
        cat_bool_t still_usable;
    } destroy = {};

    uv_thread_t tid;
    ASSERT_EQ(uv_thread_create(&tid, [](void *data) {
        struct destroy_s *self = (struct destroy_s *) data;
        if (!cat_runtime_init()) {
            return;
        }
        if (!cat_thread_register_external(&child_storage)) {
            return;
        }
        cat_atomic_uint32_store(&self->ready, 1);
        /* the case calls destroy while this thread is still running, and the
         * object must survive it */
        while (cat_atomic_uint32_load(&self->go) == 0) {
            uv_sleep(1);
        }
        self->still_usable = cat_thread_is_running(&child_storage)
            && cat_thread_get_current() == &child_storage;
        (void) cat_thread_runtime_shutdown();
        (void) cat_runtime_close();
    }, &destroy), 0);

    while (cat_atomic_uint32_load(&destroy.ready) == 0) {
        uv_sleep(1);
    }
    cat_thread_destroy(&child_storage);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);

    cat_atomic_uint32_store(&destroy.go, 1);
    ASSERT_EQ(uv_thread_join(&tid), 0);

    ASSERT_TRUE(destroy.still_usable);
    ASSERT_NE(main_thread, nullptr);
}
#endif

TEST(cat_thread, an_external_thread_registers_as_a_child)
{
    /* storage that outlives the thread which registers it */
    static cat_thread_t child_storage;
    cat_thread_t *main_thread = cat_thread_get_current();
    ASSERT_NE(main_thread, nullptr);

    struct child_s {
        cat_thread_t *parent;
        cat_bool_t is_main;
        cat_bool_t second_register_ok;
        cat_bool_t has_runtime;
        uint32_t external_count;
    } child = {};

    uv_thread_t tid;
    ASSERT_EQ(uv_thread_create(&tid, [](void *data) {
        struct child_s *self = (struct child_s *) data;
        /* the entry point of a thread that libcat did not create, which is what
         * makes its thread local block */
        self->has_runtime = cat_runtime_init();
        if (!self->has_runtime) {
            return;
        }
        if (!cat_thread_register_external(&child_storage)) {
            return;
        }
        self->parent = cat_thread_get_parent(&child_storage);
        self->is_main = cat_thread_is_main(&child_storage);
        /* the same storage asks again, which is idempotent */
        self->second_register_ok = cat_thread_register_external(&child_storage);
        self->external_count = cat_thread_get_external_count();
        (void) cat_thread_runtime_shutdown();
        /* the block is the memory of this thread, so it goes away with it */
        (void) cat_runtime_close();
    }, &child), 0);
    ASSERT_EQ(uv_thread_join(&tid), 0);

    ASSERT_TRUE(child.has_runtime);
    ASSERT_EQ(child.parent, main_thread);
    ASSERT_FALSE(child.is_main);
    ASSERT_TRUE(child.second_register_ok);
    /* the count includes the root and this child, and the roots own view of it
     * is the same, because both counters belong to the process */
    ASSERT_GE(child.external_count, 2u);
}

TEST(cat_thread, the_counters_follow_the_life_of_a_thread)
{
    /* the two counters belong to the process, so this case reads them from the
     * main thread and each child leaves them exactly as it found them */
    static cat_thread_t child_storage;
    const uint32_t count_before = cat_thread_get_count();
    const uint32_t external_before = cat_thread_get_external_count();
    ASSERT_GE(count_before, 1u);
    ASSERT_GE(external_before, 1u);

    for (int round = 0; round < 3; round++) {
        struct round_s {
            cat_bool_t registered;
            uint32_t count_while_registered;
            uint32_t external_while_registered;
        } result = {};

        uv_thread_t tid;
        ASSERT_EQ(uv_thread_create(&tid, [](void *data) {
            struct round_s *self = (struct round_s *) data;
            if (!cat_runtime_init()) {
                return;
            }
            self->registered = cat_thread_register_external(&child_storage);
            /* these counters belong to the process, so this thread reads the
             * same numbers as the main thread */
            self->count_while_registered = cat_thread_get_count();
            self->external_while_registered = cat_thread_get_external_count();
            (void) cat_thread_runtime_shutdown();
            (void) cat_runtime_close();
        }, &result), 0);
        ASSERT_EQ(uv_thread_join(&tid), 0);

        ASSERT_TRUE(result.registered);
        ASSERT_EQ(result.count_while_registered, count_before + 1);
        ASSERT_EQ(result.external_while_registered, external_before + 1);
        ASSERT_EQ(cat_thread_get_count(), count_before);
        ASSERT_EQ(cat_thread_get_external_count(), external_before);
    }
}

TEST(cat_thread, a_thread_without_a_runtime_has_no_object)
{
    /* the storage of this case is never written, because the thread has no
     * block to keep it in */
    static cat_thread_t untouched;
    uv_thread_t tid;

    ASSERT_EQ(uv_thread_create(&tid, [](void *data) {
        (void) data;
        /* this thread never takes a runtime, so it has no block to keep an
         * object or an error in, and both queries report the misuse */
        ASSERT_EQ(cat_thread_get_current(), nullptr);
        ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);
        ASSERT_STREQ(cat_get_last_error_message(), "This thread has no runtime");
        /* it also has no root to attach to, which is the same misuse */
        ASSERT_FALSE(cat_thread_register_external(&untouched));
        ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);
    }, nullptr), 0);
    ASSERT_EQ(uv_thread_join(&tid), 0);

    ASSERT_EQ(untouched.id, (cat_thread_id_t) 0);
}

TEST(cat_thread, setting_a_name_keeps_it)
{
    char name[CAT_THREAD_NAME_MAX + 1];
    cat_thread_id_t self = cat_thread_get_current_id();

    /* an old platform has no thread name at all, and the call then reports it */
    if (uv_thread_setname("cat_test") == UV_ENOSYS) {
        GTEST_SKIP_("The platform has no thread name support");
    }

    ASSERT_TRUE(cat_thread_set_name("cat_test"));
    ASSERT_EQ(uv_thread_getname(&self, name, sizeof(name)), 0);
    ASSERT_STREQ(name, "cat_test");

    /* an empty name drops it, and the platform may keep the old one */
    ASSERT_TRUE(cat_thread_set_name(nullptr));
}

TEST(cat_thread, a_name_longer_than_the_limit_is_rejected)
{
    char too_long[CAT_THREAD_NAME_MAX + 2];

    memset(too_long, 'a', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';

    ASSERT_FALSE(cat_thread_set_name(too_long));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

/* An external thread follows the teardown of the design: runtime init, register,
 * use, thread runtime shutdown, runtime close. An error that is still set when
 * the teardown starts goes away with the block that holds it. */
TEST(cat_thread, an_external_thread_follows_the_documented_teardown)
{
    static cat_thread_t child_storage;

    struct teardown_s {
        cat_bool_t has_runtime;
        cat_bool_t registered;
        cat_bool_t error_was_set;
        cat_bool_t teardown_ok;
    } child = {};

    uv_thread_t tid;
    ASSERT_EQ(uv_thread_create(&tid, [](void *data) {
        struct teardown_s *self = (struct teardown_s *) data;
        self->has_runtime = cat_runtime_init();
        if (!self->has_runtime) {
            return;
        }
        self->registered = cat_thread_register_external(&child_storage);
        if (!self->registered) {
            return;
        }
        /* a call that fails builds the message of this thread */
        (void) cat_thread_get_id(NULL);
        self->error_was_set = (cat_get_last_error_code() != 0);
        self->teardown_ok = cat_thread_runtime_shutdown() && cat_runtime_close();
    }, &child), 0);
    ASSERT_EQ(uv_thread_join(&tid), 0);

    ASSERT_TRUE(child.has_runtime);
    ASSERT_TRUE(child.registered);
    ASSERT_TRUE(child.error_was_set);
    ASSERT_TRUE(child.teardown_ok);
}

/* A user thread follows the teardown of the design in the same way, and the
 * error of the thread goes away with the runtime that holds it. */
TEST(cat_thread, a_user_thread_follows_the_documented_teardown)
{
    bool ok = false;
    bool error_was_set = false;

    std::thread worker([&] {
        if (!cat_runtime_init_all()) {
            return;
        }
        cat_coroutine_run(nullptr, [](cat_data_t *data) -> cat_data_t * {
            /* a call that fails builds the message of this thread */
            (void) cat_thread_get_id(NULL);
            *((bool *) data) = (cat_get_last_error_code() != 0);
            (void) cat_stop();
            return nullptr;
        }, &error_was_set);
        ok = cat_run(CAT_RUN_EASY);
        ok = ok && cat_coroutine_wait_all() && cat_stop();
        ok = ok && cat_runtime_shutdown_all() && cat_runtime_close_all();
    });
    worker.join();

    ASSERT_TRUE(error_was_set);
    ASSERT_TRUE(ok);
}


/* ==========================================================================
 * P6 threads: real usage first.
 * ========================================================================== */

/* The shape a program writes: start a thread, let it work, join it. */
TEST(cat_thread, a_worker_runs_and_is_joined)
{
    struct work_s {
        cat_thread_t thread;
        int value;
    } work = {};

    auto routine = [](cat_data_t *data) {
        struct work_s *self = (struct work_s *) data;

        self->value = 42;
    };

    ASSERT_TRUE(cat_thread_create(&work.thread, routine, &work));
    ASSERT_TRUE(cat_thread_join(&work.thread, TEST_IO_TIMEOUT));
    ASSERT_EQ(work.value, 42);
}

/* A thread that has something to report hands it over through a semaphore,
 * which is what the library offers instead of a return value. */
TEST(cat_thread, a_worker_hands_the_result_back_through_a_semaphore)
{
    SKIP_IF_TSAN_ON_DARWIN("Mach semaphores are invisible to ThreadSanitizer");
    struct work_s {
        cat_thread_t thread;
        cat_sem_t done;
        int value;
        int received;
    } work = {};

    ASSERT_TRUE(cat_sem_init(&work.done, 0));

    auto routine = [](cat_data_t *data) {
        struct work_s *self = (struct work_s *) data;

        self->value = 7;
        cat_sem_post(&self->done);
    };

    ASSERT_TRUE(cat_thread_create(&work.thread, routine, &work));

    /* the main thread waits for the result instead of joining */
    ASSERT_TRUE(cat_sem_acquire(&work.done, TEST_IO_TIMEOUT));
    work.received = work.value;

    ASSERT_TRUE(cat_thread_join(&work.thread, TEST_IO_TIMEOUT));
    ASSERT_EQ(work.received, 7);

    cat_sem_destroy(&work.done);
}

/* Several workers, each one joined. */
TEST(cat_thread, several_workers_are_all_joined)
{
    struct work_s {
        cat_thread_t thread;
        int index;
        std::atomic<int> *counter;
    } work[3] = {};

    /* The workers increment it at the same time, so it must be atomic. */
    std::atomic<int> counter{0};

    for (int index = 0; index < 3; index++) {
        work[index].index = index;
        work[index].counter = &counter;
    }

    auto routine = [](cat_data_t *data) {
        struct work_s *self = (struct work_s *) data;

        self->counter->fetch_add(1);
    };

    for (int index = 0; index < 3; index++) {
        ASSERT_TRUE(cat_thread_create(&work[index].thread, routine, &work[index]));
    }
    for (int index = 0; index < 3; index++) {
        ASSERT_TRUE(cat_thread_join(&work[index].thread, TEST_IO_TIMEOUT));
    }

    ASSERT_EQ(counter, 3);
}

/* A worker uses the thread facilities of the library, which is the reason it
 * takes a runtime of its own. */
TEST(cat_thread, a_worker_uses_a_shared_mutex)
{
    struct work_s {
        cat_thread_t thread;
        cat_mutex_t guard;
        int *counter;
    } work = {};

    int counter = 0;

    work.counter = &counter;
    ASSERT_TRUE(cat_mutex_init(&work.guard));

    auto routine = [](cat_data_t *data) {
        struct work_s *self = (struct work_s *) data;

        for (int step = 0; step < 50; step++) {
            if (cat_mutex_lock(&self->guard, TEST_IO_TIMEOUT)) {
                (*self->counter)++;
                cat_mutex_unlock(&self->guard);
            }
        }
    };

    ASSERT_TRUE(cat_thread_create(&work.thread, routine, &work));
    ASSERT_TRUE(cat_thread_join(&work.thread, TEST_IO_TIMEOUT));

    ASSERT_EQ(counter, 50);

    cat_mutex_destroy(&work.guard);
}

/* ==========================================================================
 * P6 threads: broad smoke.
 * ========================================================================== */

/* create takes the parent from the caller and the name from the options. */
TEST(cat_thread, create_records_the_parent_and_the_name)
{
    struct work_s {
        cat_thread_t thread;
        cat_sem_t release;
    } work = {};

    ASSERT_TRUE(cat_sem_init(&work.release, 0));
    DEFER(cat_sem_destroy(&work.release));

    /* the worker must stay alive until the check below, because this test
     * looks at the state of a running thread */
    auto routine = [](cat_data_t *data) {
        struct work_s *self = (struct work_s *) data;

        (void) cat_sem_acquire(&self->release, TEST_IO_TIMEOUT);
    };

    cat_thread_options_t options = {
        .name = "worker",
        .stack_size = 0,
    };

    ASSERT_TRUE(cat_thread_create_ex(&work.thread, routine, &work, &options));

    ASSERT_EQ(cat_thread_get_parent(&work.thread), cat_thread_get_current());
    ASSERT_STREQ(work.thread.name, "worker");
    ASSERT_TRUE(cat_thread_is_running(&work.thread));

    cat_sem_post(&work.release);

    ASSERT_TRUE(cat_thread_join(&work.thread, TEST_IO_TIMEOUT));

    ASSERT_FALSE(cat_thread_is_running(&work.thread));
}

/* A thread whose routine has returned is not running any more, even before a
 * join, and joining it afterwards still works. */
TEST(cat_thread, a_finished_thread_is_not_running_before_the_join)
{
    struct work_s {
        cat_thread_t thread;
        cat_sem_t done;
    } work = {};

    ASSERT_TRUE(cat_sem_init(&work.done, 0));

    auto routine = [](cat_data_t *data) {
        struct work_s *self = (struct work_s *) data;

        cat_sem_post(&self->done);
    };

    ASSERT_TRUE(cat_thread_create(&work.thread, routine, &work));

    /* the routine returned here, the exit flag follows it a moment later */
    ASSERT_TRUE(cat_sem_acquire(&work.done, TEST_IO_TIMEOUT));

    for (int round = 0; round < 1000 && cat_thread_is_running(&work.thread); round++) {
        ASSERT_TRUE(cat_time_delay(1));
    }
    ASSERT_FALSE(cat_thread_is_running(&work.thread));

    ASSERT_TRUE(cat_thread_join(&work.thread, TEST_IO_TIMEOUT));
    ASSERT_FALSE(cat_thread_is_running(&work.thread));

    cat_sem_destroy(&work.done);
}

/* Every state a thread goes through is visible. */
TEST(cat_thread, the_state_follows_the_lifecycle)
{
    struct work_s {
        cat_thread_t thread;
        cat_sem_t started;
        cat_sem_t release;
    } work = {};

    ASSERT_TRUE(cat_sem_init(&work.started, 0));
    ASSERT_TRUE(cat_sem_init(&work.release, 0));

    auto routine = [](cat_data_t *data) {
        struct work_s *self = (struct work_s *) data;

        cat_sem_post(&self->started);
        (void) cat_sem_acquire(&self->release, TEST_IO_TIMEOUT);
    };

    ASSERT_TRUE(cat_thread_create(&work.thread, routine, &work));

    /* the routine runs only after create returns, so this proves it started */
    ASSERT_TRUE(cat_sem_acquire(&work.started, TEST_IO_TIMEOUT));
    ASSERT_TRUE(cat_thread_is_running(&work.thread));

    cat_sem_post(&work.release);
    ASSERT_TRUE(cat_thread_join(&work.thread, TEST_IO_TIMEOUT));

    cat_sem_destroy(&work.release);
    cat_sem_destroy(&work.started);
}

/* Create and join go together, repeatedly. */
TEST(cat_thread, create_and_join_repeat_cleanly)
{
    struct work_s {
        cat_thread_t thread;
        int *counter;
    } work = {};

    int counter = 0;

    work.counter = &counter;

    auto routine = [](cat_data_t *data) {
        struct work_s *self = (struct work_s *) data;

        (*self->counter)++;
    };

    for (int round = 0; round < 10; round++) {
        ASSERT_TRUE(cat_thread_create(&work.thread, routine, &work));
        ASSERT_TRUE(cat_thread_join(&work.thread, TEST_IO_TIMEOUT));
    }

    ASSERT_EQ(counter, 10);
}

/* Joining gives the thread back to the loop instead of blocking it, so another
 * coroutine of the same thread keeps running during the join. */
TEST(cat_thread, a_join_does_not_block_the_thread)
{
    struct work_s {
        cat_thread_t thread;
        cat_sem_t release;
        bool side_ran;
        bool joined;
    } work = {};

    ASSERT_TRUE(cat_sem_init(&work.release, 0));

    auto routine = [](cat_data_t *data) {
        struct work_s *self = (struct work_s *) data;

        (void) cat_sem_acquire(&self->release, TEST_IO_TIMEOUT);
    };

    ASSERT_TRUE(cat_thread_create(&work.thread, routine, &work));

    /* a coroutine that would never run if the join blocked the thread */
    co([&] {
        work.side_ran = true;
        cat_sem_post(&work.release);
    });

    work.joined = cat_thread_join(&work.thread, TEST_IO_TIMEOUT);

    ASSERT_TRUE(work.joined);
    ASSERT_TRUE(work.side_ran);

    cat_sem_destroy(&work.release);
}

/*
 * The object of a detached thread outlives the case that started it, because the
 * thread keeps using it until it is done, so the storage below is static.
 */
struct detached_s {
    cat_thread_t thread;
    cat_sem_t created;
    cat_sem_t done;
};

/*
 * Every case that detaches needs its own storage: a detached thread keeps using
 * its object after it was started, and no join tells the next case that the
 * thread is really gone. Sharing one object between two cases is a race, which
 * the thread sanitizer reports.
 */
static detached_s &detached_finishes_case(void)
{
    static detached_s work;

    return work;
}

static detached_s &detached_join_case(void)
{
    static detached_s work;

    return work;
}

/* Wait until the detached thread is really gone, because nothing joins it. */
static void wait_until_the_detached_thread_ends(cat_thread_t *thread)
{
    for (int round = 0; round < 2000 && cat_thread_is_running(thread); round++) {
        (void) cat_time_delay(1);
    }
}

/* A detached thread finishes on its own. */
TEST(cat_thread, a_detached_thread_finishes_alone)
{
    detached_s &work = detached_finishes_case();

    ASSERT_TRUE(cat_sem_init(&work.created, 0));
    ASSERT_TRUE(cat_sem_init(&work.done, 0));

    auto routine = [](cat_data_t *data) {
        struct detached_s *self = (struct detached_s *) data;

        cat_sem_post(&self->created);
        cat_sem_post(&self->done);
    };

    ASSERT_TRUE(cat_thread_create(&work.thread, routine, &work));
    ASSERT_TRUE(cat_thread_detach(&work.thread));

    ASSERT_TRUE(cat_sem_acquire(&work.done, TEST_IO_TIMEOUT));
    wait_until_the_detached_thread_ends(&work.thread);
    ASSERT_FALSE(cat_thread_is_running(&work.thread));

    cat_sem_destroy(&work.done);
    cat_sem_destroy(&work.created);
}

/* The two arguments that cannot be missing are refused. */
TEST(cat_thread, create_refuses_missing_arguments)
{
    cat_thread_t thread = {};

    ASSERT_FALSE(cat_thread_create(&thread, nullptr, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);

    ASSERT_FALSE(cat_thread_create(nullptr, [](cat_data_t *) {}, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

/* ==========================================================================
 * P6 threads: corners.
 * ========================================================================== */

/* A worker starts a worker of its own. */
TEST(cat_thread, a_worker_can_create_another_worker)
{
    struct work_s {
        cat_thread_t thread;
        cat_thread_t inner;
        int *counter;
    } work = {};

    int counter = 0;

    work.counter = &counter;

    auto outer = [](cat_data_t *data) {
        struct work_s *self = (struct work_s *) data;

        auto inner = [](cat_data_t *inner_data) {
            struct work_s *inner_self = (struct work_s *) inner_data;

            (*inner_self->counter)++;
        };

        if (cat_thread_create(&self->inner, inner, self)) {
            (void) cat_thread_join(&self->inner, TEST_IO_TIMEOUT);
        }
        (*self->counter)++;
    };

    ASSERT_TRUE(cat_thread_create(&work.thread, outer, &work));
    ASSERT_TRUE(cat_thread_join(&work.thread, TEST_IO_TIMEOUT));

    ASSERT_EQ(counter, 2);
}

/* A thread that joins itself would wait for ever, so it is refused. */
TEST(cat_thread, joining_yourself_is_refused)
{
    cat_thread_t *self = cat_thread_get_current();

    ASSERT_FALSE(cat_thread_join(self, TEST_IO_TIMEOUT));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);
}

/* The second join of the same thread is refused, because the first one already
 * took the exit. */
TEST(cat_thread, joining_twice_is_refused)
{
    struct work_s {
        cat_thread_t thread;
    } work = {};

    ASSERT_TRUE(cat_thread_create(&work.thread, [](cat_data_t *) {}, &work));
    ASSERT_TRUE(cat_thread_join(&work.thread, TEST_IO_TIMEOUT));

    ASSERT_FALSE(cat_thread_join(&work.thread, TEST_IO_TIMEOUT));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);
}

/* A join that runs out of time reports it, and the thread can still be joined
 * afterwards. */
TEST(cat_thread, a_join_times_out_and_can_be_repeated)
{
    struct work_s {
        cat_thread_t thread;
        cat_sem_t release;
    } work = {};

    ASSERT_TRUE(cat_sem_init(&work.release, 0));

    auto routine = [](cat_data_t *data) {
        struct work_s *self = (struct work_s *) data;

        (void) cat_sem_acquire(&self->release, TEST_IO_TIMEOUT);
    };

    ASSERT_TRUE(cat_thread_create(&work.thread, routine, &work));

    ASSERT_FALSE(cat_thread_join(&work.thread, 20));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);

    cat_sem_post(&work.release);
    ASSERT_TRUE(cat_thread_join(&work.thread, TEST_IO_TIMEOUT));

    cat_sem_destroy(&work.release);
}

/* A detached thread cannot be joined. */
TEST(cat_thread, joining_a_detached_thread_is_refused)
{
    detached_s &work = detached_join_case();

    ASSERT_TRUE(cat_sem_init(&work.done, 0));

    auto routine = [](cat_data_t *data) {
        struct detached_s *self = (struct detached_s *) data;

        cat_sem_post(&self->done);
    };

    ASSERT_TRUE(cat_thread_create(&work.thread, routine, &work));
    ASSERT_TRUE(cat_thread_detach(&work.thread));

    ASSERT_FALSE(cat_thread_join(&work.thread, TEST_IO_TIMEOUT));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);

    ASSERT_TRUE(cat_sem_acquire(&work.done, TEST_IO_TIMEOUT));
    wait_until_the_detached_thread_ends(&work.thread);
    ASSERT_FALSE(cat_thread_is_running(&work.thread));

    cat_sem_destroy(&work.done);
}

/* Many threads at once, each with a coroutine of its own. */
TEST(cat_thread, many_threads_run_at_once)
{
    struct work_s {
        cat_thread_t thread;
        std::atomic<int> *counter;
    } work[8] = {};

    /* The workers increment it at the same time, so it must be atomic. */
    std::atomic<int> counter{0};

    for (int index = 0; index < 8; index++) {
        work[index].counter = &counter;
    }

    auto routine = [](cat_data_t *data) {
        struct work_s *self = (struct work_s *) data;

        self->counter->fetch_add(1);
    };

    for (int index = 0; index < 8; index++) {
        ASSERT_TRUE(cat_thread_create(&work[index].thread, routine, &work[index]));
    }
    for (int index = 0; index < 8; index++) {
        ASSERT_TRUE(cat_thread_join(&work[index].thread, TEST_IO_TIMEOUT));
    }

    ASSERT_EQ(counter, 8);
}


/* ==========================================================================
 * P6 threads: a thread that declares nothing.
 *
 * The root of a runtime is the thread that runs first, so a thread that calls
 * cat_run without registering an object of its own still gets one, and a thread
 * that waits before it ran gets a scheduler instead of an error.
 * ========================================================================== */

TEST(cat_thread, a_thread_that_runs_becomes_its_own_root)
{
    struct bare_s {
        cat_bool_t has_runtime;
        cat_bool_t run;
        cat_bool_t has_current;
        cat_bool_t is_root;
        cat_bool_t id_matches;
        cat_thread_id_t id;
        cat_thread_id_t current_id;
    } bare = {};

    uv_thread_t tid;
    ASSERT_EQ(uv_thread_create(&tid, [](void *data) {
        struct bare_s *self = (struct bare_s *) data;
        cat_thread_t *current;

        /* the whole runtime, because this thread runs coroutines of its own */
        self->has_runtime = cat_runtime_init_all();
        if (!self->has_runtime) {
            return;
        }

        /* nothing is registered, so this call has to establish the root */
        self->run = cat_run(CAT_RUN_EASY);
        current = cat_thread_get_current();
        self->has_current = current != NULL;
        self->is_root = current != NULL && cat_thread_get_parent(current) == NULL;
        if (current != NULL) {
            /* An id from uv_thread_self can not be compared with the handle that
             * uv_thread_create returned on Windows, so compare two reads of the
             * running thread instead. */
            self->id = cat_thread_get_id(current);
            self->current_id = cat_thread_get_current_id();
            self->id_matches = uv_thread_equal(&self->id, &self->current_id);
        }

        (void) cat_stop();
        (void) cat_runtime_shutdown_all();
        (void) cat_runtime_close_all();
    }, &bare), 0);
    ASSERT_EQ(uv_thread_join(&tid), 0);

    ASSERT_TRUE(bare.has_runtime);
    ASSERT_TRUE(bare.run);
    ASSERT_TRUE(bare.has_current);
    ASSERT_TRUE(bare.is_root);
    ASSERT_TRUE(bare.id_matches);
}

/* A thread whose routine takes the runtime, like a host with its own per-thread
 * bootstrap. The routine inits the runtime, then registers its own object. */
TEST(cat_thread, a_manual_runtime_worker_takes_its_own_runtime)
{
    struct work_s {
        cat_thread_t thread;
        cat_sem_t done;
        cat_bool_t runtime_ok;
        cat_bool_t bootstrap_ok;
        cat_bool_t bootstrap_twice_ok;
        cat_bool_t current_ok;
        cat_bool_t coroutine_ok;
        uint32_t count;
    } work = {};

    ASSERT_TRUE(cat_sem_init(&work.done, 0));

    const uint32_t count_before = cat_thread_get_count();

    auto routine = [](cat_data_t *data) {
        struct work_s *self = (struct work_s *) data;

        self->runtime_ok = cat_runtime_init_all();
        self->bootstrap_ok = cat_thread_bootstrap(&self->thread);
        /* a second call must change nothing */
        self->bootstrap_twice_ok = cat_thread_bootstrap(&self->thread);
        self->count = cat_thread_get_count();
        self->current_ok = cat_thread_get_current() == &self->thread;
        co([&] {
            /* the runtime of this thread drives the coroutine */
            (void) cat_time_delay(1);
            self->coroutine_ok = cat_true;
        });
        (void) cat_runtime_shutdown_all();
        (void) cat_runtime_close_all();
        cat_sem_post(&self->done);
    };

    cat_thread_options_t options = {
        .name = "manual",
        .stack_size = 0,
        .flags = CAT_THREAD_CREATE_MANUAL_RUNTIME,
    };

    ASSERT_TRUE(cat_thread_create_ex(&work.thread, routine, &work, &options));
    ASSERT_TRUE(cat_sem_acquire(&work.done, TEST_IO_TIMEOUT));
    ASSERT_TRUE(cat_thread_join(&work.thread, TEST_IO_TIMEOUT));

    ASSERT_TRUE(work.runtime_ok);
    ASSERT_TRUE(work.bootstrap_ok);
    ASSERT_TRUE(work.bootstrap_twice_ok);
    ASSERT_TRUE(work.current_ok);
    ASSERT_TRUE(work.coroutine_ok);
    ASSERT_EQ(work.count, count_before + 1);
    ASSERT_EQ(cat_thread_get_count(), count_before);

    cat_sem_destroy(&work.done);
}

/* A manual runtime worker may skip the registration. It then has no current
 * object and does not count, but join still works. */
TEST(cat_thread, a_manual_runtime_worker_without_bootstrap_is_not_registered)
{
    struct work_s {
        cat_thread_t thread;
        cat_sem_t done;
        cat_bool_t has_current;
        cat_errno_t error;
    } work = {};

    ASSERT_TRUE(cat_sem_init(&work.done, 0));

    const uint32_t count_before = cat_thread_get_count();

    auto routine = [](cat_data_t *data) {
        struct work_s *self = (struct work_s *) data;

        (void) cat_runtime_init_all();
        self->has_current = cat_thread_get_current() != NULL;
        self->error = cat_get_last_error_code();
        (void) cat_runtime_shutdown_all();
        (void) cat_runtime_close_all();
        cat_sem_post(&self->done);
    };

    cat_thread_options_t options = {
        .name = "anonymous",
        .stack_size = 0,
        .flags = CAT_THREAD_CREATE_MANUAL_RUNTIME,
    };

    ASSERT_TRUE(cat_thread_create_ex(&work.thread, routine, &work, &options));
    ASSERT_TRUE(cat_sem_acquire(&work.done, TEST_IO_TIMEOUT));
    ASSERT_TRUE(cat_thread_join(&work.thread, TEST_IO_TIMEOUT));

    ASSERT_FALSE(work.has_current);
    ASSERT_EQ(work.error, CAT_EMISUSE);
    ASSERT_EQ(cat_thread_get_count(), count_before);

    cat_sem_destroy(&work.done);
}

/* Unknown create flags are refused, so a typo does not start a wrong thread. */
TEST(cat_thread, unknown_create_flags_are_refused)
{
    cat_thread_t thread;

    auto routine = [](cat_data_t *data) {
        (void) data;
    };

    cat_thread_options_t options = {
        .name = nullptr,
        .stack_size = 0,
        .flags = 0x80000000u,
    };

    ASSERT_FALSE(cat_thread_create_ex(&thread, routine, nullptr, &options));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

#endif /* CAT_THREAD_SAFE */
