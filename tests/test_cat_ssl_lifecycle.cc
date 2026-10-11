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
TEST(cat_ssl, owned_metadata_is_released_once_on_close)
{
    struct owned_data {
        unsigned int *cleanup_count;
    };
    unsigned int cleaned = 0;
    const auto cleanup = [](void *data) {
        owned_data *owned = static_cast<owned_data *>(data);
        (*owned->cleanup_count)++;
        delete owned;
    };
    cat_ssl_context_t *context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOLS_DEFAULT);
    ASSERT_NE(context, nullptr);
    DEFER(cat_ssl_context_close(context));
    for (bool allocated : {false, true}) {
        cat_ssl_t storage;
        cat_ssl_t *ssl = cat_ssl_create(allocated ? nullptr : &storage, context);
        ASSERT_NE(ssl, nullptr);
        DEFER(cat_ssl_close(ssl));
        auto data = std::make_unique<owned_data>();
        data->cleanup_count = &cleaned;
        ASSERT_FALSE(cat_ssl_set_owned_data(ssl, data.get(), nullptr));
        ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
        ASSERT_TRUE(cat_ssl_set_owned_data(ssl, data.get(), cleanup));
        data.release();
        auto duplicate = std::make_unique<owned_data>();
        duplicate->cleanup_count = &cleaned;
        ASSERT_FALSE(cat_ssl_set_owned_data(ssl, duplicate.get(), cleanup));
        ASSERT_EQ(cat_get_last_error_code(), CAT_EALREADY);
        ASSERT_EQ(cleaned, allocated ? 1U : 0U);
    }
    ASSERT_EQ(cleaned, 2);
}

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
TEST(cat_socket, crypto_peer_fingerprint_verification)
{
    auto paths = publicCAPair->exportPEMs();
    BIO *bio = BIO_new_file(paths.cert, "r");
    ASSERT_NE(bio, nullptr);
    DEFER(BIO_free(bio));
    X509 *certificate = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    ASSERT_NE(certificate, nullptr);
    DEFER(X509_free(certificate));
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest = {};
    unsigned int digest_length = 0;
    ASSERT_EQ(X509_digest(certificate, EVP_sha256(), digest.data(), &digest_length), 1);
    ASSERT_GT(digest_length, 0);
    for (unsigned int mode = 0; mode < 4; mode++) {
        SCOPED_TRACE(mode);
        std::array<unsigned char, EVP_MAX_MD_SIZE> expected = digest;
        if (mode == 1) {
            expected[0] ^= 1;
        }
        cat_ssl_peer_fingerprint_t fingerprints[] = {
            {mode == 2 ? "unknown-digest" : "sha256", expected.data()},
            {nullptr, nullptr},
        };
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
        server_options.verify_peer = cat_false;
        server_options.certificate = paths.cert;
        server_options.certificate_key = paths.key;
        cat_socket_crypto_options_init(&client_options, cat_true);
        client_options.ca_file = paths.cert;
        client_options.peer_name = "TestCA";
        client_options.peer_fingerprints = mode == 3 ? &fingerprints[1] : fingerprints;
        struct handshake_result {
            unsigned int calls;
            cat_bool_t success;
        } client_result = {0, cat_false}, server_result = {0, cat_false};
        auto after_handshake = [](cat_ssl_t *ssl, cat_bool_t success, void *data) {
            auto *result = static_cast<handshake_result *>(data);
            result->calls++;
            result->success = success;
            if (success) {
                EXPECT_TRUE(cat_ssl_is_established(ssl));
            }
        };
        client_options.after_handshake_callback = after_handshake;
        client_options.after_handshake_callback_data = &client_result;
        server_options.after_handshake_callback = after_handshake;
        server_options.after_handshake_callback_data = &server_result;
        cat_bool_t server_ready = cat_false;
        wait_group wg;
        DEFER(if (cat_socket_is_available(&connection)) { cat_socket_close(&connection); });
        DEFER(if (cat_socket_is_available(&client)) { cat_socket_close(&client); });
        co([&] {
            wg++;
            DEFER(wg--);
            server_ready = cat_socket_enable_crypto(&connection, &server_options);
        });
        cat_bool_t ready = cat_socket_enable_crypto(&client, &client_options);
        cat_errno_t error = cat_get_last_error_code();
        ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
        ASSERT_EQ(ready, mode == 0 || mode == 3);
        ASSERT_EQ(client_result.calls, 1);
        ASSERT_EQ(client_result.success, ready);
        ASSERT_EQ(server_result.calls, 1);
        ASSERT_EQ(server_result.success, server_ready);
        if (!ready) {
            ASSERT_NE(error, 0);
            ASSERT_FALSE(cat_socket_is_available(&client));
            continue;
        }
        ASSERT_TRUE(server_ready);
        ASSERT_TRUE(cat_socket_is_encrypted(&client));
        ASSERT_FALSE(cat_socket_enable_crypto(&client, &client_options));
        ASSERT_EQ(cat_get_last_error_code(), CAT_EISCONN);
        ASSERT_EQ(client_result.calls, 1);
        ASSERT_TRUE(cat_socket_is_established(&client));
        ASSERT_TRUE(cat_socket_send(&client, CAT_STRL("pinned")));
        char buffer[CAT_STRLEN("pinned")];
        ASSERT_EQ(cat_socket_read(&connection, buffer, sizeof(buffer)), sizeof(buffer));
        ASSERT_EQ(std::string(buffer, sizeof(buffer)), "pinned");
    }
}
TEST(cat_socket, crypto_try_send_handles_queued_completion_and_close)
{
    for (bool close_pending : {false, true}) {
        SCOPED_TRACE(close_pending);
        auto paths = publicCAPair->exportPEMs();
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
        server_options.verify_peer = cat_false;
        server_options.certificate = paths.cert;
        server_options.certificate_key = paths.key;
        cat_socket_crypto_options_init(&client_options, cat_true);
        client_options.ca_file = paths.cert;
        client_options.peer_name = "TestCA";
        cat_bool_t server_ready = cat_false;
        cat_coroutine_t *reader = nullptr;
        cat_ret_t read_ready = CAT_RET_NONE;
        bool reader_done = true;
        wait_group wg;
        DEFER(if (cat_socket_is_available(&connection)) { cat_socket_close(&connection); });
        DEFER(if (cat_socket_is_available(&client)) { cat_socket_close(&client); });
        DEFER(if (reader != nullptr && !reader_done) { cat_coroutine_resume(reader, nullptr, nullptr); });
        co([&] {
            wg++;
            DEFER(wg--);
            server_ready = cat_socket_enable_crypto(&connection, &server_options);
        });
        ASSERT_TRUE(cat_socket_enable_crypto(&client, &client_options));
        ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
        ASSERT_TRUE(server_ready);
        if (!close_pending) {
            char buffer[8];
            ASSERT_EQ(cat_socket_try_recv(&client, buffer, sizeof(buffer)), CAT_EAGAIN);
            reader_done = false;
            reader = co([&] {
                wg++;
                DEFER(wg--);
                read_ready = cat_poll_one(cat_socket_get_fd(&client), POLLIN, nullptr, TEST_IO_TIMEOUT);
                reader_done = true;
            });
            ASSERT_FALSE(reader_done);
        }
        ASSERT_GT(cat_socket_set_send_buffer_size(&client, TEST_BUFFER_SIZE_STD), 0);
        ASSERT_GT(cat_socket_set_recv_buffer_size(&connection, TEST_BUFFER_SIZE_STD), 0);
        int send_size = cat_socket_get_send_buffer_size(&client);
        int recv_size = cat_socket_get_recv_buffer_size(&connection);
        ASSERT_GT(send_size, 0);
        ASSERT_GT(recv_size, 0);
        // Exceed both kernel buffers without yielding; the peer has not read yet.
        const std::string chunk = get_random_bytes(2 * (static_cast<size_t>(send_size) + recv_size));
        std::string payload;
        const cat_msec_t deadline = cat_time_msec2() + TEST_IO_TIMEOUT;
        do {
            ASSERT_LT(cat_time_msec2(), deadline);
            ASSERT_EQ(cat_socket_try_send(&client, chunk.data(), chunk.length()), chunk.length());
            payload += chunk;
        } while (client.internal->ssl->write_buffer.length == 0);
        cat_set_last_error_code(CAT_EBUSY);
        ASSERT_EQ(cat_socket_try_send(&client, CAT_STRL("next")), CAT_EAGAIN);
        ASSERT_EQ(cat_get_last_error_code(), CAT_EBUSY);
        if (close_pending) {
            ASSERT_TRUE(cat_socket_close(&client));
            continue;
        }
        std::string received(payload.length(), '\0');
        ASSERT_EQ(cat_socket_read(&connection, received.data(), received.length()), received.length());
        ASSERT_EQ(received, payload);
        if (!close_pending) {
            ASSERT_TRUE(cat_socket_send(&connection, CAT_STRL("reply")));
            ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
            ASSERT_EQ(read_ready, CAT_RET_OK);
            char reply[CAT_STRLEN("reply")];
            ASSERT_EQ(cat_socket_read(&client, reply, sizeof(reply)), sizeof(reply));
            ASSERT_EQ(std::string(reply, sizeof(reply)), "reply");
        }
        // Waiting for write readiness runs the real asynchronous completion callback.
        ASSERT_EQ(cat_poll_one(cat_socket_get_fd(&client), POLLOUT, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
        ASSERT_EQ(client.internal->ssl->write_buffer.length, 0);
        ASSERT_EQ(cat_socket_try_send(&client, CAT_STRL("next")), CAT_STRLEN("next"));
        char next[CAT_STRLEN("next")];
        ASSERT_EQ(cat_socket_read(&connection, next, sizeof(next)), sizeof(next));
        ASSERT_EQ(std::string(next, sizeof(next)), "next");
    }
}
TEST(cat_socket, crypto_send_file_errors_then_slice_transfer)
{
    std::string path = get_random_path();
    const std::string content = "file-slice-data";
    ASSERT_EQ(cat_fs_put_contents(path.c_str(), content.data(), content.length()), content.length());
    DEFER(cat_fs_unlink(path.c_str()));
    std::string missing = get_random_path();
    ASSERT_FALSE(file_exists(missing.c_str()));
    auto paths = publicCAPair->exportPEMs();
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
    server_options.verify_peer = cat_false;
    server_options.certificate = paths.cert;
    server_options.certificate_key = paths.key;
    cat_socket_crypto_options_init(&client_options, cat_true);
    client_options.ca_file = paths.cert;
    client_options.peer_name = "TestCA";
    cat_bool_t server_ready = cat_false;
    wait_group wg;
    DEFER(if (cat_socket_is_available(&connection)) { cat_socket_close(&connection); });
    DEFER(if (cat_socket_is_available(&client)) { cat_socket_close(&client); });
    co([&] {
        wg++;
        DEFER(wg--);
        server_ready = cat_socket_enable_crypto(&connection, &server_options);
    });
    ASSERT_TRUE(cat_socket_enable_crypto(&client, &client_options));
    ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
    ASSERT_TRUE(server_ready);
    ASSERT_EQ(cat_socket_send_file(&client, missing.c_str(), 0, 0), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOENT);
    for (int64_t offset : {-1, -2}) {
        ASSERT_EQ(cat_socket_send_file(&client, path.c_str(), offset, 0), -1);
        ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    }
#ifdef CAT_OS_UNIX_LIKE
    ASSERT_EQ(cat_socket_send_file(&client, TEST_TMP_PATH, 0, 0), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EISDIR);
#endif
    ASSERT_TRUE(cat_socket_is_established(&client));
    ASSERT_EQ(cat_socket_send_file_ex(&client, path.c_str(), 5, CAT_STRLEN("slice"), TEST_IO_TIMEOUT), CAT_STRLEN("slice"));
    char buffer[CAT_STRLEN("slice")];
    ASSERT_EQ(cat_socket_read(&connection, buffer, sizeof(buffer)), sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "slice");
    ASSERT_EQ(cat_socket_send_file(&client, path.c_str(), content.length(), 0), 0);
    ASSERT_TRUE(cat_socket_send(&client, CAT_STRL("after-eof")));
    char tail[CAT_STRLEN("after-eof")];
    ASSERT_EQ(cat_socket_read(&connection, tail, sizeof(tail)), sizeof(tail));
    ASSERT_EQ(std::string(tail, sizeof(tail)), "after-eof");
}
TEST(cat_socket, crypto_write_after_tls_shutdown_closes_connection)
{
    for (bool nonblocking : {false, true}) {
        SCOPED_TRACE(nonblocking);
        auto paths = publicCAPair->exportPEMs();
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
        server_options.verify_peer = cat_false;
        server_options.certificate = paths.cert;
        server_options.certificate_key = paths.key;
        cat_socket_crypto_options_init(&client_options, cat_true);
        client_options.ca_file = paths.cert;
        client_options.peer_name = "TestCA";
        cat_ssl_t *client_ssl = nullptr;
        client_options.before_handshake_callback_data = &client_ssl;
        client_options.before_handshake_callback = [](cat_ssl_t *ssl, void *data) -> cat_bool_t {
            *static_cast<cat_ssl_t **>(data) = ssl;
            return cat_true;
        };
        cat_bool_t server_ready = cat_false;
        wait_group wg;
        DEFER(if (cat_socket_is_available(&connection)) { cat_socket_close(&connection); });
        DEFER(if (cat_socket_is_available(&client)) { cat_socket_close(&client); });
        co([&] {
            wg++;
            DEFER(wg--);
            server_ready = cat_socket_enable_crypto(&connection, &server_options);
        });
        ASSERT_TRUE(cat_socket_enable_crypto(&client, &client_options));
        ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
        ASSERT_TRUE(server_ready);
        ASSERT_NE(client_ssl, nullptr);
        cat_ssl_set_shutdown(client_ssl, CAT_SSL_SENT_SHUTDOWN);
        cat_clear_last_error();
        if (nonblocking) {
            ASSERT_EQ(cat_socket_try_send(&client, CAT_STRL("after-shutdown")), CAT_ESSL);
            ASSERT_EQ(cat_get_last_error_code(), 0);
        } else {
            ASSERT_FALSE(cat_socket_send(&client, CAT_STRL("after-shutdown")));
            ASSERT_EQ(cat_get_last_error_code(), CAT_ESSL);
        }
        ASSERT_FALSE(cat_socket_is_available(&client));
        ASSERT_TRUE(cat_socket_close(&client));
    }
}
#endif
