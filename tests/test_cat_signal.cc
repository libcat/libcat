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

// FIXME: How to watch signal on Windows?
#ifndef CAT_OS_WIN

#include <array>

TEST(cat_signal, wait)
{
    const std::array<int, 4> signals{ CAT_SIGINT, CAT_SIGTERM, CAT_SIGUSR1, CAT_SIGUSR2 };
    bool killed, wait_done;
    wait_group wg;
    DEFER({
        wg();
        ASSERT_TRUE(wait_done);
    });

    for (int signum : signals) {
        killed = wait_done = false;

        co([&, signum] {
            wg++;
            DEFER(wg--);
            ASSERT_TRUE(cat_signal_wait(signum, TEST_IO_TIMEOUT));
            ASSERT_TRUE(killed);
            wait_done = true;
        });

        ASSERT_TRUE(cat_kill(cat_getpid(), signum));
        killed = true;
        ASSERT_FALSE(wait_done);
    }
}

TEST(cat_signal, wait_multi)
{
    wait_group wg;
    for (size_t n = TEST_MAX_CONCURRENCY; n--;) {
        co([&] {
            wg++;
            DEFER(wg--);
            ASSERT_TRUE(cat_signal_wait(CAT_SIGUSR1, TEST_IO_TIMEOUT));
        });
    }
    ASSERT_TRUE(cat_kill(cat_getpid(), CAT_SIGUSR1));
}

TEST(cat_signal, invalid_kill)
{
    const std::array<int, 2> invalid_signals{ CAT_SIGKILL, CAT_SIGSTOP };

    for (int signum : invalid_signals) {
        EXPECT_FALSE(cat_signal_wait(signum, TEST_IO_TIMEOUT));
        EXPECT_EQ(CAT_EINVAL, cat_get_last_error_code());
    }
}

TEST(cat_signal, timeout)
{
    ASSERT_FALSE(cat_signal_wait(CAT_SIGUSR1, 1));
    ASSERT_EQ(CAT_ETIMEDOUT, cat_get_last_error_code());
}

TEST(cat_signal, cancel)
{
    cat_coroutine_t *coroutine = co([&] {
        ASSERT_FALSE(cat_signal_wait(CAT_SIGUSR1, TEST_IO_TIMEOUT));
        ASSERT_EQ(CAT_ECANCELED, cat_get_last_error_code());
    });
    cat_coroutine_resume(coroutine, nullptr, nullptr);
}

#endif
