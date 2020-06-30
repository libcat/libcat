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

/* the cat_log_standard function is called by default. */

TEST(cat_log, info)
{
    testing::internal::CaptureStdout();
    cat_info(TEST, "info log");
    std::string output = testing::internal::GetCapturedStdout();
    ASSERT_EQ(output, "Info: <TEST> info log in R1" CAT_EOL);
}

TEST(cat_log, notice)
{
    testing::internal::CaptureStderr();
    cat_notice(TEST, "notice log");
    std::string output = testing::internal::GetCapturedStderr();
    ASSERT_EQ(output, "Notice: <TEST> notice log in R1" CAT_EOL);
}

TEST(cat_log, warn)
{
    testing::internal::CaptureStderr();
    cat_warn(TEST, "warn log");
    std::string output = testing::internal::GetCapturedStderr();
    ASSERT_EQ(output, "Warning: <TEST> warn log in R1" CAT_EOL);
}

TEST(cat_log, null)
{
    testing::internal::CaptureStderr();
    cat_info(TEST, nullptr);
    std::string output = testing::internal::GetCapturedStderr();
    ASSERT_EQ(output, "Sprintf log message failed" CAT_EOL);
}

TEST(cat_log, error)
{
    ASSERT_DEATH(cat_error(TEST, "error log"), "Error: <TEST> error log in R1" CAT_EOL);
}

TEST(cat_log, core_error)
{
    ASSERT_DEATH(cat_core_error(TEST, "core error log"), "Core Error: <TEST> core error log in R1" CAT_EOL);
}
