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
  | Author: Twosee <twose@qq.com>                                            |
  |         codinghuang <2812240764@qq.com>                                  |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

TEST(cat_dns, getaddrinfo)
{
    struct addrinfo hints;
    struct addrinfo *response;

    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = 0;
    hints.ai_family = AF_INET;

    response = cat_dns_getaddrinfo(TEST_REMOTE_IPV6_HTTP_SERVER_HOST, nullptr, &hints);
    if (response == nullptr && cat_get_last_error_code() == CAT_EAI_NONAME) {
        return;
    }
    ASSERT_NE(nullptr, response);

    cat_dns_freeaddrinfo(response);
}

TEST(cat_dns, get_ip)
{
    char ip[CAT_SOCKET_IP_BUFFER_SIZE];
    const int af_list[] = { AF_UNSPEC, AF_INET, AF_INET6 };

    for (auto n = 10; n--;) {
        for (int af : af_list) {
            bool ret;
            memset(ip, 0, sizeof(ip));
            ret = cat_dns_get_ip(CAT_STRS(ip), TEST_REMOTE_IPV6_HTTP_SERVER_HOST, af);
            if (ret == cat_false && cat_get_last_error_code() == CAT_EAI_NONAME) {
                continue;
            }
            EXPECT_TRUE(ret);
            cat_debug(DNS, "[af=%s] " TEST_REMOTE_IPV6_HTTP_SERVER_HOST " => %s\n", cat_sockaddr_af_name(af), ip);
        }
    }
}
