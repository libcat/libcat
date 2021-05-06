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

#include "test.h"

#include <list>

namespace testing
{
    cat_timeout_t CONFIG_IO_TIMEOUT = (!is_valgrind() ? 30 : 60) * 1000;
    uint32_t CONFIG_MAX_REQUESTS = !is_valgrind() ? 128 : 64;
    uint32_t CONFIG_MAX_CONCURRENCY = !is_valgrind() ? 64 : 8;

    /* REMOTE_HTTP_SERVER */
    std::string CONFIG_REMOTE_HTTP_SERVER_HOST = "http.badssl.com";
    int CONFIG_REMOTE_HTTP_SERVER_PORT = 80;
    std::string CONFIG_REMOTE_HTTP_SERVER_KEYWORD = "http.badssl.com";
    /* REMOTE_HTTPS_SERVER */
    std::string CONFIG_REMOTE_HTTPS_SERVER_HOST = "sha256.badssl.com";
    int CONFIG_REMOTE_HTTPS_SERVER_PORT = 443;
    std::string CONFIG_REMOTE_HTTPS_SERVER_KEYWORD = "sha256.badssl.com";
    /* REMOTE_IPV6_HTTP_SERVER */
    std::string CONFIG_REMOTE_IPV6_HTTP_SERVER_HOST = "www.taobao.com";
    /* TMP_PATH */
    std::string CONFIG_TMP_PATH = "/tmp";

    std::string get_random_bytes(size_t length)
    {
        CAT_ASSERT(length > 0);
        char *buffer = (char *) cat_malloc(length);
        CAT_ASSERT(buffer != NULL);
        DEFER(cat_free(buffer));
        (void) cat_snrand(buffer, length);
        return std::string(buffer, length);
    }

    cat_coroutine_t *co(std::function<void(void)> function)
    {
        return cat_coroutine_run(nullptr, [](cat_data_t *data)->cat_data_t* {
            auto function = *((std::function<void(void)> *) data);
            function();
            return nullptr;
        }, &function);
    }

    bool work(cat_work_kind_t kind, std::function<void(void)> function, cat_timeout_t timeout)
    {
        struct work_context_s {
            std::function<void(void)> function;
            uv_sem_t *sem;
        } context;
        uv_sem_t sem;
        int error;
        bool ret = true, wait = true, done = false;
        error = uv_sem_init(&sem, 0);
        if (error != 0) {
            cat_update_last_error_with_reason(error, "Sem init failed");
            return false;
        }
        context.function = function;
        context.sem = &sem;
        cat_coroutine_t *waiter = cat_coroutine_get_current();
        cat_coroutine_t *worker = co([&] {
            ret = cat_work(kind, [](cat_data_t *data) {
                struct work_context_s *context = (struct work_context_s *) data;
                std::function<void(void)> function = context->function; // must copy it
                uv_sem_post(context->sem); // notify
                function();
            }, nullptr, &context, timeout);
            done = true;
            if (wait) {
                cat_coroutine_resume(waiter, nullptr, nullptr);
            }
        });
        if (ret) {
            uv_sem_wait(&sem);
        }
        cat_coroutine_yield(nullptr, nullptr);
        wait = false;
        if (!done) {
            cat_coroutine_resume(worker, nullptr, nullptr);
        }
        uv_sem_destroy(&sem);

        return ret;
    }

    static std::list<std::function<void(void)>> shutdown_functions;

    void register_shutdown_function(std::function<void(void)> function)
    {
        shutdown_functions.push_back(function);
    }

    static void call_shutdown_functions(void)
    {
        for (auto const& f : shutdown_functions) {
            f();
        }
    }
}

class BootstrapEnvironment : public testing::Environment
{
public:
    virtual void SetUp()
    {
        ASSERT_TRUE(cat_init_all());
#ifdef CAT_CURL
        ASSERT_TRUE(cat_curl_module_init());
#endif

        cat_socket_set_global_timeout(TEST_IO_TIMEOUT);

        ASSERT_TRUE(cat_run(CAT_RUN_EASY));
        ASSERT_NE(cat_coroutine_get_current(), nullptr);

        /* IO_TIMEOUT */
        if (cat_env_exists("TEST_IO_TIMEOUT")) {
            char *timeout = cat_env_get("TEST_IO_TIMEOUT");
            testing::CONFIG_IO_TIMEOUT = atoi(timeout);
            cat_free(timeout);
        }
        /* MAX_REQUEST */
        if (cat_env_exists("TEST_MAX_REQUESTS")) {
            char *n = cat_env_get("TEST_MAX_REQUESTS");
            testing::CONFIG_MAX_REQUESTS = atoi(n);
            cat_free(n);
        }
        /* MAX_CONCURRENCY */
        if (cat_env_exists("TEST_MAX_CONCURRENCY")) {
            char *c = cat_env_get("TEST_MAX_CONCURRENCY");
            testing::CONFIG_MAX_CONCURRENCY = atoi(c);
            cat_free(c);
        }

        /* REMOTE_HTTP_SERVER */
        if (cat_env_exists("TEST_REMOTE_HTTP_SERVER_HOST")) {
            char *host = cat_env_get("TEST_REMOTE_HTTP_SERVER_HOST");
            testing::CONFIG_REMOTE_HTTP_SERVER_HOST = host;
            cat_free(host);
        }
        if (cat_env_exists("TEST_REMOTE_HTTP_SERVER_PORT")) {
            char *port = cat_env_get("TEST_REMOTE_HTTP_SERVER_PORT");
            testing::CONFIG_REMOTE_HTTP_SERVER_PORT = atoi(port);
            cat_free(port);
        }
        if (cat_env_exists("TEST_REMOTE_HTTP_SERVER_KEYWORD")) {
            char *keyword = cat_env_get("TEST_REMOTE_HTTP_SERVER_KEYWORD");
            testing::CONFIG_REMOTE_HTTP_SERVER_KEYWORD = keyword;
            cat_free(keyword);
        }
        /* REMOTE_HTTPS_SERVER */
        if (cat_env_exists("TEST_REMOTE_HTTPS_SERVER_HOST")) {
            char *host = cat_env_get("TEST_REMOTE_HTTPS_SERVER_HOST");
            testing::CONFIG_REMOTE_HTTPS_SERVER_HOST = host;
            cat_free(host);
        }
        if (cat_env_exists("TEST_REMOTE_HTTPS_SERVER_PORT")) {
            char *port = cat_env_get("TEST_REMOTE_HTTPS_SERVER_PORT");
            testing::CONFIG_REMOTE_HTTPS_SERVER_PORT = atoi(port);
            cat_free(port);
        }
        if (cat_env_exists("TEST_REMOTE_HTTPS_SERVER_KEYWORD")) {
            char *keyword = cat_env_get("TEST_REMOTE_HTTPS_SERVER_KEYWORD");
            testing::CONFIG_REMOTE_HTTPS_SERVER_KEYWORD = keyword;
            cat_free(keyword);
        }
        /* REMOTE_IPV6_HTTP_SERVER */
        if (cat_env_exists("TEST_REMOTE_IPV6_HTTP_SERVER_HOST")) {
            char *host = cat_env_get("TEST_REMOTE_IPV6_HTTP_SERVER_HOST");
            testing::CONFIG_REMOTE_IPV6_HTTP_SERVER_HOST = host;
            cat_free(host);
        }

        /* TMP_PATH */
        char *host = cat_env_get("TEST_TMP_PATH");
#ifdef CAT_OS_WIN
        if (nullptr == host || '\0' == host[0]) {
            if (nullptr != host && '\0' == host[0]) {
                cat_free(host);
            }
            host = cat_env_get("TMP");
        }
        if (nullptr == host || '\0' == host[0]) {
            if (nullptr != host && '\0' == host[0]) {
                cat_free(host);
            }
            host = cat_env_get("TEMP");
        }
#endif
        if (nullptr != host) {
            if ('\0' != host[0]) {
                testing::CONFIG_TMP_PATH = host;
            }
            cat_free(host);
        }

    }

    virtual void TearDown()
    {
        call_shutdown_functions();

        ASSERT_TRUE(cat_stop());
        ASSERT_EQ(cat_coroutine_get_count() , 1);

#ifdef CAT_CURL
        ASSERT_TRUE(cat_curl_module_shutdown());
#endif
        ASSERT_TRUE(cat_shutdown_all());
    }
};

int main(int argc, char *argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
#if GTEST_HAS_DEATH_TEST
    ::testing::GTEST_FLAG(death_test_style) = "threadsafe";
#endif
    ::testing::AddGlobalTestEnvironment(new BootstrapEnvironment);

    return RUN_ALL_TESTS();
}

TEST(test, defer)
{
    bool foo = false;
    DEFER(ASSERT_TRUE(foo));
    foo = true;
}
