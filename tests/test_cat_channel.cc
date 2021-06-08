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

#include <array>

TEST(cat_channel, base)
{
    for (cat_channel_size_t size = 0; size < TEST_MAX_REQUESTS; size++) {
        cat_channel_t *channel, _channel;

        channel = cat_channel_create(&_channel, size, sizeof(size_t), nullptr);

        co([=] {
            size_t n;
            for (size_t i = 0; i < TEST_MAX_REQUESTS; i++) {
                ASSERT_TRUE(cat_channel_pop(channel, &n, -1));
                ASSERT_EQ(n, i);
            }
        });

        for (size_t i = 0; i < TEST_MAX_REQUESTS; i++) {
            ASSERT_TRUE(cat_channel_push(channel, &i, -1));
        }
    }
}

TEST(cat_channel, get_capacity)
{
    cat_channel_t *channel, _channel;
    channel = cat_channel_create(&_channel, 1, sizeof(char), nullptr);
    DEFER(cat_channel_close(channel));

    ASSERT_EQ(1, cat_channel_get_capacity(channel));
}

TEST(cat_channel, get_length)
{
    cat_channel_t *channel, _channel;
    size_t data = 1;
    channel = cat_channel_create(&_channel, 1, sizeof(size_t), nullptr);
    DEFER(cat_channel_close(channel));

    ASSERT_TRUE(cat_channel_push(channel, &data, -1));
    ASSERT_EQ(1, cat_channel_get_length(channel));
    ASSERT_TRUE(cat_channel_pop(channel, &data, -1));
    ASSERT_EQ(0, cat_channel_get_length(channel));
}

TEST(cat_channel, is_empty)
{
    cat_channel_t *channel, _channel;
    size_t data = 1;
    channel = cat_channel_create(&_channel, 1, sizeof(size_t), nullptr);
    DEFER(cat_channel_close(channel));

    ASSERT_TRUE(cat_channel_is_empty(channel));
    ASSERT_TRUE(cat_channel_push(channel, &data, -1));
    ASSERT_FALSE(cat_channel_is_empty(channel));
}

TEST(cat_channel, is_full)
{
    cat_channel_t *channel, _channel;
    size_t data = 1;
    channel = cat_channel_create(&_channel, 1, sizeof(size_t), nullptr);
    DEFER(cat_channel_close(channel));

    ASSERT_FALSE(cat_channel_is_full(channel));
    ASSERT_TRUE(cat_channel_push(channel, &data, -1));
    ASSERT_TRUE(cat_channel_is_full(channel));
}

TEST(cat_channel, has_producers_and_is_readable)
{
    cat_channel_t *channel, _channel;
    size_t data = 1;
    size_t rdata;

    channel = cat_channel_create(&_channel, 0, sizeof(size_t), nullptr);
    DEFER(cat_channel_close(channel));

    ASSERT_FALSE(cat_channel_is_readable(channel));
    co([&] {
        ASSERT_TRUE(cat_channel_push(channel, &data, 0));
    });
    ASSERT_TRUE(cat_channel_is_readable(channel));
    ASSERT_TRUE(cat_channel_pop(channel, &rdata, 0));
    ASSERT_EQ(data, rdata);
    ASSERT_FALSE(cat_channel_is_readable(channel));
}

TEST(cat_channel, has_consumers_and_is_writable)
{
    cat_channel_t *channel, _channel;
    size_t data = 1;

    channel = cat_channel_create(&_channel, 0, sizeof(size_t), nullptr);
    DEFER(cat_channel_close(channel));

    ASSERT_FALSE(cat_channel_is_writable(channel));
    co([=] {
        size_t rdata;
        ASSERT_TRUE(cat_channel_pop(channel, &rdata, 0));
        ASSERT_EQ(data, rdata);
    });
    ASSERT_TRUE(cat_channel_is_writable(channel));
    ASSERT_TRUE(cat_channel_push(channel, &data, 0));
    ASSERT_FALSE(cat_channel_is_writable(channel));
}

TEST(cat_channel, get_dtor)
{
    cat_channel_t *channel, _channel;
    cat_channel_data_dtor_t dtor = [](const cat_data_t *data) {};

    channel = cat_channel_create(&_channel, 1, sizeof(size_t), dtor);
    DEFER(cat_channel_close(channel));

    ASSERT_EQ(dtor, cat_channel_get_dtor(channel));
}

TEST(cat_channel, set_dtor)
{
    cat_channel_t *channel, _channel;
    cat_channel_data_dtor_t dtor = [](const cat_data_t *data) {};

    channel = cat_channel_create(&_channel, 1, sizeof(size_t), nullptr);
    DEFER(cat_channel_close(channel));

    ASSERT_EQ(nullptr, cat_channel_set_dtor(channel, dtor));
    ASSERT_EQ(dtor, cat_channel_set_dtor(channel, nullptr));
    ASSERT_EQ(nullptr, cat_channel_set_dtor(channel, nullptr));
}

TEST(cat_channel, get_storage)
{
    cat_channel_t *channel, _channel;

    ([&] {
        channel = cat_channel_create(&_channel, 0, sizeof(size_t), nullptr);
        DEFER(cat_channel_close(channel));
        ASSERT_EQ(cat_channel_get_storage(channel), nullptr);
    })();

    ([&] {
        channel = cat_channel_create(&_channel, 1, sizeof(size_t), nullptr);
        DEFER(cat_channel_close(channel));
        ASSERT_NE(cat_channel_get_storage(channel), nullptr);
    })();
}

TEST(cat_channel, pop_null)
{
    for (cat_channel_size_t capacity = 0; capacity < 2; capacity++) [=] {
        cat_channel_t *channel, _channel;
        cat_bool_t foo = cat_false;
        cat_data_t *data = &foo;

        channel = cat_channel_create(&_channel, capacity, sizeof(data), [](const cat_data_t *data) {
            **((cat_bool_t **) data) = !(**((cat_bool_t **) data));
        });
        DEFER(cat_channel_close(channel));

        co([&] {
            ASSERT_TRUE(cat_channel_push(channel, &data, 0));
        });
        if (capacity == 0) {
            ASSERT_TRUE(cat_channel_has_producers(channel));
        }
        ASSERT_TRUE(cat_channel_pop(channel, nullptr, 0));
        ASSERT_TRUE(foo);

        co([&] {
            ASSERT_TRUE(cat_channel_pop(channel, nullptr, 0));
        });
        if (capacity == 0) {
            ASSERT_TRUE(cat_channel_has_consumers(channel));
        }
        ASSERT_TRUE(cat_channel_push(channel, &data, 0));
        ASSERT_FALSE(foo);
    }();
}

TEST(cat_channel, cancel)
{
    for (cat_channel_size_t capacity = 0; capacity < 2; capacity++) [=] {
        cat_coroutine_t *coroutine = co([=] {
            cat_channel_t *channel, _channel;
            char data[] = "x";

            channel = cat_channel_create(&_channel, capacity, sizeof(data), nullptr);
            DEFER(cat_channel_close(channel));
            if (capacity == 1) {
                ASSERT_TRUE(cat_channel_push(channel, &data, -1));
            }
            /* main coroutine try to push data but blocking */
            ASSERT_FALSE(cat_channel_push(channel, &data, -1));
            /* cancel main coroutine */
            ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
            if (capacity == 1) {
                ASSERT_TRUE(cat_channel_pop(channel, &data, -1));
            }
            /* main coroutine try to pop data but blocking */
            ASSERT_FALSE(cat_channel_pop(channel, &data, -1));
            /* cancel main coroutine */
            ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
        });

        for (size_t n = 2; n--;) {
            /* cancel the timeout of the coroutine with resume instead of pop channnel */
            cat_coroutine_resume(coroutine, nullptr, nullptr);
        }
    }();
}

TEST(cat_channel, closing)
{
    for (cat_channel_size_t capacity = 0; capacity < 2; capacity++) [=] {
        cat_channel_t *channel, _channel;
        size_t data = 0;
        bool push_over = false, pop_over = false;

        channel = cat_channel_create(&_channel, capacity, sizeof(data), nullptr);
        cat_channel_enable_reuse(channel); /* why we need it here? */
        DEFER(cat_channel_close(channel));

        if (capacity == 1) {
            ASSERT_TRUE(cat_channel_push(channel, &data, 0));
        }
        co([&] {
            ASSERT_FALSE(cat_channel_push(channel, &data, 0));
            ASSERT_FALSE(cat_channel_is_available(channel));
            ASSERT_TRUE(cat_channel_get_flags(channel) & CAT_CHANNEL_FLAG_CLOSING);
            push_over = true;
        });
        ASSERT_TRUE(cat_channel_has_producers(channel));
        cat_channel_close(channel);
        ASSERT_TRUE(push_over);

        co([&] {
            ASSERT_FALSE(cat_channel_pop(channel, nullptr, 0));
            ASSERT_FALSE(cat_channel_is_available(channel));
            ASSERT_TRUE(cat_channel_get_flags(channel) & CAT_CHANNEL_FLAG_CLOSING);
            pop_over = true;
        });
        ASSERT_TRUE(cat_channel_has_consumers(channel));
        cat_channel_close(channel);
        ASSERT_TRUE(pop_over);
    }();
}

TEST(cat_channel, reuse)
{
    cat_channel_t *channel, _channel;
    size_t data;

    channel = cat_channel_create(&_channel, 1, sizeof(data), nullptr);
    ASSERT_FALSE(cat_channel_get_flags(channel) & CAT_CHANNEL_FLAG_REUSE);
    cat_channel_close(channel);
    ASSERT_FALSE(cat_channel_push(channel, &data, -1));
    ASSERT_EQ(CAT_ECLOSED, cat_get_last_error_code());
    ASSERT_TRUE(cat_channel_get_flags(channel) & CAT_CHANNEL_FLAG_CLOSED);

    channel = cat_channel_create(&_channel, 1, sizeof(data), nullptr);
    cat_channel_enable_reuse(channel);
    DEFER(cat_channel_close(channel));
    ASSERT_TRUE(cat_channel_get_flags(channel) & CAT_CHANNEL_FLAG_REUSE);
    cat_channel_close(channel);
    ASSERT_TRUE(cat_channel_push(channel, &data, -1));
}

/* unbuffered {{{ */

TEST(cat_channel_unbuffered, base)
{
    cat_channel_t channel;
    cat_bool_t data = cat_false;
    bool channel_closed = false;

    ASSERT_NE(cat_channel_create(&channel, 0, sizeof(cat_bool_t), nullptr), nullptr);
    co([&]() {
        cat_bool_t data = cat_true;
        ASSERT_TRUE(cat_channel_push(&channel, &data, -1));
        cat_channel_close(&channel);
        channel_closed = true;
    });
    ASSERT_TRUE(cat_channel_pop(&channel, &data, -1));
    ASSERT_TRUE(data);
    ASSERT_TRUE(channel_closed);
}

TEST(cat_channel_unbuffered, push_timeout)
{
    cat_channel_t *channel, _channel;
    char data[] = "x";

    channel = cat_channel_create(&_channel, 0, sizeof(data), nullptr);
    DEFER(cat_channel_close(channel));
    ASSERT_FALSE(cat_channel_push(channel, &data, 0));
    ASSERT_EQ(CAT_ETIMEDOUT, cat_get_last_error_code());
}

TEST(cat_channel_unbuffered, pop_timeout)
{
    cat_channel_t *channel, _channel;

    channel = cat_channel_create(&_channel, 0, 0, nullptr);
    DEFER(cat_channel_close(channel));
    ASSERT_FALSE(cat_channel_pop(channel, nullptr, 0));
    ASSERT_EQ(CAT_ETIMEDOUT, cat_get_last_error_code());
}

TEST(cat_channel_unbuffered, push_with_timeout)
{
    cat_channel_t *channel, _channel;
    const char data[] = "x";

    channel = cat_channel_create(&_channel, 0, sizeof(data), nullptr);
    DEFER(cat_channel_close(channel));

    co([=] {
        char buffer[sizeof(data)];
        ASSERT_TRUE(cat_channel_pop(channel, buffer, 0));
        ASSERT_STREQ(buffer, data);
    });

    ASSERT_TRUE(cat_channel_push(channel, &data, 0));
}

TEST(cat_channel_unbuffered, pop_with_timeout)
{
    cat_channel_t *channel, _channel;
    const char data[] = "x";
    char buffer[sizeof(data)];

    channel = cat_channel_create(&_channel, 0, sizeof(data), nullptr);
    DEFER(cat_channel_close(channel));

    co([&] {
        ASSERT_TRUE(cat_channel_push(channel, data, 0));
    });

    ASSERT_TRUE(cat_channel_pop(channel, buffer, 0));
    ASSERT_STREQ(buffer, data);
}

/* }}} unbuffered */

/* buffered {{{ */

TEST(cat_channel_buffered, create)
{
    cat_channel_t *channel, _channel;

    channel = cat_channel_create(&_channel, 1, sizeof(size_t), nullptr);
    DEFER(cat_channel_close(channel));
    ASSERT_EQ(1, channel->capacity);
    ASSERT_EQ(0, channel->length);
    ASSERT_EQ(sizeof(size_t), channel->data_size);
    ASSERT_EQ(nullptr, channel->dtor);
}

TEST(cat_channel_buffered, push_and_pop)
{
    cat_channel_t *channel, _channel;
    size_t data;

    /**
     * we must push a valid variable address, cannot push a value directly.
     * for example, the following invocation is wrong:
     * cat_channel_push(channel, (cat_data_t *, -1)(uintptr_t) 1)
     */
    channel = cat_channel_create(&_channel, 1, sizeof(size_t), nullptr);
    DEFER(cat_channel_close(channel));
    co([channel] {
        for (size_t i = 0; i < TEST_MAX_REQUESTS; i++) {
            ASSERT_TRUE(cat_channel_push(channel, &i, -1));
        }
    });
    for (size_t i = 0; i < TEST_MAX_REQUESTS; i++) {
        ASSERT_TRUE(cat_channel_pop(channel, &data, -1));
        ASSERT_EQ(data, i);
    }
}

TEST(cat_channel_buffered, push_eq_data_size)
{
    cat_channel_t *channel, _channel;
    const char data[] = "xyz";
    char actual[sizeof(data)] = {0};

    channel = cat_channel_create(&_channel, 3, sizeof(data), nullptr);
    DEFER(cat_channel_close(channel));
    ASSERT_TRUE(cat_channel_push(channel, data, -1));
    ASSERT_TRUE(cat_channel_pop(channel, actual, -1));
    ASSERT_STREQ(data, actual);
}

TEST(cat_channel_buffered, push_gt_data_size)
{
    cat_channel_t *channel, _channel;
    const char data[] = "xyz";
    char actual[sizeof(data)] = {0};
    const char expect[] = "x";

    channel = cat_channel_create(&_channel, 1, sizeof(char), nullptr);
    DEFER(cat_channel_close(channel));
    /* because of the truncation, the actual stored data is the character a */
    ASSERT_TRUE(cat_channel_push(channel, data, -1));
    ASSERT_TRUE(cat_channel_pop(channel, actual, -1));
    ASSERT_STREQ(expect, actual);
}

TEST(cat_channel_buffered, push_timeout)
{
    cat_event_wait(); // call it before timing test
    cat_msec_t s = cat_time_msec();
    cat_channel_t *channel, _channel;
    channel = cat_channel_create(&_channel, 1, sizeof(size_t), nullptr);
    DEFER(cat_channel_close(channel));
    size_t n = 1;
    ASSERT_TRUE(cat_channel_push(channel, &n, 0));
    ASSERT_FALSE(cat_channel_push(channel, &n, 10));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
    s = cat_time_msec() - s;
    ASSERT_GE(s, 5);
}

TEST(cat_channel_buffered, pop_timeout)
{
    cat_event_wait(); // call it before timing test
    cat_msec_t s = cat_time_msec();
    cat_channel_t *channel, _channel;
    channel = cat_channel_create(&_channel, 10, sizeof(size_t), nullptr);
    DEFER(cat_channel_close(channel));
    size_t n;
    ASSERT_FALSE(cat_channel_pop(channel, &n, 10));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
    s = cat_time_msec() - s;
    ASSERT_GE(s, 5);
}

TEST(cat_channel_buffered, multi)
{
    for (cat_channel_size_t size = 0; size < TEST_MAX_REQUESTS; size++) {
        cat_channel_t *channel, _channel;
        channel = cat_channel_create(&_channel, size, sizeof(size_t), nullptr);

        for (size_t i = 0; i < TEST_MAX_REQUESTS; i++) {
            co([=]() {
                size_t n;
                ASSERT_TRUE(cat_channel_pop(channel, &n, -1));
                ASSERT_EQ(n, i);
            });
        }

        for (size_t i = 0; i < TEST_MAX_REQUESTS; i++) {
            ASSERT_TRUE(cat_channel_push(channel, &i, -1));
        }

        ASSERT_EQ(channel->length, 0);
    }
}

TEST(cat_channel_buffered, close_has_dtor)
{
    cat_channel_t *channel, _channel;
    size_t n = 1;

    channel = cat_channel_create(&_channel, 1, sizeof(size_t), [](const cat_data_t *data) {
        std::cout << "dtor";
    });

    testing::internal::CaptureStdout();
    cat_channel_push(channel, &n, -1);
    cat_channel_close(channel);
    std::string output = testing::internal::GetCapturedStdout();
    ASSERT_EQ("dtor", output);
}

TEST(cat_channel_buffered, close_no_dtor)
{
    cat_channel_t *channel, _channel;

    channel = cat_channel_create(&_channel, 5, sizeof(size_t), nullptr);

    co([=] {
        size_t n;
        for (size_t i = 0; i < TEST_MAX_REQUESTS; i++) {
            if (cat_channel_pop(channel, &n, -1) != cat_true) {
                ASSERT_EQ(i, TEST_MAX_REQUESTS / 2);
                break;
            }
            ASSERT_EQ(n, i);
        }
    });

    for (size_t i = 0; i < TEST_MAX_REQUESTS; i++) {
        if (i == TEST_MAX_REQUESTS / 2) {
            cat_channel_close(channel);
            break;
        }
        ASSERT_TRUE(cat_channel_push(channel, &i, -1));
    }
}

/* }}} buffered */

/* select {{{ */

TEST(cat_channel_select, base)
{
    for (auto capacity : std::array<cat_channel_size_t, 2>{ 0, 1 })  {
        cat_channel_t channel;
        cat_channel_select_response_t *response;

        ASSERT_NE(cat_channel_create(&channel, capacity, sizeof(cat_bool_t), nullptr), nullptr);

        auto push = [&]() {
            cat_bool_t data;
            for (int n = 2; n--;) {
                cat_channel_select_request_t requests[] = {{ &channel, { &data }, CAT_CHANNEL_OPCODE_PUSH, cat_false }};
                data = cat_true;
                response = cat_channel_select(requests, CAT_ARRAY_SIZE(requests), -1);
                ASSERT_NE(response, nullptr);
                ASSERT_FALSE(response->error);
                ASSERT_EQ(response->channel, &channel);
            }
        };
        auto pop = [&]() {
            cat_bool_t data;
            for (int n = 2; n--;) {
                cat_channel_select_request_t requests[] = {{ &channel, { &data }, CAT_CHANNEL_OPCODE_POP, cat_false }};
                data = cat_false;
                response = cat_channel_select(requests, CAT_ARRAY_SIZE(requests), -1);
                ASSERT_NE(response, nullptr);
                ASSERT_FALSE(response->error);
                ASSERT_EQ(response->channel, &channel);
                ASSERT_TRUE(data);
            }
        };

        co(push);
        pop();

        co(pop);
        push();

        co([&]() {
            cat_channel_select_request_t requests[1];
            cat_bool_t data = cat_false;
            bool tried = false;
            if (capacity == 1) {
                ASSERT_TRUE(cat_channel_push(&channel, &data, -1));
            }
            for (auto opcode : std::array<cat_channel_opcode_t, 2>{ CAT_CHANNEL_OPCODE_PUSH, CAT_CHANNEL_OPCODE_POP }) {
                *requests = { &channel, { &data }, opcode, cat_false };
                for (int n = 2; n--;) {
                    response = cat_channel_select(requests, CAT_ARRAY_SIZE(requests), -1);
                    ASSERT_EQ(response->channel, &channel);
                    ASSERT_TRUE(response->error);
                    ASSERT_FALSE(cat_channel_is_available(response->channel));
                    ASSERT_EQ(!tried ? CAT_ECANCELED : CAT_ECLOSING, cat_get_last_error_code());
                    /* first try it would return ECANCELED, if you still tried to opreate it, it would return EINVAL */
                    tried = true;
                }
            }
        });
        cat_channel_close(&channel);
    }
}

TEST(cat_channel_select, unbuffered)
{
    for (auto read_channel_first : std::array<bool, 2>{ false, true })  {
        cat_channel_t *channel, read_channel, write_channel;
        cat_bool_t read_data = cat_false, write_data = cat_true;
        cat_channel_select_request_t requests[] = {
            { &read_channel, { &read_data }, CAT_CHANNEL_OPCODE_POP, cat_false },
            { &write_channel, { &write_data }, CAT_CHANNEL_OPCODE_PUSH, cat_false }
        };
        cat_channel_select_response_t *response;
        bool push_over = false, pop_over = false;

        ASSERT_NE(cat_channel_create(&read_channel, 0, sizeof(cat_bool_t), nullptr), nullptr);
        ASSERT_NE(cat_channel_create(&write_channel, 0, sizeof(cat_bool_t), nullptr), nullptr);
        co([&]() {
            cat_bool_t data = cat_true;
            if (!read_channel_first) {
                ASSERT_TRUE(cat_time_delay(0));
            }
            ASSERT_TRUE(cat_channel_push(&read_channel, &data, -1));
            push_over = true;
        });
        co([&]() {
            cat_bool_t data = false;
            if (read_channel_first) {
                ASSERT_TRUE(cat_time_delay(0));
            }
            ASSERT_TRUE(cat_channel_pop(&write_channel, &data, -1));
            ASSERT_TRUE(data);
            pop_over = true;
        });
        for (int n = 0; n < 2; n++) {
            response = cat_channel_select(requests, CAT_ARRAY_SIZE(requests), -1);
            ASSERT_NE(response, nullptr);
            ASSERT_FALSE(response->error);
            channel = response->channel;
            if (channel == &read_channel) {
                ASSERT_TRUE(read_data);
                ASSERT_EQ(n, read_channel_first ? 0 : 1);
            } else if (channel == &write_channel) {
                ASSERT_EQ(n, read_channel_first ? 1 : 0);
            } else {
                ASSERT_FALSE("never here");
            }
        }
        cat_channel_close(&read_channel);
        cat_channel_close(&write_channel);
        /* we should yield to the sub coroutine and let them released the channel */
        ASSERT_TRUE(cat_time_delay(0));
        ASSERT_TRUE(push_over);
        ASSERT_TRUE(pop_over);
    }
}

TEST(cat_channel_select, timeout)
{
    cat_channel_t channel;
    cat_channel_select_response_t *response;
    cat_bool_t data = cat_false;

    ASSERT_NE(cat_channel_create(&channel, 0, sizeof(cat_bool_t), nullptr), nullptr);

    do {
        cat_channel_select_request_t requests[] = {{ &channel, { &data }, CAT_CHANNEL_OPCODE_PUSH, cat_false }};
        response = cat_channel_select(requests, CAT_ARRAY_SIZE(requests), 0);
        ASSERT_EQ(response, nullptr);
        ASSERT_EQ(CAT_ETIMEDOUT, cat_get_last_error_code());
    } while (0);

    do {
        cat_channel_select_request_t requests[] = {{ &channel, { &data }, CAT_CHANNEL_OPCODE_POP, cat_false }};
        response = cat_channel_select(requests, CAT_ARRAY_SIZE(requests), 0);
        ASSERT_EQ(response, nullptr);
        ASSERT_EQ(CAT_ETIMEDOUT, cat_get_last_error_code());
    } while (0);
}

TEST(cat_channel_select, cancel)
{
    cat_coroutine_t *coroutine = cat_coroutine_get_current();
    cat_channel_t channel;
    cat_channel_select_response_t *response;
    cat_bool_t data = cat_false;

    ASSERT_NE(cat_channel_create(&channel, 0, sizeof(cat_bool_t), nullptr), nullptr);

    co([=] {
        ASSERT_TRUE(cat_time_delay(0));
        cat_coroutine_resume(coroutine, nullptr, nullptr);
    });

    do {
        cat_channel_select_request_t requests[] = {{ &channel, { &data }, CAT_CHANNEL_OPCODE_PUSH, cat_false }};
        response = cat_channel_select(requests, CAT_ARRAY_SIZE(requests), -1);
        ASSERT_EQ(response, nullptr);
        ASSERT_EQ(CAT_ECANCELED, cat_get_last_error_code());
    } while (0);

    co([=] {
        ASSERT_TRUE(cat_time_delay(0));
        cat_coroutine_resume(coroutine, nullptr, nullptr);
    });

    do {
        cat_channel_select_request_t requests[] = {{ &channel, { &data }, CAT_CHANNEL_OPCODE_POP, cat_false }};
        response = cat_channel_select(requests, CAT_ARRAY_SIZE(requests), 0);
        ASSERT_EQ(response, nullptr);
        ASSERT_EQ(CAT_ECANCELED, cat_get_last_error_code());
    } while (0);
}

/* }}} channel select test */
