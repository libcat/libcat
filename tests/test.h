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
  |         dixyes <dixyes@gmail.com>                                        |
  |         codinghuang <2812240764@qq.com>                                  |
  +--------------------------------------------------------------------------+
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <array>

#include "cat_api.h"

/* ext, not enabled by default */
#include "cat_curl.h"

/* GTEST_SKIP and GTEST_SKIP_ shim */
#ifndef GTEST_SKIP
# define GTEST_SKIP() {/* do nothing */}
#endif
#ifndef GTEST_SKIP_
# define GTEST_SKIP_(message) {printf("skip: %s\n", message);}
#endif

/* common macros */

#define SKIP_IF(expression) do { \
    if (expression) { \
        GTEST_SKIP(); \
        return; \
    } \
} while (0)

#define SKIP_IF_(expression, message) do { \
    if (expression) { \
        GTEST_SKIP_(message); \
        return; \
    } \
} while (0)

#define SKIP_IF_OFFLINE()      SKIP_IF_(is_offline(), "Internet connection required")
#define SKIP_IF_USE_VALGRIND() SKIP_IF_(is_valgrind(), "Valgrind is too slow")

#define TEST_BUFFER_SIZE_STD               8192

#define TEST_IO_TIMEOUT                    ::testing::CONFIG_IO_TIMEOUT

#define TEST_MAX_REQUESTS                  ::testing::CONFIG_MAX_REQUESTS
#define TEST_MAX_CONCURRENCY               ::testing::CONFIG_MAX_CONCURRENCY

#define TEST_LISTEN_HOST                   "localhost"
#define TEST_LISTEN_IPV4                   "127.0.0.1"
#define TEST_LISTEN_IPV6                   "::1"
#define TEST_TMP_PATH                      ::testing::CONFIG_TMP_PATH.c_str()
#ifndef CAT_OS_WIN
#define TEST_PATH_SEP                      "/"
#define TEST_PIPE_PATH                     "/tmp/cat_test.sock"
#define TEST_PIPE_PATH_FMT                 "/tmp/cat_test_%s.sock"
#else
#define TEST_PATH_SEP                      "\\"
#define TEST_PIPE_PATH                     "\\\\?\\pipe\\cat_test"
#define TEST_PIPE_PATH_FMT                 "\\\\?\\pipe\\cat_test_%s"
#endif
#define TEST_SERVER_BACKLOG                8192

#define TEST_REMOTE_HTTP_SERVER            ::testing::CONFIG_REMOTE_HTTP_SERVER_HOST.c_str(), ::testing::CONFIG_REMOTE_HTTP_SERVER_HOST.length(), ::testing::CONFIG_REMOTE_HTTP_SERVER_PORT
#define TEST_REMOTE_HTTP_SERVER_HOST       ::testing::CONFIG_REMOTE_HTTP_SERVER_HOST.c_str()
#define TEST_REMOTE_HTTP_SERVER_PORT       ::testing::CONFIG_REMOTE_HTTP_SERVER_PORT
#define TEST_REMOTE_HTTP_SERVER_KEYWORD    ::testing::CONFIG_REMOTE_HTTP_SERVER_KEYWORD

#define TEST_REMOTE_HTTPS_SERVER           ::testing::CONFIG_REMOTE_HTTPS_SERVER_HOST.c_str(), ::testing::CONFIG_REMOTE_HTTPS_SERVER_HOST.length(), ::testing::CONFIG_REMOTE_HTTPS_SERVER_PORT
#define TEST_REMOTE_HTTPS_SERVER_HOST      ::testing::CONFIG_REMOTE_HTTPS_SERVER_HOST.c_str()
#define TEST_REMOTE_HTTPS_SERVER_PORT      ::testing::CONFIG_REMOTE_HTTPS_SERVER_PORT
#define TEST_REMOTE_HTTPS_SERVER_KEYWORD   ::testing::CONFIG_REMOTE_HTTPS_SERVER_KEYWORD

#define TEST_REMOTE_IPV6_HTTP_SERVER_HOST  ::testing::CONFIG_REMOTE_IPV6_HTTP_SERVER_HOST.c_str()

#define TEST_HTTP_STATUS_CODE_OK           200
#define TEST_HTTP_CLIENT_FAKE_USERAGENT    "curl/7.58.0"

#ifdef CAT_SSL
#define TEST_SERVER_SSL_CA_FILE                 ::testing::CONFIG_SSL_CA_FILE.c_str()

#define TEST_SERVER_SSL_CERTIFICATE             ::testing::CONFIG_SERVER_SSL_CERTIFICATE.c_str()
#define TEST_SERVER_SSL_CERTIFICATE_KEY         ::testing::CONFIG_SERVER_SSL_CERTIFICATE_KEY.c_str()
#define TEST_CLIENT_SSL_CERTIFICATE             ::testing::CONFIG_CLIENT_SSL_CERTIFICATE.c_str()
#define TEST_CLIENT_SSL_CERTIFICATE_KEY         ::testing::CONFIG_CLIENT_SSL_CERTIFICATE_KEY.c_str()

#define TEST_SSL_CERTIFICATE_PASSPHRASE         ::testing::CONFIG_SSL_CERTIFICATE_PASSPHRASE.c_str()
#define TEST_SERVER_SSL_CERTIFICATE_ENCODED     ::testing::CONFIG_SERVER_SSL_CERTIFICATE_ENCODED.c_str()
#define TEST_SERVER_SSL_CERTIFICATE_KEY_ENCODED ::testing::CONFIG_SERVER_SSL_CERTIFICATE_KEY_ENCODED.c_str()
#define TEST_CLIENT_SSL_CERTIFICATE_ENCODED     ::testing::CONFIG_CLIENT_SSL_CERTIFICATE_ENCODED.c_str()
#define TEST_CLIENT_SSL_CERTIFICATE_KEY_ENCODED ::testing::CONFIG_CLIENT_SSL_CERTIFICATE_KEY_ENCODED.c_str()
#endif

#define PP_CAT(a, b) PP_CAT_I(a, b)
#define PP_CAT_I(a, b) PP_CAT_II(~, a ## b)
#define PP_CAT_II(p, res) res
#define UNIQUE_NAME(base) PP_CAT(base, __LINE__)

#define DEFER(X)     std::shared_ptr<void> UNIQUE_NAME(defer)(nullptr, [&](...){ X; })
#define DEFER2(X, N) std::shared_ptr<void> __DEFER__##N(nullptr, [&](...){ X; })

/* test dependency management */

#define TEST_REQUIREMENT_NAME(test_suite_name, test_name) \
        _test_##test_suite_name##_##test_name

#define TEST_REQUIREMENT_DTOR_NAME(test_suite_name, test_name) \
        _test_##test_suite_name##_##test_name##_dtor

#define TEST_REQUIREMENT(test_suite_name, test_name) \
TEST_REQUIREMENT_DTOR(test_suite_name, test_name); \
void TEST_REQUIREMENT_NAME(test_suite_name, test_name)(void)

#define TEST_REQUIREMENT_DTOR(test_suite_name, test_name) \
void TEST_REQUIREMENT_DTOR_NAME(test_suite_name, test_name)(void)

#define TEST_REQUIRE(condition, test_suite_name, test_name) \
    if (!(condition)) { \
        TEST_REQUIREMENT_NAME(test_suite_name, test_name)(); \
        SKIP_IF_(!(condition), #test_suite_name "." #test_name " is not available"); \
    } \
    DEFER(TEST_REQUIREMENT_DTOR_NAME(test_suite_name, test_name)())

/* cover all test source files */

using namespace testing;

namespace testing
{
    /* common vars */

    extern cat_timeout_t CONFIG_IO_TIMEOUT;
    extern uint32_t CONFIG_MAX_REQUESTS;
    extern uint32_t CONFIG_MAX_CONCURRENCY;

    /* REMOTE_HTTP_SERVER */
    extern std::string CONFIG_REMOTE_HTTP_SERVER_HOST;
    extern int CONFIG_REMOTE_HTTP_SERVER_PORT;
    extern std::string CONFIG_REMOTE_HTTP_SERVER_KEYWORD;
    /* REMOTE_HTTPS_SERVER */
    extern std::string CONFIG_REMOTE_HTTPS_SERVER_HOST;
    extern int CONFIG_REMOTE_HTTPS_SERVER_PORT;
    extern std::string CONFIG_REMOTE_HTTPS_SERVER_KEYWORD;
    /* REMOTE_IPV6_HTTP_SERVER */
    extern std::string CONFIG_REMOTE_IPV6_HTTP_SERVER_HOST;
    /* TMP_PATH */
    extern std::string CONFIG_TMP_PATH;

#ifdef CAT_SSL
    extern std::string CONFIG_SSL_CA_FILE;
    extern std::string CONFIG_SERVER_SSL_CERTIFICATE;
    extern std::string CONFIG_SERVER_SSL_CERTIFICATE_KEY;
    extern std::string CONFIG_CLIENT_SSL_CERTIFICATE;
    extern std::string CONFIG_CLIENT_SSL_CERTIFICATE_KEY;
    extern std::string CONFIG_SSL_CERTIFICATE_PASSPHRASE;
    extern std::string CONFIG_SERVER_SSL_CERTIFICATE_ENCODED;
    extern std::string CONFIG_SERVER_SSL_CERTIFICATE_KEY_ENCODED;
    extern std::string CONFIG_CLIENT_SSL_CERTIFICATE_ENCODED;
    extern std::string CONFIG_CLIENT_SSL_CERTIFICATE_KEY_ENCODED;
#endif

    /* common functions */

    static inline bool is_valgrind(void)
    {
#ifdef CAT_HAVE_ASAN
        return true;
#else
        return cat_env_is_true("USE_VALGRIND", cat_false);
#endif
    }

    static inline bool is_offline(void)
    {
        return cat_env_is_true("OFFLINE", cat_false);
    }

    // https://stackoverflow.com/questions/2342162/stdstring-formatting-like-sprintf
    template <typename... Args>
    std::string string_format(const char *format, Args... args)
    {
        size_t size = (size_t) std::snprintf(nullptr, 0, format, args...) + 1; // Extra space for '\0'
        CAT_ASSERT(((ssize_t) size) > 0);
        char *buffer = (char *) cat_malloc(size);
        CAT_ASSERT(buffer != NULL);
        DEFER(cat_free(buffer));
        std::snprintf(buffer, size, format, args...);
        return std::string(buffer, buffer + size - 1); // We don't want the '\0' inside
    }

    bool has_debugger(void);
    const char *get_debugger_name(void);

    bool file_exists(const char *filename);
    std::string file_get_contents(const char *filename);
    bool file_put_contents(const char *filename, const std::string content);
    bool file_put_contents(const char *filename, const char *content, size_t length);

    std::string get_random_bytes(size_t length = TEST_BUFFER_SIZE_STD);

    static inline std::string get_random_path(size_t length = 8)
    {
        std::string random_bytes = get_random_bytes(length);
        return string_format("%s/%s", TEST_TMP_PATH, random_bytes.c_str());
    }

    cat_coroutine_t *co(std::function<void(void)> function);
    bool defer(std::function<void(void)> function);
    bool work(cat_work_kind_t kind, std::function<void(void)> function, cat_timeout_t timeout);

    void register_shutdown_function(std::function<void(void)> function);

    /* common classes */

    class wait_group
    {
    protected:
        cat_sync_wait_group_t wg;

    public:

        wait_group(ssize_t delta = 0)
        {
            (void) cat_sync_wait_group_create(&wg);
            if (!cat_sync_wait_group_add(&wg, delta)) {
                throw cat_get_last_error_message();
            }
        }

        ~wait_group()
        {
            cat_sync_wait_group_wait(&wg, CAT_TIMEOUT_FOREVER);
        }

        wait_group &operator++() // front
        {
            if (!cat_sync_wait_group_add(&wg, 1)) {
                throw cat_get_last_error_message();
            }
            return *this;
        }

        wait_group &operator++(int o) // back
        {
            if (!cat_sync_wait_group_add(&wg, 1)) {
                throw cat_get_last_error_message();
            }
            return *this;
        }

        wait_group &operator--() // front
        {
            if (!cat_sync_wait_group_done(&wg)) {
                throw cat_get_last_error_message();
            }
            return *this;
        }

        wait_group &operator--(int o) // back
        {
            if (!cat_sync_wait_group_done(&wg)) {
                throw cat_get_last_error_message();
            }
            return *this;
        }

        wait_group &operator+(int delta)
        {
            if (!cat_sync_wait_group_add(&wg, delta)) {
                throw cat_get_last_error_message();
            }
            return *this;
        }

        bool operator()(cat_timeout_t timeout = CAT_TIMEOUT_FOREVER)
        {
            return cat_sync_wait_group_wait(&wg, timeout);
        }
    };
}
