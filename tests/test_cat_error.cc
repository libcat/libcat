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
  | Author: codinghuang <2812240764@qq.com>                                  |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

TEST(cat_error, update_last_error_null)
{
    testing::internal::CaptureStderr();
    cat_update_last_error(CAT_UNKNOWN, NULL);
    std::string output = testing::internal::GetCapturedStderr();
    ASSERT_EQ(output, "Sprintf last error message failed" CAT_EOL);
}

TEST(cat_error, strerror)
{
    std::string output;

    output = cat_strerror(CAT_UNKNOWN);
    ASSERT_EQ(output, "unknown error");
    output = cat_strerror(CAT_EAGAIN);
    ASSERT_EQ(output, "resource temporarily unavailable");
    output = cat_strerror(INT_MAX);
    ASSERT_EQ(output, "unknown error");
}
