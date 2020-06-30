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
  +--------------------------------------------------------------------------+
 */

#include "test.h"

#define CAT_SSL_BUFFER_SIZE 16384

TEST(cat_ssl, socket_connect)
{
//    cat_socket_t *socket = cat_socket_create(NULL, CAT_SOCKET_TYPE_TCP);
//    DEFER(cat_socket_close(socket));
//    ASSERT_TRUE(cat_socket_connect(socket, TEST_REMOTE_HTTPS_SERVER));
//
//    cat_ssl_t *ssl = cat_ssl_alloc(NULL);
//    ASSERT_NE(ssl, nullptr);
//    DEFER(cat_ssl_free(ssl));
//
//    ASSERT_TRUE(cat_ssl_open(ssl, CAT_SSL_TYPE_CLIENT));
//    DEFER(cat_ssl_close(ssl));
//
//    ASSERT_TRUE(cat_ssl_set_host_name(ssl, TEST_REMOTE_HTTPS_SERVER_HOST));
//
//    const char *buffer = NULL;
//    size_t length = 0;
//    ssize_t n;

//    while (true) {
//        if (cat_ssl_read_encrypted_bytes(ssl, buffer, size)) {
//
//        }
//        cat_ret_t ret = cat_ssl_handshake(ssl);
//        if (ret == CAT_SSL_HANDSHAKE_WANT_IO) {
//            if (length > 0) {
//                n = cat_socket_send(socket, buffer, length);
//                cat_debug(SSL, "cat_socket_send: n=%zd", n);
//                ASSERT_EQ(n, length);
//            }
//            n = cat_socket_recv(socket, ssl->rbuffer.value, ssl->rbuffer.size);
//            ASSERT_GT(n, 0);
//            cat_debug(SOCKET, "cat_socket_recv: n=%zd", n);
//            buffer = ssl->rbuffer.value;
//            length = n;
//            continue;
//        }
//        if (ret == CAT_SSL_HANDSHAKE_OK) {
//            break;
//        }
//        ASSERT_TRUE(0 && "CAT_SSL_HANDSHAKE_ERROR");
//    }
//
//    cat_const_string_t request = cat_const_string(
//        "GET / HTTP/1.1\r\n"
//        "Host: " TEST_REMOTE_HTTP_SERVER_HOST "\r\n"
//        "\r\n"
//    );
//
//    cat_debug(SSL, "Send request");
//    n = SSL_write(ssl->connection, request.data, request.length);
//    ASSERT_EQ(n, request.length);
//    n = BIO_read(ssl->nbio, ssl->rbuffer.value, ssl->rbuffer.size);
//    ASSERT_GT(n, 0);
//    cat_debug(SSL, "BIO_read: n=%zd, %.*s", n, (int ) n, buffer);
//    n = cat_socket_send(socket, buffer, n);
//    cat_debug(SSL, "cat_socket_send: n=%zd", n);
}
