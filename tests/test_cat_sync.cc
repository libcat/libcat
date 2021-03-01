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

TEST(cat_sync_wait_group, base)
{
    cat_sync_wait_group_t *wg, _wg;
    size_t done = 0;

    wg = cat_sync_wait_group_create(&_wg);
    ASSERT_NE(wg, nullptr);

    ASSERT_TRUE(cat_sync_wait_group_add(wg, 1));
    co([wg, &done] {
        ASSERT_EQ(cat_time_sleep(0), 0UL);
        done++;
        ASSERT_TRUE(cat_sync_wait_group_done(wg));
    });

    co([wg, &done] {
        ASSERT_TRUE(cat_sync_wait_group_add(wg, 1));
        ASSERT_EQ(cat_time_sleep(0), 0UL);
        done++;
        ASSERT_TRUE(cat_sync_wait_group_done(wg));
    });

    ASSERT_TRUE(cat_sync_wait_group_add(wg, 8));
    for (size_t n = 8; n--;) {
        co([wg, &done] {
            ASSERT_EQ(cat_time_sleep(0), 0UL);
            done++;
            ASSERT_TRUE(cat_sync_wait_group_done(wg));
        });
    }

    ASSERT_TRUE(cat_sync_wait_group_wait(wg, TEST_IO_TIMEOUT));

    ASSERT_EQ(done, 10ULL);
}

TEST(cat_sync_wait_group, error)
{
    cat_sync_wait_group_t *wg, _wg;

    wg = cat_sync_wait_group_create(&_wg);
    ASSERT_NE(wg, nullptr);

    // negative
    ASSERT_FALSE(cat_sync_wait_group_add(wg, -1));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);
    ASSERT_FALSE(cat_sync_wait_group_done(wg));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);

    co([wg] {
        ASSERT_EQ(cat_time_sleep(0), 0UL);
        // add during waiting
        ASSERT_FALSE(cat_sync_wait_group_add(wg, 1));
        ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);
        // multi wait
        ASSERT_FALSE(cat_sync_wait_group_wait(wg, 0));
        ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);
    });

    // wait timeout
    ASSERT_TRUE(cat_sync_wait_group_add(wg, 1));
    ASSERT_FALSE(cat_sync_wait_group_wait(wg, 1));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
    ASSERT_TRUE(cat_sync_wait_group_add(wg, -1));

    // wait with count 0
    ASSERT_TRUE(cat_sync_wait_group_wait(wg, TEST_IO_TIMEOUT));

    // wait has been canceled
    cat_coroutine_t *coroutine = cat_coroutine_get_current();
    co([coroutine] {
        ASSERT_EQ(cat_time_sleep(0), 0UL);
        ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
    });
    ASSERT_TRUE(cat_sync_wait_group_add(wg, 1));
    ASSERT_FALSE(cat_sync_wait_group_wait(wg, TEST_IO_TIMEOUT));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
}
