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

TEST(cat, string)
{
    char *name = cat_sprintf(
        "%s v%d.%d.%d%s%s",
        "libcat", CAT_MAJOR_VERSION, CAT_MINOR_VERSION, CAT_RELEASE_VERSION, CAT_STRLEN(CAT_EXTRA_VERSION) > 0 ? "-" : "", CAT_EXTRA_VERSION
    );
    ASSERT_STREQ(name, "libcat v" CAT_VERSION);
    cat_free(name);
}

TEST(cat_sys, sleep)
{
    ASSERT_EQ(cat_sys_sleep(0), 0);
}

TEST(cat_sys, usleep)
{
    ASSERT_EQ(cat_sys_usleep(1), 0);
}

TEST(cat_sys, nanosleep)
{
    cat_timespec req = { 0, 1000 };
    ASSERT_EQ(cat_sys_nanosleep(&req, NULL), 0);
}

TEST(cat_sys, nanosleep_failed)
{
    cat_timespec req = { 0, INT_MAX };
    ASSERT_EQ(cat_sys_nanosleep(&req, NULL), -1);
    ASSERT_EQ(cat_translate_sys_error(cat_sys_errno), CAT_EINVAL);
}

TEST(cat, runtime_init_with_error_output_stdout)
{
    FILE *original_error_output = cat_get_error_output();
    DEFER(cat_set_error_output(original_error_output));

    cat_env_set("CAT_LOG_ERROR_OUTPUT", "stdout");
    cat_runtime_shutdown();
    cat_runtime_init();
    testing::internal::CaptureStdout();
    CAT_WARN(CORE, "Now error log is stdout");
    std::string output = testing::internal::GetCapturedStdout();
    ASSERT_NE(output.find("Now error log is stdout"), std::string::npos);
}

TEST(cat, runtime_init_with_error_output_stderr)
{
    FILE *original_error_output = cat_get_error_output();
    DEFER(cat_set_error_output(original_error_output));

    cat_env_set("CAT_LOG_ERROR_OUTPUT", "stderr");
    cat_runtime_shutdown();
    cat_runtime_init();
    testing::internal::CaptureStderr();
    CAT_WARN(CORE, "Now error log is stderr");
    std::string output = testing::internal::GetCapturedStderr();
    ASSERT_NE(output.find("Now error log is stderr"), std::string::npos);
}

#ifdef CAT_ENABLE_DEBUG_LOG
TEST(cat, enable_debug_mode)
{
    cat_log_types_t log_types = CAT_LOG_G(types);
    unsigned int debug_log_level = CAT_LOG_G(debug_level);
    cat_enable_debug_log_mode();
    ASSERT_EQ(CAT_LOG_G(types), CAT_LOG_TYPES_ALL);
    CAT_LOG_G(debug_level) = debug_log_level;
    CAT_LOG_G(types) = log_types;
}
#endif

TEST(cat, setup_args)
{
    ASSERT_DEATH_IF_SUPPORTED(cat_setup_args(0, nullptr), "misuse");
}

TEST(cat, exepath)
{
    const cat_const_string_t *exepath = cat_exepath();
    ASSERT_NE(exepath, nullptr);
    const cat_const_string_t *exepath2 = cat_exepath();
    ASSERT_NE(exepath2, nullptr);
    ASSERT_EQ(exepath, exepath2);
}

TEST(cat, getpid)
{
    ASSERT_GT(cat_getpid(), 0);
}

TEST(cat, getppid)
{
    ASSERT_GT(cat_getppid(), 0);
}

TEST(cat, process_title)
{
    const std::string custom_title = "cat_test";
    bool titled = cat_set_process_title(custom_title.c_str());
    char *process_title;

    process_title = cat_get_process_title(NULL, 0);
    ASSERT_NE(process_title, nullptr);
    DEFER(cat_free(process_title));

    if (titled) {
        ASSERT_EQ(custom_title, std::string(process_title));
    }
}

#include <thread>

TEST(cat, is_main_thread)
{
    ASSERT_TRUE(cat_is_main_thread());
}

TEST(cat, is_not_main_thread)
{
    bool is_main_thread = true;

    std::thread thread([&is_main_thread] {
        is_main_thread = cat_is_main_thread();
    });
    thread.join();

    ASSERT_FALSE(is_main_thread);
}

#if CAT_USE_BUG_DETECTOR
TEST(cat, bug_detection)
{
    ASSERT_DEATH_IF_SUPPORTED(([] {
        char *foo = nullptr;
        foo[0] = 'x';
    })(), "bug occurred");
}
#endif
