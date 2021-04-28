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

TEST(cat_time, nsec)
{
    cat_nsec_t s = cat_time_nsec();
    ASSERT_GT(cat_time_nsec() - s, 0);
}

TEST(cat_time, msec)
{
    cat_msec_t s = cat_time_msec_cached();
    ASSERT_EQ(cat_time_msec_cached() - s, 0);
    ASSERT_EQ(cat_time_msleep(1), 0);
    ASSERT_GT(cat_time_msec_cached() - s, 0);
}

TEST(cat_time, msleep)
{
    cat_msec_t s = cat_time_msec();
    ASSERT_EQ(cat_time_msleep(10), 0);
    s = cat_time_msec() - s;
    ASSERT_GE(s, 5);
}

TEST(cat_time, usleep)
{
    cat_msec_t s = cat_time_msec();
    ASSERT_EQ(cat_time_usleep(10 * 1000), 0);
    ASSERT_GE(s, 5);
}

TEST(cat_time, nanosleep)
{
    cat_msec_t s = cat_time_msec();
    timespec time = { 0, 10 * 1000 * 1000 };
    ASSERT_EQ(cat_time_nanosleep(&time, nullptr), 0);
    s = cat_time_msec() - s;
    ASSERT_GE(s, 5);
}

TEST(cat_time, sleep_zero)
{
    cat_coroutine_round_t round = cat_coroutine_get_current_round();
    EXPECT_EQ(cat_time_sleep(0), 0);
    EXPECT_GT(cat_coroutine_get_current_round(), round);
}

static std::string time_format_msec(cat_msec_t msec)
{
    char *s = cat_time_format_msec(msec);
    DEFER(cat_free(s));
    return std::string(s);
}

TEST(cat_time, format_msec)
{
    ASSERT_EQ(time_format_msec(1ULL), "1ms");
    ASSERT_EQ(time_format_msec(10ULL), "10ms");
    ASSERT_EQ(time_format_msec(100ULL), "100ms");
    ASSERT_EQ(time_format_msec(1000ULL), "1s0ms");
    ASSERT_EQ(time_format_msec(1001ULL), "1s1ms");
    ASSERT_EQ(time_format_msec(60ULL * 1000ULL), "1m0s0ms");
    ASSERT_EQ(time_format_msec(60ULL * 1000ULL + 1000ULL + 1ULL), "1m1s1ms");
    ASSERT_EQ(time_format_msec(60ULL * 60ULL * 1000ULL), "1h0m0s0ms");
    ASSERT_EQ(time_format_msec(24ULL * 60ULL * 60ULL * 1000ULL + 60ULL * 60ULL * 1000ULL + 60ULL * 1000ULL + 1000ULL + 1ULL), "1d1h1m1s1ms");
    ASSERT_EQ(time_format_msec(365ULL * 24ULL * 60ULL * 60ULL * 1000ULL), "365d0h0m0s0ms");
}

TEST(cat_time, delay_cancel)
{
    cat_coroutine_t *coroutine = co([] {
        ASSERT_EQ(cat_time_delay(CAT_TIMEOUT_FOREVER), CAT_RET_NONE);
    });
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
}

TEST(cat_time, sleep_cancel)
{
    cat_coroutine_t *coroutine = co([] {
        ASSERT_EQ(cat_time_sleep(1), 1);
    });
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
}

TEST(cat_time, msleep_cancel)
{
    cat_coroutine_t *coroutine = co([] {
        ASSERT_EQ(cat_time_msleep(1), 1);
    });
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
}

TEST(cat_time, msleep_cancel2)
{
    cat_coroutine_t *waiter = cat_coroutine_get_current();
    bool done = false;
    co([&] {
        EXPECT_EQ(cat_time_msleep(10), 0);
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

TEST(cat_time, usleep_cancel)
{
    cat_coroutine_t *coroutine = co([] {
        ASSERT_EQ(cat_time_usleep(1000), -1);
    });
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
}

TEST(cat_time, nanosleep_cancel)
{
    cat_coroutine_t *coroutine = co([] {
        struct timespec rqtp;
        struct timespec rmtp;
        rqtp.tv_sec = 1;
        rqtp.tv_nsec = 1000 * 1000;
        ASSERT_EQ(cat_time_nanosleep(&rqtp, &rmtp), -1);
        ASSERT_EQ(rmtp.tv_sec, 1);
        ASSERT_EQ(rmtp.tv_nsec, 1000 * 1000);
    });
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
}

TEST(cat_time, nanosleep_inval)
{
    struct timespec rqtp;
    rqtp.tv_sec = -1;
    rqtp.tv_nsec = 0;
    ASSERT_EQ(cat_time_nanosleep(&rqtp, nullptr), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    rqtp.tv_sec = 0;
    rqtp.tv_nsec = -1;
    ASSERT_EQ(cat_time_nanosleep(&rqtp, nullptr), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    rqtp.tv_sec = 1;
    rqtp.tv_nsec = 999999999 + 1;
    ASSERT_EQ(cat_time_nanosleep(&rqtp, nullptr), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

TEST(cat_time, tv2to)
{
    struct timeval tv;
    ASSERT_EQ(cat_time_tv2to(nullptr), -1);
    tv.tv_sec = -1;
    tv.tv_usec = 0;
    ASSERT_EQ(cat_time_tv2to(&tv), -1);
    tv.tv_sec = 1;
    tv.tv_usec = 1;
    ASSERT_EQ(cat_time_tv2to(&tv), 1000);
    tv.tv_usec = 1000;
    ASSERT_EQ(cat_time_tv2to(&tv), 1001);
}

TEST(cat_time, blocking)
{
    cat_coroutine_t *coroutine = co([] {
        cat_msec_t r = cat_time_msleep(1);
        ASSERT_EQ(r, 1);
        ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
    });
    usleep(10);
    ASSERT_TRUE(cat_time_delay(0));
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
}
