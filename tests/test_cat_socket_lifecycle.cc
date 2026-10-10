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

TEST(cat_socket, recreate_storage)
{
    cat_socket_t socket;
    ASSERT_EQ(cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP4), &socket);
    DEFER(if (cat_socket_is_available(&socket)) { cat_socket_close(&socket); });
    cat_socket_id_t id = cat_socket_get_id(&socket);
    ASSERT_EQ(cat_socket_recreate(&socket, CAT_SOCKET_TYPE_UDP4), &socket);
    ASSERT_NE(cat_socket_get_id(&socket), id);
    ASSERT_EQ(cat_socket_get_type(&socket), CAT_SOCKET_TYPE_UDP4);
    ASSERT_TRUE(cat_socket_is_open(&socket));
    ASSERT_FALSE(cat_socket_is_established(&socket));
    ASSERT_TRUE(cat_socket_close(&socket));
    ASSERT_EQ(cat_socket_recreate(&socket, CAT_SOCKET_TYPE_TCP4), &socket);
    ASSERT_TRUE(cat_socket_is_available(&socket));
    ASSERT_EQ(cat_socket_get_af(&socket), AF_INET);
}

TEST(cat_socket, recreate_allocated)
{
    cat_socket_t *socket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_TCP4);
    ASSERT_NE(socket, nullptr);
    // 堆分配对象重建时旧对象已释放，后续只能使用返回的新对象。
    socket = cat_socket_recreate(socket, CAT_SOCKET_TYPE_UDP4);
    ASSERT_NE(socket, nullptr);
    DEFER(cat_socket_close(socket));
    ASSERT_EQ(cat_socket_get_type(socket), CAT_SOCKET_TYPE_UDP4);
    ASSERT_TRUE(cat_socket_bind_to(socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_GT(cat_socket_get_sock_port(socket), 0);
}

TEST(cat_socket, rejected_creation_preserves_caller_storage)
{
    const cat_socket_type_t types[] = {
        CAT_SOCKET_TYPE_ANY,
        CAT_SOCKET_TYPE_TTY,
        static_cast<cat_socket_type_t>(CAT_SOCKET_TYPE_FLAG_LOCAL),
#ifdef CAT_OS_UNIX_LIKE
        static_cast<cat_socket_type_t>(CAT_SOCKET_TYPE_UDG | CAT_SOCKET_TYPE_FLAG_IPC),
#endif
    };
    for (cat_socket_type_t type : types) {
        SCOPED_TRACE(type);
        cat_socket_t socket;
        cat_socket_init(&socket);
        ASSERT_EQ(cat_socket_create(&socket, type), nullptr);
        ASSERT_NE(cat_get_last_error_code(), 0);
        ASSERT_FALSE(cat_socket_is_available(&socket));
        ASSERT_EQ(cat_socket_create(nullptr, type), nullptr);
        ASSERT_NE(cat_get_last_error_code(), 0);
        ASSERT_EQ(cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP4), &socket);
        ASSERT_TRUE(cat_socket_close(&socket));
    }
}

TEST(cat_socket, open_invalid_source)
{
    cat_socket_t destination, source;
    ASSERT_EQ(cat_socket_create(&destination, CAT_SOCKET_TYPE_TCP4), &destination);
    DEFER(cat_socket_close(&destination));
    cat_socket_init(&source);
    ASSERT_FALSE(cat_socket_open_socket(&destination, &source));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_TRUE(cat_socket_is_available(&destination));
    ASSERT_FALSE(cat_socket_open_os_socket(&destination, CAT_OS_INVALID_SOCKET));
    ASSERT_NE(cat_get_last_error_code(), 0);
    ASSERT_FALSE(cat_socket_open_os_fd(&destination, CAT_OS_INVALID_FD));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    cat_socket_t ipc;
    ASSERT_EQ(cat_socket_create(&ipc, CAT_SOCKET_TYPE_IPCC), &ipc);
    DEFER(cat_socket_close(&ipc));
    ASSERT_FALSE(cat_socket_open_os_socket(&ipc, CAT_OS_INVALID_SOCKET));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    char buffer[4] = {'k', 'e', 'p', 't'};
    cat_set_last_error_code(CAT_EBUSY);
    ASSERT_EQ(cat_socket_try_recv(&ipc, buffer, sizeof(buffer)), CAT_ENOTCONN);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBUSY);
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "kept");
    ASSERT_TRUE(cat_socket_is_available(&ipc));
}

TEST(cat_socket, timeout_options)
{
    const struct {
        cat_timeout_t (*get_global)(void);
        void (*set_global)(cat_timeout_t);
        cat_timeout_t (*get)(const cat_socket_t *);
        cat_bool_t (*set)(cat_socket_t *, cat_timeout_t);
    } options[] = {
        {cat_socket_get_global_dns_timeout, cat_socket_set_global_dns_timeout, cat_socket_get_dns_timeout, cat_socket_set_dns_timeout},
        {cat_socket_get_global_accept_timeout, cat_socket_set_global_accept_timeout, cat_socket_get_accept_timeout, cat_socket_set_accept_timeout},
        {cat_socket_get_global_connect_timeout, cat_socket_set_global_connect_timeout, cat_socket_get_connect_timeout, cat_socket_set_connect_timeout},
        {cat_socket_get_global_handshake_timeout, cat_socket_set_global_handshake_timeout, cat_socket_get_handshake_timeout, cat_socket_set_handshake_timeout},
        {cat_socket_get_global_read_timeout, cat_socket_set_global_read_timeout, cat_socket_get_read_timeout, cat_socket_set_read_timeout},
        {cat_socket_get_global_write_timeout, cat_socket_set_global_write_timeout, cat_socket_get_write_timeout, cat_socket_set_write_timeout},
    };
    cat_socket_t socket;
    ASSERT_EQ(cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP4), &socket);
    DEFER(cat_socket_close(&socket));
    for (const auto &option : options) {
        cat_timeout_t original = option.get_global();
        DEFER(option.set_global(original));
        option.set_global(73);
        ASSERT_EQ(option.get(&socket), 73);
        ASSERT_TRUE(option.set(&socket, 31));
        option.set_global(91);
        ASSERT_EQ(option.get(&socket), 31);
        ASSERT_TRUE(option.set(&socket, CAT_SOCKET_TIMEOUT_STORAGE_DEFAULT));
        ASSERT_EQ(option.get(&socket), 91);
        ASSERT_TRUE(option.set(&socket, static_cast<cat_timeout_t>(INT32_MAX) + 1));
        ASSERT_EQ(option.get(&socket), CAT_TIMEOUT_FOREVER);
    }
    ASSERT_TRUE(cat_socket_set_accept_timeout(&socket, 43));
    ASSERT_TRUE(cat_socket_set_timeout(&socket, 29));
    ASSERT_EQ(cat_socket_get_dns_timeout(&socket), 29);
    ASSERT_EQ(cat_socket_get_read_timeout(&socket), 29);
    ASSERT_EQ(cat_socket_get_write_timeout(&socket), 29);
    // 整体超时配置不改变 accept，监听等待遵守单独的 accept 设置。
    ASSERT_EQ(cat_socket_get_accept_timeout(&socket), 43);
    ASSERT_TRUE(cat_socket_close(&socket));
    for (const auto &option : options) {
        ASSERT_EQ(option.get(&socket), CAT_TIMEOUT_INVALID);
        ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
        ASSERT_FALSE(option.set(&socket, 1));
        ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    }
    ASSERT_FALSE(cat_socket_set_timeout(&socket, 1));
}

TEST(cat_socket, datagram_write_vector)
{
    cat_socket_t receiver, sender;
    ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_UDP4), &receiver);
    DEFER(cat_socket_close(&receiver));
    ASSERT_TRUE(cat_socket_bind_to(&receiver, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_EQ(cat_socket_create(&sender, CAT_SOCKET_TYPE_UDP4), &sender);
    DEFER(cat_socket_close(&sender));
    const cat_socket_write_vector_t vector[] = {
        cat_socket_write_vector_init("ab", 2),
        cat_socket_write_vector_init("", 0),
        cat_socket_write_vector_init("cd", 2),
    };
    ASSERT_EQ(cat_socket_write_vector_length(vector, CAT_ARRAY_SIZE(vector)), 4);
    ASSERT_EQ(cat_socket_write_vector_length(vector, 0), 0);
    const cat_sockaddr_info_t *address = cat_socket_getsockname_fast(&receiver);
    ASSERT_NE(address, nullptr);
    ASSERT_TRUE(cat_socket_writeto(&sender, vector, CAT_ARRAY_SIZE(vector), &address->address.common, address->length));
    char buffer[4];
    ASSERT_EQ(cat_socket_recv(&receiver, buffer, sizeof(buffer)), sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "abcd");
}

TEST(cat_socket, stream_write_vector)
{
    cat_socket_t server, client, connection;
    ASSERT_EQ(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP4), &server);
    DEFER(cat_socket_close(&server));
    ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
    ASSERT_EQ(cat_socket_create(&client, CAT_SOCKET_TYPE_TCP4), &client);
    DEFER(cat_socket_close(&client));
    ASSERT_TRUE(cat_socket_connect_to(&client, CAT_STRL(TEST_LISTEN_IPV4), cat_socket_get_sock_port(&server)));
    ASSERT_EQ(cat_socket_create(&connection, CAT_SOCKET_TYPE_TCP), &connection);
    DEFER(cat_socket_close(&connection));
    ASSERT_TRUE(cat_socket_accept(&server, &connection));
    const cat_socket_write_vector_t vector[] = {
        cat_socket_write_vector_init("a\0", 2),
        cat_socket_write_vector_init("bc", 2),
    };
    for (cat_bool_t nonblocking : {cat_false, cat_true}) {
        if (nonblocking) {
            ASSERT_EQ(cat_socket_try_write(&client, vector, CAT_ARRAY_SIZE(vector)), 4);
        } else {
            ASSERT_TRUE(cat_socket_write(&client, vector, CAT_ARRAY_SIZE(vector)));
        }
        char buffer[4];
        ASSERT_EQ(cat_socket_read(&connection, buffer, sizeof(buffer)), sizeof(buffer));
        ASSERT_EQ(std::string(buffer, sizeof(buffer)), std::string("a\0bc", 4));
    }
}

TEST(cat_socket, raw_address_bind_connect_and_names)
{
    for (bool explicit_options : {false, true}) {
        SCOPED_TRACE(explicit_options);
        cat_socket_t server, client, connection;
        ASSERT_EQ(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP4), &server);
        DEFER(cat_socket_close(&server));
        cat_sockaddr_info_t address = {};
        address.address.common.sa_family = AF_INET;
        address.length = sizeof(address.address);
        ASSERT_TRUE(cat_sockaddr_getbyname(&address.address.common, &address.length, CAT_STRL(TEST_LISTEN_IPV4), 0));
        if (explicit_options) {
            ASSERT_TRUE(cat_socket_bind_ex(&server, &address.address.common, address.length, CAT_SOCKET_BIND_FLAG_REUSEADDR));
        } else {
            ASSERT_TRUE(cat_socket_bind(&server, &address.address.common, address.length));
        }
        ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
        address.length = sizeof(address.address);
        ASSERT_TRUE(cat_socket_getsockname(&server, &address.address.common, &address.length));
        ASSERT_GT(cat_sockaddr_get_port(&address.address.common), 0);
        ASSERT_EQ(cat_socket_create(&client, CAT_SOCKET_TYPE_TCP4), &client);
        DEFER(cat_socket_close(&client));
        if (explicit_options) {
            ASSERT_TRUE(cat_socket_connect_ex(&client, &address.address.common, address.length, TEST_IO_TIMEOUT));
        } else {
            ASSERT_TRUE(cat_socket_connect(&client, &address.address.common, address.length));
        }
        ASSERT_EQ(cat_socket_create(&connection, CAT_SOCKET_TYPE_TCP), &connection);
        DEFER(cat_socket_close(&connection));
        ASSERT_TRUE(cat_socket_accept(&server, &connection));
        cat_sockaddr_info_t peer;
        peer.length = sizeof(peer.address);
        ASSERT_TRUE(cat_socket_getpeername(&client, &peer.address.common, &peer.length));
        ASSERT_EQ(cat_sockaddr_get_port(&peer.address.common), cat_socket_get_sock_port(&server));
        const cat_sockaddr_info_t *cached = cat_socket_getsockname_fast(&client);
        ASSERT_NE(cached, nullptr);
        peer.length = sizeof(peer.address);
        ASSERT_TRUE(cat_socket_getsockname(&client, &peer.address.common, &peer.length));
        ASSERT_EQ(peer.length, cached->length);
        ASSERT_EQ(cat_sockaddr_get_port(&peer.address.common), cat_sockaddr_get_port(&cached->address.common));
        ASSERT_TRUE(cat_socket_send(&client, CAT_STRL("raw-address")));
        char buffer[CAT_STRLEN("raw-address")];
        ASSERT_EQ(cat_socket_read(&connection, buffer, sizeof(buffer)), sizeof(buffer));
        ASSERT_EQ(std::string(buffer, sizeof(buffer)), "raw-address");
    }
}

TEST(cat_socket, raw_address_validation_then_datagram_reuse)
{
    cat_socket_t receiver, sender;
    ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_UDP4), &receiver);
    DEFER(cat_socket_close(&receiver));
    ASSERT_EQ(cat_socket_create(&sender, CAT_SOCKET_TYPE_UDP4), &sender);
    DEFER(cat_socket_close(&sender));
    cat_sockaddr_info_t address = {};
    address.address.common.sa_family = AF_INET;
    address.length = sizeof(address.address);
    ASSERT_TRUE(cat_sockaddr_getbyname(&address.address.common, &address.length, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_FALSE(cat_socket_bind(&receiver, &address.address.common, CAT_SOCKADDR_HEADER_LENGTH));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_TRUE(cat_socket_bind(&receiver, &address.address.common, address.length));
    address.length = sizeof(address.address);
    ASSERT_TRUE(cat_socket_getsockname(&receiver, &address.address.common, &address.length));
    ASSERT_TRUE(cat_socket_bind_to(&sender, CAT_STRL(TEST_LISTEN_IPV4), 0));
    cat_set_last_error_code(CAT_EBUSY);
    ASSERT_EQ(cat_socket_try_sendto(&sender, CAT_STRL("bad"), &address.address.common, CAT_SOCKADDR_HEADER_LENGTH), CAT_EINVAL);
    const cat_socket_write_vector_t vector = cat_socket_write_vector_init("bad", 3);
    ASSERT_EQ(cat_socket_try_writeto(&sender, &vector, 1, &address.address.common, CAT_SOCKADDR_HEADER_LENGTH), CAT_EINVAL);
    cat_sockaddr_info_t invalid = address;
    invalid.address.common.sa_family = AF_UNSPEC;
    ASSERT_EQ(cat_socket_try_sendto(&sender, CAT_STRL("bad"), &invalid.address.common, invalid.length), CAT_EAFNOSUPPORT);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBUSY);
    ASSERT_EQ(cat_socket_try_sendto(&sender, CAT_STRL("valid"), &address.address.common, address.length), CAT_STRLEN("valid"));
    char buffer[CAT_STRLEN("valid")];
    ASSERT_EQ(cat_socket_recv_ex(&receiver, buffer, sizeof(buffer), TEST_IO_TIMEOUT), sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "valid");
    ASSERT_TRUE(cat_socket_is_available(&receiver));
    ASSERT_TRUE(cat_socket_is_available(&sender));
}

TEST(cat_socket, accept_requires_lazy_connection)
{
    cat_socket_t server, opened, unavailable, wrong_type;
    ASSERT_EQ(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP4), &server);
    DEFER(cat_socket_close(&server));
    ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
    ASSERT_EQ(cat_socket_create(&opened, CAT_SOCKET_TYPE_TCP4), &opened);
    DEFER(cat_socket_close(&opened));
    ASSERT_FALSE(cat_socket_accept(&server, &opened));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);
    cat_socket_init(&unavailable);
    ASSERT_FALSE(cat_socket_accept(&server, &unavailable));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_EQ(cat_socket_create(&wrong_type, CAT_SOCKET_TYPE_UDP), &wrong_type);
    DEFER(cat_socket_close(&wrong_type));
    ASSERT_FALSE(cat_socket_accept(&server, &wrong_type));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

TEST(cat_socket, try_connect_localhost)
{
    cat_socket_t server;
    ASSERT_EQ(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP4), &server);
    DEFER(cat_socket_close(&server));
    ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
    for (cat_bool_t by_name : {cat_false, cat_true}) {
        cat_socket_t client, connection;
        ASSERT_EQ(cat_socket_create(&client, CAT_SOCKET_TYPE_TCP4), &client);
        DEFER(cat_socket_close(&client));
        if (by_name) {
            ASSERT_TRUE(cat_socket_try_connect_to(&client, CAT_STRL("localhost"), cat_socket_get_sock_port(&server)));
        } else {
            const cat_sockaddr_info_t *address = cat_socket_getsockname_fast(&server);
            ASSERT_NE(address, nullptr);
            ASSERT_TRUE(cat_socket_try_connect(&client, &address->address.common, address->length));
        }
        ASSERT_EQ(cat_poll_one(cat_socket_get_fd(&client), POLLOUT, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
        ASSERT_EQ(cat_socket_get_connection_error(&client), 0);
        ASSERT_EQ(cat_socket_create(&connection, CAT_SOCKET_TYPE_TCP), &connection);
        DEFER(cat_socket_close(&connection));
        ASSERT_TRUE(cat_socket_accept(&server, &connection));
        ASSERT_TRUE(cat_socket_send(&client, CAT_STRL("connected")));
        char buffer[CAT_STRLEN("connected")];
        ASSERT_EQ(cat_socket_read(&connection, buffer, sizeof(buffer)), sizeof(buffer));
        ASSERT_EQ(std::string(buffer, sizeof(buffer)), "connected");
    }
}

TEST(cat_socket, closed_socket_io_and_options)
{
    cat_socket_t socket;
    ASSERT_EQ(cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP4), &socket);
    ASSERT_TRUE(cat_socket_close(&socket));
    const struct {
        ssize_t (*read)(cat_socket_t *, char *, size_t);
        ssize_t result;
        cat_errno_t last_error;
    } reads[] = {
        {cat_socket_read, -1, CAT_EBADF},
        {cat_socket_recv, -1, CAT_EBADF},
        {cat_socket_try_recv, CAT_EBADF, 0},
    };
    char buffer[8] = "content";
    for (const auto &operation : reads) {
        cat_clear_last_error();
        ASSERT_EQ(operation.read(&socket, buffer, sizeof(buffer)), operation.result);
        ASSERT_EQ(cat_get_last_error_code(), operation.last_error);
        ASSERT_STREQ(buffer, "content");
    }
    ASSERT_EQ(cat_socket_peek(&socket, buffer, sizeof(buffer)), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_EQ(cat_socket_peekfrom(&socket, buffer, sizeof(buffer), nullptr, nullptr), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_EQ(cat_socket_peek_from(&socket, buffer, sizeof(buffer), nullptr, nullptr, nullptr), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_STREQ(buffer, "content");
    const struct {
        int (*get)(const cat_socket_t *);
        int (*set)(cat_socket_t *, int);
    } buffers[] = {
        {cat_socket_get_recv_buffer_size, cat_socket_set_recv_buffer_size},
        {cat_socket_get_send_buffer_size, cat_socket_set_send_buffer_size},
    };
    for (const auto &option : buffers) {
        ASSERT_EQ(option.get(&socket), -1);
        ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
        ASSERT_EQ(option.set(&socket, 4096), -1);
        ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    }
    ASSERT_FALSE(cat_socket_set_tcp_nodelay(&socket, cat_true));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_FALSE(cat_socket_set_tcp_keepalive(&socket, cat_true, 1));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_FALSE(cat_socket_set_udp_broadcast(&socket, cat_true));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_FALSE(cat_socket_get_tcp_nodelay(&socket));
    ASSERT_FALSE(cat_socket_get_tcp_keepalive(&socket));
    ASSERT_EQ(cat_socket_get_tcp_keepalive_delay(&socket), 0);
    ASSERT_FALSE(cat_socket_get_udp_broadcast(&socket));
    ASSERT_EQ(cat_socket_get_fd(&socket), CAT_SOCKET_INVALID_FD);
    std::string path = get_random_path();
    ASSERT_EQ(cat_socket_send_file(&socket, path.c_str(), 0, 0), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_FALSE(cat_socket_bind_to(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_FALSE(cat_socket_listen(&socket, TEST_SERVER_BACKLOG));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_FALSE(cat_socket_connect_to(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_FALSE(cat_socket_send(&socket, CAT_STRL("data")));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_EQ(cat_socket_try_send(&socket, CAT_STRL("data")), CAT_EBADF);
    ASSERT_EQ(cat_socket_try_send_to(&socket, CAT_STRL("data"), CAT_STRL(TEST_LISTEN_IPV4), 0), CAT_EBADF);
}

#ifdef CAT_OS_UNIX_LIKE
TEST(cat_socket, sendfile_partial_timeout_closes_socket)
{
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
    size_t buffer_capacity = static_cast<size_t>(cat_socket_get_send_buffer_size(&sender)) +
        static_cast<size_t>(cat_socket_get_recv_buffer_size(&receiver));
    const std::string data(buffer_capacity * 64, 's');
    const std::string path = get_random_path();
    ASSERT_EQ(cat_fs_put_contents(path.c_str(), data.data(), data.size()), static_cast<ssize_t>(data.size()));
    DEFER(cat_fs_unlink(path.c_str()));
    // A peer that does not read forces partial transfer followed by a writable wait.
    ASSERT_EQ(cat_socket_send_file_ex(&sender, path.c_str(), 0, data.size(), 10), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
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
#endif

#ifdef CAT_OS_UNIX_LIKE
TEST(cat_socket, reuseport_capability_and_shared_listener_port)
{
    cat_socket_t first;
    ASSERT_EQ(cat_socket_create(&first, CAT_SOCKET_TYPE_TCP4), &first);
    DEFER(cat_socket_close(&first));
    const cat_socket_bind_flags_t flags = CAT_SOCKET_BIND_FLAG_REUSEADDR | CAT_SOCKET_BIND_FLAG_REUSEPORT;
    ASSERT_TRUE(cat_socket_bind_to_ex(&first, CAT_STRL(TEST_LISTEN_IPV4), 0, flags));
    ASSERT_TRUE(cat_socket_listen(&first, TEST_SERVER_BACKLOG));
    if (!cat_socket_is_tcp_reuseport_load_balancing_supported()) {
        // Unsupported platforms retain the existing accepted bind flag fallback.
        return;
    }
    cat_socket_t second;
    ASSERT_EQ(cat_socket_create(&second, CAT_SOCKET_TYPE_TCP4), &second);
    DEFER(cat_socket_close(&second));
    int port = cat_socket_get_sock_port(&first);
    ASSERT_GT(port, 0);
    ASSERT_TRUE(cat_socket_bind_to_ex(&second, CAT_STRL(TEST_LISTEN_IPV4), port, flags));
    ASSERT_TRUE(cat_socket_listen(&second, TEST_SERVER_BACKLOG));
    ASSERT_EQ(cat_socket_get_sock_port(&second), port);
}

TEST(cat_socket, pipe_bind_path_and_flag_errors)
{
    cat_socket_t socket;
    ASSERT_EQ(cat_socket_create(&socket, CAT_SOCKET_TYPE_PIPE), &socket);
    DEFER(cat_socket_close(&socket));
    ASSERT_EQ(cat_socket_get_fd(&socket), CAT_SOCKET_INVALID_FD);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    std::string oversized(CAT_SOCKADDR_MAX_PATH * 2, 'a');
    ASSERT_FALSE(cat_socket_bind_to(&socket, oversized.data(), oversized.length(), 0));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENAMETOOLONG);
    ASSERT_FALSE(cat_socket_is_open(&socket));
    std::string path = get_random_path();
    ASSERT_FALSE(cat_socket_bind_to_ex(&socket, path.data(), path.length(), 0, CAT_SOCKET_BIND_FLAG_REUSEADDR));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_TRUE(cat_socket_bind_to(&socket, path.data(), path.length(), 0));
    DEFER(cat_fs_unlink(path.c_str()));
    ASSERT_TRUE(cat_socket_is_open(&socket));
}

#endif
