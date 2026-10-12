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

#if defined(CAT_THREAD_SAFE)

#include <thread>
#include <vector>

#if defined(CAT_USE_THREAD_KEY)
/* A thread-safe build keeps the module globals per thread.
 * A new thread owns no globals until it initializes its own runtime, and the
 * block it gets is different from the one that the main thread uses. */
TEST(cat_tsrm, globals_are_per_thread)
{
    void *main_globals = uv_key_get(&cat_globals_key);
    ASSERT_NE(main_globals, nullptr);

    /* mark the main thread globals */
    cat_bool_t original = CAT_G(show_last_error);
    CAT_G(show_last_error) = cat_true;

    void *before_init = (void *) 1;
    void *worker_globals = nullptr;
    bool sees_main_value = true;
    bool own_value_ok = false;

    std::thread worker([&] {
        before_init = uv_key_get(&cat_globals_key);
        cat_globals_runtime_init();
        worker_globals = uv_key_get(&cat_globals_key);
        /* the fresh block is zeroed, it must not carry the main thread value */
        sees_main_value = (CAT_G(show_last_error) == cat_true);
        CAT_G(show_last_error) = cat_true;
        own_value_ok = (CAT_G(show_last_error) == cat_true);
        cat_globals_runtime_close();
    });
    worker.join();

    CAT_G(show_last_error) = original;

    ASSERT_EQ(before_init, nullptr);
    ASSERT_NE(worker_globals, nullptr);
    ASSERT_NE(worker_globals, main_globals);
    ASSERT_FALSE(sees_main_value);
    ASSERT_TRUE(own_value_ok);
}

/* Re-initializing the runtime must keep the state that the other modules set up. */
TEST(cat_tsrm, runtime_reinit_keeps_module_state)
{
    cat_coroutine_t *main_coroutine = cat_coroutine_get_main();
    cat_coroutine_stack_size_t stack_size = cat_coroutine_get_default_stack_size();
    void *globals = uv_key_get(&cat_globals_key);

    ASSERT_TRUE(cat_runtime_shutdown());
    ASSERT_TRUE(cat_runtime_init());

    /* drop the runtime without touching the per-thread globals or the modules */
    ASSERT_EQ(uv_key_get(&cat_globals_key), globals);
    ASSERT_EQ(cat_coroutine_get_main(), main_coroutine);
    ASSERT_EQ(cat_coroutine_get_default_stack_size(), stack_size);
    ASSERT_NE(cat_coroutine_get_current(), nullptr);
}
#endif /* CAT_USE_THREAD_KEY */

/* A thread runs a full runtime on its own: init, event loop, coroutine, teardown.
 * The main thread keeps its runtime and is not affected. */
TEST(cat_tsrm, worker_thread_runs_own_runtime)
{
    bool ok = false;
    bool coroutine_ran = false;
    cat_coroutine_id_t worker_current_id = CAT_COROUTINE_MAX_ID;
    cat_coroutine_count_t worker_count = 0;

    std::thread worker([&] {
        ok = cat_runtime_init_all() && cat_run(CAT_RUN_EASY);
        if (!ok) {
            return;
        }
        worker_current_id = cat_coroutine_get_current_id();
        worker_count = cat_coroutine_get_count();
        cat_coroutine_run(nullptr, [](cat_data_t *data) -> cat_data_t * {
            *((bool *) data) = (cat_time_delay(1) == CAT_RET_OK);
            return nullptr;
        }, &coroutine_ran);
        ok = cat_coroutine_wait_all();
        ok = ok && cat_stop() && cat_runtime_shutdown_all() && cat_runtime_close_all();
    });
    worker.join();

    ASSERT_TRUE(ok);
    ASSERT_EQ(worker_current_id, CAT_COROUTINE_MAIN_ID);
    ASSERT_EQ(worker_count, 1);
    ASSERT_TRUE(coroutine_ran);

    /* the main thread runtime is still alive and still owns its own coroutine */
    ASSERT_NE(cat_coroutine_get_current(), nullptr);
    ASSERT_EQ(cat_coroutine_get_current_id(), CAT_COROUTINE_MAIN_ID);
}

/* The last error belongs to the thread that set it. */
TEST(cat_tsrm, last_error_is_per_thread)
{
    cat_update_last_error(CAT_ECANCELED, "main thread error");

    bool worker_ok = false;
    bool worker_started_clean = false;
    cat_errno_t worker_code = CAT_ECANCELED;
    std::string worker_message;

    std::thread worker([&] {
        if (!cat_runtime_init_all()) {
            return;
        }
        /* a fresh thread has no error yet */
        worker_started_clean = (cat_get_last_error_code() == 0);
        cat_update_last_error(CAT_ETIMEDOUT, "worker thread error");
        worker_code = cat_get_last_error_code();
        const char *message = cat_get_last_error_message();
        worker_message = message != nullptr ? message : "";
        worker_ok = cat_runtime_shutdown_all() && cat_runtime_close_all();
    });
    worker.join();

    ASSERT_TRUE(worker_ok);
    ASSERT_TRUE(worker_started_clean);
    ASSERT_EQ(worker_code, CAT_ETIMEDOUT);
    ASSERT_EQ(worker_message, "worker thread error");

    /* the main thread error is untouched */
    ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
    ASSERT_STREQ(cat_get_last_error_message(), "main thread error");
    cat_clear_last_error();
}

/* Several threads run their own event loop and coroutine runtime at the same time. */
TEST(cat_tsrm, threads_run_loops_concurrently)
{
    const unsigned int thread_count = 4;
    std::atomic<unsigned int> ready(0);
    std::atomic<unsigned int> done(0);
    std::atomic<bool> all_ok(true);
    std::vector<std::thread> workers;
    workers.reserve(thread_count);

    for (unsigned int index = 0; index < thread_count; index++) {
        workers.emplace_back([&, index] {
            if (!cat_runtime_init_all() || !cat_run(CAT_RUN_EASY)) {
                all_ok = false;
                ready++;
                return;
            }
            /* a per-thread value, it must survive on this thread only */
            CAT_G(show_last_error) = (cat_bool_t) (index & 1);
            ready++;
            /* do not start the loop before every thread has its runtime up */
            while (ready.load() < thread_count) {
                std::this_thread::yield();
            }
            bool coroutine_ran = false;
            cat_coroutine_run(nullptr, [](cat_data_t *data) -> cat_data_t * {
                *((bool *) data) = (cat_time_delay(1) == CAT_RET_OK);
                return nullptr;
            }, &coroutine_ran);
            /* the coroutine yields on the delay, read its result only after it finished */
            if (!cat_coroutine_wait_all()) {
                all_ok = false;
            }
            if (!coroutine_ran) {
                all_ok = false;
            }
            if (CAT_G(show_last_error) != (cat_bool_t) (index & 1)) {
                all_ok = false;
            }
            if (!cat_stop() || !cat_runtime_shutdown_all() || !cat_runtime_close_all()) {
                all_ok = false;
            }
            done++;
        });
    }

    /* the main thread keeps using its own runtime while the workers run */
    ASSERT_TRUE(cat_coroutine_wait_all());
    for (auto &worker : workers) {
        worker.join();
    }

    ASSERT_TRUE(all_ok.load());
    ASSERT_EQ(done.load(), thread_count);
    ASSERT_EQ(cat_coroutine_get_count(), 1);
}

#endif /* CAT_THREAD_SAFE */
