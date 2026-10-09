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
    CAT_LOG_INFO(TEST, "info log");
    std::string output = testing::internal::GetCapturedStdout();
    ASSERT_EQ(output, "[    MAIN    ] Info: <TEST> info log\n");
}

TEST(cat_log, notice)
{
    testing::internal::CaptureStderr();
    CAT_NOTICE(TEST, "notice log");
    std::string output = testing::internal::GetCapturedStderr();
    ASSERT_EQ(output, "[    MAIN    ] Notice: <TEST> notice log\n");
}

TEST(cat_log, warn)
{
    testing::internal::CaptureStderr();
    CAT_WARN(TEST, "warn log");
    std::string output = testing::internal::GetCapturedStderr();
    ASSERT_EQ(output, "[    MAIN    ] Warning: <TEST> warn log\n");
}

TEST(cat_log, error)
{
    ASSERT_DEATH_IF_SUPPORTED({
        CAT_ERROR(TEST, "error log");
    }, "\\[    MAIN    \\] Error: <TEST> error log\n");
}

TEST(cat_log, core_error)
{
    ASSERT_DEATH_IF_SUPPORTED({
        CAT_CORE_ERROR(TEST, "core error log");
    }, "\\[    MAIN    \\] Core Error: <TEST> core error log\n");
}

TEST(cat_log, buffer_quote)
{
    char *tmp_str;
    const char *str;
    str = cat_log_str_quote(CAT_STRL("foo\r\n"), &tmp_str);
    ASSERT_NE(str, nullptr);
    ASSERT_STREQ(str, "\"foo\\r\\n\"");
    cat_free(tmp_str);
}

TEST(cat_log, timestamps)
{
    cat_log_globals_t globals = CAT_G(log_globals);
    DEFER(CAT_G(log_globals) = globals);
    CAT_LOG_G(timestamps_format) = "stamp";
    for (unsigned int level : {1U, 2U, 3U, 4U}) {
        for (cat_bool_t relative : {cat_false, cat_true}) {
            CAT_LOG_G(show_timestamps) = level;
            CAT_LOG_G(show_timestamps_as_relative) = relative;
            testing::internal::CaptureStdout();
            CAT_LOG_INFO(TEST, "timestamp log");
            std::string output = testing::internal::GetCapturedStdout();
            ASSERT_EQ(output.front(), '[');
            ASSERT_NE(output.find("] [    MAIN    ] Info: <TEST> timestamp log\n"), std::string::npos);
            if (!relative) {
                ASSERT_EQ(output.find("[stamp"), 0);
            }
        }
    }
}

TEST(cat_log, runtime_configuration_matches_environment)
{
#ifdef CAT_ENABLE_DEBUG_LOG
    char *output = cat_env_get_silent("CAT_LOG_DEBUG_OUTPUT", nullptr);
    DEFER(cat_free(output));
    if (output != nullptr) {
        if (cat_strcasecmp(output, "stdout") == 0) {
            ASSERT_EQ(cat_get_debug_output(), stdout);
        } else if (cat_strcasecmp(output, "stderr") == 0) {
            ASSERT_EQ(cat_get_debug_output(), stderr);
        }
    }
#endif
    char *format = cat_env_get_silent("CAT_LOG_TIMESTAMPS_FORMAT", nullptr);
    DEFER(cat_free(format));
    const char *expected = "%F %T";
    if (format != nullptr) {
        if (cat_strcasecmp(format, "time") == 0) {
            expected = "%T";
        } else if (cat_strcasecmp(format, "unix") == 0) {
            expected = "%s";
        }
    }
    ASSERT_STREQ(CAT_LOG_G(timestamps_format), expected);
    ASSERT_EQ(CAT_LOG_G(show_timestamps), cat_env_get_i("CAT_LOG_SHOW_TIMESTAMPS", 0));
    ASSERT_EQ(CAT_LOG_G(show_timestamps_as_relative), cat_env_is_true("CAT_LOG_SHOW_TIMESTAMPS_AS_RELATIVE", cat_false));
}

TEST(cat_log, quote_limit)
{
    size_t size = CAT_LOG_G(str_size);
    DEFER(CAT_LOG_G(str_size) = size);
    CAT_LOG_G(str_size) = 3;
    char *temporary = nullptr;
    const char *quoted = cat_log_str_quote(CAT_STRL("abcdef"), &temporary);
    ASSERT_NE(temporary, nullptr);
    DEFER(cat_free(temporary));
    ASSERT_STREQ(quoted, "\"abc...\"");
    char *unlimited = nullptr;
    quoted = cat_log_str_quote_unlimited(CAT_STRL("abcdef"), &unlimited);
    ASSERT_NE(unlimited, nullptr);
    DEFER(cat_free(unlimited));
    ASSERT_STREQ(quoted, "\"abcdef\"");
}

TEST(cat_log, filtered_module)
{
    cat_log_globals_t globals = CAT_G(log_globals);
    DEFER(CAT_G(log_globals) = globals);
    char modules[] = "OTHER, TEST";
    CAT_LOG_G(module_name_filter) = modules;
    testing::internal::CaptureStdout();
    CAT_LOG_INFO(IGNORED, "hidden");
    CAT_LOG_INFO(TEST, "visible");
    std::string output = testing::internal::GetCapturedStdout();
    ASSERT_EQ(output, "[    MAIN    ] Info: <TEST> visible\n");
}

TEST(cat_log, write_failure_and_libc_fallback)
{
    // fallback 状态属于进程；在子进程验证写入失败及后续恢复，不影响其它日志用例。
    ASSERT_EXIT({
        bool valid = false;
        {
            std::string path = get_random_path();
            FILE *created = fopen(path.c_str(), "w");
            if (created == nullptr) {
                exit(1);
            }
            fclose(created);
            FILE *readonly = fopen(path.c_str(), "r");
            if (readonly == nullptr) {
                unlink(path.c_str());
                exit(1);
            }
            bool rejected = !cat_log_fwrite(readonly, CAT_STRL("cannot write"));
            fclose(readonly);
            FILE *writable = fopen(path.c_str(), "w+");
            if (writable == nullptr) {
                unlink(path.c_str());
                exit(1);
            }
            bool written = cat_log_fwrite(writable, CAT_STRL("fallback works"));
            rewind(writable);
            char buffer[CAT_STRLEN("fallback works")];
            valid = rejected && written && fread(buffer, 1, sizeof(buffer), writable) == sizeof(buffer) &&
                memcmp(buffer, CAT_STRL("fallback works")) == 0;
            fclose(writable);
            unlink(path.c_str());
        }
        exit(valid ? 0 : 1);
    }, testing::ExitedWithCode(0), "un-outputted log message");
}
