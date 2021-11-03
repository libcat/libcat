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
  |         codinghuang <2812240764@qq.com>                                  |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

#if 0
TEST(cat_coroutine, stackoverflow)
{
    co([] {
        std::function<void(void)> fn;
        fn = [&fn] {
            char buffer[CAT_BUFFER_DEFAULT_SIZE];
            memset(buffer, 0, sizeof(buffer));
            fn();
        };
        fn();
    });
}
#endif

TEST(cat_coroutine, set_default_stack_zero_size)
{
    cat_coroutine_stack_size_t original_size = CAT_COROUTINE_G(default_stack_size);
    cat_coroutine_stack_size_t new_size = 0;
    cat_coroutine_stack_size_t size;

    /* do nothing and return origin stack size */
    size = cat_coroutine_set_default_stack_size(new_size);
    ASSERT_EQ(original_size, size);
    ASSERT_EQ(original_size, CAT_COROUTINE_G(default_stack_size));

    /* register origin stack size */
    cat_coroutine_set_default_stack_size(original_size);
}

TEST(cat_coroutine, set_default_stack_lt_min_stack_size)
{
    cat_coroutine_stack_size_t original_size = CAT_COROUTINE_G(default_stack_size);
    cat_coroutine_stack_size_t new_size = CAT_COROUTINE_MIN_STACK_SIZE - 1;
    cat_coroutine_stack_size_t size;

    /* set to coroutine min stack size and return origin stack size */
    size = cat_coroutine_set_default_stack_size(new_size);
    ASSERT_EQ(original_size, size);
    ASSERT_EQ(CAT_COROUTINE_MIN_STACK_SIZE, CAT_COROUTINE_G(default_stack_size));

    /* register origin stack size */
    cat_coroutine_set_default_stack_size(original_size);
}

TEST(cat_coroutine, set_default_stack_gt_max_stack_size)
{
    cat_coroutine_stack_size_t original_size = CAT_COROUTINE_G(default_stack_size);
    cat_coroutine_stack_size_t new_size = CAT_COROUTINE_MAX_STACK_SIZE + 1;
    cat_coroutine_stack_size_t size;

    /* set to coroutine max stack size and return origin stack size */
    size = cat_coroutine_set_default_stack_size(new_size);
    ASSERT_EQ(original_size, size);
    ASSERT_EQ(CAT_COROUTINE_MAX_STACK_SIZE, CAT_COROUTINE_G(default_stack_size));

    /* register origin stack size */
    cat_coroutine_set_default_stack_size(original_size);
}

TEST(cat_coroutine, set_default_stack_size_double)
{
    cat_coroutine_stack_size_t original_size = CAT_COROUTINE_G(default_stack_size);
    cat_coroutine_stack_size_t new_size = original_size * 2;
    cat_coroutine_stack_size_t size;

    /* set new stack size and return origin stack size */
    size = cat_coroutine_set_default_stack_size(new_size);
    ASSERT_EQ(original_size, size);
    ASSERT_EQ(new_size, CAT_COROUTINE_G(default_stack_size));

    /* register origin stack size */
    cat_coroutine_set_default_stack_size(original_size);
}

TEST(cat_coroutine, get_default_stack_size_in_main)
{
    cat_coroutine_stack_size_t size;

    size = cat_coroutine_get_default_stack_size();
    ASSERT_EQ(CAT_COROUTINE_G(default_stack_size), size);
}

TEST(cat_coroutine, get_default_stack_size_not_in_main)
{
    cat_coroutine_run(nullptr, [](cat_data_t *data)->cat_data_t* {
        cat_coroutine_stack_size_t size;
        size = cat_coroutine_get_default_stack_size();
        EXPECT_EQ(CAT_COROUTINE_G(default_stack_size), size);
        return nullptr;
    }, nullptr);
}

TEST(cat_coroutine, get_current_in_main)
{
    cat_coroutine_t *coroutine;

    coroutine = cat_coroutine_get_current();
    ASSERT_EQ(CAT_COROUTINE_G(current), coroutine);
}

TEST(cat_coroutine, get_current_not_in_main)
{
    cat_coroutine_t *coroutine = cat_coroutine_create(nullptr, [](cat_data_t *data)->cat_data_t* {
        cat_coroutine_t *coroutine = (cat_coroutine_t *) data;
        EXPECT_EQ(coroutine, cat_coroutine_get_current());
        return nullptr;
    });

    ASSERT_TRUE(cat_coroutine_resume(coroutine, coroutine, nullptr));
}

TEST(cat_coroutine, get_current_id_in_main)
{
    cat_coroutine_id_t cid;

    cid = cat_coroutine_get_current_id();
    ASSERT_EQ(CAT_COROUTINE_MAIN_ID, cid);
}

TEST(cat_coroutine, get_current_id_not_in_main)
{
    cat_coroutine_run(nullptr, [](cat_data_t *data)->cat_data_t* {
        cat_coroutine_id_t cid;
        cid = cat_coroutine_get_current_id();
        EXPECT_GT(cid, CAT_COROUTINE_MAIN_ID);
        return nullptr;
    }, nullptr);
}

TEST(cat_coroutine, get_main_in_main)
{
    cat_coroutine_t *main;

    main = cat_coroutine_get_main();
    ASSERT_EQ(CAT_COROUTINE_G(main), main);
}

TEST(cat_coroutine, get_main_not_in_main)
{
    cat_coroutine_run(nullptr, [](cat_data_t *data)->cat_data_t* {
        cat_coroutine_t *main;
        main = cat_coroutine_get_main();
        EXPECT_EQ(CAT_COROUTINE_G(main), main);
        return nullptr;
    }, nullptr);
}

TEST(cat_coroutine, get_scheduler_in_main)
{
    cat_coroutine_t *scheduler;
    scheduler = cat_coroutine_get_scheduler();
    ASSERT_EQ(CAT_COROUTINE_G(scheduler), scheduler);
}

TEST(cat_coroutine, get_scheduler_not_in_main)
{
    cat_coroutine_run(nullptr, [](cat_data_t *data)->cat_data_t* {
        cat_coroutine_t *scheduler;

        scheduler = cat_coroutine_get_scheduler();
        EXPECT_EQ(CAT_COROUTINE_G(scheduler), scheduler);
        return nullptr;
    }, nullptr);
}

TEST(cat_coroutine, get_last_id_in_main)
{
    cat_coroutine_id_t last_id;

    last_id = cat_coroutine_get_last_id();
    ASSERT_EQ(CAT_COROUTINE_G(last_id), last_id);
}

TEST(cat_coroutine, get_last_id_not_in_main)
{
    cat_coroutine_run(nullptr, [](cat_data_t *data)->cat_data_t* {
        cat_coroutine_id_t last_id;
        last_id = cat_coroutine_get_last_id();
        EXPECT_EQ(CAT_COROUTINE_G(last_id), last_id);
        return nullptr;
    }, nullptr);
}

TEST(cat_coroutine, get_count_in_main)
{
    cat_coroutine_count_t count;
    count = cat_coroutine_get_count();
    ASSERT_EQ(CAT_COROUTINE_G(count), count);
}

TEST(cat_coroutine, get_count_not_in_main)
{
    cat_coroutine_run(nullptr, [](cat_data_t *data)->cat_data_t* {
        cat_coroutine_count_t count;
        count = cat_coroutine_get_count();
        EXPECT_EQ(CAT_COROUTINE_G(count), count);
        return nullptr;
    }, nullptr);
}

TEST(cat_coroutine, get_real_count)
{
    SKIP_IF(cat_coroutine_get_scheduler() == nullptr);
    ASSERT_EQ(cat_coroutine_get_real_count(), cat_coroutine_get_count() + 1);
}

TEST(cat_coroutine, get_peak_count_in_main)
{
    cat_coroutine_count_t peak_count;
    peak_count = cat_coroutine_get_peak_count();
    ASSERT_EQ(CAT_COROUTINE_G(peak_count), peak_count);
}

TEST(cat_coroutine, get_peak_count_not_in_main)
{
    cat_coroutine_run(nullptr, [](cat_data_t *data)->cat_data_t* {
        cat_coroutine_count_t peak_count;
        peak_count = cat_coroutine_get_peak_count();
        EXPECT_EQ(CAT_COROUTINE_G(peak_count), peak_count);
        return nullptr;
    }, nullptr);
}

TEST(cat_coroutine, get_round_in_main)
{
    ASSERT_EQ(CAT_COROUTINE_G(round), cat_coroutine_get_current_round());
}

TEST(cat_coroutine, get_round_not_in_main)
{
    co([] {
        EXPECT_EQ(CAT_COROUTINE_G(round),  cat_coroutine_get_current_round());
    });
}

TEST(cat_coroutine, get_round)
{
    cat_coroutine_round_t round = cat_coroutine_get_current_round();

    ASSERT_EQ(round, cat_coroutine_get_round(cat_coroutine_get_current()));

    co([=] {
        ASSERT_EQ(round + 1, cat_coroutine_get_round(cat_coroutine_get_current()));
    });

    ASSERT_EQ(round + 2, cat_coroutine_get_round(cat_coroutine_get_current()));
}

TEST(cat_coroutine, get_opcode)
{
    cat_coroutine_t *coroutine = cat_coroutine_get_current();

    ASSERT_EQ(cat_coroutine_get_opcodes(coroutine), coroutine->opcodes);

    /* make lcov happy */
    cat_coroutine_set_opcodes(coroutine, cat_coroutine_get_opcodes(coroutine));
}

TEST(cat_coroutine, get_id_main)
{
    cat_coroutine_id_t id;

    id = cat_coroutine_get_id(cat_coroutine_get_main());
    ASSERT_EQ(CAT_COROUTINE_MAIN_ID, id);
}

TEST(cat_coroutine, get_id_not_main)
{
    cat_coroutine_run(nullptr, [](cat_data_t *data)->cat_data_t* {
        cat_coroutine_id_t id;
        id = cat_coroutine_get_id(cat_coroutine_get_current());
        EXPECT_GE(id, 2);
        return nullptr;
    }, nullptr);
}

TEST(cat_coroutine, get_from)
{
    cat_coroutine_t *from;

    from = cat_coroutine_get_from(cat_coroutine_get_main());
    ASSERT_EQ(cat_coroutine_get_main()->from, from);
}

TEST(cat_coroutine, get_previous)
{
    cat_coroutine_t *previous;

    previous = cat_coroutine_get_previous(cat_coroutine_get_main());
    ASSERT_EQ(cat_coroutine_get_main()->previous, previous);
}

TEST(cat_coroutine, get_stack_size)
{
    cat_coroutine_stack_size_t stack_size;

    stack_size = cat_coroutine_get_stack_size(cat_coroutine_get_main());
    ASSERT_EQ(cat_coroutine_get_main()->stack_size, stack_size);
}

TEST(cat_coroutine, state_name)
{
    ASSERT_STREQ("none", cat_coroutine_state_name(CAT_COROUTINE_STATE_NONE));
    ASSERT_STREQ("running", cat_coroutine_state_name(CAT_COROUTINE_STATE_RUNNING));
    ASSERT_STREQ("waiting", cat_coroutine_state_name(CAT_COROUTINE_STATE_WAITING));
    ASSERT_STREQ("dead", cat_coroutine_state_name(CAT_COROUTINE_STATE_DEAD));
}

TEST(cat_coroutine, get_state_and_state_name)
{
    cat_coroutine_t coroutine;
    coroutine.state = CAT_COROUTINE_STATE_NONE;
    ASSERT_EQ(CAT_COROUTINE_STATE_NONE, cat_coroutine_get_state(&coroutine));
    ASSERT_STREQ("none", cat_coroutine_get_state_name(&coroutine));
    ASSERT_EQ(cat_coroutine_create(&coroutine, [](cat_data_t *data)->cat_data_t* {
        EXPECT_EQ(CAT_COROUTINE_STATE_RUNNING, cat_coroutine_get_state(cat_coroutine_get_current()));
        EXPECT_STREQ("running", cat_coroutine_get_state_name(cat_coroutine_get_current()));
        EXPECT_TRUE(cat_coroutine_yield(nullptr, nullptr));
        return nullptr;
    }), &coroutine);
    ASSERT_EQ(CAT_COROUTINE_STATE_WAITING, cat_coroutine_get_state(&coroutine));
    ASSERT_STREQ("waiting", cat_coroutine_get_state_name(&coroutine));
    ASSERT_TRUE(cat_coroutine_resume(&coroutine, nullptr, nullptr));
    ASSERT_EQ(CAT_COROUTINE_STATE_WAITING, cat_coroutine_get_state(&coroutine));
    ASSERT_STREQ("waiting", cat_coroutine_get_state_name(&coroutine));
    ASSERT_TRUE(cat_coroutine_resume(&coroutine, nullptr, nullptr));
    ASSERT_EQ(CAT_COROUTINE_STATE_DEAD, cat_coroutine_get_state(&coroutine));
    ASSERT_STREQ("dead", cat_coroutine_get_state_name(&coroutine));
}

TEST(cat_coroutine, get_start_time)
{
    cat_msec_t time = cat_time_msec();

    cat_coroutine_run(nullptr, [](cat_data_t *data)->cat_data_t* {
        cat_msec_t time = *(cat_msec_t *) data;
        EXPECT_GE(cat_coroutine_get_start_time(cat_coroutine_get_current()), time);
        return nullptr;
    }, &time);
}

TEST(cat_coroutine, get_elapsed)
{
    ASSERT_GT(cat_coroutine_get_elapsed(cat_coroutine_get_current()), 0);
}

TEST(cat_coroutine, get_elapsed_as_string)
{
    char *elapsed = cat_coroutine_get_elapsed_as_string(cat_coroutine_get_current());

    ASSERT_NE(elapsed, nullptr);

    cat_free(elapsed);
}

TEST(cat_coroutine, get_elapsed_zero)
{
    cat_coroutine_t coroutine;
    char *elapsed;

    ASSERT_NE(
        cat_coroutine_create(
            &coroutine, [](cat_data_t *data)->cat_data_t* {
                return nullptr;
            }
        ), nullptr
    );
    DEFER(ASSERT_TRUE(cat_coroutine_close(&coroutine)));

    ASSERT_EQ(cat_coroutine_get_elapsed(&coroutine), 0);
    elapsed = cat_coroutine_get_elapsed_as_string(&coroutine);
    ASSERT_EQ(std::string(elapsed), std::string("0ms"));
    cat_free(elapsed);
}

TEST(cat_coroutine, get_elapsed_without_start)
{
    cat_coroutine_t *coroutine = cat_coroutine_create(nullptr, [](cat_data_t *data)->cat_data_t* {
        return nullptr;
    });
    DEFER(ASSERT_TRUE(cat_coroutine_close(coroutine)));

    ASSERT_EQ(0, cat_coroutine_get_elapsed(coroutine));
}

TEST(cat_coroutine, scheduler_run_duplicated)
{
    ASSERT_NE(cat_coroutine_get_scheduler(), nullptr);
    ASSERT_EQ(cat_coroutine_scheduler_run(nullptr, nullptr), nullptr);
    ASSERT_EQ(CAT_EMISUSE, cat_get_last_error_code());
}

TEST(cat_coroutine, scheduler_stop_duplicated)
{
    cat_coroutine_t *origin_scheduler = CAT_COROUTINE_G(scheduler);
    CAT_COROUTINE_G(scheduler) = nullptr;
    DEFER(CAT_COROUTINE_G(scheduler) = origin_scheduler);

    ASSERT_EQ(cat_coroutine_scheduler_close(), nullptr);
    ASSERT_EQ(CAT_EMISUSE, cat_get_last_error_code());
    ASSERT_STREQ("No scheduler is available", cat_get_last_error_message());
}

TEST(cat_coroutine, wait_for)
{
    cat_coroutine_t *new_coroutine;

    new_coroutine = cat_coroutine_create(nullptr, [](cat_data_t *data)->cat_data_t* {
        cat_coroutine_t *from_coroutine = (cat_coroutine_t *) data;

        /* switch to from_coroutine */
        EXPECT_TRUE(cat_time_delay(0));

        cat_coroutine_resume(from_coroutine, nullptr, nullptr);

        return nullptr;
    });

    ASSERT_TRUE(cat_coroutine_resume(new_coroutine, cat_coroutine_get_current(), nullptr));

    /* wait for new_coroutine to resume itself */
    ASSERT_TRUE(cat_coroutine_wait_for(new_coroutine));
}

TEST(cat_coroutine, unlock_unlocked_coroutine)
{
    cat_coroutine_t *new_coroutine;

    new_coroutine = cat_coroutine_create(nullptr, [](cat_data_t *data)->cat_data_t* {
        return nullptr;
    });
    DEFER(ASSERT_TRUE(cat_coroutine_close(new_coroutine)));

    ASSERT_FALSE(cat_coroutine_unlock(new_coroutine));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

TEST(cat_coroutine, register_resume)
{
    cat_coroutine_resume_t origin_resume = cat_coroutine_resume;
    cat_coroutine_resume_t new_resume = nullptr;
    cat_coroutine_resume_t resume;

    /* set new resume function and return origin resume function */
    resume = cat_coroutine_register_resume(new_resume);
    ASSERT_EQ(origin_resume, resume);
    ASSERT_EQ(nullptr, cat_coroutine_resume);

    /* register origin resume function */
    cat_coroutine_register_resume(origin_resume);
}

TEST(cat_coroutine, register_main)
{
    cat_coroutine_t *original_main = cat_coroutine_get_main();
    cat_coroutine_t new_main = { };
    cat_coroutine_t *ret_coroutine;

    ret_coroutine = cat_coroutine_register_main(&new_main);
    ASSERT_EQ(original_main, ret_coroutine);
    ASSERT_EQ(&new_main, cat_coroutine_get_main());

    /* register origin main coroutine */
    cat_coroutine_register_main(original_main);
}

TEST(cat_coroutine, data)
{
    cat_coroutine_t *coroutine = cat_coroutine_create(nullptr, [](cat_data_t *data)->cat_data_t* {
        int n = 0;
        for (; n < 10; n++) {
            EXPECT_TRUE(cat_coroutine_yield((cat_data_t *) (intptr_t) n, nullptr));
        }
        return (cat_data_t *) (intptr_t) -1;
    });
    int n = 0;
    while (true) {
        cat_data_t *data;
        EXPECT_TRUE(cat_coroutine_resume(coroutine, nullptr, &data));
        if (((int) (intptr_t) data) == -1) {
            break;
        }
        EXPECT_EQ((int) (intptr_t) data, n++);
    }
}

TEST(cat_coroutine, stacked)
{
    cat_coroutine_t coroutine;
    cat_coroutine_create(&coroutine, [](cat_data_t *data)->cat_data_t* {
        return (cat_data_t *) "OK";
    });
    cat_data_t *data;
    ASSERT_TRUE(cat_coroutine_resume(&coroutine, nullptr, &data));
    ASSERT_STREQ("OK", (const char *) data);
    ASSERT_EQ(coroutine.state, CAT_COROUTINE_STATE_DEAD);
}

TEST(cat_coroutine, is_x)
{
    cat_coroutine_t coroutine;
    cat_coroutine_create(&coroutine, [](cat_data_t *data)->cat_data_t* {
        EXPECT_TRUE(cat_coroutine_yield(nullptr, nullptr));
        return nullptr;
    });

    ASSERT_TRUE(cat_coroutine_is_available(&coroutine));
    ASSERT_FALSE(cat_coroutine_is_alive(&coroutine));
    ASSERT_FALSE(cat_coroutine_is_over(&coroutine));

    ASSERT_TRUE(cat_coroutine_resume(&coroutine, nullptr, nullptr));
    ASSERT_TRUE(cat_coroutine_is_available(&coroutine));
    ASSERT_TRUE(cat_coroutine_is_alive(&coroutine));
    ASSERT_FALSE(cat_coroutine_is_over(&coroutine));

    ASSERT_TRUE(cat_coroutine_resume(&coroutine, nullptr, nullptr));
    ASSERT_FALSE(cat_coroutine_is_available(&coroutine));
    ASSERT_TRUE(cat_coroutine_is_over(&coroutine));
}

TEST(cat_coroutine, resume_current)
{
    ASSERT_FALSE(cat_coroutine_resume(cat_coroutine_get_current(), nullptr, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBUSY);
}

TEST(cat_coroutine, resume_locked)
{
    cat_coroutine_t *coroutine = co([] {
        cat_coroutine_lock();
    });

    ASSERT_FALSE(cat_coroutine_resume(coroutine, nullptr, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ELOCKED);
    ASSERT_TRUE(cat_coroutine_unlock(coroutine));
}

TEST(cat_coroutine, resume_dead)
{
    cat_coroutine_t coroutine;
    cat_coroutine_create(&coroutine, [](cat_data_t *data)->cat_data_t* { return nullptr; });
    ASSERT_TRUE(cat_coroutine_resume(&coroutine, nullptr, nullptr));
    ASSERT_EQ(coroutine.state, CAT_COROUTINE_STATE_DEAD);
    ASSERT_FALSE(cat_coroutine_resume(&coroutine, nullptr, nullptr));
}

TEST(cat_coroutine, resume_running)
{
    co([] {
        co([] {
            cat_coroutine_t *coroutine = cat_coroutine_get_current();
            ASSERT_FALSE(cat_coroutine_resume(
                cat_coroutine_get_previous(
                    cat_coroutine_get_previous(coroutine)
                ), nullptr, nullptr
            ));
            ASSERT_EQ(cat_get_last_error_code(), CAT_EBUSY);
        });
    });
}

TEST(cat_coroutine, resume_scheduler)
{
    ASSERT_FALSE(cat_coroutine_resume(cat_coroutine_get_scheduler(), nullptr, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBUSY);
}

TEST(cat_coroutine, wait)
{
    cat_coroutine_t *coroutine = cat_coroutine_get_current(), *waiter;

    waiter = co([=] {
        ASSERT_TRUE(cat_coroutine_wait_for(coroutine));
    });

    co([=] {
        ASSERT_FALSE(cat_coroutine_resume(waiter, nullptr, nullptr));
        ASSERT_EQ(cat_get_last_error_code(), CAT_EAGAIN);
    });

    ASSERT_TRUE(cat_coroutine_resume(waiter, nullptr, nullptr));
}

TEST(cat_coroutine, dead_lock)
{
    cat_log_type_t original_dead_lock_log_type = cat_coroutine_get_dead_lock_log_type();
    cat_coroutine_set_dead_lock_log_type(CAT_LOG_TYPE_ERROR);

    ASSERT_DEATH_IF_SUPPORTED(cat_coroutine_yield(nullptr, nullptr), "Dead lock");

    cat_coroutine_set_dead_lock_log_type(original_dead_lock_log_type);
}

TEST(cat_coroutine, get_by_index)
{
    cat_event_wait_all();
    ASSERT_EQ(cat_coroutine_get_by_index((cat_coroutine_count_t) -1), nullptr);
    cat_coroutine_t *coroutine0 = cat_coroutine_get_previous(cat_coroutine_get_current());
    if (coroutine0 != nullptr) {
        co([=] {
            ASSERT_EQ(cat_coroutine_get_by_index(0), coroutine0);
        });
    }
    cat_coroutine_t *coroutine1 = cat_coroutine_get_current();
    co([=] {
        ASSERT_EQ(cat_coroutine_get_by_index(coroutine0 ? 1 : 0), coroutine1);
    });
}

TEST(cat_coroutine, get_root)
{
    SKIP_IF(cat_coroutine_get_scheduler() == nullptr);
    ASSERT_EQ(cat_coroutine_get_root(), cat_coroutine_get_scheduler());
}

TEST(cat_coroutine, no_where_to_go)
{
    defer([] {
        ASSERT_FALSE(cat_coroutine_yield(nullptr, nullptr));
        ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);
    });
}

TEST(cat_coroutine, get_role_name)
{
    defer([] {
        ASSERT_STREQ(cat_coroutine_get_current_role_name(), "scheduler");
    });
    ASSERT_STREQ(cat_coroutine_get_current_role_name(), "main");

    cat_coroutine_t *current_coroutine = cat_coroutine_get_current();
    CAT_COROUTINE_G(current) = nullptr;
    ASSERT_STREQ(cat_coroutine_get_current_role_name(), "main");
    CAT_COROUTINE_G(current) = current_coroutine;
}
