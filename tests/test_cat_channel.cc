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
  | Author: Twosee <twose@qq.com>                                            |
  | Author: codinghuang <2812240764@qq.com>                                  |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

/* unbuffered {{{ */

TEST(cat_channel_unbuffered, push_ex_timeout_and_no_producer)
{
    cat_channel_t *channel, _channel;
    char data[] = "a";

    channel = cat_channel_create(&_channel, 0, sizeof(data), nullptr);
    DEFER(cat_channel_close(channel));
    ASSERT_FALSE(cat_channel_push_ex(channel, &data, 1));
}

TEST(cat_channel_unbuffered, push_ex_timeout_and_has_producer)
{
    cat_channel_t *channel, _channel;
    char data[] = "a";

    channel = cat_channel_create(&_channel, 0, sizeof(data), nullptr);
    DEFER(cat_channel_close(channel));

    cat_coroutine_run(nullptr, [](cat_data_t *data) {
        cat_channel_t *channel = (cat_channel_t *) data;
        char actual[3] = {0};
        char expect[] = "a";

        EXPECT_TRUE(cat_channel_pop(channel, actual));
        EXPECT_STREQ(expect, actual);

        return CAT_COROUTINE_DATA_NULL;
    }, channel);

    ASSERT_TRUE(cat_channel_push_ex(channel, &data, 1));
}

TEST(cat_channel_unbuffered, push_cancel_continue)
{
    cat_channel_t *channel, _channel;
    char data[] = "a";

    channel = cat_channel_create(&_channel, 0, sizeof(data), nullptr);
    DEFER(cat_channel_close(channel));

    cat_coroutine_run(nullptr, [](cat_data_t *data) {
        cat_coroutine_t *coroutine = (cat_coroutine_t *) data;
        /* switch to main coroutine */
        cat_time_sleep(0);
        /* cancel the timeout of the main coroutine with resume instead of pop channnel */
        cat_coroutine_resume_ez(coroutine);
        return CAT_COROUTINE_DATA_NULL;
    }, cat_coroutine_get_current());

    /* main coroutine try to push data but blocking */
    ASSERT_FALSE(cat_channel_push(channel, &data));
    /* cancel main coroutine timeout */
    ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
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
    for (size_t i = 0; i < 2; i++)
    {
        ASSERT_EQ(&channel->storage, channel->storage[i]);
        ASSERT_EQ(&channel->consumers, channel->consumers[i]);
        ASSERT_EQ(&channel->storage, channel->storage[i]);
    }
}

TEST(cat_channel_buffered, push)
{
    cat_channel_t *channel, _channel;
    size_t data = 1;

    /**
     * we must push a valid variable address, cannot push a value directly.
     * for example, the following invocation is wrong:
     * cat_channel_push(channel, (cat_data_t *)(uintptr_t)1)
     */
    channel = cat_channel_create(&_channel, 1, sizeof(size_t), nullptr);
    DEFER(cat_channel_close(channel));
    ASSERT_TRUE(cat_channel_push(channel, &data));
}

TEST(cat_channel_buffered, push_eq_data_size)
{
    cat_channel_t *channel, _channel;
    char data[] = "ab";
    char actual[3] = {0};
    char expect[] = "ab";

    channel = cat_channel_create(&_channel, 3, sizeof(data), nullptr);
    DEFER(cat_channel_close(channel));
    ASSERT_TRUE(cat_channel_push(channel, data));
    ASSERT_TRUE(cat_channel_pop(channel, actual));
    ASSERT_STREQ(expect, actual);
}

TEST(cat_channel_buffered, push_gt_data_size)
{
    cat_channel_t *channel, _channel;
    char data[] = "ab";
    char actual[3] = {0};
    char expect[] = "a";

    channel = cat_channel_create(&_channel, 1, sizeof(char), nullptr);
    DEFER(cat_channel_close(channel));
    /* because of the truncation, the actual stored data is the character a */
    ASSERT_TRUE(cat_channel_push(channel, data));
    ASSERT_TRUE(cat_channel_pop(channel, actual));
    ASSERT_STREQ(expect, actual);
}

TEST(cat_channel_buffered, get_capacity)
{
    cat_channel_t *channel, _channel;
    channel = cat_channel_create(&_channel, 1, sizeof(char), nullptr);
    DEFER(cat_channel_close(channel));

    ASSERT_EQ(1, cat_channel_get_capacity(channel));
}

TEST(cat_channel_buffered, get_length)
{
    cat_channel_t *channel, _channel;
    size_t data = 1;
    channel = cat_channel_create(&_channel, 1, sizeof(size_t), nullptr);
    DEFER(cat_channel_close(channel));

    ASSERT_TRUE(cat_channel_push(channel, &data));
    ASSERT_EQ(1, cat_channel_get_length(channel));
    ASSERT_TRUE(cat_channel_pop(channel, &data));
    ASSERT_EQ(0, cat_channel_get_length(channel));
}

TEST(cat_channel_buffered, is_empty)
{
    cat_channel_t *channel, _channel;
    size_t data = 1;
    channel = cat_channel_create(&_channel, 1, sizeof(size_t), nullptr);
    DEFER(cat_channel_close(channel));

    ASSERT_TRUE(cat_channel_is_empty(channel));
    ASSERT_TRUE(cat_channel_push(channel, &data));
    ASSERT_FALSE(cat_channel_is_empty(channel));
}

TEST(cat_channel_buffered, is_full)
{
    cat_channel_t *channel, _channel;
    size_t data = 1;
    channel = cat_channel_create(&_channel, 1, sizeof(size_t), nullptr);
    DEFER(cat_channel_close(channel));

    ASSERT_FALSE(cat_channel_is_full(channel));
    ASSERT_TRUE(cat_channel_push(channel, &data));
    ASSERT_TRUE(cat_channel_is_full(channel));
}

TEST(cat_channel_buffered, has_producers)
{
    cat_channel_t *channel, _channel;
    size_t data = 1;
    channel = cat_channel_create(&_channel, 1, sizeof(size_t), nullptr);
    DEFER(cat_channel_close(channel));

    ASSERT_TRUE(cat_channel_push_ex(channel, &data, 1));
    ASSERT_TRUE(cat_channel_has_producers(channel));
}

TEST(cat_channel_buffered, has_consumers)
{
    cat_channel_t *channel, _channel;
    size_t data = 1;
    channel = cat_channel_create(&_channel, 1, sizeof(size_t), nullptr);
    DEFER(cat_channel_close(channel));

    ASSERT_FALSE(cat_channel_pop_ex(channel, &data, 1));
    ASSERT_TRUE(cat_channel_has_consumers(channel));
}

TEST(cat_channel_buffered, get_dtor)
{
    cat_channel_t *channel, _channel;
    channel = cat_channel_create(&_channel, 1, sizeof(size_t), [](cat_data_t *data) {});
    DEFER(cat_channel_close(channel));

    ASSERT_NE(nullptr, cat_channel_get_dtor(channel));
}

TEST(cat_channel_buffered, set_dtor)
{
    cat_channel_t *channel, _channel;
    channel = cat_channel_create(&_channel, 1, sizeof(size_t), nullptr);
    DEFER(cat_channel_close(channel));

    ASSERT_EQ(nullptr, cat_channel_set_dtor(channel, [](cat_data_t *data) {}));
}

TEST(cat_channel_buffered, base)
{
    for (size_t size = 0; size < 100; size++) {
        cat_channel_t *channel, _channel;
        channel = cat_channel_create(&_channel, size, sizeof(size_t), nullptr);

        cat_coroutine_run(nullptr, [](cat_data_t *data) {
            cat_channel_t *channel = (cat_channel_t *) data;
            size_t n;
            for (size_t i = 0; i < 100; i++) {
                EXPECT_TRUE(cat_channel_pop(channel, &n));
                EXPECT_EQ(n, i);
            }
            return CAT_COROUTINE_DATA_NULL;
        }, channel);

        for (size_t i = 0; i < 100; i++) {
            EXPECT_TRUE(cat_channel_push(channel, &i));
        }
    }
}

TEST(cat_channel_buffered, timeout)
{
    cat_msec_t s = cat_time_msec();
    cat_channel_t *channel, _channel;
    channel = cat_channel_create(&_channel, 5, sizeof(size_t), nullptr);
    size_t n;
    ASSERT_FALSE(cat_channel_pop_ex(channel, &n, 5));
    s = cat_time_msec() - s;
    ASSERT_GE(s, 5);
}

TEST(cat_channel_buffered, multi)
{
    for (size_t size = 0; size < 10; size++) {
        cat_channel_t *channel, _channel;
        channel = cat_channel_create(&_channel, size, sizeof(size_t), nullptr);

        for (size_t i = 0; i < 10; i++) {
            struct _info {
                cat_channel_t *channel;
                size_t i;
            } info = { channel, i };
            cat_coroutine_run(nullptr, [](cat_data_t *data) {
                struct _info info = *((struct _info *) data);
                size_t n;
                EXPECT_TRUE(cat_channel_pop(info.channel, &n));
                EXPECT_EQ(n, info.i);
                return CAT_COROUTINE_DATA_NULL;
            }, &info);
        }

        for (size_t i = 0; i < 10; i++) {
            EXPECT_TRUE(cat_channel_push(channel, &i));
        }

        EXPECT_EQ(channel->length, 0);
    }
}

TEST(cat_channel_buffered, close_has_dtor)
{
    cat_channel_t *channel, _channel;
    size_t n = 1;

    testing::internal::CaptureStdout();

    channel = cat_channel_create(&_channel, 1, sizeof(size_t), [](cat_data_t *data) {
        std::cout << "dtor";
    });

    cat_channel_push(channel, &n);
    cat_channel_close(channel);
    std::string output = testing::internal::GetCapturedStdout();
    ASSERT_EQ("dtor", output);
}

TEST(cat_channel_buffered, close_no_dtor)
{
    cat_channel_t *channel, _channel;

    channel = cat_channel_create(&_channel, 5, sizeof(size_t), nullptr);

    cat_coroutine_run(nullptr, [](cat_data_t *data) {
        cat_channel_t *channel = (cat_channel_t *) data;
        size_t n;
        for (size_t i = 0; i < 100; i++) {
            if (cat_channel_pop(channel, &n) != cat_true) {
                EXPECT_EQ(i, 50);
                break;
            }
            EXPECT_EQ(n, i);
        }
        return CAT_COROUTINE_DATA_NULL;
    }, channel);

    for (size_t i = 0; i < 100; i++) {
        if (i == 50) {
            cat_channel_close(channel);
            break;
        }
        EXPECT_TRUE(cat_channel_push(channel, &i));
    }
}

/* }}} buffered channel test */
