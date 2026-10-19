/*
  +--------------------------------------------------------------------------+
  | libcat                                                                   |
  +--------------------------------------------------------------------------+
  | Licensed under the Apache License, Version 2.0 (the "License");          |
  | you may not use this file except in compliance with the License.         |
  | You may obtain a copy of the License at                                  |
  | http://www.apache.org/licenses/LICENSE-2.0                               |
  +--------------------------------------------------------------------------+
  | Author: Yun Dou <dixyes@gmail.com>                                       |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

#if defined(CAT_THREAD_SAFE)

#include <atomic>
#include <string>

#include "cat_thannel.h"
#include "cat_thread.h"

/* Wait for a worker to report, because a worker runs on its own thread and the
 * main thread reads its result afterwards. */
static bool wait_for(const std::atomic<bool> &flag, int milliseconds = 5000)
{
    for (int waited = 0; waited < milliseconds; waited++) {
        if (flag) {
            return true;
        }
        (void) cat_time_delay(1);
    }

    return flag;
}

/* ==========================================================================
 * Real usage: the shapes a program actually writes.
 * ========================================================================== */

TEST(cat_thannel, a_worker_hands_values_to_the_main_thread)
{
    struct hand_s {
        cat_thread_t thread;
        cat_thannel_t thannel;
        int count;
        cat_bool_t worker_ok;
    } hand = {};

    hand.count = 64;
    ASSERT_NE(cat_thannel_create(&hand.thannel, 4), nullptr);
    DEFER(cat_thannel_cleanup(&hand.thannel));

    ASSERT_TRUE(cat_thread_create(&hand.thread, [](cat_data_t *data) {
        struct hand_s *self = (struct hand_s *) data;

        self->worker_ok = cat_true;
        for (int index = 0; index < self->count; index++) {
            char value[32];
            int length = snprintf(value, sizeof(value), "value-%d", index);

            if (!cat_thannel_push(&self->thannel, value, (size_t) length + 1, TEST_IO_TIMEOUT)) {
                self->worker_ok = cat_false;
                return;
            }
        }
    }, &hand));
    DEFER(cat_thread_join(&hand.thread, TEST_IO_TIMEOUT));

    for (int index = 0; index < hand.count; index++) {
        char expected[32];
        char *value = nullptr;
        size_t size = 0;
        int length = snprintf(expected, sizeof(expected), "value-%d", index);

        ASSERT_TRUE(cat_thannel_pop(&hand.thannel, reinterpret_cast<void **>(&value), &size,
            TEST_IO_TIMEOUT));
        ASSERT_NE(value, nullptr);
        ASSERT_EQ(size, (size_t) length + 1);
        ASSERT_STREQ(value, expected);
        cat_sys_free(value);
    }

    ASSERT_TRUE(hand.worker_ok);
    ASSERT_TRUE(cat_thannel_is_empty(&hand.thannel));
}

TEST(cat_thannel, a_full_queue_holds_the_producer)
{
    struct hold_s {
        cat_thread_t thread;
        cat_thannel_t thannel;
        std::atomic<bool> first_done;
        std::atomic<bool> second_done;
        cat_bool_t second_ok;
    } hold;

    hold.first_done = false;
    hold.second_done = false;
    hold.second_ok = cat_false;

    ASSERT_NE(cat_thannel_create(&hold.thannel, 1), nullptr);
    DEFER(cat_thannel_cleanup(&hold.thannel));

    ASSERT_TRUE(cat_thread_create(&hold.thread, [](cat_data_t *data) {
        struct hold_s *self = (struct hold_s *) data;

        if (!cat_thannel_push(&self->thannel, "a", 1, TEST_IO_TIMEOUT)) {
            return;
        }
        self->first_done = true;

        self->second_ok = cat_thannel_push(&self->thannel, "b", 1, TEST_IO_TIMEOUT);
        self->second_done = true;
    }, &hold));
    DEFER(cat_thread_join(&hold.thread, TEST_IO_TIMEOUT));

    /* The queue holds one message, so the second push has no room until a pop. */
    while (!hold.first_done) {
        ASSERT_TRUE(cat_time_delay(1));
    }
    ASSERT_TRUE(cat_time_delay(30));
    ASSERT_FALSE(hold.second_done);

    void *value = nullptr;
    size_t size = 0;
    ASSERT_TRUE(cat_thannel_pop(&hold.thannel, &value, &size, TEST_IO_TIMEOUT));
    ASSERT_EQ(size, 1u);
    cat_sys_free(value);

    ASSERT_TRUE(wait_for(hold.second_done));
    ASSERT_TRUE(hold.second_ok);
}

TEST(cat_thannel, a_rendezvous_push_returns_after_the_pop)
{
    struct rendezvous_s {
        cat_thread_t thread;
        cat_thannel_t thannel;
        std::atomic<bool> push_started;
        std::atomic<bool> push_done;
        cat_bool_t push_ok;
    } rendezvous;

    rendezvous.push_started = false;
    rendezvous.push_done = false;
    rendezvous.push_ok = cat_false;

    ASSERT_NE(cat_thannel_create(&rendezvous.thannel, CAT_THANNEL_CAPACITY_UNBUFFERED), nullptr);
    DEFER(cat_thannel_cleanup(&rendezvous.thannel));

    ASSERT_TRUE(cat_thread_create(&rendezvous.thread, [](cat_data_t *data) {
        struct rendezvous_s *self = (struct rendezvous_s *) data;

        self->push_started = true;
        self->push_ok = cat_thannel_push(&self->thannel, "hand off", 9, TEST_IO_TIMEOUT);
        self->push_done = true;
    }, &rendezvous));
    DEFER(cat_thread_join(&rendezvous.thread, TEST_IO_TIMEOUT));

    while (!rendezvous.push_started) {
        ASSERT_TRUE(cat_time_delay(1));
    }
    ASSERT_TRUE(cat_time_delay(30));
    ASSERT_FALSE(rendezvous.push_done);

    void *value = nullptr;
    size_t size = 0;
    ASSERT_TRUE(cat_thannel_pop(&rendezvous.thannel, &value, &size, TEST_IO_TIMEOUT));
    ASSERT_EQ(size, 9u);
    ASSERT_EQ(memcmp(value, "hand off", 9), 0);
    cat_sys_free(value);

    ASSERT_TRUE(wait_for(rendezvous.push_done));
    ASSERT_TRUE(rendezvous.push_ok);
}

TEST(cat_thannel, a_close_wakes_the_producer_that_waits_for_room)
{
    struct wake_s {
        cat_thread_t thread;
        cat_thannel_t thannel;
        std::atomic<bool> started;
        std::atomic<bool> done;
        cat_bool_t push_ok;
        cat_errno_t error;
    } wake = {};

    wake.started = false;
    wake.done = false;

    ASSERT_NE(cat_thannel_create(&wake.thannel, 1), nullptr);
    DEFER(cat_thannel_cleanup(&wake.thannel));

    ASSERT_TRUE(cat_thannel_push(&wake.thannel, "full", 4, TEST_IO_TIMEOUT));

    ASSERT_TRUE(cat_thread_create(&wake.thread, [](cat_data_t *data) {
        struct wake_s *self = (struct wake_s *) data;

        self->started = true;
        self->push_ok = cat_thannel_push(&self->thannel, "wait", 4, TEST_IO_TIMEOUT);
        self->error = cat_get_last_error_code();
        self->done = true;
    }, &wake));
    DEFER(cat_thread_join(&wake.thread, TEST_IO_TIMEOUT));

    while (!wake.started) {
        ASSERT_TRUE(cat_time_delay(1));
    }
    ASSERT_TRUE(cat_time_delay(20));
    ASSERT_TRUE(cat_thannel_close(&wake.thannel));

    ASSERT_TRUE(wait_for(wake.done));
    ASSERT_FALSE(wake.push_ok);
    ASSERT_EQ(wake.error, CAT_ECLOSED);
}

TEST(cat_thannel, a_close_wakes_the_consumer_that_waits_for_a_message)
{
    struct wake_s {
        cat_thread_t thread;
        cat_thannel_t thannel;
        std::atomic<bool> started;
        std::atomic<bool> done;
        cat_bool_t pop_ok;
        cat_errno_t error;
    } wake = {};

    wake.started = false;
    wake.done = false;

    ASSERT_NE(cat_thannel_create(&wake.thannel, 1), nullptr);
    DEFER(cat_thannel_cleanup(&wake.thannel));

    ASSERT_TRUE(cat_thread_create(&wake.thread, [](cat_data_t *data) {
        struct wake_s *self = (struct wake_s *) data;
        void *value = nullptr;
        size_t size = 0;

        self->started = true;
        self->pop_ok = cat_thannel_pop(&self->thannel, &value, &size, TEST_IO_TIMEOUT);
        self->error = cat_get_last_error_code();
        self->done = true;
    }, &wake));
    DEFER(cat_thread_join(&wake.thread, TEST_IO_TIMEOUT));

    while (!wake.started) {
        ASSERT_TRUE(cat_time_delay(1));
    }
    ASSERT_TRUE(cat_time_delay(20));
    ASSERT_TRUE(cat_thannel_close(&wake.thannel));

    ASSERT_TRUE(wait_for(wake.done));
    ASSERT_FALSE(wake.pop_ok);
    ASSERT_EQ(wake.error, CAT_ECLOSED);
}

/* ==========================================================================
 * Broad smoke: every entry point, cheaply.
 * ========================================================================== */

TEST(cat_thannel, every_entry_point_works_on_a_buffered_queue)
{
    cat_thannel_t thannel = {};

    ASSERT_EQ(cat_thannel_create(&thannel, 4), &thannel);
    DEFER(cat_thannel_cleanup(&thannel));
    ASSERT_EQ(cat_thannel_get_capacity(&thannel), 4u);
    ASSERT_EQ(cat_thannel_get_length(&thannel), 0u);
    ASSERT_TRUE(cat_thannel_is_empty(&thannel));
    ASSERT_FALSE(cat_thannel_is_full(&thannel));
    ASSERT_FALSE(cat_thannel_is_closed(&thannel));

    for (int index = 0; index < 4; index++) {
        ASSERT_TRUE(cat_thannel_push(&thannel, &index, sizeof(index), TEST_IO_TIMEOUT));
    }
    ASSERT_EQ(cat_thannel_get_length(&thannel), 4u);
    ASSERT_FALSE(cat_thannel_is_empty(&thannel));
    ASSERT_TRUE(cat_thannel_is_full(&thannel));

    for (int index = 0; index < 4; index++) {
        void *value = nullptr;
        size_t size = 0;
        int received = -1;

        ASSERT_TRUE(cat_thannel_pop(&thannel, &value, &size, TEST_IO_TIMEOUT));
        ASSERT_EQ(size, sizeof(received));
        memcpy(&received, value, size);
        ASSERT_EQ(received, index);
        cat_sys_free(value);
    }
    ASSERT_TRUE(cat_thannel_is_empty(&thannel));

    ASSERT_TRUE(cat_thannel_close(&thannel));
    ASSERT_TRUE(cat_thannel_is_closed(&thannel));
}

/* ==========================================================================
 * Corner cases: the edges that a real program hits by accident.
 * ========================================================================== */

TEST(cat_thannel, a_value_keeps_the_bytes_it_was_given)
{
    cat_thannel_t thannel = {};
    const char value[] = {'a', '\0', 'b', (char) 0xff, 'c'};

    ASSERT_NE(cat_thannel_create(&thannel, 2), nullptr);
    DEFER(cat_thannel_cleanup(&thannel));

    ASSERT_TRUE(cat_thannel_push(&thannel, value, sizeof(value), TEST_IO_TIMEOUT));

    void *received = nullptr;
    size_t size = 0;
    ASSERT_TRUE(cat_thannel_pop(&thannel, &received, &size, TEST_IO_TIMEOUT));
    ASSERT_EQ(size, sizeof(value));
    ASSERT_EQ(memcmp(received, value, sizeof(value)), 0);
    cat_sys_free(received);
}

TEST(cat_thannel, an_empty_message_is_allowed)
{
    cat_thannel_t thannel = {};

    ASSERT_NE(cat_thannel_create(&thannel, 1), nullptr);
    DEFER(cat_thannel_cleanup(&thannel));

    ASSERT_TRUE(cat_thannel_push(&thannel, nullptr, 0, TEST_IO_TIMEOUT));

    void *received = nullptr;
    size_t size = 1;
    ASSERT_TRUE(cat_thannel_pop(&thannel, &received, &size, TEST_IO_TIMEOUT));
    ASSERT_EQ(size, 0u);
    ASSERT_EQ(received, nullptr);
}

TEST(cat_thannel, a_null_payload_needs_a_zero_size)
{
    cat_thannel_t thannel = {};

    ASSERT_NE(cat_thannel_create(&thannel, 1), nullptr);
    DEFER(cat_thannel_cleanup(&thannel));

    ASSERT_FALSE(cat_thannel_push(&thannel, nullptr, 4, TEST_IO_TIMEOUT));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_TRUE(cat_thannel_is_empty(&thannel));
}

TEST(cat_thannel, a_null_argument_is_refused)
{
    cat_thannel_t thannel = {};
    void *value = nullptr;
    size_t size = 0;

    ASSERT_EQ(cat_thannel_create(nullptr, 1), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);

    ASSERT_NE(cat_thannel_create(&thannel, 1), nullptr);
    DEFER(cat_thannel_cleanup(&thannel));

    ASSERT_FALSE(cat_thannel_pop(&thannel, nullptr, &size, TEST_IO_TIMEOUT));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_FALSE(cat_thannel_pop(&thannel, &value, nullptr, TEST_IO_TIMEOUT));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

TEST(cat_thannel, a_push_after_the_close_is_refused)
{
    cat_thannel_t thannel = {};

    ASSERT_NE(cat_thannel_create(&thannel, 2), nullptr);
    DEFER(cat_thannel_cleanup(&thannel));

    ASSERT_TRUE(cat_thannel_close(&thannel));
    ASSERT_FALSE(cat_thannel_push(&thannel, "x", 1, TEST_IO_TIMEOUT));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ECLOSED);
}

TEST(cat_thannel, a_consumer_drains_before_the_close_is_reported)
{
    cat_thannel_t thannel = {};

    ASSERT_NE(cat_thannel_create(&thannel, 4), nullptr);
    DEFER(cat_thannel_cleanup(&thannel));

    for (int index = 0; index < 3; index++) {
        ASSERT_TRUE(cat_thannel_push(&thannel, &index, sizeof(index), TEST_IO_TIMEOUT));
    }
    ASSERT_TRUE(cat_thannel_close(&thannel));

    for (int index = 0; index < 3; index++) {
        void *value = nullptr;
        size_t size = 0;
        int received = -1;

        ASSERT_TRUE(cat_thannel_pop(&thannel, &value, &size, TEST_IO_TIMEOUT));
        memcpy(&received, value, size);
        ASSERT_EQ(received, index);
        cat_sys_free(value);
    }

    void *value = nullptr;
    size_t size = 0;
    ASSERT_FALSE(cat_thannel_pop(&thannel, &value, &size, TEST_IO_TIMEOUT));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ECLOSED);
}

TEST(cat_thannel, a_wait_times_out_in_both_directions)
{
    cat_thannel_t thannel = {};

    ASSERT_NE(cat_thannel_create(&thannel, 1), nullptr);
    DEFER(cat_thannel_cleanup(&thannel));

    void *value = nullptr;
    size_t size = 0;
    ASSERT_FALSE(cat_thannel_pop(&thannel, &value, &size, 30));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);

    ASSERT_TRUE(cat_thannel_push(&thannel, "full", 4, TEST_IO_TIMEOUT));
    ASSERT_FALSE(cat_thannel_push(&thannel, "more", 4, 30));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
}

TEST(cat_thannel, a_double_close_is_fine)
{
    cat_thannel_t thannel = {};

    ASSERT_NE(cat_thannel_create(&thannel, 1), nullptr);
    DEFER(cat_thannel_cleanup(&thannel));

    ASSERT_TRUE(cat_thannel_close(&thannel));
    ASSERT_TRUE(cat_thannel_close(&thannel));
    ASSERT_TRUE(cat_thannel_is_closed(&thannel));
}

TEST(cat_thannel, a_large_message_survives_the_round_trip)
{
    const size_t size = 1024 * 1024;
    cat_thannel_t thannel = {};
    char *value = (char *) cat_sys_malloc(size);
    char *received = nullptr;
    size_t received_size = 0;

    ASSERT_NE(value, nullptr);
    DEFER(cat_sys_free(value));
    for (size_t index = 0; index < size; index++) {
        value[index] = (char) (index * 31u);
    }

    ASSERT_NE(cat_thannel_create(&thannel, 1), nullptr);
    DEFER(cat_thannel_cleanup(&thannel));

    ASSERT_TRUE(cat_thannel_push(&thannel, value, size, TEST_IO_TIMEOUT));
    ASSERT_TRUE(cat_thannel_pop(&thannel, (void **) &received, &received_size, TEST_IO_TIMEOUT));
    ASSERT_EQ(received_size, size);
    ASSERT_EQ(memcmp(received, value, size), 0);
    cat_sys_free(received);
}

TEST(cat_thannel, two_producers_of_a_rendezvous_all_arrive)
{
    struct producer_s {
        cat_thread_t thread;
        cat_thannel_t *thannel;
        int value;
        cat_bool_t ok;
    };

    struct pair_s {
        cat_thannel_t thannel;
        struct producer_s producers[2];
    } pair = {};

    ASSERT_NE(cat_thannel_create(&pair.thannel, CAT_THANNEL_CAPACITY_UNBUFFERED), nullptr);
    DEFER(cat_thannel_cleanup(&pair.thannel));

    for (int index = 0; index < 2; index++) {
        pair.producers[index].thannel = &pair.thannel;
        pair.producers[index].value = index;
        pair.producers[index].ok = cat_false;
        ASSERT_TRUE(cat_thread_create(&pair.producers[index].thread, [](cat_data_t *data) {
            struct producer_s *self = (struct producer_s *) data;

            self->ok = cat_thannel_push(self->thannel, &self->value, sizeof(self->value),
                TEST_IO_TIMEOUT);
        }, &pair.producers[index]));
    }

    int seen[2] = {0, 0};
    for (int index = 0; index < 2; index++) {
        void *value = nullptr;
        size_t size = 0;
        int received = -1;

        ASSERT_TRUE(cat_thannel_pop(&pair.thannel, &value, &size, TEST_IO_TIMEOUT));
        ASSERT_EQ(size, sizeof(received));
        memcpy(&received, value, size);
        ASSERT_TRUE(received == 0 || received == 1);
        seen[received]++;
        cat_sys_free(value);
    }

    ASSERT_TRUE(cat_thread_join(&pair.producers[0].thread, TEST_IO_TIMEOUT));
    ASSERT_TRUE(cat_thread_join(&pair.producers[1].thread, TEST_IO_TIMEOUT));
    ASSERT_TRUE(pair.producers[0].ok);
    ASSERT_TRUE(pair.producers[1].ok);
    ASSERT_EQ(seen[0], 1);
    ASSERT_EQ(seen[1], 1);
}

#endif /* CAT_THREAD_SAFE */
