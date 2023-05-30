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

TEST(cat_work, base)
{
    cat_coroutine_t *coroutine = cat_coroutine_get_current();
    uv_sem_t sem;
    bool done = false;

    ASSERT_EQ(uv_sem_init(&sem, 0), 0);
    DEFER(uv_sem_destroy(&sem));
    co([&] {
        ASSERT_TRUE(work(CAT_WORK_KIND_SLOW_IO, [&] {
            uv_sem_wait(&sem);
            done = true;
        }, TEST_IO_TIMEOUT));
        ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
    });

    ASSERT_FALSE(done);
    uv_sem_post(&sem);
    ASSERT_TRUE(cat_coroutine_yield(nullptr, nullptr));
    ASSERT_TRUE(done);
}

TEST(cat_work, timeout)
{
    ASSERT_FALSE(work(CAT_WORK_KIND_SLOW_IO, [] {
        cat_sys_usleep(100 * 1000);
    }, 1));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
}

TEST(cat_work, cancel)
{
    cat_coroutine_t *coroutine = co([] {
        bool ret = work(CAT_WORK_KIND_SLOW_IO, [] {
            cat_sys_usleep(1000); // 1ms
        }, TEST_IO_TIMEOUT);
        ASSERT_FALSE(ret);
        ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
    });
    cat_coroutine_resume(coroutine, nullptr, nullptr);
}
