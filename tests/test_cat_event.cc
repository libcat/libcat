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
  |         codinghuang <2812240764@qq.com>                                  |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

#include <thread>
#include <chrono>

#ifdef CAT_OS_UNIX_LIKE
#include <sched.h>
#endif

TEST(cat_event, is_running)
{
    ASSERT_TRUE(cat_event_is_running());
}

TEST(cat_event, scheduler_function)
{
    ASSERT_EQ(CAT_COROUTINE_DATA_ERROR, cat_event_scheduler_function(nullptr));
    ASSERT_EQ(CAT_EMISUSE, cat_get_last_error_code());
    ASSERT_STREQ("Event loop is running", cat_get_last_error_message());
}

TEST(cat_event, defer)
{
    bool done = false;
    ASSERT_TRUE(cat_event_defer([](cat_data_t *data) {
        *((bool *) data) = true;
    }, &done));
    cat_time_sleep(0);
    ASSERT_TRUE(done);
}
