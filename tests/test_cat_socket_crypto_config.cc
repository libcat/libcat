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

TEST(cat_socket, large_certificate_handshake_and_transfer)
{
    std::string names = "DNS:localhost";
    for (unsigned int index = 0; names.size() < CAT_SSL_BUFFER_SIZE * 2; index++) {
        names += ",DNS:name" + std::to_string(index) + ".example";
    }
    X509KeyCertPairConfig config = {
        {"keyType", "RSA2048"},
        {"CN", "localhost"},
        {"notBeforeOffsetSeconds", 0},
        {"notAfterOffsetSeconds", 365 * 86400},
        {"subjectAltName", names},
        {"basicConstraints", "CA:FALSE"},
    };
    auto certificate = X509KeyCertPair::create(config);
    ASSERT_NE(certificate, nullptr);
    const auto paths = certificate->exportPEMs();
    cat_socket_t server, client, connection;
    ASSERT_EQ(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP4), &server);
    DEFER(cat_socket_close(&server));
    ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
    ASSERT_EQ(cat_socket_create(&client, CAT_SOCKET_TYPE_TCP4), &client);
    DEFER(if (cat_socket_is_available(&client)) { cat_socket_close(&client); });
    ASSERT_TRUE(cat_socket_connect_to(&client, CAT_STRL(TEST_LISTEN_IPV4), cat_socket_get_sock_port(&server)));
    ASSERT_EQ(cat_socket_create(&connection, CAT_SOCKET_TYPE_TCP), &connection);
    DEFER(if (cat_socket_is_available(&connection)) { cat_socket_close(&connection); });
    ASSERT_TRUE(cat_socket_accept(&server, &connection));
    cat_socket_crypto_options_t client_options, server_options;
    cat_socket_crypto_options_init(&server_options, cat_false);
    server_options.certificate = paths.cert;
    server_options.certificate_key = paths.key;
    server_options.protocols = CAT_SSL_PROTOCOL_TLSv1_2;
    int certificate_size = 0;
    server_options.before_handshake_callback_data = &certificate_size;
    server_options.before_handshake_callback = [](cat_ssl_t *ssl, void *data) -> cat_bool_t {
        X509 *certificate = SSL_get_certificate(ssl->connection);
        if (certificate == nullptr) {
            return cat_false;
        }
        *static_cast<int *>(data) = i2d_X509(certificate, nullptr);
        return *static_cast<int *>(data) > 0;
    };
    cat_socket_crypto_options_init(&client_options, cat_true);
    client_options.ca_file = paths.cert;
    client_options.peer_name = "localhost";
    client_options.protocols = CAT_SSL_PROTOCOL_TLSv1_2;
    wait_group wg;
    cat_bool_t server_ready = cat_false;
    co([&] {
        wg++;
        DEFER(wg--);
        server_ready = cat_socket_enable_crypto(&connection, &server_options);
        if (!server_ready && cat_socket_is_available(&connection)) {
            cat_socket_close(&connection);
        }
    });
    DEFER(if (cat_socket_is_available(&connection)) { cat_socket_close(&connection); }
        if (cat_socket_is_available(&client)) { cat_socket_close(&client); } wg(TEST_IO_TIMEOUT));
    ASSERT_TRUE(cat_socket_enable_crypto(&client, &client_options));
    ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
    ASSERT_TRUE(server_ready);
    ASSERT_GT(certificate_size, CAT_SSL_BUFFER_SIZE);
    // A certificate larger than the BIO buffers must complete without losing TLS records.
    ASSERT_TRUE(cat_socket_send(&client, CAT_STRL("large-handshake")));
    char buffer[CAT_STRLEN("large-handshake")];
    ASSERT_EQ(cat_socket_read(&connection, buffer, sizeof(buffer)), sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "large-handshake");
}

TEST(cat_socket, interrupted_handshake_reports_failure_once)
{
    for (bool close_peer : {false, true}) {
        SCOPED_TRACE(close_peer);
        cat_socket_t server, client, connection;
        ASSERT_EQ(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP4), &server);
        DEFER(cat_socket_close(&server));
        ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
        ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
        ASSERT_EQ(cat_socket_create(&client, CAT_SOCKET_TYPE_TCP4), &client);
        DEFER(if (cat_socket_is_available(&client)) { cat_socket_close(&client); });
        ASSERT_TRUE(cat_socket_connect_to(&client, CAT_STRL(TEST_LISTEN_IPV4), cat_socket_get_sock_port(&server)));
        ASSERT_EQ(cat_socket_create(&connection, CAT_SOCKET_TYPE_TCP), &connection);
        DEFER(if (cat_socket_is_available(&connection)) { cat_socket_close(&connection); });
        ASSERT_TRUE(cat_socket_accept(&server, &connection));
        cat_socket_crypto_options_t options;
        cat_socket_crypto_options_init(&options, cat_true);
        options.verify_peer = cat_false;
        options.verify_peer_name = cat_false;
        unsigned int callbacks = 0;
        options.after_handshake_callback_data = &callbacks;
        options.after_handshake_callback = [](cat_ssl_t *ssl, cat_bool_t success, void *data) {
            ++*static_cast<unsigned int *>(data);
            EXPECT_FALSE(success);
            EXPECT_FALSE(cat_ssl_is_established(ssl));
        };
        wait_group wg;
        if (close_peer) {
            co([&] {
                wg++;
                DEFER(wg--);
                unsigned char header[5];
                // Drain the ClientHello record so a normal TCP EOF interrupts the handshake.
                if (cat_socket_read(&connection, reinterpret_cast<char *>(header), sizeof(header)) == sizeof(header)) {
                    size_t length = (static_cast<size_t>(header[3]) << 8) | header[4];
                    std::string hello(length, '\0');
                    EXPECT_EQ(cat_socket_read(&connection, hello.data(), hello.size()), hello.size());
                } else {
                    ADD_FAILURE() << cat_get_last_error_message();
                }
                cat_socket_close(&connection);
            });
        }
        DEFER(if (cat_socket_is_available(&connection)) { cat_socket_close(&connection); } wg(TEST_IO_TIMEOUT));
        ASSERT_FALSE(cat_socket_enable_crypto_ex(&client, &options, close_peer ? TEST_IO_TIMEOUT : 1));
        cat_errno_t error = cat_get_last_error_code();
        ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
        ASSERT_EQ(error, close_peer ? CAT_ECONNRESET : CAT_ETIMEDOUT);
        ASSERT_EQ(callbacks, 1);
        ASSERT_FALSE(cat_socket_is_available(&client));
    }
}
#endif
