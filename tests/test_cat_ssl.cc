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
#ifdef CAT_SSL

TEST(cat_ssl, remote_https_server)
{
    SKIP_IF(is_offline());
    cat_socket_t *socket;
    char buffer[TEST_BUFFER_SIZE_STD];
    ssize_t nread;

    socket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_TCP);
    ASSERT_NE(socket, nullptr);
    DEFER(cat_socket_close(socket));

    ASSERT_TRUE(cat_socket_connect(socket, TEST_REMOTE_HTTPS_SERVER));
    ASSERT_TRUE(cat_socket_enable_crypto(socket, nullptr, nullptr));

    char *request = cat_sprintf(
        "GET / HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: %s\r\n"
        "Accept: */*\r\n"
        "\r\n",
        TEST_REMOTE_HTTPS_SERVER_HOST,
        TEST_HTTP_CLIENT_FAKE_USERAGENT
    );
    ASSERT_NE(request, nullptr);
    DEFER(cat_free(request));
    ASSERT_TRUE(cat_socket_send(socket, request, strlen(request)));
    nread = cat_socket_recv(socket, CAT_STRS(buffer));
    ASSERT_GT(nread, 0);
    cat_debug(SOCKET, "Data[%zd]: %.*s", nread, (int) nread, buffer);
    ASSERT_NE(std::string(buffer, nread).find(TEST_REMOTE_HTTPS_SERVER_KEYWORD), std::string::npos);
}

#endif
