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
 */

#include "test.h"

#ifdef CAT_SSL
TEST(cat_ssl, allocated_connection_close_preserves_context)
{
    cat_ssl_context_t *context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOLS_DEFAULT);
    ASSERT_NE(context, nullptr);
    DEFER(cat_ssl_context_close(context));
    unsigned int references = CAT_REF_GET(context);
    for (unsigned int i = 0; i < 3; i++) {
        cat_ssl_t *ssl = cat_ssl_create(nullptr, context);
        ASSERT_NE(ssl, nullptr);
        ASSERT_EQ(CAT_REF_GET(context), references + 1);
        cat_ssl_close(ssl);
        ASSERT_EQ(CAT_REF_GET(context), references);
    }
}
TEST(cat_socket, rejected_handshake_callback_releases_ssl_and_preserves_tcp)
{
    static const int index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr,
        [](void *, void *data, CRYPTO_EX_DATA *, int, long, void *) {
            if (data != nullptr) {
                (*static_cast<unsigned int *>(data))++;
            }
        });
    ASSERT_GE(index, 0);
    unsigned int freed = 0;
    cat_socket_t server, client, connection;
    ASSERT_EQ(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP4), &server);
    DEFER(cat_socket_close(&server));
    ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
    ASSERT_EQ(cat_socket_create(&client, CAT_SOCKET_TYPE_TCP4), &client);
    DEFER(if (cat_socket_is_available(&client)) { cat_socket_close(&client); });
    ASSERT_TRUE(cat_socket_connect_to(&client, CAT_STRL(TEST_LISTEN_IPV4), cat_socket_get_sock_port(&server)));
    ASSERT_EQ(cat_socket_create(&connection, CAT_SOCKET_TYPE_TCP), &connection);
    DEFER(cat_socket_close(&connection));
    ASSERT_TRUE(cat_socket_accept(&server, &connection));
    cat_socket_crypto_options_t options;
    cat_socket_crypto_options_init(&options, cat_true);
    options.verify_peer = cat_false;
    options.verify_peer_name = cat_false;
    options.before_handshake_callback_data = &freed;
    options.before_handshake_callback = [](cat_ssl_t *ssl, void *data) -> cat_bool_t {
        EXPECT_EQ(SSL_set_ex_data(ssl->connection, index, data), 1);
        cat_update_last_error(CAT_ECANCELED, "Handshake rejected before sending TLS data");
        return cat_false;
    };
    ASSERT_FALSE(cat_socket_enable_crypto(&client, &options));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
    // OpenSSL invokes the free callback when the real SSL object is released.
    ASSERT_EQ(freed, 1);
    ASSERT_TRUE(cat_socket_is_established(&client));
    ASSERT_FALSE(cat_socket_is_encrypted(&client));
    char buffer[CAT_STRLEN("plain-after-rejection")];
    ASSERT_EQ(cat_socket_try_recv(&connection, buffer, sizeof(buffer)), CAT_EAGAIN);
    ASSERT_TRUE(cat_socket_send(&client, CAT_STRL("plain-after-rejection")));
    ASSERT_EQ(cat_socket_read(&connection, buffer, sizeof(buffer)), sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "plain-after-rejection");
}
#endif
