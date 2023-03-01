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

#include "test_config.h"

#ifdef CAT_OS_UNIX_LIKE
#include <sys/types.h>
#include <sys/ptrace.h>
#endif

#include <list>
#include <fstream>
#include <sstream>

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

#ifdef CAT_SSL
    std::string CONFIG_SSL_CA_FILE;
    std::string CONFIG_SERVER_SSL_CERTIFICATE;
    std::string CONFIG_SERVER_SSL_CERTIFICATE_KEY;
    std::string CONFIG_CLIENT_SSL_CERTIFICATE;
    std::string CONFIG_CLIENT_SSL_CERTIFICATE_KEY;
    std::string CONFIG_SSL_CERTIFICATE_PASSPHRASE;
    std::string CONFIG_SERVER_SSL_CERTIFICATE_ENCODED;
    std::string CONFIG_SERVER_SSL_CERTIFICATE_KEY_ENCODED;
    std::string CONFIG_CLIENT_SSL_CERTIFICATE_ENCODED;
    std::string CONFIG_CLIENT_SSL_CERTIFICATE_KEY_ENCODED;
#endif

    bool has_debugger(void)
    {
#ifdef CAT_OS_WIN
        BOOL debugger_present = FALSE;
        CheckRemoteDebuggerPresent(GetCurrentProcess(), &debugger_present);
        return debugger_present == TRUE;
#else
        return get_debugger_name() != nullptr;
#endif
    }

    const char *get_debugger_name(void)
    {
#ifdef CAT_OS_LINUX
        std::string proc_path = string_format("/proc/%d/status", getppid());
        std::ifstream proc_file(proc_path);
        if (proc_file.good()) {
            std::string line;
            getline(proc_file, line);
            if (line.find("gdb") != std::string::npos) {
                return "gdb";
            } else if (line.find("ltrace") != std::string::npos) {
                return "ltrace";
            } else if (line.find("strace") != std::string::npos) {
                return "strace";
            }
        }
#endif
        return nullptr;
    }

    bool file_exists(const char *filename)
    {
        std::ifstream file(filename);
        return file.good();
    }

    std::string file_get_contents(const char *filename)
    {
        std::stringstream ss;
        ss << std::ifstream(filename).rdbuf();
        return ss.str();
    }

    bool file_put_contents(const char *filename, const std::string content)
    {
        return file_put_contents(filename, content.c_str(), content.length());
    }

    bool file_put_contents(const char *filename, const char *content, size_t length)
    {
        FILE *file = fopen(filename, "w"
#ifdef CAT_OS_WIN
            // for no LF -> CRLF auto translation on Windows
            "b"
#endif
        );
        if (file == NULL) {
            return false;
        }
        ssize_t nwrite = fwrite(content, sizeof(char), length, file);
        fclose(file);
        return nwrite == length;
    }

    bool remove_file(const char *filename)
    {
        return remove(filename) == 0;
    }

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
        std::function<void(void)> *function_ptr = new std::function<void(void)>(function);
        return cat_coroutine_run(nullptr, [](cat_data_t *data)->cat_data_t* {
            std::function<void(void)> *function = (std::function<void(void)> *) data;
            (*function)();
            delete function;
            return nullptr;
        }, function_ptr);
    }

    bool defer(std::function<void(void)> function)
    {
        std::function<void(void)> *function_ptr = new std::function<void(void)>(function);
        return cat_event_loop_defer_task_create(NULL, [](cat_event_loop_defer_task_t *task, cat_data_t *data) {
            std::function<void(void)> *function = (std::function<void(void)> *) data;
            (*function)();
            delete function;
            cat_event_loop_defer_task_close(task);
        }, function_ptr);
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
        ASSERT_TRUE(cat_module_init_all());
        ASSERT_TRUE(cat_http_module_init());
#ifdef CAT_CURL
        ASSERT_TRUE(cat_curl_module_init());
#endif
        ASSERT_TRUE(cat_runtime_init_all());
#ifdef CAT_CURL
        ASSERT_TRUE(cat_curl_runtime_init());
#endif
#ifdef CAT_PQ
        ASSERT_TRUE(cat_pq_runtime_init());
#endif
        cat_set_error_log(stderr);

        ASSERT_TRUE(cat_run(CAT_RUN_EASY));
        ASSERT_NE(cat_coroutine_get_current(), nullptr);

        /* IO_TIMEOUT */
        if (cat_env_exists("TEST_IO_TIMEOUT")) {
            char *timeout = cat_env_get("TEST_IO_TIMEOUT");
            testing::CONFIG_IO_TIMEOUT = atoi(timeout);
            cat_free(timeout);
        }
        cat_socket_set_global_timeout(TEST_IO_TIMEOUT);
        cat_socket_set_global_accept_timeout(TEST_IO_TIMEOUT);
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
        char *host = nullptr;
        if (!cat_env_is_empty("TEST_TMP_PATH")) {
            host = cat_env_get("TEST_TMP_PATH");
        }
#ifdef CAT_OS_WIN
        else if (!cat_env_is_empty("TEMP")) {
            host = cat_env_get("TEMP");
        } else if (!cat_env_is_empty("TMP")) {
            host = cat_env_get("TMP");
        }
#endif
        if (host != nullptr) {
            testing::CONFIG_TMP_PATH = host;
            cat_free(host);
        }

#ifdef CAT_SSL
        testing::CONFIG_SSL_CA_FILE = string_format("%s/cat_ssl_ca.crt", TEST_TMP_PATH);

        ASSERT_TRUE(file_put_contents(testing::CONFIG_SSL_CA_FILE.c_str(), TEST_SERVER_SSL_CA_CONTENT));
        testing::CONFIG_SERVER_SSL_CERTIFICATE = string_format("%s/cat_ssl_server.crt", TEST_TMP_PATH);
        ASSERT_TRUE(file_put_contents(testing::CONFIG_SERVER_SSL_CERTIFICATE.c_str(), TEST_SERVER_SSL_CERTIFICATE_CONTENT));
        testing::CONFIG_SERVER_SSL_CERTIFICATE_KEY = string_format("%s/cat_ssl_server.key", TEST_TMP_PATH);
        ASSERT_TRUE(file_put_contents(testing::CONFIG_SERVER_SSL_CERTIFICATE_KEY.c_str(), TEST_SERVER_SSL_CERTIFICATE_KEY_CONTENT));
        testing::CONFIG_CLIENT_SSL_CERTIFICATE = string_format("%s/cat_ssl_client.crt", TEST_TMP_PATH);
        ASSERT_TRUE(file_put_contents(testing::CONFIG_CLIENT_SSL_CERTIFICATE.c_str(), TEST_CLIENT_SSL_CERTIFICATE_CONTENT));
        testing::CONFIG_CLIENT_SSL_CERTIFICATE_KEY = string_format("%s/cat_ssl_client.key", TEST_TMP_PATH);
        ASSERT_TRUE(file_put_contents(testing::CONFIG_CLIENT_SSL_CERTIFICATE_KEY.c_str(), TEST_CLIENT_SSL_CERTIFICATE_KEY_CONTENT));

        testing::CONFIG_SSL_CERTIFICATE_PASSPHRASE = TEST_SSL_CERTIFICATE_PASSPHRASE_CONTENT;
        testing::CONFIG_SERVER_SSL_CERTIFICATE_ENCODED = string_format("%s/cat_ssl_server_encoded.crt", TEST_TMP_PATH);
        ASSERT_TRUE(file_put_contents(testing::CONFIG_SERVER_SSL_CERTIFICATE_ENCODED.c_str(), TEST_SERVER_SSL_CERTIFICATE_ENCODED_CONTENT));
        testing::CONFIG_SERVER_SSL_CERTIFICATE_KEY_ENCODED = string_format("%s/cat_ssl_server_encoded.key", TEST_TMP_PATH);
        ASSERT_TRUE(file_put_contents(testing::CONFIG_SERVER_SSL_CERTIFICATE_KEY_ENCODED.c_str(), TEST_SERVER_SSL_CERTIFICATE_KEY_ENCODED_CONTENT));
        testing::CONFIG_CLIENT_SSL_CERTIFICATE_ENCODED = string_format("%s/cat_ssl_client_encoded.crt", TEST_TMP_PATH);
        ASSERT_TRUE(file_put_contents(testing::CONFIG_CLIENT_SSL_CERTIFICATE_ENCODED.c_str(), TEST_CLIENT_SSL_CERTIFICATE_ENCODED_CONTENT));
        testing::CONFIG_CLIENT_SSL_CERTIFICATE_KEY_ENCODED = string_format("%s/cat_ssl_client_encoded.key", TEST_TMP_PATH);
        ASSERT_TRUE(file_put_contents(testing::CONFIG_CLIENT_SSL_CERTIFICATE_KEY_ENCODED.c_str(), TEST_CLIENT_SSL_CERTIFICATE_KEY_ENCODED_CONTENT));
#endif
    }

    virtual void TearDown()
    {
#if defined(CAT_SSL) && !defined(CAT_DEBUG)
        remove_file(testing::CONFIG_SSL_CA_FILE.c_str());
        remove_file(testing::CONFIG_SERVER_SSL_CERTIFICATE.c_str());
        remove_file(testing::CONFIG_SERVER_SSL_CERTIFICATE_KEY.c_str());
        remove_file(testing::CONFIG_CLIENT_SSL_CERTIFICATE.c_str());
        remove_file(testing::CONFIG_CLIENT_SSL_CERTIFICATE_KEY.c_str());
        remove_file(testing::CONFIG_SERVER_SSL_CERTIFICATE_ENCODED.c_str());
        remove_file(testing::CONFIG_SERVER_SSL_CERTIFICATE_KEY_ENCODED.c_str());
        remove_file(testing::CONFIG_CLIENT_SSL_CERTIFICATE_ENCODED.c_str());
        remove_file(testing::CONFIG_CLIENT_SSL_CERTIFICATE_KEY_ENCODED.c_str());
#endif

        call_shutdown_functions();

        ASSERT_TRUE(cat_stop());
        ASSERT_EQ(cat_coroutine_get_count() , 1);

        ASSERT_TRUE(cat_runtime_shutdown_all());
#ifdef CAT_PQ
        ASSERT_TRUE(cat_pq_runtime_close());
#endif
#ifdef CAT_CURL
        ASSERT_TRUE(cat_curl_runtime_close());
#endif
        ASSERT_TRUE(cat_runtime_close_all());
#ifdef CAT_CURL
        ASSERT_TRUE(cat_curl_module_shutdown());
#endif
        ASSERT_TRUE(cat_module_shutdown_all());
    }
};

int main(int argc, char *argv[])
{
    /* it may take ownership of the memory that argv points to */
    argv = cat_setup_args(argc, argv);

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

TEST(test, wait_group)
{
    bool done[10] = { false };
    {
        wait_group wg;
        for (int n = 0; n < 10; n++) {
            co([&, n] {
                wg++;
                DEFER(wg--);
                ASSERT_TRUE(cat_time_delay(n));
                done[n] = true;
            });
        }
    }
    for (int n = 0; n < 10; n++) {
        ASSERT_TRUE(done[n]);
    }
}

extern "C"
{
    void __ubsan_on_report()
    {
        FAIL() << "Encountered an undefined behavior sanitizer error";
    }
    void __asan_on_error(void)
    {
        FAIL() << "Encountered an address sanitizer error";
    }
    void __tsan_on_report()
    {
        FAIL() << "Encountered a thread sanitizer error";
    }
}
