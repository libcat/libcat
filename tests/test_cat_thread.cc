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

#endif /* CAT_THREAD_SAFE */
