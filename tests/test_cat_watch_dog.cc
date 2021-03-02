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

const std::string keyword = "Watch-Dog";

TEST(cat_watch_dog, base)
{
    testing::internal::CaptureStderr();

    ASSERT_LT(cat_watch_dog_get_threshold(), 0);

    ASSERT_TRUE(cat_watch_dog_run(nullptr, 0, 0, nullptr));
    DEFER(cat_watch_dog_stop());

    ASSERT_GT(cat_watch_dog_get_threshold(), 0);

    co([=] {
        usleep((cat_watch_dog_get_quantum() / 1000) * 10);
    });

    std::string output = testing::internal::GetCapturedStderr();
    std::string::size_type pos = 0;
    size_t count = 0;
    while ((pos = output.find(keyword, pos)) != std::string::npos) {
         count++;
         pos += keyword.length();
    }
    ASSERT_GT(count, 0UL);
    ASSERT_LT(count, 50UL);
}

TEST(cat_watch_dog, single)
{
    // recycle possible residual coroutines
    cat_time_wait(0);

    testing::internal::CaptureStderr();

    ASSERT_TRUE(cat_watch_dog_run(nullptr, 0, 0, nullptr));
    DEFER(cat_watch_dog_stop());

    usleep((cat_watch_dog_get_quantum() / 1000) * 3);

    std::string output = testing::internal::GetCapturedStderr();
    ASSERT_TRUE(output.find(keyword) == std::string::npos);
}

TEST(cat_watch_dog, scheduler_blocking)
{
    testing::internal::CaptureStderr();

    ASSERT_TRUE(cat_watch_dog_run(nullptr, 0, 0, nullptr));
    DEFER(cat_watch_dog_stop());

    cat_time_usleep((cat_watch_dog_get_quantum() / 1000) * 3);

    std::string output = testing::internal::GetCapturedStderr();
    ASSERT_TRUE(output.find(keyword) == std::string::npos);
}

TEST(cat_watch_dog, nothing)
{
    testing::internal::CaptureStderr();

    ASSERT_TRUE(cat_watch_dog_run(nullptr, 0, 0, nullptr));
    DEFER(cat_watch_dog_stop());

    auto sleeper = [=] {
        cat_nsec_t nsec = ((cat_watch_dog_get_quantum() / 1000) * 3) / TEST_MAX_REQUESTS;
        size_t n = TEST_MAX_REQUESTS;
        while (n--) {
            cat_time_usleep(nsec);
        }
    };

    for (size_t c = TEST_MAX_CONCURRENCY; c--;) {
        co(sleeper);
    }
    sleeper();

    std::string output = testing::internal::GetCapturedStderr();
    ASSERT_TRUE(output.find(keyword) == std::string::npos);
}

TEST(cat_watch_dog, duplicated)
{
    ASSERT_TRUE(cat_watch_dog_run(
        nullptr,
        CAT_WATCH_DOG_DEFAULT_QUANTUM + 1 /* code cov */,
        CAT_WATCH_DOG_THRESHOLD_DISABLED, /* code cov */
        cat_watch_dog_alert_standard
    ));
    ASSERT_FALSE(cat_watch_dog_run(nullptr, 0, 0, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);
    ASSERT_TRUE(cat_watch_dog_stop());
    ASSERT_FALSE(cat_watch_dog_stop());
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);
}

TEST(cat_watch_dog, not_running)
{
    ASSERT_EQ(cat_watch_dog_get_quantum(), -1);
}
