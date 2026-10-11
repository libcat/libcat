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
  +--------------------------------------------------------------------------+
 */

#include "test.h"

namespace {

class select_channels
{
public:
    static constexpr size_t count = 3;
    std::array<cat_channel_t, count> channels{};
    std::array<cat_bool_t, count> data{};
    std::array<cat_channel_select_request_t, count> requests{};
    cat_channel_select_response_t *response = nullptr;
    cat_errno_t error = 0;
    wait_group workers;
    size_t initialized = 0;

    void initialize(cat_channel_size_t capacity, unsigned int push_mask)
    {
        for (size_t i = 0; i < count; i++) {
            ASSERT_NE(cat_channel_create(&channels[i], capacity, sizeof(cat_bool_t), nullptr), nullptr);
            initialized++;
            auto opcode = (push_mask & (1U << i)) ? CAT_CHANNEL_SELECT_EVENT_PUSH : CAT_CHANNEL_SELECT_EVENT_POP;
            data[i] = opcode == CAT_CHANNEL_SELECT_EVENT_PUSH;
            requests[i] = { &channels[i], { &data[i] }, opcode, cat_false };
            if (capacity != 0 && opcode == CAT_CHANNEL_SELECT_EVENT_PUSH) {
                cat_bool_t occupied = cat_false;
                ASSERT_TRUE(cat_channel_push(&channels[i], &occupied, 0));
            }
        }
    }

    ~select_channels()
    {
        // Close before joining: assertion returns must also release every blocked worker.
        for (size_t i = 0; i < initialized; i++) {
            cat_channel_cleanup(&channels[i]);
        }
        workers();
    }

    cat_coroutine_t *start(cat_timeout_t timeout = 1000)
    {
        workers++;
        return co([this, timeout] {
            DEFER(workers--);
            response = cat_channel_select(requests.data(), requests.size(), timeout);
            error = cat_get_last_error_code();
        });
    }

    void assert_waiting()
    {
        for (const auto &request : requests) {
            if (request.opcode == CAT_CHANNEL_SELECT_EVENT_PUSH) {
                EXPECT_TRUE(cat_channel_has_producers(request.channel));
            } else {
                EXPECT_TRUE(cat_channel_has_consumers(request.channel));
            }
        }
    }

    void assert_detached()
    {
        for (size_t i = 0; i < initialized; i++) {
            EXPECT_FALSE(cat_channel_has_producers(&channels[i]));
            EXPECT_FALSE(cat_channel_has_consumers(&channels[i]));
        }
    }

    void assert_reusable(size_t selected = count)
    {
        for (size_t i = 0; i < initialized; i++) {
            auto *channel = &channels[i];
            if (!cat_channel_is_available(channel)) {
                continue;
            }
            if (cat_channel_get_capacity(channel) != 0 && requests[i].opcode == CAT_CHANNEL_SELECT_EVENT_PUSH) {
                cat_bool_t buffered;
                EXPECT_EQ(cat_channel_get_length(channel), 1U);
                ASSERT_TRUE(cat_channel_pop(channel, &buffered, 0));
                EXPECT_EQ(buffered, i == selected ? cat_true : cat_false);
            } else {
                EXPECT_TRUE(cat_channel_is_empty(channel));
            }
            workers++;
            co([this, channel] {
                DEFER(workers--);
                cat_bool_t value = cat_true;
                EXPECT_TRUE(cat_channel_push(channel, &value, 1000));
            });
            cat_bool_t received = cat_false;
            EXPECT_TRUE(cat_channel_pop(channel, &received, 1000));
            EXPECT_TRUE(received);
            workers();
        }
        assert_detached();
    }
};

} // namespace

TEST(cat_channel_select_cleanup, close_order)
{
    for (auto capacity : { 0U, 1U }) {
        for (unsigned int mask = 0; mask < (1U << select_channels::count); mask++) {
            for (size_t position = 0; position < select_channels::count; position++) {
                SCOPED_TRACE(string_format("capacity=%u mask=%u closed=%zu", capacity, mask, position));
                select_channels selection;
                ASSERT_NO_FATAL_FAILURE(selection.initialize(capacity, mask));
                selection.start();
                selection.assert_waiting();
                ASSERT_TRUE(cat_channel_close(&selection.channels[position]));
                selection.workers();
                ASSERT_EQ(selection.response, &selection.requests[position]);
                EXPECT_TRUE(selection.response->error);
                EXPECT_EQ(selection.error, CAT_ECLOSED);
                selection.assert_detached();
                selection.assert_reusable();
            }
        }
    }
}

TEST(cat_channel_select_cleanup, successful_selection)
{
    for (auto capacity : { 0U, 1U }) {
        for (unsigned int mask = 0; mask < (1U << select_channels::count); mask++) {
            for (size_t position = 0; position < select_channels::count; position++) {
                SCOPED_TRACE(string_format("capacity=%u mask=%u selected=%zu", capacity, mask, position));
                select_channels selection;
                ASSERT_NO_FATAL_FAILURE(selection.initialize(capacity, mask));
                selection.start();
                selection.assert_waiting();
                cat_bool_t value = cat_true;
                if (selection.requests[position].opcode == CAT_CHANNEL_SELECT_EVENT_PUSH) {
                    ASSERT_TRUE(cat_channel_pop(&selection.channels[position], &value, 0));
                    EXPECT_EQ(value, capacity == 0 ? cat_true : cat_false);
                } else {
                    ASSERT_TRUE(cat_channel_push(&selection.channels[position], &value, 0));
                    EXPECT_TRUE(selection.data[position]);
                }
                selection.workers();
                ASSERT_EQ(selection.response, &selection.requests[position]);
                EXPECT_FALSE(selection.response->error);
                selection.assert_detached();
                selection.assert_reusable(position);
            }
        }
    }
}

TEST(cat_channel_select_cleanup, timeout_and_cancel)
{
    for (auto capacity : { 0U, 1U }) {
        for (unsigned int mask = 0; mask < (1U << select_channels::count); mask++) {
            for (auto timeout : { 0, 1, -1 }) {
                SCOPED_TRACE(string_format("capacity=%u mask=%u timeout=%d", capacity, mask, timeout));
                select_channels selection;
                ASSERT_NO_FATAL_FAILURE(selection.initialize(capacity, mask));
                cat_coroutine_t *worker = selection.start(timeout);
                if (timeout != 0) {
                    selection.assert_waiting();
                }
                if (timeout == -1) {
                    ASSERT_TRUE(cat_coroutine_resume(worker, nullptr, nullptr));
                }
                selection.workers();
                EXPECT_EQ(selection.response, nullptr);
                EXPECT_EQ(selection.error, timeout == -1 ? CAT_ECANCELED : CAT_ETIMEDOUT);
                selection.assert_detached();
                selection.assert_reusable();
            }
        }
    }
}

TEST(cat_channel_select_cleanup, duplicate_channel)
{
    for (auto capacity : { 0U, 1U }) {
        for (unsigned int mask = 0; mask < (1U << select_channels::count); mask++) {
            // Buffered mixed operations cannot all block on the same channel at once.
            if (capacity != 0 && mask != 0 && mask != (1U << select_channels::count) - 1) {
                continue;
            }
            SCOPED_TRACE(string_format("capacity=%u mask=%u", capacity, mask));
            select_channels selection;
            ASSERT_NO_FATAL_FAILURE(selection.initialize(capacity, mask));
            for (auto &request : selection.requests) {
                request.channel = &selection.channels[0];
            }
            selection.start();
            selection.assert_waiting();
            ASSERT_TRUE(cat_channel_close(&selection.channels[0]));
            selection.workers();
            ASSERT_NE(selection.response, nullptr);
            EXPECT_EQ(selection.response->channel, &selection.channels[0]);
            EXPECT_TRUE(selection.response->error);
            EXPECT_EQ(selection.error, CAT_ECLOSED);
            selection.assert_detached();
        }
    }
}

TEST(cat_channel_select_cleanup, shared_channel)
{
    for (auto capacity : { 0U, 1U }) {
        for (auto push : { false, true }) {
            for (size_t position = 0; position < select_channels::count; position++) {
                SCOPED_TRACE(string_format("capacity=%u push=%d shared=%zu", capacity, push, position));
                cat_channel_t shared;
                ASSERT_NE(cat_channel_create(&shared, capacity, sizeof(cat_bool_t), nullptr), nullptr);
                DEFER(cat_channel_cleanup(&shared));
                if (capacity != 0 && push) {
                    cat_bool_t occupied = cat_false;
                    ASSERT_TRUE(cat_channel_push(&shared, &occupied, 0));
                }
                std::array<select_channels, 8> selections;
                for (auto &selection : selections) {
                    ASSERT_NO_FATAL_FAILURE(selection.initialize(capacity, push ? (1U << select_channels::count) - 1 : 0));
                    selection.requests[position].channel = &shared;
                    selection.start();
                    selection.assert_waiting();
                }
                ASSERT_TRUE(cat_channel_close(&shared));
                for (auto &selection : selections) {
                    selection.workers();
                    ASSERT_EQ(selection.response, &selection.requests[position]);
                    EXPECT_TRUE(selection.response->error);
                    EXPECT_EQ(selection.error, CAT_ECLOSED);
                    selection.assert_detached();
                }
                EXPECT_FALSE(cat_channel_has_producers(&shared));
                EXPECT_FALSE(cat_channel_has_consumers(&shared));
            }
        }
    }
}
