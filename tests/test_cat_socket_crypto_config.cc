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
TEST(cat_socket, crypto_configuration_errors_preserve_tcp)
{
    enum class failure {
        certificate_callback,
        ca_callback,
        client_ca_list,
        certificate,
        private_key,
#ifdef CAT_SSL_HAVE_TLS_ALPN
        alpn,
#endif
    };
    const auto paths = publicCAPair->exportPEMs();
    for (failure error : {failure::certificate_callback, failure::ca_callback,
        failure::client_ca_list, failure::certificate, failure::private_key,
#ifdef CAT_SSL_HAVE_TLS_ALPN
        failure::alpn,
#endif
    }) {
        SCOPED_TRACE(static_cast<int>(error));
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
        cat_socket_crypto_options_init(&options, error != failure::client_ca_list);
        options.verify_peer = cat_false;
        options.verify_peer_name = cat_false;
        bool callback_called = false, handshake_started = false, handshake_finished = false;
        options.context = &callback_called;
        auto reject = [](cat_ssl_context_t *, cat_socket_crypto_options_t *options) -> cat_bool_t {
            *static_cast<bool *>(options->context) = true;
            cat_update_last_error(CAT_ECANCELED, "Certificate configuration rejected");
            return cat_false;
        };
        cat_errno_t expected_error = CAT_ESSL;
        std::string protocols(65535, 'a');
        switch (error) {
            case failure::certificate_callback:
                options.load_certificate = reject;
                expected_error = CAT_ECANCELED;
                break;
            case failure::ca_callback:
                options.verify_peer = cat_true;
                options.ca_file = paths.key;
                options.load_ca = reject;
                expected_error = CAT_ECANCELED;
                break;
            case failure::client_ca_list:
                options.verify_peer = cat_true;
                options.ca_file = paths.key;
                break;
            case failure::certificate:
                options.certificate = paths.key;
                break;
            case failure::private_key:
                options.certificate = paths.cert;
                options.certificate_key = paths.cert;
                break;
#ifdef CAT_SSL_HAVE_TLS_ALPN
            case failure::alpn:
                options.alpn_protocols = protocols.c_str();
                expected_error = CAT_EINVAL;
                break;
#endif
        }
        options.before_handshake_callback_data = &handshake_started;
        options.before_handshake_callback = [](cat_ssl_t *, void *data) -> cat_bool_t {
            *static_cast<bool *>(data) = true;
            return cat_true;
        };
        options.after_handshake_callback_data = &handshake_finished;
        options.after_handshake_callback = [](cat_ssl_t *, cat_bool_t, void *data) {
            *static_cast<bool *>(data) = true;
        };
        ASSERT_FALSE(cat_socket_enable_crypto(&client, &options));
        ASSERT_EQ(cat_get_last_error_code(), expected_error) << cat_get_last_error_message();
        ASSERT_EQ(callback_called, error == failure::certificate_callback || error == failure::ca_callback);
        ASSERT_FALSE(handshake_started);
        ASSERT_FALSE(handshake_finished);
        ASSERT_TRUE(cat_socket_is_established(&client));
        ASSERT_FALSE(cat_socket_is_encrypted(&client));
        // Configuration fails before sending TLS records; the original TCP stream remains usable.
        char buffer[CAT_STRLEN("plain-after-configuration-error")];
        ASSERT_EQ(cat_socket_try_recv(&connection, buffer, sizeof(buffer)), CAT_EAGAIN);
        ASSERT_TRUE(cat_socket_send(&client, CAT_STRL("plain-after-configuration-error")));
        ASSERT_EQ(cat_socket_read(&connection, buffer, sizeof(buffer)), sizeof(buffer));
        ASSERT_EQ(std::string(buffer, sizeof(buffer)), "plain-after-configuration-error");
    }
}
#endif
