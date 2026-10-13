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
            cat_sys_usleep(1000);
            cat_async_notify(async);
        }, TEST_IO_TIMEOUT));
    });
    ASSERT_TRUE(cat_async_wait_and_close(async, nullptr, TEST_IO_TIMEOUT));
}

TEST(cat_async, many_notifies_are_serialized_across_threads)
{
    /* uv_async_send coalesces notifications, so the wait can be resolved by any
     * of them. The point here is that concurrent senders still leave the busy
     * counter consistent: the wait must finish instead of hanging or spinning
     * forever, and the handle must stay usable up to the close */
    cat_async_t *async = cat_async_create(nullptr);
    ASSERT_NE(async, nullptr);

    const size_t repeat = 64;
    for (size_t n = 0; n < repeat; n++) {
        co([async] {
            ASSERT_TRUE(work(CAT_WORK_KIND_CPU, [async] {
                ASSERT_EQ(cat_async_notify(async), 0);
            }, TEST_IO_TIMEOUT));
        });
    }

    ASSERT_TRUE(cat_async_wait_and_close(async, nullptr, TEST_IO_TIMEOUT));
}

TEST(cat_async, notify_timeout)
{
    uv_sem_t sem;
    ASSERT_EQ(uv_sem_init(&sem, 0), 0);
    DEFER(uv_sem_destroy(&sem));

    cat_async_t *async = cat_async_create(nullptr);
    ASSERT_NE(async, nullptr);
    wait_group wg;
    // Release notification after checking the timeout, even when an assertion fails.
    // The wait group joins the worker before the semaphore leaves scope.
    DEFER(uv_sem_post(&sem));
    co([&] {
        wg++;
        DEFER(wg--);
        ASSERT_TRUE(work(CAT_WORK_KIND_SLOW_IO, [&] {
            uv_sem_wait(&sem);
            cat_async_notify(async);
        }, TEST_IO_TIMEOUT));
    });
    ASSERT_FALSE(cat_async_wait_and_close(async, nullptr, 0));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
}

TEST(cat_async, cancel)
{
    static bool done;
    done = false;
    cat_async_t *async = cat_async_create(nullptr);
    ASSERT_NE(async, nullptr);
    co([async] {
        ASSERT_TRUE(work(CAT_WORK_KIND_SLOW_IO, [async] {
            while (!done) {
                cat_sys_usleep(1000);
            }
            cat_async_notify(async);
        }, TEST_IO_TIMEOUT));
    });
    cat_coroutine_t *coroutine = co([&] {
        ASSERT_FALSE(cat_async_wait_and_close(async, nullptr, TEST_IO_TIMEOUT));
        ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
    });
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
    done = true;
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
    // Let the event loop deliver the notification before exercising fast cleanup.
    ASSERT_TRUE(cat_time_delay(0));
    ASSERT_TRUE(async->done);
    ASSERT_TRUE(cat_async_cleanup(async, nullptr));
}

TEST(cat_async, cleanup_later)
{
    cat_async_t *async = cat_async_create(nullptr);
    ASSERT_NE(async, nullptr);
    co([async] {
        ASSERT_TRUE(work(CAT_WORK_KIND_SLOW_IO, [async] {
            cat_sys_usleep(1000);
            cat_async_notify(async);
        }, TEST_IO_TIMEOUT));
    });
    ASSERT_TRUE(cat_async_cleanup(async, nullptr));
}

TEST(cat_async, reject_cleanup_while_waiting)
{
    cat_async_t *async = cat_async_create(nullptr);
    ASSERT_NE(async, nullptr);
    bool notified = false;
    bool completed = false;
    wait_group wg;
    // Release the original waiter even if an assertion returns before notification.
    DEFER(if (!notified) { cat_async_notify(async); } wg());
    co([&] {
        wg++;
        DEFER(wg--);
        ASSERT_TRUE(cat_async_wait_and_close(async, nullptr, TEST_IO_TIMEOUT));
        completed = true;
    });
    ASSERT_FALSE(completed);
    ASSERT_FALSE(cat_async_cleanup(async, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_FALSE(cat_async_close(async, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_FALSE(cat_async_wait_and_close(async, nullptr, 0));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_FALSE(completed);
    ASSERT_EQ(cat_async_notify(async), 0);
    notified = true;
    ASSERT_TRUE(wg());
    ASSERT_TRUE(completed);
}
