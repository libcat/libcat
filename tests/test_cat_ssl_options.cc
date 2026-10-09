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

TEST(cat_ssl, context_protocols)
{
    cat_ssl_context_t *context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOL_TLSv1_2);
    ASSERT_NE(context, nullptr);
    DEFER(cat_ssl_context_close(context));
    unsigned long options = SSL_CTX_get_options(context->ctx);
    ASSERT_EQ(options & SSL_OP_NO_TLSv1_2, 0);
    ASSERT_NE(options & SSL_OP_NO_TLSv1, 0);
    cat_ssl_context_set_protocols(context, CAT_SSL_PROTOCOLS_DEFAULT);
    ASSERT_EQ(SSL_CTX_get_options(context->ctx) & SSL_OP_NO_TLSv1_2, 0);
    cat_ssl_context_set_verify_depth(context, 3);
    ASSERT_EQ(SSL_CTX_get_verify_depth(context->ctx), 3);
    cat_ssl_context_set_no_ticket(context);
    ASSERT_NE(SSL_CTX_get_options(context->ctx) & SSL_OP_NO_TICKET, 0);
    cat_ssl_context_set_no_compression(context);
    ASSERT_NE(SSL_CTX_get_options(context->ctx) & SSL_OP_NO_COMPRESSION, 0);
    cat_ssl_context_enable_verify_peer(context);
    ASSERT_NE(SSL_CTX_get_verify_mode(context->ctx) & SSL_VERIFY_PEER, 0);
    cat_ssl_context_disable_verify_peer(context);
    ASSERT_EQ(SSL_CTX_get_verify_mode(context->ctx), SSL_VERIFY_NONE);
#ifdef CAT_SSL_HAVE_SECURITY_LEVEL
    cat_ssl_context_set_security_level(context, 2);
    ASSERT_EQ(SSL_CTX_get_security_level(context->ctx), 2);
    cat_ssl_context_set_security_level(context, -1);
    ASSERT_EQ(SSL_CTX_get_security_level(context->ctx), 5);
    cat_ssl_context_set_security_level(context, 6);
    ASSERT_EQ(SSL_CTX_get_security_level(context->ctx), 5);
#endif
}

#ifdef CAT_SSL_HAVE_TLS_ALPN
TEST(cat_ssl, alpn_server_protocols)
{
    cat_ssl_context_t *context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOLS_DEFAULT);
    ASSERT_NE(context, nullptr);
    DEFER(cat_ssl_context_close(context));
    ASSERT_TRUE(cas_ssl_context_set_alpn_protocols(context, cat_false, "h2,http/1.1"));
    const char expected[] = "\x02h2\x08http/1.1";
    ASSERT_EQ(context->alpn.length, sizeof(expected) - 1);
    ASSERT_EQ(memcmp(context->alpn.value, expected, sizeof(expected) - 1), 0);
    std::string protocol(255, 'a');
    ASSERT_TRUE(cas_ssl_context_set_alpn_protocols(context, cat_false, protocol.c_str()));
    ASSERT_EQ(context->alpn.length, protocol.length() + 1);
    ASSERT_EQ(static_cast<unsigned char>(context->alpn.value[0]), 255);
    ASSERT_EQ(std::string(context->alpn.value + 1, protocol.length()), protocol);
    std::string too_long(256, 'b');
    ASSERT_FALSE(cas_ssl_context_set_alpn_protocols(context, cat_false, too_long.c_str()));
    // 无效新配置不能覆盖先前成功设置的协议列表。
    ASSERT_EQ(context->alpn.length, protocol.length() + 1);
    ASSERT_EQ(std::string(context->alpn.value + 1, protocol.length()), protocol);
    too_long.assign(65535, 'b');
    ASSERT_FALSE(cas_ssl_context_set_alpn_protocols(context, cat_false, too_long.c_str()));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}
#endif

TEST(cat_ssl, context_certificate_errors)
{
    cat_ssl_context_t *context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOLS_DEFAULT);
    ASSERT_NE(context, nullptr);
    DEFER(cat_ssl_context_close(context));
    auto paths = publicCAPair->exportPEMs();
    ASSERT_NE(paths.cert, nullptr);
    ASSERT_NE(paths.key, nullptr);
    ASSERT_FALSE(cat_ssl_context_set_certificate(context, paths.key, paths.key));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ESSL);
    ASSERT_FALSE(cat_ssl_context_set_certificate(context, paths.cert, paths.cert));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ESSL);
    ASSERT_TRUE(cat_ssl_context_set_certificate(context, paths.cert, paths.key));
    ASSERT_FALSE(cat_ssl_context_load_verify_locations(context, paths.key, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ESSL);
    ASSERT_TRUE(cat_ssl_context_load_verify_locations(context, paths.cert, nullptr));
    ASSERT_FALSE(cat_ssl_context_set_client_ca_list(context, paths.key));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ESSL);
    ASSERT_NE(std::string(cat_get_last_error_message()).find(paths.key), std::string::npos);
    ASSERT_TRUE(cat_ssl_context_set_client_ca_list(context, paths.cert));
}

TEST(cat_ssl, encrypted_private_key)
{
    cat_ssl_context_t *context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOLS_DEFAULT);
    ASSERT_NE(context, nullptr);
    DEFER(cat_ssl_context_close(context));
    const char passphrase[] = "test private key passphrase";
    X509KeyCertPairConfig config = {
        {"keyType", "RSA2048"},
        {"CN", "localhost"},
        {"notBeforeOffsetSeconds", 0},
        {"notAfterOffsetSeconds", 86400},
    };
    auto pair = X509KeyCertPair::create(config);
    ASSERT_NE(pair, nullptr);
    auto paths = pair->exportPEMs(passphrase);
    ASSERT_NE(paths.key, nullptr);
    ASSERT_NE(paths.cert, nullptr);
    ASSERT_TRUE(cat_ssl_context_set_passphrase(context, CAT_STRL("wrong passphrase")));
    ASSERT_FALSE(cat_ssl_context_set_certificate(context, paths.cert, paths.key));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ESSL);
    ASSERT_TRUE(cat_ssl_context_set_passphrase(context, CAT_STRL(passphrase)));
    ASSERT_TRUE(cat_ssl_context_set_certificate(context, paths.cert, paths.key));
}

TEST(cat_ssl, connection_without_peer)
{
    cat_ssl_context_t *context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOLS_DEFAULT);
    ASSERT_NE(context, nullptr);
    DEFER(cat_ssl_context_close(context));
    cat_ssl_t ssl = {};
    ASSERT_EQ(cat_ssl_create(&ssl, context), &ssl);
    DEFER(cat_ssl_close(&ssl));
    ASSERT_FALSE(cat_ssl_is_established(&ssl));
    ASSERT_FALSE(cat_ssl_verify_peer(&ssl, cat_false));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOCERT);
    ASSERT_FALSE(cat_ssl_check_host(&ssl, CAT_STRL("localhost")));
    ASSERT_TRUE(cat_ssl_set_sni_server_name(&ssl, "localhost"));
    ASSERT_STREQ(SSL_get_servername(ssl.connection, TLSEXT_NAMETYPE_host_name), "localhost");
    std::string oversized_name(256, 'a');
    ASSERT_FALSE(cat_ssl_set_sni_server_name(&ssl, oversized_name.c_str()));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ESSL);
    cat_ssl_set_connect_state(&ssl);
    ASSERT_EQ(cat_ssl_handshake(&ssl), CAT_SSL_RET_WANT_READ);
    char bytes[CAT_SSL_BUFFER_SIZE];
    ASSERT_GT(cat_ssl_read_encrypted_bytes(&ssl, bytes, sizeof(bytes)), 0);
    ASSERT_EQ(cat_ssl_read_encrypted_bytes(&ssl, bytes, sizeof(bytes)), CAT_RET_NONE);
    cat_ssl_set_shutdown(&ssl, CAT_SSL_SENT_SHUTDOWN);
    ASSERT_EQ(cat_ssl_get_shutdown(&ssl), CAT_SSL_SENT_SHUTDOWN);
    cat_ssl_set_quiet_shutdown(&ssl, cat_true);
    ASSERT_EQ(SSL_get_quiet_shutdown(ssl.connection), 1);
    cat_ssl_unrecoverable_error(&ssl);
    ASSERT_TRUE(cat_ssl_is_down(&ssl));
    ASSERT_EQ(cat_ssl_get_shutdown(&ssl), CAT_SSL_SENT_SHUTDOWN | CAT_SSL_RECEIVED_SHUTDOWN);
}

static void transfer_handshake_bytes(cat_ssl_t *from, cat_ssl_t *to)
{
    char buffer[CAT_SSL_BUFFER_SIZE];
    while (true) {
        int length = cat_ssl_read_encrypted_bytes(from, buffer, sizeof(buffer));
        ASSERT_GE(length, 0);
        if (length == CAT_RET_NONE) {
            return;
        }
        ASSERT_EQ(cat_ssl_write_encrypted_bytes(to, buffer, length), length);
    }
}

TEST(cat_ssl, bio_handshake_and_binary_records)
{
    cat_ssl_context_t *server_context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOL_TLSv1_2);
    ASSERT_NE(server_context, nullptr);
    DEFER(cat_ssl_context_close(server_context));
    cat_ssl_context_t *client_context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOL_TLSv1_2);
    ASSERT_NE(client_context, nullptr);
    DEFER(cat_ssl_context_close(client_context));
    auto paths = publicCAPair->exportPEMs();
    ASSERT_TRUE(cat_ssl_context_set_certificate(server_context, paths.cert, paths.key));
    ASSERT_TRUE(cat_ssl_context_load_verify_locations(client_context, paths.cert, nullptr));
    cat_ssl_context_enable_verify_peer(client_context);
#ifdef CAT_SSL_HAVE_TLS_ALPN
    ASSERT_TRUE(cas_ssl_context_set_alpn_protocols(server_context, cat_false, "h2,http/1.1"));
    ASSERT_TRUE(cas_ssl_context_set_alpn_protocols(client_context, cat_true, "http/1.1"));
    ASSERT_TRUE(cas_ssl_context_set_alpn_protocols(client_context, cat_true, "h2,http/1.1"));
#endif
    cat_ssl_t server = {}, client = {};
    ASSERT_EQ(cat_ssl_create(&server, server_context), &server);
    DEFER(cat_ssl_close(&server));
    ASSERT_EQ(cat_ssl_create(&client, client_context), &client);
    DEFER(cat_ssl_close(&client));
    cat_ssl_set_accept_state(&server);
    cat_ssl_set_connect_state(&client);
    bool complete = false;
    for (size_t attempt = 0; attempt < 32; attempt++) {
        cat_ssl_ret_t client_ret = cat_ssl_handshake(&client);
        ASSERT_NE(client_ret, CAT_SSL_RET_ERROR);
        ASSERT_NO_FATAL_FAILURE(transfer_handshake_bytes(&client, &server));
        cat_ssl_ret_t server_ret = cat_ssl_handshake(&server);
        ASSERT_NE(server_ret, CAT_SSL_RET_ERROR);
        ASSERT_NO_FATAL_FAILURE(transfer_handshake_bytes(&server, &client));
        if (client_ret == CAT_SSL_RET_OK && server_ret == CAT_SSL_RET_OK) {
            complete = true;
            break;
        }
    }
    ASSERT_TRUE(complete);
    ASSERT_TRUE(cat_ssl_is_established(&client));
    ASSERT_TRUE(cat_ssl_is_established(&server));
    ASSERT_TRUE(cat_ssl_verify_peer(&client, cat_false));
    ASSERT_TRUE(cat_ssl_check_host(&client, CAT_STRL("TestCA")));
    ASSERT_FALSE(cat_ssl_check_host(&client, CAT_STRL("other.example")));
    ASSERT_FALSE(cat_ssl_check_host(&client, "", 0));
#ifdef CAT_SSL_HAVE_TLS_ALPN
    const unsigned char *selected = nullptr;
    unsigned int selected_length = 0;
    SSL_get0_alpn_selected(client.connection, &selected, &selected_length);
    ASSERT_EQ(selected_length, 2);
    ASSERT_EQ(std::string(reinterpret_cast<const char *>(selected), selected_length), "h2");
#endif

    std::string input = get_random_bytes(CAT_SSL_MAX_PLAIN_LENGTH * 2 + 1);
    input[0] = '\0';
    input[CAT_SSL_MAX_PLAIN_LENGTH] = '\0';
    cat_io_vector_t vectors[2];
    vectors[0].base = input.data();
    vectors[0].length = input.length() / 2;
    vectors[1].base = input.data() + vectors[0].length;
    vectors[1].length = input.length() - vectors[0].length;
    char *encrypted = nullptr;
    size_t encrypted_length = 0;
    ASSERT_TRUE(cat_ssl_encrypt(&client, vectors, CAT_ARRAY_SIZE(vectors), &encrypted, &encrypted_length));
    ASSERT_NE(encrypted, nullptr);
    DEFER(cat_free(encrypted));
    ASSERT_GT(encrypted_length, input.length());
    ASSERT_TRUE(cat_buffer_append(&server.read_buffer, encrypted, encrypted_length));
    std::string output;
    // 多条 TLS record 分批解密，数据中的 NUL 不能被当成字符串终止符。
    while (output.length() < input.length()) {
        char buffer[257];
        size_t length = sizeof(buffer);
        cat_bool_t eof = cat_false;
        ASSERT_TRUE(cat_ssl_decrypt(&server, buffer, &length, &eof));
        ASSERT_FALSE(eof);
        ASSERT_GT(length, 0);
        output.append(buffer, length);
        ASSERT_LE(output.length(), input.length());
    }
    ASSERT_EQ(output, input);
    ASSERT_EQ(server.read_buffer.length, 0);
    ASSERT_EQ(SSL_shutdown(client.connection), 0);
    char shutdown_bytes[CAT_SSL_BUFFER_SIZE];
    int shutdown_length = cat_ssl_read_encrypted_bytes(&client, shutdown_bytes, sizeof(shutdown_bytes));
    ASSERT_GT(shutdown_length, 0);
    ASSERT_TRUE(cat_buffer_append(&server.read_buffer, shutdown_bytes, shutdown_length));
    char buffer[16];
    size_t length = sizeof(buffer);
    cat_bool_t eof = cat_false;
    ASSERT_TRUE(cat_ssl_decrypt(&server, buffer, &length, &eof));
    ASSERT_EQ(length, 0);
    ASSERT_TRUE(eof);

}

TEST(cat_ssl, self_signed_and_expired_peer_certificates)
{
    for (bool expired : {false, true}) {
        SCOPED_TRACE(expired);
        X509KeyCertPairConfig config = {
            {"keyType", "RSA2048"},
            {"CN", "localhost"},
            {"notBeforeOffsetSeconds", -3600},
            {"notAfterOffsetSeconds", expired ? -1800 : 86400},
        };
        auto pair = X509KeyCertPair::create(config);
        ASSERT_NE(pair, nullptr);
        auto paths = pair->exportPEMs();
        ASSERT_NE(paths.cert, nullptr);
        ASSERT_NE(paths.key, nullptr);
        cat_ssl_context_t *server_context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOL_TLSv1_2);
        ASSERT_NE(server_context, nullptr);
        DEFER(cat_ssl_context_close(server_context));
        ASSERT_TRUE(cat_ssl_context_set_certificate(server_context, paths.cert, paths.key));
        cat_ssl_context_t *client_context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOL_TLSv1_2);
        ASSERT_NE(client_context, nullptr);
        DEFER(cat_ssl_context_close(client_context));
        // Let the handshake finish, then check the real verification result explicitly.
        cat_ssl_context_disable_verify_peer(client_context);
        if (expired) {
            ASSERT_TRUE(cat_ssl_context_load_verify_locations(client_context, paths.cert, nullptr));
        }
        cat_ssl_t server = {}, client = {};
        ASSERT_EQ(cat_ssl_create(&server, server_context), &server);
        DEFER(cat_ssl_close(&server));
        ASSERT_EQ(cat_ssl_create(&client, client_context), &client);
        DEFER(cat_ssl_close(&client));
        cat_ssl_set_accept_state(&server);
        cat_ssl_set_connect_state(&client);
        bool established = false;
        for (size_t attempt = 0; attempt < 32; attempt++) {
            cat_ssl_ret_t client_ret = cat_ssl_handshake(&client);
            ASSERT_NE(client_ret, CAT_SSL_RET_ERROR);
            ASSERT_NO_FATAL_FAILURE(transfer_handshake_bytes(&client, &server));
            cat_ssl_ret_t server_ret = cat_ssl_handshake(&server);
            ASSERT_NE(server_ret, CAT_SSL_RET_ERROR);
            ASSERT_NO_FATAL_FAILURE(transfer_handshake_bytes(&server, &client));
            if (client_ret == CAT_SSL_RET_OK && server_ret == CAT_SSL_RET_OK) {
                established = true;
                break;
            }
        }
        ASSERT_TRUE(established);
        ASSERT_EQ(SSL_get_verify_result(client.connection),
            expired ? X509_V_ERR_CERT_HAS_EXPIRED : X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT);
        ASSERT_FALSE(cat_ssl_verify_peer(&client, cat_false));
        ASSERT_EQ(cat_get_last_error_code(), CAT_ECERT);
        ASSERT_EQ(cat_ssl_verify_peer(&client, cat_true), !expired);
        if (expired) {
            ASSERT_EQ(cat_get_last_error_code(), CAT_ECERT);
        }
        ASSERT_TRUE(cat_ssl_check_host(&client, CAT_STRL("localhost")));
    }
}

TEST(cat_socket, crypto_setup_errors_and_timeout)
{
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
    ASSERT_FALSE(cat_socket_enable_crypto(&client, &options));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_TRUE(cat_socket_is_established(&client));
    options.peer_name = "localhost";
    options.ca_file = publicCAPair->exportPEMs().key;
    options.load_ca = [](cat_ssl_context_t *context, cat_socket_crypto_options_t *) -> cat_bool_t {
        return cat_ssl_context_load_verify_locations(context, publicCAPair->exportPEMs().cert, nullptr);
    };
    options.no_ticket = cat_true;
    options.no_compression = cat_true;
    struct {
        bool before;
        bool after;
        cat_bool_t success;
    } callbacks = {false, false, cat_true};
    options.before_handshake_callback_data = &callbacks.before;
    options.before_handshake_callback = [](cat_ssl_t *ssl, void *data) -> cat_bool_t {
        *static_cast<bool *>(data) = true;
        return SSL_set_max_proto_version(ssl->connection, TLS1_2_VERSION) == 1;
    };
    options.after_handshake_callback_data = &callbacks;
    options.after_handshake_callback = [](cat_ssl_t *, cat_bool_t success, void *data) {
        auto *state = static_cast<decltype(callbacks) *>(data);
        state->after = true;
        state->success = success;
    };
    // TCP 对端保持连接但不参与 TLS；握手超时应报告失败并关闭不可恢复的连接。
    ASSERT_FALSE(cat_socket_enable_crypto_ex(&client, &options, 1));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
    ASSERT_TRUE(callbacks.before);
    ASSERT_TRUE(callbacks.after);
    ASSERT_FALSE(callbacks.success);
    ASSERT_FALSE(cat_socket_is_available(&client));
}

TEST(cat_ssl, create_buffer_allocation_failure)
{
    cat_ssl_context_t *context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOLS_DEFAULT);
    ASSERT_NE(context, nullptr);
    DEFER(cat_ssl_context_close(context));
    cat_buffer_allocator_t allocator = cat_buffer_allocator;
    DEFER(cat_buffer_register_allocator(&allocator));
    cat_buffer_allocator_t failing = allocator;
    failing.alloc_function = [](size_t) -> char * { return nullptr; };
    ASSERT_TRUE(cat_buffer_register_allocator(&failing));
    ASSERT_EQ(cat_ssl_create(nullptr, context), nullptr);
    ASSERT_NE(cat_get_last_error_code(), 0);
    ASSERT_TRUE(cat_buffer_register_allocator(&allocator));
    cat_ssl_t ssl = {};
    ASSERT_EQ(cat_ssl_create(&ssl, context), &ssl);
    DEFER(cat_ssl_close(&ssl));
    ASSERT_FALSE(cat_ssl_is_established(&ssl));
}

TEST(cat_ssl, state_changes_and_plaintext_handshake_error)
{
    cat_ssl_context_t *context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOLS_DEFAULT);
    ASSERT_NE(context, nullptr);
    DEFER(cat_ssl_context_close(context));
    cat_ssl_t ssl = {};
    ASSERT_EQ(cat_ssl_create(&ssl, context), &ssl);
    DEFER(cat_ssl_close(&ssl));
    for (unsigned int repeat = 0; repeat < 2; repeat++) {
        cat_ssl_set_connect_state(&ssl);
        cat_ssl_set_connect_state(&ssl);
        ASSERT_EQ(SSL_is_server(ssl.connection), 0);
        cat_ssl_set_accept_state(&ssl);
        cat_ssl_set_accept_state(&ssl);
        ASSERT_EQ(SSL_is_server(ssl.connection), 1);
    }
    const char request[] = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    ASSERT_EQ(cat_ssl_write_encrypted_bytes(&ssl, CAT_STRL(request)), CAT_STRLEN(request));
    ASSERT_EQ(cat_ssl_handshake(&ssl), CAT_SSL_RET_ERROR);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ESSL);
    ASSERT_FALSE(cat_ssl_is_established(&ssl));

}

TEST(cat_ssl, bio_full_buffer_retries)
{
    cat_ssl_context_t *context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOLS_DEFAULT);
    ASSERT_NE(context, nullptr);
    DEFER(cat_ssl_context_close(context));
    cat_ssl_t ssl = {};
    ASSERT_EQ(cat_ssl_create(&ssl, context), &ssl);
    DEFER(cat_ssl_close(&ssl));
    std::string data(CAT_SSL_BUFFER_SIZE * 4, 'x');
    int written = cat_ssl_write_encrypted_bytes(&ssl, data.data(), data.length());
    ASSERT_GT(written, 0);
    ASSERT_LT(static_cast<size_t>(written), data.length());
    ASSERT_EQ(cat_ssl_write_encrypted_bytes(&ssl, data.data() + written, data.length() - written), CAT_RET_NONE);
    ASSERT_FALSE(cat_ssl_is_down(&ssl));
}


TEST(cat_ssl, plaintext_decrypt_error)
{
    cat_ssl_context_t *context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOLS_DEFAULT);
    ASSERT_NE(context, nullptr);
    DEFER(cat_ssl_context_close(context));
    cat_ssl_t ssl = {};
    ASSERT_EQ(cat_ssl_create(&ssl, context), &ssl);
    DEFER(cat_ssl_close(&ssl));
    cat_ssl_set_accept_state(&ssl);
    ASSERT_TRUE(cat_buffer_append(&ssl.read_buffer, CAT_STRL("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")));
    char output[16];
    size_t length = sizeof(output);
    cat_bool_t eof = cat_false;
    ASSERT_FALSE(cat_ssl_decrypt(&ssl, output, &length, &eof));
    ASSERT_EQ(length, 0);
    ASSERT_FALSE(eof);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ESSL);
    ASSERT_TRUE(cat_ssl_is_down(&ssl));
    ASSERT_EQ(cat_ssl_get_shutdown(&ssl), CAT_SSL_SENT_SHUTDOWN | CAT_SSL_RECEIVED_SHUTDOWN);
}

#endif
