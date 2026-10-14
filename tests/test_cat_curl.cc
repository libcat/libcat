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
#ifdef CAT_CURL

#include <vector>

static size_t cat_test_curl_write_function(const char *ptr, size_t length, size_t n, cat_buffer_t *buffer)
{
    cat_buffer_append(buffer, ptr, length * n);

    return length * n;
}

static std::string cat_test_curl_remote_url()
{
    std::string host = TEST_REMOTE_HTTP_SERVER_HOST;
    if (host.find("://") != std::string::npos) {
        return host;
    }
    if (host.find(':') != std::string::npos && host.front() != '[') {
        host = "[" + host + "]";
    }
    return "http://" + host + ":" + std::to_string(TEST_REMOTE_HTTP_SERVER_PORT);
}

static CURLcode cat_test_curl_query(const char *url, std::string &response, cat_msec_t timeout = TEST_IO_TIMEOUT, CURL **ch_ptr = nullptr)
{
    CURL *ch;
    CURLcode code;
    cat_buffer_t buffer;

    if (!cat_buffer_create(&buffer, 0)) {
        return CURLE_OUT_OF_MEMORY;
    }

    ch = curl_easy_init();
    if (ch == nullptr) {
        return CURLE_FAILED_INIT;
    }
    if (ch_ptr) {
        *ch_ptr = ch;
    }
    curl_easy_setopt(ch, CURLOPT_URL, url);
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, cat_test_curl_write_function);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(ch, CURLOPT_TIMEOUT_MS, timeout);
    code = cat_curl_easy_perform(ch);

    response = std::string(buffer.value, buffer.length);
    cat_buffer_close(&buffer);
    curl_easy_cleanup(ch);

    return code;
}

TEST(cat_curl, base)
{
    SKIP_IF_OFFLINE();
    std::string response;
    ASSERT_EQ(cat_test_curl_query(cat_test_curl_remote_url().c_str(), response), CURLE_OK);
    ASSERT_NE(response.find(TEST_REMOTE_HTTP_SERVER_KEYWORD), std::string::npos);
}

TEST(cat_curl, concurrency)
{
    ASSERT_TRUE(cat_coroutine_wait_all()); // call it before timing test
    auto concurrency = ([](uint32_t concurrency) {
        ASSERT_GT(concurrency, 0u);
        cat_socket_t server;
        ASSERT_NE(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP), nullptr);
        uint32_t received = 0, completed = 0;
        std::string server_url;
        wait_group wg;
        // Cancel a pending accept before joining coroutines that capture this scope.
        DEFER(cat_socket_close(&server));
        ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
        ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
        server_url = std::string("http://") + TEST_LISTEN_IPV4 + ":" + std::to_string(cat_socket_get_port(&server, false));
        co([&] {
            wg++;
            DEFER(wg--);
            // Socket addresses must stay fixed while libuv owns their handles.
            std::vector<cat_socket_t> connections(concurrency);
            size_t created = 0;
            DEFER(for (size_t n = 0; n < created; n++) {
                cat_socket_close(&connections[n]);
            });
            for (size_t n = 0; n < concurrency; n++) {
                cat_socket_t *conn = &connections[n];
                ASSERT_NE(cat_socket_create(conn, CAT_SOCKET_TYPE_TCP), nullptr);
                created++;
                ASSERT_TRUE(cat_socket_accept(&server, conn)) << "Received " << received << " of " << concurrency << " requests";
                std::string request;
                char buffer[4096];
                while (request.find("\r\n\r\n") == std::string::npos) {
                    ssize_t length = cat_socket_recv(conn, buffer, sizeof(buffer));
                    ASSERT_GT(length, 0) << "Received " << received << " of " << concurrency << " requests";
                    request.append(buffer, length);
                }
                received++;
            }
            // Withhold every response until all requests are in flight.
            // A serialized client cannot reach this point before its timeout.
            ASSERT_EQ(received, concurrency);
            ASSERT_EQ(completed, 0u);
            for (size_t n = 0; n < concurrency; n++) {
                ASSERT_TRUE(cat_socket_send(&connections[n], CAT_STRL(
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Length: 2\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "OK"
                )));
            }
        });
        for (size_t n = 0; n < concurrency; n++) {
            co([&] {
                wg++;
                DEFER(wg--);
                cat_buffer_t buffer;
                ASSERT_TRUE(cat_buffer_create(&buffer, 0));
                DEFER(cat_buffer_close(&buffer));
                CURL *ch = curl_easy_init();
                ASSERT_NE(ch, nullptr);
                DEFER(curl_easy_cleanup(ch));
                curl_easy_setopt(ch, CURLOPT_URL, server_url.c_str());
                // The loopback service must not use an environment proxy.
                curl_easy_setopt(ch, CURLOPT_PROXY, "");
                curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, cat_test_curl_write_function);
                curl_easy_setopt(ch, CURLOPT_WRITEDATA, &buffer);
                curl_easy_setopt(ch, CURLOPT_TIMEOUT_MS, TEST_IO_TIMEOUT);
                ASSERT_EQ(cat_curl_easy_perform(ch), CURLE_OK)
                    << "Received " << received << ", completed " << completed << " of " << concurrency << " requests";
                ASSERT_EQ(std::string(buffer.value, buffer.length), "OK");
                completed++;
            });
        }
        ASSERT_TRUE(wg(TEST_IO_TIMEOUT))
            << "Received " << received << ", completed " << completed << " of " << concurrency << " requests";
        ASSERT_EQ(received, concurrency);
        ASSERT_EQ(completed, concurrency);
    });

    if (!is_valgrind()) {
        ASSERT_NO_FATAL_FAILURE(concurrency(TEST_MAX_CONCURRENCY / 8));
        ASSERT_NO_FATAL_FAILURE(concurrency(TEST_MAX_CONCURRENCY / 2));
    } else {
        ASSERT_NO_FATAL_FAILURE(concurrency(TEST_MAX_CONCURRENCY));
    }
}

TEST(cat_curl, cancel)
{
    SKIP_IF_OFFLINE();
    wait_group wg;
    cat_coroutine_t *coroutine = co([&wg] {
        wg++;
        DEFER(wg--);
        std::string response;
        ASSERT_NE(cat_test_curl_query(cat_test_curl_remote_url().c_str(), response), CURLE_OK);
        ASSERT_EQ(response.find(TEST_REMOTE_HTTP_SERVER_KEYWORD), std::string::npos);
    });
    cat_coroutine_resume(coroutine, nullptr, nullptr);
}

TEST(cat_curl, timeout)
{
    SKIP_IF_OFFLINE();
    std::string response;
    ASSERT_EQ(cat_test_curl_query(cat_test_curl_remote_url().c_str(), response, 1), CURLE_OPERATION_TIMEDOUT);
    ASSERT_EQ(response.find(TEST_REMOTE_HTTP_SERVER_KEYWORD), std::string::npos);
}

TEST(cat_curl, busy)
{
    SKIP_IF_OFFLINE();
    wait_group wg;
    CURL *ch;
    co([&] {
        wg++;
        DEFER(wg--);
        std::string response;
        ASSERT_EQ(cat_test_curl_query(cat_test_curl_remote_url().c_str(), response, TEST_IO_TIMEOUT, &ch), CURLE_OK);
        ASSERT_NE(response.find(TEST_REMOTE_HTTP_SERVER_KEYWORD), std::string::npos);
    });
    ASSERT_EQ(cat_curl_easy_perform(ch), CURLE_AGAIN);
}

TEST(cat_curl_multi, base)
{
    SKIP_IF_OFFLINE();
    CURL *ch;
    CURLM *mh;
    int still_running = 0;
    int repeats = 0;

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    DEFER(curl_easy_cleanup(ch));
    curl_easy_setopt(ch, CURLOPT_URL, cat_test_curl_remote_url().c_str());
    curl_easy_setopt(ch, CURLOPT_TIMEOUT_MS, TEST_IO_TIMEOUT);
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, cat_test_curl_write_function);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, &buffer);
    mh = cat_curl_multi_init();
    ASSERT_NE(mh, nullptr);
    DEFER(cat_curl_multi_cleanup(mh));
    ASSERT_EQ(curl_multi_add_handle(mh, ch), CURLM_OK);
    DEFER(curl_multi_remove_handle(mh, ch));
    ASSERT_EQ(cat_curl_multi_perform(mh, &still_running), CURLM_OK);

    while (still_running) {
        int numfds;
        ASSERT_EQ(cat_curl_multi_wait(mh, NULL, 0, 1000, &numfds), CURLM_OK);
        if (!numfds) {
            repeats++;
            if (repeats > 1) {
                cat_time_msleep(1);
            }
        } else {
            repeats = 0;
        }
        ASSERT_EQ(cat_curl_multi_perform(mh, &still_running), CURLM_OK);
    }

    ASSERT_NE(
        std::string(buffer.value, buffer.length).find(TEST_REMOTE_HTTP_SERVER_KEYWORD),
        std::string::npos
    );
}


TEST(cat_curl_multi, multi)
{
    SKIP_IF_OFFLINE();
    CURL *chs[8];
    CURLM *mh;
    int still_running = 0;
    int repeats = 0;
    cat_buffer_t buffers[8];

    struct test_case {
        const char *url;
        const char *keyword;
    } test_cases[] = {
        { "https://www.apple.com", "apple" },
        { "https://www.bing.com", "bing" },
        { "https://www.baidu.com", "baidu" },
        { "https://www.taobao.com", "taobao" },
    };

    for (size_t i = 0; i < 8; i++) {
        ASSERT_TRUE(cat_buffer_create(&buffers[i], 0));
    }
    DEFER(for (size_t i = 0; i < 8; i++) {
        cat_buffer_close(&buffers[i]);
    });

    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        for (size_t j = 0; j < 2; j++) {
            chs[j * 4 + i] = curl_easy_init();
            ASSERT_NE(chs[j * 4 + i], nullptr);
            curl_easy_setopt(chs[j * 4 + i], CURLOPT_URL, test_cases[i].url);
            curl_easy_setopt(chs[j * 4 + i], CURLOPT_TIMEOUT_MS, TEST_IO_TIMEOUT);
            curl_easy_setopt(chs[j * 4 + i], CURLOPT_FOLLOWLOCATION, 1);
            curl_easy_setopt(chs[j * 4 + i], CURLOPT_WRITEFUNCTION, cat_test_curl_write_function);
            curl_easy_setopt(chs[j * 4 + i], CURLOPT_WRITEDATA, &buffers[j * 4 + i]);
        }
    }

    mh = cat_curl_multi_init();
    ASSERT_NE(mh, nullptr);
    DEFER(ASSERT_EQ(cat_curl_multi_cleanup(mh), CURLM_OK));

    for (size_t i = 0; i < 8; i++) {
        ASSERT_EQ(curl_multi_add_handle(mh, chs[i]), CURLM_OK);
    }
    DEFER(for (size_t i = 0; i < 8; i++) {
        ASSERT_EQ(curl_multi_remove_handle(mh, chs[i]), CURLM_OK);
        curl_easy_cleanup(chs[i]);
    });

    ASSERT_EQ(cat_curl_multi_perform(mh, &still_running), CURLM_OK);

    while (still_running) {
        int numfds;
        ASSERT_EQ(cat_curl_multi_wait(mh, NULL, 0, 1000, &numfds), CURLM_OK);
        if (!numfds) {
            repeats++;
            if (repeats > 1) {
                cat_time_msleep(1);
            }
        } else {
            repeats = 0;
        }
        ASSERT_EQ(cat_curl_multi_perform(mh, &still_running), CURLM_OK);
    }

    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        for (size_t j = 0; j < 2; j++) {
            ASSERT_NE(
                std::string(buffers[j * 4 + i].value, buffers[j * 4 + i].length).find(test_cases[i].keyword),
                std::string::npos
            );
        }
    }
}

TEST(cat_curl_multi, just_only_perform)
{
    SKIP_IF_OFFLINE();
    CURL *ch;
    CURLM *mh;
    int still_running = 0;

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    DEFER(curl_easy_cleanup(ch));
    curl_easy_setopt(ch, CURLOPT_URL, cat_test_curl_remote_url().c_str());
    curl_easy_setopt(ch, CURLOPT_TIMEOUT_MS, TEST_IO_TIMEOUT);
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, cat_test_curl_write_function);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, &buffer);
    mh = cat_curl_multi_init();
    ASSERT_NE(mh, nullptr);
    DEFER(cat_curl_multi_cleanup(mh));
    ASSERT_EQ(curl_multi_add_handle(mh, ch), CURLM_OK);
    DEFER(curl_multi_remove_handle(mh, ch));

    do {
        ASSERT_EQ(cat_curl_multi_perform(mh, &still_running), CURLM_OK);
    } while (still_running);

    ASSERT_NE(
        std::string(buffer.value, buffer.length).find(TEST_REMOTE_HTTP_SERVER_KEYWORD),
        std::string::npos
    );
}

TEST(cat_curl_multi, bad_usage_of_composer)
{
    SKIP_IF_OFFLINE();
    CURL *ch;
    CURLM *mh;
    int still_running = 0;

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    DEFER(curl_easy_cleanup(ch));
    curl_easy_setopt(ch, CURLOPT_URL, cat_test_curl_remote_url().c_str());
    curl_easy_setopt(ch, CURLOPT_TIMEOUT_MS, TEST_IO_TIMEOUT);
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, cat_test_curl_write_function);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, &buffer);
    mh = cat_curl_multi_init();
    ASSERT_NE(mh, nullptr);
    DEFER(cat_curl_multi_cleanup(mh));
    ASSERT_EQ(curl_multi_add_handle(mh, ch), CURLM_OK);
    DEFER(curl_multi_remove_handle(mh, ch));

    do {
        ASSERT_EQ(cat_curl_multi_perform(mh, &still_running), CURLM_OK);
    } while (still_running);

    /* ignore still_running like composer
     * https://github.com/composer/composer/blob/2fe3244ddb68aa5260844a57c18ff027e279b83d/src/Composer/Util/Http/CurlDownloader.php#L305 */
    ASSERT_EQ(cat_curl_multi_perform(mh, &still_running), CURLM_OK);
    cat_msec_t s = cat_time_msec();
    ASSERT_EQ(cat_curl_multi_wait(mh, NULL, 0, TEST_IO_TIMEOUT, nullptr), CURLM_OK);
    s = cat_time_msec() - s;
    ASSERT_LT(s, 100);

    ASSERT_NE(
        std::string(buffer.value, buffer.length).find(TEST_REMOTE_HTTP_SERVER_KEYWORD),
        std::string::npos
    );
}

TEST(cat_curl_multi, sleep_without_wait)
{
    SKIP_IF_OFFLINE();
    CURL *ch;
    CURLM *mh;
    int still_running = 0;
    char error[CURL_ERROR_SIZE] = {};

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    DEFER(curl_easy_cleanup(ch));
    curl_easy_setopt(ch, CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt(ch, CURLOPT_URL, cat_test_curl_remote_url().c_str());
    curl_easy_setopt(ch, CURLOPT_TIMEOUT_MS, TEST_IO_TIMEOUT);
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, cat_test_curl_write_function);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, &buffer);
    mh = cat_curl_multi_init();
    ASSERT_NE(mh, nullptr);
    DEFER(cat_curl_multi_cleanup(mh));
    ASSERT_EQ(curl_multi_add_handle(mh, ch), CURLM_OK);
    DEFER(curl_multi_remove_handle(mh, ch));

    ASSERT_EQ(cat_curl_multi_perform(mh, &still_running), CURLM_OK);

    ASSERT_EQ(cat_time_msleep(1000), 0);

    do {
        ASSERT_EQ(cat_curl_multi_perform(mh, &still_running), CURLM_OK);
    } while (still_running);

    int remaining;
    CURLMsg *message = curl_multi_info_read(mh, &remaining);
    ASSERT_NE(message, nullptr);
    ASSERT_EQ(message->msg, CURLMSG_DONE);
    ASSERT_EQ(message->data.result, CURLE_OK) << error;

    ASSERT_NE(
        std::string(buffer.value, buffer.length).find(TEST_REMOTE_HTTP_SERVER_KEYWORD),
        std::string::npos
    );
}

TEST(cat_curl_multi, no_wait_sleep_1)
{
    SKIP_IF_OFFLINE();
    CURL *ch;
    CURLM *mh;
    int still_running = 0;

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    DEFER(curl_easy_cleanup(ch));
    curl_easy_setopt(ch, CURLOPT_URL, cat_test_curl_remote_url().c_str());
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, cat_test_curl_write_function);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, &buffer);
    mh = cat_curl_multi_init();
    ASSERT_NE(mh, nullptr);
    DEFER(cat_curl_multi_cleanup(mh));

    ASSERT_EQ(curl_multi_add_handle(mh, ch), CURLM_OK);
    DEFER(curl_multi_remove_handle(mh, ch));

    ASSERT_EQ(cat_curl_multi_perform(mh, &still_running), CURLM_OK);

    ASSERT_EQ(cat_time_msleep(1000), 0);
}

TEST(cat_curl_multi, no_wait_sleep_0)
{
    SKIP_IF_OFFLINE();
    CURL *ch;
    CURLM *mh;
    int still_running = 0;

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    DEFER(curl_easy_cleanup(ch));
    curl_easy_setopt(ch, CURLOPT_URL, cat_test_curl_remote_url().c_str());
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, cat_test_curl_write_function);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, &buffer);
    mh = cat_curl_multi_init();
    ASSERT_NE(mh, nullptr);
    DEFER(cat_curl_multi_cleanup(mh));

    ASSERT_EQ(curl_multi_add_handle(mh, ch), CURLM_OK);
    DEFER(curl_multi_remove_handle(mh, ch));

    ASSERT_EQ(cat_curl_multi_perform(mh, &still_running), CURLM_OK);

    ASSERT_EQ(cat_time_msleep(0), 0);
}

TEST(cat_curl_multi, sleep_without_wait_in_coro)
{
    SKIP_IF_OFFLINE();
    CURL *ch;
    CURLM *mh;
    int still_running = 0;

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    DEFER(curl_easy_cleanup(ch));
    curl_easy_setopt(ch, CURLOPT_URL, cat_test_curl_remote_url().c_str());
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, cat_test_curl_write_function);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, &buffer);
    mh = cat_curl_multi_init();
    ASSERT_NE(mh, nullptr);
    DEFER(cat_curl_multi_cleanup(mh));
    ASSERT_EQ(curl_multi_add_handle(mh, ch), CURLM_OK);
    DEFER(curl_multi_remove_handle(mh, ch));

    co([&] {
        // should be "cancelled", but we use internal error for compatibility
        ASSERT_EQ(cat_curl_multi_perform(mh, &still_running), CURLM_INTERNAL_ERROR);
    });
}

TEST(cat_curl, expect_100_continue)
{
    CURL *ch;

    cat_socket_t server;
    ASSERT_NE(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP), nullptr);
    wait_group wg;
    // Close the client and listener before joining the server coroutine.
    // The accepted connection stays alive until its sender and reader finish.
    DEFER(cat_socket_close(&server));
    ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
    // fake http server that wont send 100-continue
    co([&] {
        wg++;
        DEFER(wg--);
        cat_socket_t conn;
        ASSERT_NE(cat_socket_create(&conn, CAT_SOCKET_TYPE_TCP), nullptr);
        DEFER(cat_socket_close(&conn));
        ASSERT_TRUE(cat_socket_accept(&server, &conn));
        cat_time_msleep(200);
        ASSERT_TRUE(cat_socket_send(&conn, CAT_STRL(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 13\r\n"
            "\r\n"
            "Hello, World!"
        )));
        // curl will keep sending data after received 200
        // so we need to read all data to avoid blocking
        // then close the connection after curl finished
        char dummy_buffer[16384];
        ssize_t n = -CAT_EAGAIN;
        while (n > 0 || n == -CAT_EAGAIN) {
            n = cat_socket_recv(&conn, dummy_buffer, sizeof(dummy_buffer));
        }
    });
    std::string server_url = std::string("http://") + TEST_LISTEN_IPV4 + ":" + std::to_string(cat_socket_get_port(&server, false));

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));

    // curl will expect 100-continue for body size > 1024 * 1024
    char *body = (char *)malloc(1024 * 1024 + 4096);
    memset(body, 'a', 1024 * 1024 + 4096);
    body[1024 * 1024 + 4095] = '\0';
    DEFER(free(body));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    DEFER(curl_easy_cleanup(ch));
    curl_easy_setopt(ch, CURLOPT_URL, server_url.c_str());
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(ch, CURLOPT_EXPECT_100_TIMEOUT_MS, 1);
    curl_easy_setopt(ch, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(ch, CURLOPT_POSTFIELDSIZE, 1024 * 1024 + 4096);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, cat_test_curl_write_function);
    // curl_easy_setopt(ch, CURLOPT_VERBOSE, 1);

    ASSERT_EQ(cat_curl_easy_perform(ch), CURLE_OK);

    ASSERT_NE(
        std::string(buffer.value, buffer.length).find("Hello, World!"),
        std::string::npos
    );
}

static int cat_test_curl_yield_and_close_socket(void *clientp, curl_socket_t sockfd)
{
    (void) clientp;
    // libcurl has called socket function with CURL_POLL_REMOVE before this callback,
    // yield to let the event loop free the removed socket context while socket_action() is still running
    cat_time_msleep(1);
#ifndef CAT_OS_WIN
    return close(sockfd);
#else
    return closesocket(sockfd);
#endif
}

TEST(cat_curl, yield_after_socket_removed)
{
    cat_socket_t server;
    ASSERT_NE(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP), nullptr);
    DEFER(cat_socket_close(&server));
    ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
    wait_group wg;
    // "Connection: close" makes libcurl close the socket inside socket_action() of the same socket
    co([&] {
        wg++;
        DEFER(wg--);
        cat_socket_t conn;
        ASSERT_NE(cat_socket_create(&conn, CAT_SOCKET_TYPE_TCP), nullptr);
        DEFER(cat_socket_close(&conn));
        ASSERT_TRUE(cat_socket_accept(&server, &conn));
        std::string request;
        char buffer[4096];
        while (request.find("\r\n\r\n") == std::string::npos) {
            ssize_t n = cat_socket_recv(&conn, buffer, sizeof(buffer));
            ASSERT_GT(n, 0);
            request.append(buffer, n);
        }
        ASSERT_TRUE(cat_socket_send(&conn, CAT_STRL(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 2\r\n"
            "Connection: close\r\n"
            "\r\n"
            "OK"
        )));
    });
    std::string server_url = std::string("http://") + TEST_LISTEN_IPV4 + ":" + std::to_string(cat_socket_get_port(&server, false));

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));

    CURL *ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    DEFER(curl_easy_cleanup(ch));
    curl_easy_setopt(ch, CURLOPT_URL, server_url.c_str());
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, cat_test_curl_write_function);
    curl_easy_setopt(ch, CURLOPT_CLOSESOCKETFUNCTION, cat_test_curl_yield_and_close_socket);
    curl_easy_setopt(ch, CURLOPT_TIMEOUT_MS, TEST_IO_TIMEOUT);

    ASSERT_EQ(cat_curl_easy_perform(ch), CURLE_OK);
    ASSERT_EQ(std::string(buffer.value, buffer.length), "OK");
}

TEST(cat_curl_multi, optional_outputs_and_extra_descriptors)
{
    CURLM *multi = cat_curl_multi_init();
    ASSERT_NE(multi, nullptr);
    DEFER(cat_curl_multi_cleanup(multi));
    ASSERT_EQ(cat_curl_multi_perform(multi, nullptr), CURLM_OK);
    ASSERT_EQ(cat_curl_multi_wait(multi, nullptr, 0, 0, nullptr), CURLM_OK);

    cat_socket_t socket;
    ASSERT_EQ(cat_socket_create(&socket, CAT_SOCKET_TYPE_UDP), &socket);
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind_to(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    curl_waitfd extra = {};
    extra.fd = cat_socket_get_fd(&socket);
    ASSERT_NE(extra.fd, CAT_SOCKET_INVALID_FD);
    extra.events = CURL_WAIT_POLLIN;
    // Extra descriptors are unsupported; rejection must leave the multi usable.
    ASSERT_EQ(cat_curl_multi_wait(multi, &extra, 1, 0, nullptr), CURLM_INTERNAL_ERROR);
    ASSERT_EQ(cat_curl_multi_wait(multi, &extra, 0, 0, nullptr), CURLM_INTERNAL_ERROR);
    ASSERT_EQ(cat_curl_multi_wait(multi, nullptr, 1, 0, nullptr), CURLM_INTERNAL_ERROR);
    int running = -1;
    ASSERT_EQ(cat_curl_multi_perform(multi, &running), CURLM_OK);
    ASSERT_EQ(running, 0);
    int numfds = -1;
    ASSERT_EQ(cat_curl_multi_wait(multi, nullptr, 0, 0, &numfds), CURLM_OK);
    ASSERT_EQ(numfds, 0);
}

TEST(cat_curl_multi, cleanup_unfinished_transfer)
{
    // Finish the next request with a fresh multi after closing an unfinished one.
    for (cat_timeout_t wait_timeout : std::array<cat_timeout_t, 3>{TEST_IO_TIMEOUT, -1, 0}) {
        SCOPED_TRACE(wait_timeout);
        const bool complete = wait_timeout == 0;
        cat_socket_t server;
        ASSERT_EQ(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP), &server);
        DEFER(cat_socket_close(&server));
        ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
        ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
        bool received = false;
        ssize_t final_read = -1;
        wait_group wg;
        co([&] {
            wg++;
            DEFER(wg--);
            cat_socket_t connection;
            ASSERT_EQ(cat_socket_create(&connection, CAT_SOCKET_TYPE_TCP), &connection);
            DEFER(cat_socket_close(&connection));
            ASSERT_TRUE(cat_socket_accept(&server, &connection));
            ASSERT_TRUE(cat_socket_set_read_timeout(&connection, TEST_IO_TIMEOUT));
            std::string request;
            char buffer[4096];
            while (request.find("\r\n\r\n") == std::string::npos) {
                ssize_t n = cat_socket_recv(&connection, buffer, sizeof(buffer));
                ASSERT_GT(n, 0);
                request.append(buffer, n);
            }
            received = true;
            if (complete) {
                ASSERT_TRUE(cat_socket_send(&connection, CAT_STRL(
                    "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK")));
            }
            final_read = cat_socket_recv(&connection, buffer, sizeof(buffer));
        });
        std::string url = std::string("http://") + TEST_LISTEN_IPV4 + ":" +
            std::to_string(cat_socket_get_sock_port(&server));
        cat_buffer_t response;
        ASSERT_TRUE(cat_buffer_create(&response, 0));
        DEFER(cat_buffer_close(&response));
        CURL *easy = curl_easy_init();
        ASSERT_NE(easy, nullptr);
        DEFER(curl_easy_cleanup(easy));
        ASSERT_EQ(curl_easy_setopt(easy, CURLOPT_URL, url.c_str()), CURLE_OK);
        ASSERT_EQ(curl_easy_setopt(easy, CURLOPT_PROXY, ""), CURLE_OK);
        ASSERT_EQ(curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, TEST_IO_TIMEOUT), CURLE_OK);
        ASSERT_EQ(curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, cat_test_curl_write_function), CURLE_OK);
        ASSERT_EQ(curl_easy_setopt(easy, CURLOPT_WRITEDATA, &response), CURLE_OK);
        bool wait_done = complete;
        CURLMcode wait_result = CURLM_OK;
        CURLM *multi = cat_curl_multi_init();
        ASSERT_NE(multi, nullptr);
        DEFER(if (multi != nullptr) { cat_curl_multi_cleanup(multi); });
        ASSERT_EQ(curl_multi_add_handle(multi, easy), CURLM_OK);
        int running = 1;
        while (!received || (complete && running != 0)) {
            ASSERT_EQ(cat_curl_multi_perform(multi, &running), CURLM_OK);
            ASSERT_TRUE(received || running != 0);
        }
        if (complete) {
            int remaining;
            CURLMsg *message = curl_multi_info_read(multi, &remaining);
            ASSERT_NE(message, nullptr);
            ASSERT_EQ(message->msg, CURLMSG_DONE);
            ASSERT_EQ(message->data.result, CURLE_OK);
            ASSERT_EQ(std::string(response.value, response.length), "OK");
        } else {
            ASSERT_EQ(running, 1);
            ASSERT_EQ(response.length, 0);
            co([&] {
                wg++;
                DEFER(wg--);
                do {
                    wait_result = cat_curl_multi_wait(multi, nullptr, 0, static_cast<int>(wait_timeout), nullptr);
                } while (wait_result == CURLM_OK);
                wait_done = true;
            });
            ASSERT_FALSE(wait_done);
            int ready = -1;
            ASSERT_EQ(cat_curl_multi_wait(multi, nullptr, 0, 0, &ready), CURLM_INTERNAL_ERROR);
            ASSERT_EQ(ready, 0);
            ASSERT_FALSE(wait_done);
        }
        // Exercise cleanup without remove_handle, after the request reached the peer.
        ASSERT_EQ(cat_curl_multi_cleanup(multi), CURLM_OK);
        multi = nullptr;
        ASSERT_TRUE(wait_done);
        if (!complete) {
            ASSERT_EQ(wait_result, CURLM_INTERNAL_ERROR);
        }
        curl_easy_cleanup(easy);
        easy = nullptr;
        wg();
        ASSERT_EQ(final_read, 0);
    }
}

TEST(cat_curl_multi, expect_100_continue)
{
    CURL *ch;
    CURLM *mh;
    int still_running = 1;

    cat_socket_t server;
    ASSERT_NE(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP), nullptr);
    wait_group wg;
    // Close the client and listener before joining the server coroutine.
    // The accepted connection stays alive until its sender and reader finish.
    DEFER(cat_socket_close(&server));
    ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
    // fake http server that wont send 100-continue
    co([&] {
        wg++;
        DEFER(wg--);
        cat_socket_t conn;
        ASSERT_NE(cat_socket_create(&conn, CAT_SOCKET_TYPE_TCP), nullptr);
        DEFER(cat_socket_close(&conn));
        ASSERT_TRUE(cat_socket_accept(&server, &conn));
        cat_time_msleep(200);
        ASSERT_TRUE(cat_socket_send(&conn, CAT_STRL(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 13\r\n"
            "\r\n"
            "Hello, World!"
        )));
        // curl will keep sending data after received 200
        // so we need to read all data to avoid blocking
        // then close the connection after curl finished
        char dummy_buffer[16384];
        ssize_t n = -CAT_EAGAIN;
        while (n > 0 || n == -CAT_EAGAIN) {
            n = cat_socket_recv(&conn, dummy_buffer, sizeof(dummy_buffer));
        }
    });
    std::string server_url = std::string("http://") + TEST_LISTEN_IPV4 + ":" + std::to_string(cat_socket_get_port(&server, false));

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));

    // curl will expect 100-continue for body size > 1024 * 1024
    char *body = (char *)malloc(1024 * 1024 + 4096);
    memset(body, 'a', 1024 * 1024 + 4096);
    body[1024 * 1024 + 4095] = '\0';
    DEFER(free(body));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    DEFER(curl_easy_cleanup(ch));
    curl_easy_setopt(ch, CURLOPT_URL, server_url.c_str());
    curl_easy_setopt(ch, CURLOPT_TIMEOUT_MS, TEST_IO_TIMEOUT);
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(ch, CURLOPT_EXPECT_100_TIMEOUT_MS, 1);
    curl_easy_setopt(ch, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(ch, CURLOPT_POSTFIELDSIZE, 1024 * 1024 + 4096);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, cat_test_curl_write_function);
    // curl_easy_setopt(ch, CURLOPT_VERBOSE, 1);

    mh = cat_curl_multi_init();
    ASSERT_NE(mh, nullptr);
    DEFER(cat_curl_multi_cleanup(mh));
    ASSERT_EQ(curl_multi_add_handle(mh, ch), CURLM_OK);
    DEFER(curl_multi_remove_handle(mh, ch));

    while (still_running) {
        ASSERT_EQ(cat_curl_multi_perform(mh, &still_running), CURLM_OK);
    }

    ASSERT_NE(
        std::string(buffer.value, buffer.length).find("Hello, World!"),
        std::string::npos
    );
}

#endif
