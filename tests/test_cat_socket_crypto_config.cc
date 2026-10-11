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

TEST(cat_socket, invalid_tls_record_closes_established_connection)
{
    const auto paths = publicCAPair->exportPEMs();
    for (bool blocking : {false, true}) {
        SCOPED_TRACE(blocking);
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
        cat_socket_crypto_options_t server_options, client_options;
        cat_socket_crypto_options_init(&server_options, cat_false);
        server_options.certificate = paths.cert;
        server_options.certificate_key = paths.key;
        cat_socket_crypto_options_init(&client_options, cat_true);
        client_options.ca_file = paths.cert;
        client_options.peer_name = "TestCA";
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
        ASSERT_TRUE(cat_socket_send(&client, CAT_STRL("verified")));
        char buffer[CAT_STRLEN("verified")];
        ASSERT_EQ(cat_socket_read(&connection, buffer, sizeof(buffer)), sizeof(buffer));
        ASSERT_EQ(std::string(buffer, sizeof(buffer)), "verified");
        // The peer sends plaintext on the established TLS transport; it must not become application data.
        ASSERT_EQ(send(cat_socket_get_fd(&client), CAT_STRL("not a TLS record"), 0), CAT_STRLEN("not a TLS record"));
        if (blocking) {
            ASSERT_EQ(cat_socket_recv(&connection, buffer, sizeof(buffer)), -1);
            ASSERT_EQ(cat_get_last_error_code(), CAT_ESSL);
        } else {
            ASSERT_EQ(cat_poll_one(cat_socket_get_fd(&connection), POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
            cat_update_last_error(CAT_ECANCELED, "Previous unrelated operation failed");
            ASSERT_EQ(cat_socket_try_recv(&connection, buffer, sizeof(buffer)), CAT_ESSL);
            ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
        }
        ASSERT_FALSE(cat_socket_is_available(&connection));
    }
}

static cat_bool_t drive_tls_peer_handshake(cat_ssl_t *ssl, cat_socket_t *transport)
{
    // Drive the peer's initial and subsequent handshakes over a real TCP stream.
    char buffer[CAT_SSL_BUFFER_SIZE];
    for (size_t attempt = 0; attempt < 32; attempt++) {
        int result = SSL_do_handshake(ssl->connection);
        int error = result == 1 ? SSL_ERROR_NONE : SSL_get_error(ssl->connection, result);
        if (error != SSL_ERROR_NONE && error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
            return cat_false;
        }
        int length;
        while ((length = cat_ssl_read_encrypted_bytes(ssl, buffer, sizeof(buffer))) > 0) {
            if (!cat_socket_send(transport, buffer, static_cast<size_t>(length))) {
                return cat_false;
            }
        }
        if (length < 0) {
            return cat_false;
        }
        if (result == 1 && !SSL_renegotiate_pending(ssl->connection)) {
            return cat_true;
        }
        if (error == SSL_ERROR_WANT_READ) {
            ssize_t received = cat_socket_recv(transport, buffer, sizeof(buffer));
            if (received <= 0 || cat_ssl_write_encrypted_bytes(ssl, buffer, static_cast<size_t>(received)) != received) {
                return cat_false;
            }
        }
    }
    return cat_false;
}

TEST(cat_socket, renegotiation_decisions_preserve_connection_lifetime)
{
    enum class decision { reject, pause_reject, pause_accept };
    const auto paths = publicCAPair->exportPEMs();
    for (decision action : {decision::reject, decision::pause_reject, decision::pause_accept}) {
        SCOPED_TRACE(static_cast<int>(action));
        struct notification {
            cat_socket_t *socket = nullptr;
            decision action;
            unsigned int calls = 0;
            cat_bool_t paused = cat_false;
            cat_bool_t close_denied = cat_false;
            cat_bool_t switch_denied = cat_false;
        } state;
        state.action = action;
        cat_ssl_context_t *peer_context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOL_TLSv1_2);
        ASSERT_NE(peer_context, nullptr);
        DEFER(cat_ssl_context_close(peer_context));
        cat_ssl_context_disable_verify_peer(peer_context);
        cat_ssl_t peer = {};
        ASSERT_EQ(cat_ssl_create(&peer, peer_context), &peer);
        DEFER(cat_ssl_close(&peer));
        cat_ssl_set_connect_state(&peer);
        cat_ssl_configure_renegotiation(&peer, cat_true, nullptr, nullptr);
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
        ASSERT_FALSE(cat_socket_resume_renegotiation(&connection, cat_true));
        ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
        state.socket = &connection;
        cat_socket_crypto_options_t options;
        cat_socket_crypto_options_init(&options, cat_false);
        options.protocols = CAT_SSL_PROTOCOL_TLSv1_2;
        options.certificate = paths.cert;
        options.certificate_key = paths.key;
        options.allow_renegotiation = cat_true;
        options.renegotiation_callback_data = &state;
        options.renegotiation_callback = [](cat_ssl_t *ssl, void *data) -> cat_bool_t {
            notification *state = static_cast<notification *>(data);
            state->calls++;
            state->switch_denied = cat_coroutine_switch_denied();
            state->close_denied = !cat_socket_close(state->socket) && cat_get_last_error_code() == CAT_ELOCKED;
            if (state->action == decision::reject) {
                return cat_false;
            }
            state->paused = cat_ssl_pause_renegotiation(ssl);
            return state->paused;
        };
        wait_group wg;
        cat_bool_t peer_ready = cat_false, peer_completed = cat_false;
        co([&] {
            wg++;
            DEFER(wg--);
            peer_ready = drive_tls_peer_handshake(&peer, &client);
            if (!peer_ready && cat_socket_is_available(&client)) {
                cat_socket_close(&client);
            }
        });
        DEFER(if (cat_socket_is_available(&connection)) { cat_socket_close(&connection); }
            if (cat_socket_is_available(&client)) { cat_socket_close(&client); } wg(TEST_IO_TIMEOUT));
        ASSERT_TRUE(cat_socket_enable_crypto(&connection, &options)) << cat_get_last_error_message();
        ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
        ASSERT_TRUE(peer_ready);
        ASSERT_EQ(SSL_renegotiate(peer.connection), 1);
        co([&] {
            wg++;
            DEFER(wg--);
            peer_completed = drive_tls_peer_handshake(&peer, &client);
            if (peer_completed) {
                char plaintext[] = "ok";
                cat_io_vector_t vector;
                vector.base = plaintext;
                vector.length = sizeof(plaintext) - 1;
                char *encrypted = nullptr;
                size_t length = 0;
                DEFER(cat_free(encrypted));
                peer_completed = cat_ssl_encrypt(&peer, &vector, 1, &encrypted, &length) &&
                    cat_socket_send(&client, encrypted, length);
            }
        });
        const cat_bool_t switch_denied = cat_coroutine_switch_denied();
        char buffer[16];
        ssize_t received = cat_socket_recv(&connection, buffer, sizeof(buffer));
        ASSERT_EQ(state.calls, 1);
        ASSERT_TRUE(state.close_denied);
        ASSERT_TRUE(state.switch_denied);
        ASSERT_EQ(cat_coroutine_switch_denied(), switch_denied);
        if (action == decision::reject) {
            ASSERT_EQ(received, 0);
        } else {
            ASSERT_EQ(received, -1);
            ASSERT_EQ(cat_get_last_error_code(), CAT_EAGAIN);
            ASSERT_TRUE(state.paused);
            ASSERT_TRUE(cat_socket_is_available(&connection));
            ASSERT_FALSE(cat_socket_send(&connection, CAT_STRL("pending")));
            ASSERT_EQ(cat_get_last_error_code(), CAT_ELOCKED);
            ASSERT_TRUE(cat_socket_resume_renegotiation(&connection, action == decision::pause_accept));
            if (action == decision::pause_accept) {
                ASSERT_EQ(cat_socket_read(&connection, buffer, 2), 2);
                ASSERT_EQ(std::string(buffer, 2), "ok");
            }
        }
        if (cat_socket_is_available(&connection)) {
            ASSERT_TRUE(cat_socket_close(&connection));
        }
        ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
        ASSERT_EQ(peer_completed, action == decision::pause_accept);
    }
}
#endif
