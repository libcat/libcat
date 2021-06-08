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
    ASSERT_EQ(cat_sys_usleep(100), 0);
    ASSERT_GT(cat_time_nsec() - s, 10);
}

TEST(cat_time, msec)
{
    cat_nsec_t s = cat_time_msec();
    ASSERT_EQ(cat_sys_usleep(3000), 0);
    ASSERT_GT(cat_time_msec() - s, 1);
}

TEST(cat_time, nsec2)
{
    cat_nsec_t s = cat_time_nsec2();
    ASSERT_EQ(cat_sys_usleep(100), 0);
    ASSERT_GT(cat_time_nsec2() - s, 10);
}

TEST(cat_time, msec2)
{
    cat_nsec_t s = cat_time_msec2();
    ASSERT_EQ(cat_sys_usleep(3000), 0);
    ASSERT_GT(cat_time_msec2() - s, 1);
}

TEST(cat_time, microtime)
{
    double s = cat_microtime();
    ASSERT_EQ(cat_sys_usleep(3000), 0);
    ASSERT_GT(cat_microtime() - s, 0.001);
}

TEST(cat_time, msec_cached)
{
    cat_event_wait(); // call it before timing test
    cat_msec_t s = cat_time_msec_cached();
    ASSERT_EQ(cat_time_msec_cached() - s, 0);
    ASSERT_EQ(cat_time_msleep(3), 0);
    ASSERT_GT(cat_time_msec_cached() - s, 0);
}

TEST(cat_time, msleep)
{
    cat_event_wait(); // call it before timing test
    cat_msec_t s = cat_time_msec();
    ASSERT_EQ(cat_time_msleep(10), 0);
    s = cat_time_msec() - s;
    ASSERT_GE(s, 5);
}

TEST(cat_time, usleep)
{
    cat_event_wait(); // call it before timing test
    cat_msec_t s = cat_time_msec();
    ASSERT_EQ(cat_time_usleep(10 * 1000), 0);
    s = cat_time_msec() - s;
    ASSERT_GE(s, 5);
}

TEST(cat_time, nanosleep)
{
    cat_event_wait(); // call it before timing test
    cat_msec_t s = cat_time_msec();
    cat_timespec time = { 0, 10 * 1000 * 1000 };
    ASSERT_EQ(cat_time_nanosleep(&time, nullptr), 0);
    s = cat_time_msec() - s;
    ASSERT_GE(s, 5);
}

TEST(cat_time, sleep_zero)
{
    cat_coroutine_round_t round = cat_coroutine_get_current_round();
    ASSERT_TRUE(cat_time_delay(0));
    ASSERT_GT(cat_coroutine_get_current_round(), round);
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
        ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
    });
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
}

TEST(cat_time, msleep_cancel)
{
    cat_coroutine_t *coroutine = co([] {
        ASSERT_EQ(cat_time_msleep(1), 1);
        ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
    });
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
}

TEST(cat_time, msleep_cancel2)
{
    cat_event_wait(); // call it before timing test
    cat_coroutine_t *waiter = cat_coroutine_get_current();
    bool done = false;
    co([&] {
        ASSERT_EQ(cat_time_msleep(10), 0);
        done = true;
        // cancel the sleep of main
        cat_coroutine_resume(waiter, nullptr, nullptr);
    });
    cat_msec_t s = cat_time_msec();
    ASSERT_GT(cat_time_msleep(100), 0);
    s = cat_time_msec() - s;
    ASSERT_GE(s, 5);
    ASSERT_EQ(done, true);
    ASSERT_EQ(cat_time_msleep(1), 0);
    ASSERT_EQ(cat_time_msleep(1), 0);
    ASSERT_EQ(cat_time_msleep(1), 0);
}

TEST(cat_time, usleep_cancel)
{
    cat_coroutine_t *coroutine = co([] {
        ASSERT_EQ(cat_time_usleep(1000), -1);
        ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
    });
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
}

TEST(cat_time, nanosleep_cancel)
{
    cat_coroutine_t *coroutine = co([] {
        struct cat_timespec req;
        struct cat_timespec rem;
        req.tv_sec = 1;
        req.tv_nsec = 1000 * 1000;
        ASSERT_EQ(cat_time_nanosleep(&req, &rem), -1);
        ASSERT_EQ(rem.tv_sec, 1);
        ASSERT_EQ(rem.tv_nsec, 1000 * 1000);
        ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
    });
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
}

TEST(cat_time, nanosleep_inval)
{
    struct cat_timespec req;
    req.tv_sec = -1;
    req.tv_nsec = 0;
    ASSERT_EQ(cat_time_nanosleep(&req, nullptr), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    req.tv_sec = 0;
    req.tv_nsec = -1;
    ASSERT_EQ(cat_time_nanosleep(&req, nullptr), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    req.tv_sec = 1;
    req.tv_nsec = 999999999 + 1;
    ASSERT_EQ(cat_time_nanosleep(&req, nullptr), -1);
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
    cat_sys_usleep(10);
    ASSERT_TRUE(cat_time_delay(0));
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
}
