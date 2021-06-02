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

#include "test.h"

TEST(cat_dns, getaddrinfo)
{
    struct addrinfo *response, *presponse;
    const struct addrinfo hints = { /* .ai_flags = */ AI_ALL };

    const char *domains[] = {
        TEST_REMOTE_IPV6_HTTP_SERVER_HOST,
        TEST_REMOTE_HTTP_SERVER_HOST,
        TEST_REMOTE_HTTPS_SERVER_HOST,
        "localhost"
    };

    for (int i = 0; i < CAT_ARRAY_SIZE(domains); i++) {
        response = cat_dns_getaddrinfo(domains[i], nullptr, &hints);
        if (response == nullptr && cat_get_last_error_code() == CAT_EAI_NONAME) {
            break;
        }
        ASSERT_NE(nullptr, response);
        for (presponse = response; presponse; presponse = presponse->ai_next) {
            switch (presponse->ai_addr->sa_family) {
                case AF_INET: {
                    ASSERT_EQ(presponse->ai_addrlen, sizeof(struct sockaddr_in));
                    break;
                }
                case AF_INET6: {
                    ASSERT_EQ(presponse->ai_addrlen, sizeof(struct sockaddr_in6));
                    break;
                }
            }
        }
        cat_dns_freeaddrinfo(response);
    }
}

TEST(cat_dns, get_ip)
{
    char ip[CAT_SOCKET_IP_BUFFER_SIZE];
    const cat_sa_family_t af_list[] = { AF_UNSPEC, AF_INET
    // FIXME: Windows may return WSANO_DATA when query IPV6 address by unknown reason,
    // possible a bug (feature?)
#ifndef CAT_OS_WIN
    , AF_INET6
#endif
    };

    for (auto n = 10; n--;) {
        for (cat_sa_family_t af : af_list) {
            bool ret;
            memset(ip, 0, sizeof(ip));
            ret = cat_dns_get_ip(CAT_STRS(ip), TEST_REMOTE_IPV6_HTTP_SERVER_HOST, af);
            if (ret == cat_false && cat_get_last_error_code() == CAT_EAI_NONAME) {
                continue;
            }
            EXPECT_TRUE(ret);
            cat_debug(DNS, "[af=%s] %s => %s\n", TEST_REMOTE_IPV6_HTTP_SERVER_HOST, cat_sockaddr_af_name(af), ip);
        }
    }
}

TEST(cat_dns, getaddrinfo_invalid)
{
    ASSERT_EQ(cat_dns_getaddrinfo(nullptr, nullptr, nullptr), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

TEST(cat_dns, cancel)
{
    cat_coroutine_t *current = cat_coroutine_get_current();
    char ip[CAT_SOCKET_IP_BUFFER_SIZE] = { 0 };
    bool ret;

    co([&] {
        cat_time_sleep(0);
        cat_coroutine_resume(current, nullptr, nullptr);
    });
    ret = cat_dns_get_ip(CAT_STRS(ip), TEST_REMOTE_IPV6_HTTP_SERVER_HOST, AF_UNSPEC);
    ASSERT_EQ(ret, cat_false);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
}

TEST(cat_dns, timeout)
{
    ASSERT_FALSE(cat_dns_get_ip_ex(nullptr, 0, TEST_REMOTE_IPV6_HTTP_SERVER_HOST, AF_UNSPEC, 0));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
}

TEST(cat_dns, nospc)
{
    ASSERT_FALSE(cat_dns_get_ip(nullptr, 0, TEST_REMOTE_IPV6_HTTP_SERVER_HOST, AF_INET));
    if (cat_get_last_error_code() != CAT_EAI_NONAME) {
        ASSERT_EQ(cat_get_last_error_code(), CAT_ENOSPC);
    }
#ifndef CAT_OS_WIN /* See above for reasons */
    ASSERT_FALSE(cat_dns_get_ip(nullptr, 0, TEST_REMOTE_IPV6_HTTP_SERVER_HOST, AF_INET6));
    if (cat_get_last_error_code() != CAT_EAI_NONAME) {
        ASSERT_EQ(cat_get_last_error_code(), CAT_ENOSPC);
    }
#endif
}
