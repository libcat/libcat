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

static size_t cat_test_curl_write_function(const char *ptr, size_t length, size_t n, cat_buffer_t *buffer)
{
    cat_buffer_append(buffer, ptr, length * n);

    return length * n;
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
    std::string response;
    ASSERT_EQ(cat_test_curl_query(TEST_REMOTE_HTTP_SERVER_HOST, response), CURLE_OK);
    ASSERT_NE(response.find(TEST_REMOTE_HTTP_SERVER_KEYWORD), std::string::npos);
}

TEST(cat_curl, concurrency)
{
    ASSERT_TRUE(cat_coroutine_wait_all()); // call it before timing test
    auto concurrency = ([](uint32_t concurrency) {
        cat_sync_wait_group_t wg;
        ASSERT_NE(cat_sync_wait_group_create(&wg), nullptr);
        ASSERT_TRUE(cat_sync_wait_group_add(&wg, concurrency));
        for (size_t n = 0; n < concurrency; n++) {
            co([&wg] {
                std::string response;
                ASSERT_EQ(cat_test_curl_query(TEST_REMOTE_HTTP_SERVER_HOST, response), CURLE_OK);
                ASSERT_NE(response.find(TEST_REMOTE_HTTP_SERVER_KEYWORD), std::string::npos);
                ASSERT_TRUE(cat_sync_wait_group_done(&wg));
            });
        }
        ASSERT_TRUE(cat_sync_wait_group_wait(&wg, TEST_IO_TIMEOUT));
    });

    if (!is_valgrind()) {
        auto s1 = cat_time_msec();
        concurrency(TEST_MAX_CONCURRENCY / 8);
        s1 = cat_time_msec() - s1;

        auto s2 = cat_time_msec();
        concurrency(TEST_MAX_CONCURRENCY / 2);
        s2 = cat_time_msec() - s2;

        ASSERT_LE(s2 / s1, 3.0);
    } else {
        concurrency(TEST_MAX_CONCURRENCY);
    }
}

TEST(cat_curl, cancel)
{
    wait_group wg;
    cat_coroutine_t *coroutine = co([&wg] {
        wg++;
        DEFER(wg--);
        std::string response;
        ASSERT_NE(cat_test_curl_query(TEST_REMOTE_HTTP_SERVER_HOST, response), CURLE_OK);
        ASSERT_EQ(response.find(TEST_REMOTE_HTTP_SERVER_KEYWORD), std::string::npos);
    });
    cat_coroutine_resume(coroutine, nullptr, nullptr);
}

TEST(cat_curl, timeout)
{
    std::string response;
    ASSERT_EQ(cat_test_curl_query(TEST_REMOTE_HTTP_SERVER_HOST, response, 1), CURLE_OPERATION_TIMEDOUT);
    ASSERT_EQ(response.find(TEST_REMOTE_HTTP_SERVER_KEYWORD), std::string::npos);
}

TEST(cat_curl, busy)
{
    wait_group wg;
    CURL *ch;
    co([&] {
        wg++;
        DEFER(wg--);
        std::string response;
        ASSERT_EQ(cat_test_curl_query(TEST_REMOTE_HTTP_SERVER_HOST, response, TEST_IO_TIMEOUT, &ch), CURLE_OK);
        ASSERT_NE(response.find(TEST_REMOTE_HTTP_SERVER_KEYWORD), std::string::npos);
    });
    ASSERT_EQ(cat_curl_easy_perform(ch), CURLE_AGAIN);
}

TEST(cat_curl_multi, base)
{
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
    curl_easy_setopt(ch, CURLOPT_URL, TEST_REMOTE_HTTP_SERVER_HOST);
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
    CURL *ch;
    CURLM *mh;
    int still_running = 0;

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    DEFER(curl_easy_cleanup(ch));
    curl_easy_setopt(ch, CURLOPT_URL, TEST_REMOTE_HTTP_SERVER_HOST);
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
    CURL *ch;
    CURLM *mh;
    int still_running = 0;

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    DEFER(curl_easy_cleanup(ch));
    curl_easy_setopt(ch, CURLOPT_URL, TEST_REMOTE_HTTP_SERVER_HOST);
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
    CURL *ch;
    CURLM *mh;
    int still_running = 0;

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    DEFER(curl_easy_cleanup(ch));
    curl_easy_setopt(ch, CURLOPT_URL, TEST_REMOTE_HTTP_SERVER_HOST);
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

    ASSERT_NE(
        std::string(buffer.value, buffer.length).find(TEST_REMOTE_HTTP_SERVER_KEYWORD),
        std::string::npos
    );
}

TEST(cat_curl_multi, no_wait_sleep_1)
{
    CURL *ch;
    CURLM *mh;
    int still_running = 0;

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    DEFER(curl_easy_cleanup(ch));
    curl_easy_setopt(ch, CURLOPT_URL, TEST_REMOTE_HTTP_SERVER_HOST);
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
    CURL *ch;
    CURLM *mh;
    int still_running = 0;

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    DEFER(curl_easy_cleanup(ch));
    curl_easy_setopt(ch, CURLOPT_URL, TEST_REMOTE_HTTP_SERVER_HOST);
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
    CURL *ch;
    CURLM *mh;
    int still_running = 0;

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    DEFER(curl_easy_cleanup(ch));
    curl_easy_setopt(ch, CURLOPT_URL, TEST_REMOTE_HTTP_SERVER_HOST);
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

    cat_socket_t server, conn;
    bool connected = false;
    ASSERT_NE(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP), nullptr);
    DEFER(cat_socket_close(&server));
    DEFER([&] {
        if (connected) {
            cat_socket_close(&conn);
        }
    } ());
    ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
    // fake http server that wont send 100-continue
    co([&] {
        ASSERT_NE(cat_socket_create(&conn, CAT_SOCKET_TYPE_TCP), nullptr);
        ASSERT_TRUE(cat_socket_accept(&server, &conn));
        connected = true;
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

TEST(cat_curl_multi, expect_100_continue)
{
    CURL *ch;
    CURLM *mh;
    int still_running = 1;

    cat_socket_t server, conn;
    bool connected = false;
    ASSERT_NE(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP), nullptr);
    DEFER(cat_socket_close(&server));
    DEFER([&] {
        if (connected) {
            cat_socket_close(&conn);
        }
    } ());
    ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
    // fake http server that wont send 100-continue
    co([&] {
        ASSERT_NE(cat_socket_create(&conn, CAT_SOCKET_TYPE_TCP), nullptr);
        ASSERT_TRUE(cat_socket_accept(&server, &conn));
        connected = true;
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
