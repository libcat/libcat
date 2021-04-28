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

#include <gtest/gtest.h>

#include <memory>

#include "cat_api.h"

/* ext, not enabled by default */
#include "cat_curl.h"

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
#define TEST_LISTEN_IPV6                   "::"
#define TEST_TMP_PATH                      ::testing::CONFIG_TMP_PATH.c_str()
#ifndef CAT_OS_WIN
#define TEST_PATH_SEP                      "/"
#define TEST_PIPE_PATH                     "/tmp/cat_test.sock"
#else
#define TEST_PATH_SEP                      "\\"
#define TEST_PIPE_PATH                     "\\\\?\\pipe\\cat_test"
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

#define PP_CAT(a, b) PP_CAT_I(a, b)
#define PP_CAT_I(a, b) PP_CAT_II(~, a ## b)
#define PP_CAT_II(p, res) res
#define UNIQUE_NAME(base) PP_CAT(base, __LINE__)

#define DEFER(X) std::shared_ptr<void> UNIQUE_NAME(defer)(nullptr, [&](...){ X; })

/* cover all test source files */

using namespace testing;

namespace testing
{
    /* common functions */

    static inline bool is_valgrind(void)
    {
#if defined(__SANITIZE_ADDRESS__) || ( defined(__has_feature) && __has_feature(address_sanitizer) )
        return true;
#else
        return cat_env_is_true("USE_VALGRIND", cat_false);
#endif
    }

    static inline bool is_offline(void)
    {
        return cat_env_is_true("OFFLINE", cat_false);
    }

    cat_coroutine_t *co(std::function<void(void)> function);
    bool work(cat_work_kind_t kind, std::function<void(void)> function, cat_timeout_t timeout);

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
}
