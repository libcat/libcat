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

TEST(cat_async, base)
{
    cat_async_t *async = cat_async_create(nullptr);
    ASSERT_NE(async, nullptr);
    co([async] {
        ASSERT_TRUE(work(CAT_WORK_KIND_CPU, [async] {
            cat_async_notify(async);
        }, TEST_IO_TIMEOUT));
    });
    ASSERT_TRUE(cat_async_wait_and_close(async, nullptr, TEST_IO_TIMEOUT));
}

TEST(cat_async, notify_later)
{
    cat_async_t *async = cat_async_create(nullptr);
    ASSERT_NE(async, nullptr);
    co([async] {
        ASSERT_TRUE(work(CAT_WORK_KIND_SLOW_IO, [async] {
            usleep(1000);
            cat_async_notify(async);
        }, TEST_IO_TIMEOUT));
    });
    ASSERT_TRUE(cat_async_wait_and_close(async, nullptr, TEST_IO_TIMEOUT));
}

TEST(cat_async, notify_timeout)
{
    cat_async_t *async = cat_async_create(nullptr);
    ASSERT_NE(async, nullptr);
    co([async] {
        ASSERT_TRUE(work(CAT_WORK_KIND_SLOW_IO, [async] {
            usleep(1000);
            cat_async_notify(async);
        }, TEST_IO_TIMEOUT));
    });
    ASSERT_FALSE(cat_async_wait_and_close(async, nullptr, 0));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
}

TEST(cat_async, cancel)
{
    cat_async_t *async = cat_async_create(nullptr);
    ASSERT_NE(async, nullptr);
    co([async] {
        ASSERT_TRUE(work(CAT_WORK_KIND_SLOW_IO, [async] {
            usleep(1000);
            cat_async_notify(async);
        }, TEST_IO_TIMEOUT));
    });
    cat_coroutine_t *coroutine = cat_coroutine_get_current();
    co([&] {
        ASSERT_TRUE(cat_time_delay(0));
        ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
    });
    ASSERT_FALSE(cat_async_wait_and_close(async, nullptr, TEST_IO_TIMEOUT));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
}

TEST(cat_async, fast_end)
{
    cat_async_t *async = cat_async_create(nullptr);
    ASSERT_NE(async, nullptr);
    ASSERT_TRUE(work(CAT_WORK_KIND_CPU, [async] {
        cat_async_notify(async);
    }, TEST_IO_TIMEOUT));
    ASSERT_TRUE(cat_async_wait_and_close(async, nullptr, TEST_IO_TIMEOUT));
}

TEST(cat_async, never_run)
{
    cat_async_t *async = cat_async_create(nullptr);
    ASSERT_NE(async, nullptr);
    ASSERT_TRUE(cat_async_close(async, nullptr));
}

TEST(cat_async, cleanup_fast_end)
{
    cat_async_t *async = cat_async_create(nullptr);
    ASSERT_NE(async, nullptr);
    ASSERT_TRUE(work(CAT_WORK_KIND_CPU, [async] {
        cat_async_notify(async);
    }, TEST_IO_TIMEOUT));
    ASSERT_TRUE(cat_async_cleanup(async, nullptr));
}

TEST(cat_async, cleanup_later)
{
    cat_async_t *async = cat_async_create(nullptr);
    ASSERT_NE(async, nullptr);
    co([async] {
        ASSERT_TRUE(work(CAT_WORK_KIND_SLOW_IO, [async] {
            usleep(1000);
            cat_async_notify(async);
        }, TEST_IO_TIMEOUT));
    });
    ASSERT_TRUE(cat_async_cleanup(async, nullptr));
}
