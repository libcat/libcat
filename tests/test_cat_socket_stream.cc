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

TEST(cat_socket, partial_stream_read_preserves_received_bytes)
{
    for (bool start_read_first : {false, true}) {
        SCOPED_TRACE(start_read_first);
        cat_socket_t server, sender, receiver;
        ASSERT_EQ(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP4), &server);
        DEFER(cat_socket_close(&server));
        ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
        ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
        ASSERT_EQ(cat_socket_create(&sender, CAT_SOCKET_TYPE_TCP4), &sender);
        DEFER(if (cat_socket_is_available(&sender)) { cat_socket_close(&sender); });
        ASSERT_TRUE(cat_socket_connect_to(&sender, CAT_STRL(TEST_LISTEN_IPV4), cat_socket_get_sock_port(&server)));
        ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_TCP), &receiver);
        DEFER(cat_socket_close(&receiver));
        ASSERT_TRUE(cat_socket_accept(&server, &receiver));
        char buffer[8] = {};
        ASSERT_EQ(cat_socket_recv(&receiver, buffer, 0), -1);
        ASSERT_EQ(cat_get_last_error_code(), CAT_ENOBUFS);
        ASSERT_TRUE(cat_socket_is_established(&receiver));
        wait_group wg;
        ssize_t received = -1;
        cat_errno_t error = 0;
        auto read = [&] {
            received = cat_socket_read(&receiver, buffer, sizeof(buffer));
            error = cat_get_last_error_code();
        };
        DEFER(if (cat_socket_is_available(&sender)) { cat_socket_close(&sender); } wg(TEST_IO_TIMEOUT));
        if (start_read_first) {
            co([&] { wg++; DEFER(wg--); read(); });
            ASSERT_EQ(cat_socket_get_io_state(&receiver), CAT_SOCKET_IO_FLAG_READ);
        }
        ASSERT_TRUE(cat_socket_send(&sender, CAT_STRL("abc")));
        ASSERT_TRUE(cat_socket_close(&sender));
        if (!start_read_first) {
            ASSERT_EQ(cat_poll_one(cat_socket_get_fd(&receiver), POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
            read();
        }
        ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
        // A failed eight-byte read still returns its three bytes, with the reset recorded separately.
        ASSERT_EQ(received, 3);
        ASSERT_EQ(std::string(buffer, received), "abc");
        ASSERT_EQ(error, CAT_ECONNRESET);
        ASSERT_EQ(cat_socket_recv(&receiver, buffer, sizeof(buffer)), 0);
    }
}

TEST(cat_socket, close_cancels_pending_accept)
{
    cat_socket_t server, connection;
    ASSERT_EQ(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP4), &server);
    DEFER(if (cat_socket_is_available(&server)) { cat_socket_close(&server); });
    ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
    ASSERT_EQ(cat_socket_create(&connection, CAT_SOCKET_TYPE_TCP), &connection);
    DEFER(cat_socket_close(&connection));
    wait_group wg;
    bool accepted = true;
    cat_errno_t error = 0;
    co([&] {
        wg++;
        DEFER(wg--);
        accepted = cat_socket_accept(&server, &connection);
        error = cat_get_last_error_code();
    });
    DEFER(if (cat_socket_is_available(&server)) { cat_socket_close(&server); } wg(TEST_IO_TIMEOUT));
    ASSERT_EQ(cat_socket_get_io_state(&server), CAT_SOCKET_IO_FLAG_ACCEPT);
    ASSERT_TRUE(cat_socket_close(&server));
    ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
    ASSERT_FALSE(accepted);
    ASSERT_EQ(error, CAT_ECANCELED);
    ASSERT_FALSE(cat_socket_is_available(&server));
    ASSERT_TRUE(cat_socket_is_available(&connection));
    ASSERT_FALSE(cat_socket_is_established(&connection));
}

TEST(cat_socket, failed_second_listener_can_recreate)
{
    cat_socket_t first, second;
    ASSERT_EQ(cat_socket_create(&first, CAT_SOCKET_TYPE_TCP4), &first);
    DEFER(if (cat_socket_is_available(&first)) { cat_socket_close(&first); });
    const cat_sockaddr_info_t *unbound = cat_socket_getsockname_fast(&first);
    ASSERT_NE(unbound, nullptr);
    ASSERT_EQ(cat_socket_get_sock_port(&first), 0);
    ASSERT_TRUE(cat_socket_bind_to(&first, CAT_STRL(TEST_LISTEN_IPV4), 0));
    int port = cat_socket_get_sock_port(&first);
    ASSERT_GT(port, 0);
    ASSERT_TRUE(cat_socket_listen(&first, TEST_SERVER_BACKLOG));
    ASSERT_EQ(cat_socket_create(&second, CAT_SOCKET_TYPE_TCP4), &second);
    DEFER(cat_socket_close(&second));
    ASSERT_TRUE(cat_socket_bind_to(&second, CAT_STRL(TEST_LISTEN_IPV4), port));
    ASSERT_FALSE(cat_socket_listen(&second, TEST_SERVER_BACKLOG));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EADDRINUSE);
    ASSERT_TRUE(cat_socket_is_available(&second));
    // libuv defers the bind error until listen; recreation discards that failed handle.
    ASSERT_EQ(cat_socket_get_sock_port(&second), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EADDRINUSE);
    ASSERT_TRUE(cat_socket_close(&first));
    ASSERT_EQ(cat_socket_recreate(&second, CAT_SOCKET_TYPE_TCP4), &second);
    ASSERT_TRUE(cat_coroutine_wait_all());
    ASSERT_TRUE(cat_socket_bind_to(&second, CAT_STRL(TEST_LISTEN_IPV4), port));
    ASSERT_TRUE(cat_socket_listen(&second, TEST_SERVER_BACKLOG));
}

#ifdef CAT_OS_UNIX_LIKE
TEST(cat_socket, blocked_stream_write_timeout_and_close)
{
    for (bool close_while_writing : {false, true}) {
        SCOPED_TRACE(close_while_writing);
        cat_socket_t server, sender, receiver;
        ASSERT_EQ(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP4), &server);
        DEFER(cat_socket_close(&server));
        ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
        ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
        ASSERT_EQ(cat_socket_create(&sender, CAT_SOCKET_TYPE_TCP4), &sender);
        DEFER(if (cat_socket_is_available(&sender)) { cat_socket_close(&sender); });
        ASSERT_TRUE(cat_socket_connect_to(&sender, CAT_STRL(TEST_LISTEN_IPV4), cat_socket_get_sock_port(&server)));
        ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_TCP), &receiver);
        DEFER(cat_socket_close(&receiver));
        ASSERT_TRUE(cat_socket_accept(&server, &receiver));
        ASSERT_GT(cat_socket_set_send_buffer_size(&sender, 4096), 0);
        ASSERT_GT(cat_socket_set_recv_buffer_size(&receiver, 4096), 0);
        size_t capacity = static_cast<size_t>(cat_socket_get_send_buffer_size(&sender)) +
            static_cast<size_t>(cat_socket_get_recv_buffer_size(&receiver));
        const std::string data(capacity * 64, 'w');
        wait_group wg;
        bool sent = true;
        cat_errno_t error = 0;
        co([&] {
            wg++;
            DEFER(wg--);
            sent = cat_socket_send_ex(&sender, data.data(), data.size(), close_while_writing ? TEST_IO_TIMEOUT : 10);
            error = cat_get_last_error_code();
        });
        // Unblock and join before releasing the buffer captured by the writer.
        DEFER(if (cat_socket_is_available(&sender)) { cat_socket_close(&sender); } wg(TEST_IO_TIMEOUT));
        ASSERT_EQ(cat_socket_get_io_state(&sender), CAT_SOCKET_IO_FLAG_WRITE);
        if (close_while_writing) {
            ASSERT_TRUE(cat_socket_close(&sender));
        }
        ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
        ASSERT_FALSE(sent);
        ASSERT_EQ(error, close_while_writing ? CAT_ECANCELED : CAT_ETIMEDOUT);
        ASSERT_FALSE(cat_socket_is_available(&sender));
        std::string received;
        char buffer[4096];
        while (true) {
            ssize_t length = cat_socket_recv_ex(&receiver, buffer, sizeof(buffer), TEST_IO_TIMEOUT);
            ASSERT_GE(length, 0);
            if (length == 0) {
                break;
            }
            received.append(buffer, length);
        }
        ASSERT_FALSE(received.empty());
        ASSERT_LT(received.size(), data.size());
        ASSERT_EQ(received, data.substr(0, received.size()));
    }
}
#endif
