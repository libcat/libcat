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
 */

#include "test.h"

TEST(cat_coroutine, switch_denied_then_resume)
{
    bool ran = false;
    cat_coroutine_t coroutine;
    ASSERT_EQ(cat_coroutine_create(&coroutine, [](cat_data_t *data) -> cat_data_t * {
        *static_cast<bool *>(data) = true;
        return nullptr;
    }), &coroutine);
    DEFER(if (cat_coroutine_is_available(&coroutine)) { cat_coroutine_close(&coroutine); });
    ASSERT_EQ(cat_coroutine_get_start_time(&coroutine), 0);
    ASSERT_EQ(cat_coroutine_get_end_time(&coroutine), 0);
    ASSERT_EQ(cat_coroutine_get_elapsed(&coroutine), 0);
    ASSERT_FALSE(cat_coroutine_switch_denied());
    cat_coroutine_switch_deny();
    DEFER(cat_coroutine_switch_allow());
    ASSERT_TRUE(cat_coroutine_switch_denied());
    ASSERT_FALSE(cat_coroutine_resume(&coroutine, &ran, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);
    ASSERT_FALSE(ran);
    ASSERT_TRUE(cat_coroutine_is_available(&coroutine));
    ASSERT_FALSE(cat_coroutine_yield(nullptr, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);
    cat_coroutine_switch_allow();
    ASSERT_FALSE(cat_coroutine_switch_denied());
    ASSERT_TRUE(cat_coroutine_resume(&coroutine, &ran, nullptr));
    ASSERT_TRUE(ran);
    ASSERT_TRUE(cat_coroutine_is_over(&coroutine));
    ASSERT_GE(cat_coroutine_get_end_time(&coroutine), cat_coroutine_get_start_time(&coroutine));
    ASSERT_EQ(cat_coroutine_get_elapsed(&coroutine),
        cat_coroutine_get_end_time(&coroutine) - cat_coroutine_get_start_time(&coroutine));
}

TEST(cat_coroutine, close_rejects_waiting_and_uninitialized_coroutines)
{
    cat_coroutine_t coroutine;
    ASSERT_EQ(cat_coroutine_create(&coroutine, [](cat_data_t *) -> cat_data_t * {
        EXPECT_TRUE(cat_coroutine_yield(nullptr, nullptr));
        return nullptr;
    }), &coroutine);
    DEFER(if (cat_coroutine_is_alive(&coroutine)) {
        cat_coroutine_resume(&coroutine, nullptr, nullptr);
    } else if (cat_coroutine_is_available(&coroutine)) {
        cat_coroutine_close(&coroutine);
    });
    ASSERT_TRUE(cat_coroutine_resume(&coroutine, nullptr, nullptr));
    ASSERT_TRUE(cat_coroutine_is_alive(&coroutine));
    ASSERT_FALSE(cat_coroutine_close(&coroutine));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBUSY);
    ASSERT_TRUE(cat_coroutine_is_available(&coroutine));
    ASSERT_TRUE(cat_coroutine_resume(&coroutine, nullptr, nullptr));
    ASSERT_TRUE(cat_coroutine_is_over(&coroutine));
    ASSERT_FALSE(cat_coroutine_resume(&coroutine, nullptr, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ESRCH);
    cat_coroutine_t uninitialized = {};
    ASSERT_FALSE(cat_coroutine_resume(&uninitialized, nullptr, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);
    ASSERT_FALSE(cat_coroutine_close(&uninitialized));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
}
