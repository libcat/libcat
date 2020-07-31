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
  | Author: Twosee <twose@qq.com>                                            |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

#include <array>

TEST(cat_signal, wait)
{
    const std::array<int, 4> signals{ SIGINT, SIGTERM, SIGUSR1, SIGUSR2 };
    static bool killed, wait_done;

    for (static int signal : signals) {
        killed = wait_done = false;

        cat_coroutine_run_ez(NULL, []() {
            cat_time_sleep(0);
            // TODO: cat_getpid()
            EXPECT_TRUE(cat_kill(getpid(), signal));
            EXPECT_FALSE(wait_done);
            killed = true;
        });

        EXPECT_TRUE(cat_signal_wait(signal, TEST_IO_TIMEOUT));
        EXPECT_TRUE(killed);
        wait_done = true;
    }
}

TEST(cat_signal, invalid_kill)
{
    const std::array<int, 2> invalid_signals{ SIGKILL, SIGSTOP };

    for (int signal : invalid_signals) {
        EXPECT_FALSE(cat_signal_wait(signal, TEST_IO_TIMEOUT));
        EXPECT_EQ(CAT_EINVAL, cat_get_last_error_code());
    }
}

TEST(cat_signal, timeout)
{
    ASSERT_FALSE(cat_signal_wait(SIGTERM, 1));
    ASSERT_EQ(CAT_ETIMEDOUT, cat_get_last_error_code());
}

TEST(cat_signal, cancel)
{
    cat_coroutine_run(NULL, [](cat_data_t *data) {
        cat_coroutine_t *coroutine = (cat_coroutine_t *) data;
        cat_time_sleep(0);
        EXPECT_TRUE(cat_coroutine_resume_ez(coroutine));
        return CAT_COROUTINE_DATA_NULL;
    }, cat_coroutine_get_current());
    ASSERT_FALSE(cat_signal_wait(SIGTERM, TEST_IO_TIMEOUT));
    ASSERT_EQ(CAT_ECANCELED, cat_get_last_error_code());
}
