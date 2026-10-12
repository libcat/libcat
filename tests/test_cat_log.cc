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

/* The thread-safe build adds the current thread id to every line, see cat_log_standard(). */
static std::string cat_test_log_thread_tag(void)
{
#ifdef CAT_THREAD_SAFE
# ifdef CAT_OS_WIN
    return "T" + std::to_string(GetCurrentThreadId()) + " ";
# else
    /* must match the cast in cat_log_standard() */
    return "T" + std::to_string((unsigned) (uintptr_t) pthread_self()) + " ";
# endif
#else
    return "";
#endif
}

/* Death tests run in a separate process, so the thread id can not be predicted.
 * Match any id instead. */
static std::string cat_test_log_thread_tag_regex(void)
{
#ifdef CAT_THREAD_SAFE
    /* gtest only supports a simple regex subset, so no character classes here. */
    return "T.+ ";
#else
    return "";
#endif
}

TEST(cat_log, info)
{
    testing::internal::CaptureStdout();
    CAT_LOG_INFO(TEST, "info log");
    std::string output = testing::internal::GetCapturedStdout();
    ASSERT_EQ(output, "[    MAIN    ] " + cat_test_log_thread_tag() + "Info: <TEST> info log\n");
}

TEST(cat_log, notice)
{
    testing::internal::CaptureStderr();
    CAT_NOTICE(TEST, "notice log");
    std::string output = testing::internal::GetCapturedStderr();
    ASSERT_EQ(output, "[    MAIN    ] " + cat_test_log_thread_tag() + "Notice: <TEST> notice log\n");
}

TEST(cat_log, warn)
{
    testing::internal::CaptureStderr();
    CAT_WARN(TEST, "warn log");
    std::string output = testing::internal::GetCapturedStderr();
    ASSERT_EQ(output, "[    MAIN    ] " + cat_test_log_thread_tag() + "Warning: <TEST> warn log\n");
}

TEST(cat_log, error)
{
    ASSERT_DEATH_IF_SUPPORTED({
        CAT_ERROR(TEST, "error log");
    }, "\\[    MAIN    \\] " + cat_test_log_thread_tag_regex() + "Error: <TEST> error log\n");
}

TEST(cat_log, core_error)
{
    ASSERT_DEATH_IF_SUPPORTED({
        CAT_CORE_ERROR(TEST, "core error log");
    }, "\\[    MAIN    \\] " + cat_test_log_thread_tag_regex() + "Core Error: <TEST> core error log\n");
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
            ASSERT_NE(output.find("] [    MAIN    ] " + cat_test_log_thread_tag() + "Info: <TEST> timestamp log\n"), std::string::npos);
            if (!relative) {
                ASSERT_EQ(output.find("[stamp"), 0);
            }
        }
    }
    CAT_LOG_G(timestamps_format) = "";
    CAT_LOG_G(show_timestamps) = 1;
    CAT_LOG_G(show_timestamps_as_relative) = cat_false;
    testing::internal::CaptureStdout();
    CAT_LOG_INFO(TEST, "timestamp log");
    const std::string output = testing::internal::GetCapturedStdout();
    ASSERT_EQ(output.find("[] [    MAIN    ] " + cat_test_log_thread_tag() + "Info: <TEST> timestamp log\n"), 0);
}

TEST(cat_log, relative_timestamps_cross_second_boundary)
{
    cat_log_globals_t globals = CAT_G(log_globals);
    DEFER(CAT_G(log_globals) = globals);
#ifdef CAT_ENABLE_DEBUG_LOG
    CAT_LOG_G(debug_level) = 0;
#endif
    CAT_LOG_G(show_timestamps) = 3;
    CAT_LOG_G(show_timestamps_as_relative) = cat_true;
    constexpr int64_t nanoseconds_per_second = 1000000000;
    auto difference = [](const struct timespec &to, const struct timespec &from) -> int64_t {
        return static_cast<int64_t>(to.tv_sec - from.tv_sec) * nanoseconds_per_second + to.tv_nsec - from.tv_nsec;
    };
    struct timespec previous_before = {}, previous_after = {}, before = {}, after = {};
    bool crossed = false;
    const cat_msec_t deadline = cat_time_msec2() + TEST_IO_TIMEOUT;
    testing::internal::CaptureStdout();
    do {
        cat_clock_gettime_monotonic(&before);
        CAT_LOG_INFO(TEST, "relative timestamp log");
        cat_clock_gettime_monotonic(&after);
        // These bounds prove that the two recorded timestamps require a nanosecond borrow.
        if (previous_after.tv_sec != 0 && before.tv_sec > previous_after.tv_sec &&
            after.tv_sec == before.tv_sec && after.tv_nsec < previous_before.tv_nsec) {
            crossed = true;
            break;
        }
        previous_before = before;
        previous_after = after;
        cat_sys_usleep(1000);
    } while (cat_time_msec2() < deadline);
    const std::string output = testing::internal::GetCapturedStdout();
    ASSERT_TRUE(crossed);
    std::istringstream lines(output);
    std::string line, last;
    while (std::getline(lines, line)) {
        if (line.find("relative timestamp log") != std::string::npos) {
            last = line;
        }
    }
    long seconds, nanoseconds;
    ASSERT_EQ(sscanf(last.c_str(), "[%ld.%ld]", &seconds, &nanoseconds), 2);
    ASSERT_GE(nanoseconds, 0);
    ASSERT_LT(nanoseconds, nanoseconds_per_second);
    const int64_t elapsed = static_cast<int64_t>(seconds) * nanoseconds_per_second + nanoseconds;
    ASSERT_GE(elapsed, difference(before, previous_after));
    ASSERT_LE(elapsed, difference(after, previous_before));
}

TEST(cat_log, runtime_configuration_matches_environment)
{
    // TestEnvironment redirects errors to stderr; the core runtime tests verify the environment setting.
    char *modules = cat_env_get_silent("CAT_LOG_MODULE_NAME_FILTER", nullptr);
    DEFER(cat_free(modules));
    if (modules != nullptr && modules[0] != '\0') {
        ASSERT_STREQ(CAT_LOG_G(module_name_filter), modules);
    } else {
        ASSERT_EQ(CAT_LOG_G(module_name_filter), nullptr);
    }
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
    ASSERT_EQ(output, "[    MAIN    ] " + cat_test_log_thread_tag() + "Info: <TEST> visible\n");
}

TEST(cat_log, buffer_growth_failure_and_recovery)
{
    static cat_buffer_realloc_function_t original_realloc;
    cat_buffer_allocator_t allocator = cat_buffer_allocator;
    original_realloc = allocator.realloc_function;
    DEFER(cat_buffer_register_allocator(&allocator));
    cat_log_globals_t globals = CAT_G(log_globals);
    DEFER(CAT_G(log_globals) = globals);
    cat_bool_t show_last_error = CAT_G(show_last_error);
    DEFER(CAT_G(show_last_error) = show_last_error);
    CAT_G(show_last_error) = cat_false;
    cat_set_error_output(stderr);
    cat_buffer_allocator_t failing = allocator;
    failing.realloc_function = [](char *value, size_t size) -> char * {
        if (value != nullptr) {
            errno = ENOMEM;
            return nullptr;
        }
        return original_realloc(value, size);
    };
    ASSERT_TRUE(cat_buffer_register_allocator(&failing));
    const std::string message(cat_getpagesize() * 2, 'x');
    testing::internal::CaptureStderr();
    CAT_NOTICE(TEST, "%s", message.c_str());
    const std::string failure = testing::internal::GetCapturedStderr();
    ASSERT_NE(failure.find("vprintf() log message failed"), std::string::npos);
    ASSERT_EQ(cat_get_last_error_code(), CAT_UNCODED);
    ASSERT_TRUE(cat_buffer_register_allocator(&allocator));
    testing::internal::CaptureStderr();
    CAT_NOTICE(TEST, "logging recovered");
    const std::string recovered = testing::internal::GetCapturedStderr();
    ASSERT_NE(recovered.find("logging recovered"), std::string::npos);
}

TEST(cat_log, write_failure_and_libc_fallback)
{
    // The fallback is process-wide; isolate output failure and recovery from other log tests.
    const auto check_write_failure = []() {
        bool valid = false;
        bool pipe_rejected = true;
#ifdef CAT_OS_UNIX_LIKE
        {
            int fds[2];
            if (pipe(fds) != 0) { exit(1); }
            DEFER(close(fds[0]));
            FILE *output = fdopen(fds[1], "w");
            if (output == nullptr) { close(fds[1]); exit(1); }
            DEFER(fclose(output));
            const int flags = fcntl(fds[1], F_GETFL);
            if (flags < 0 || fcntl(fds[1], F_SETFL, flags | O_NONBLOCK) != 0) { exit(1); }
            char bytes[4096] = {};
            ssize_t n;
            do {
                n = write(fds[1], bytes, sizeof(bytes));
            } while (n > 0 || (n < 0 && errno == EINTR));
            if (n != -1 || (errno != EAGAIN && errno != EWOULDBLOCK)) { exit(1); }
            do {
                n = write(fds[1], bytes, 1);
            } while (n > 0 || (n < 0 && errno == EINTR));
            if (n != -1 || (errno != EAGAIN && errno != EWOULDBLOCK)) { exit(1); }
            // fwrite can accept buffered bytes while the following flush fails.
            pipe_rejected = !cat_log_fwrite(output, CAT_STRL("full pipe"));
            clearerr(output);
            FILE *error_output = cat_get_error_output();
            cat_set_error_output(output);
            CAT_NOTICE(TEST, "full pipe logging");
            pipe_rejected = pipe_rejected && ferror(output) != 0;
            cat_set_error_output(error_output);
        }
#endif
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
            valid = pipe_rejected && rejected && written && fread(buffer, 1, sizeof(buffer), writable) == sizeof(buffer) &&
                memcmp(buffer, CAT_STRL("fallback works")) == 0;
            fclose(writable);
            unlink(path.c_str());
        }
        exit(valid ? 0 : 1);
    };
    ASSERT_EXIT(check_write_failure(), testing::ExitedWithCode(0), "un-outputted log message");
}
