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

static size_t cat_curl_write_function(const char *ptr, size_t length, size_t n, cat_buffer_t *buffer)
{
    cat_buffer_append(buffer, ptr, length * n);

    return length * n;
}
static void cat_curl_query(const char *url, std::string &response)
{
    CURL *ch;
    CURLcode ret;

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    curl_easy_setopt(ch, CURLOPT_URL, url);
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, cat_curl_write_function);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, &buffer);
    ret = cat_curl_easy_perform(ch);
    ASSERT_EQ(ret, CURLE_OK);
    response = std::string(buffer.value, buffer.length);
    cat_buffer_close(&buffer);
    curl_easy_cleanup(ch);
}

TEST(cat_curl, base)
{
    std::string response;
    cat_curl_query(TEST_REMOTE_HTTP_SERVER_HOST, response);
    ASSERT_NE(response.find(TEST_REMOTE_HTTP_SERVER_KEYWORD), std::string::npos);
}

TEST(cat_curl, time_usage)
{
    cat_sync_wait_group_t wg;
    ASSERT_NE(cat_sync_wait_group_create(&wg), nullptr);
    ASSERT_TRUE(cat_sync_wait_group_add(&wg, TEST_MAX_CONCURRENCY));
    for (size_t n = 0; n < TEST_MAX_CONCURRENCY; n++) {
        co([&wg] {
            std::string response;
            cat_curl_query(TEST_REMOTE_HTTP_SERVER_HOST, response);
            ASSERT_NE(response.find(TEST_REMOTE_HTTP_SERVER_KEYWORD), std::string::npos);
            ASSERT_TRUE(cat_sync_wait_group_done(&wg));
        });
    }
    ASSERT_TRUE(cat_sync_wait_group_wait(&wg, TEST_IO_TIMEOUT));
}

#endif
