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

TEST(cat_time, msleep)
{
    cat_msec_t s = cat_time_msec();
    ASSERT_EQ(cat_time_msleep(10), 0);
    s = cat_time_msec() - s;
    ASSERT_GE(s, 10);
}

TEST(cat_time, usleep)
{
    cat_msec_t s = cat_time_msec();
    ASSERT_EQ(cat_time_usleep(10 * 1000), 0);
    ASSERT_GE(s, 10);
}

TEST(cat_time, nanosleep)
{
    cat_msec_t s = cat_time_msec();
    timespec time = { 0, 10 * 1000 * 1000 };
    ASSERT_EQ(cat_time_nanosleep(&time, nullptr), 0);
    s = cat_time_msec() - s;
    ASSERT_GE(s, 10);
}

TEST(cat_time, msleep_cancel)
{
    cat_coroutine_t *waiter = cat_coroutine_get_current();
    bool done = false;
    co([&] {
        EXPECT_EQ(cat_time_msleep(5), 0);
        // cancel the sleep of main
        cat_coroutine_resume(waiter, nullptr, nullptr);
        done = true;
    });
    cat_msec_t s = cat_time_msec();
    EXPECT_GT(cat_time_msleep(100), 0);
    s = cat_time_msec() - s;
    ASSERT_GE(s, 5);
    EXPECT_EQ(cat_time_msleep(1), 0);
    EXPECT_EQ(cat_time_msleep(1), 0);
    EXPECT_EQ(cat_time_msleep(1), 0);
    EXPECT_EQ(done, true);
}

TEST(cat_time, sleep_zero)
{
    cat_coroutine_round_t round = cat_coroutine_get_current_round();
    EXPECT_EQ(cat_time_sleep(0), 0UL);
    EXPECT_GT(cat_coroutine_get_current_round(), round);
}
