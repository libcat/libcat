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
  |         codinghuang <2812240764@qq.com>                                  |
  +--------------------------------------------------------------------------+
 */

#include "test.h"
extern "C"
{
#include "llhttp.h"
}

#ifdef CAT_IDE_HELPER
#include "uv-common.h"
#else
#include "../deps/libuv/src/uv-common.h"
#endif

static void echo_stream_server_connection_handler(cat_socket_t *server)
{
    while (true) {
        cat_socket_t *client = cat_socket_accept(server, nullptr);
        if (client == nullptr) {
            EXPECT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
            break;
        }
        co([client] {
            DEFER(cat_socket_close(client));
            ASSERT_STREQ(cat_socket_get_role_name(client), "session");
            while (true) {
                char read_buffer[TEST_BUFFER_SIZE_STD];
                ssize_t read_n = cat_socket_recv(client, CAT_STRS(read_buffer));
                ASSERT_GE(read_n, 0);
                if (read_n == 0) {
                    break;
                }
                if (std::string(read_buffer, read_n) == "RESET") {
                    break;
                }
                if (std::string(read_buffer, read_n) == "TIMEOUT") {
                    ASSERT_EQ(cat_socket_recv(client, CAT_STRS(read_buffer)), 0);
                    break;
                }
                ASSERT_TRUE(cat_socket_send(client, read_buffer, read_n));
                continue;
            }
        });
    }
}

cat_coroutine_t *echo_tcp_server;
char echo_tcp_server_ip[CAT_SOCKET_IPV6_BUFFER_SIZE];
size_t echo_tcp_server_ip_length = sizeof(echo_tcp_server_ip);
int echo_tcp_server_port;

TEST_REQUIREMENT(cat_socket, echo_tcp_server)
{
    co([] {
        cat_socket_t server;

        ASSERT_NE(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP), nullptr);
        DEFER(cat_socket_close(&server));
        ASSERT_TRUE(cat_socket_bind(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
        ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
        ASSERT_STREQ(cat_socket_get_role_name(&server), "server");
        ASSERT_TRUE(cat_socket_get_sock_address(&server, echo_tcp_server_ip, &echo_tcp_server_ip_length));
        DEFER(echo_tcp_server_ip[0] = '\0'; echo_tcp_server_ip_length = sizeof(echo_tcp_server_ip));
        ASSERT_GT(echo_tcp_server_port = cat_socket_get_sock_port(&server), 0);
        DEFER(echo_tcp_server_port = 0);
        echo_tcp_server = cat_coroutine_get_current();
        DEFER(echo_tcp_server = nullptr);
        cat_socket_set_read_timeout(&server, -1);

        echo_stream_server_connection_handler(&server);
    });
    ASSERT_NE(echo_tcp_server, nullptr);
}

TEST_REQUIREMENT_DTOR(cat_socket, echo_tcp_server)
{
    ASSERT_NE(echo_tcp_server, nullptr);
    cat_coroutine_resume(echo_tcp_server, nullptr, nullptr);
    ASSERT_EQ(echo_tcp_server, nullptr);
}

cat_coroutine_t *echo_udp_server;
char echo_udp_server_ip[CAT_SOCKET_IPV6_BUFFER_SIZE];
size_t echo_udp_server_ip_length = CAT_SOCKET_IPV6_BUFFER_SIZE;
int echo_udp_server_port;

TEST_REQUIREMENT(cat_socket, echo_udp_server)
{
    co([] {
        cat_socket_t server;

        ASSERT_NE(cat_socket_create(&server, CAT_SOCKET_TYPE_UDP), nullptr);
        DEFER(cat_socket_close(&server));
        ASSERT_TRUE(cat_socket_bind(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
        ASSERT_TRUE(cat_socket_get_sock_address(&server, echo_udp_server_ip, &echo_udp_server_ip_length));
        DEFER(echo_udp_server_ip[0] = '\0'; echo_udp_server_ip_length = sizeof(echo_udp_server_ip));
        ASSERT_GT(echo_udp_server_port = cat_socket_get_sock_port(&server), 0);
        DEFER(echo_udp_server_port = 0);
        echo_udp_server = cat_coroutine_get_current();
        DEFER(echo_udp_server = nullptr;);
        cat_socket_set_read_timeout(&server, -1);

        while (true) {
            char read_buffer[TEST_BUFFER_SIZE_STD];
            cat_sockaddr_union_t address;
            cat_socklen_t address_length = sizeof(address);
            ssize_t read_n = cat_socket_recvfrom(
                &server, CAT_STRS(read_buffer),
                &address.common,
                &address_length
            );
            if (read_n <= 0) {
                break;
            }
            ASSERT_TRUE(cat_socket_sendto(
                &server, read_buffer, read_n,
                &address.common, address_length
            ));
            continue;
        }
    });
    ASSERT_NE(echo_udp_server, nullptr);
}

TEST_REQUIREMENT_DTOR(cat_socket, echo_udp_server)
{
    ASSERT_NE(echo_udp_server, nullptr);
    cat_coroutine_resume(echo_udp_server, nullptr, nullptr);
    ASSERT_EQ(echo_udp_server, nullptr);
}

static std::string get_random_pipe_path(void)
{
    std::string random_bytes = get_random_bytes(8);
    return string_format(TEST_PIPE_PATH_FMT, random_bytes.c_str());
}

cat_coroutine_t *echo_pipe_server;
std::string echo_pipe_server_path;

TEST_REQUIREMENT(cat_socket, echo_pipe_server)
{
    co([] {
        cat_socket_t server;

        ASSERT_NE(cat_socket_create(&server, CAT_SOCKET_TYPE_PIPE), nullptr);
        DEFER(cat_socket_close(&server));
        echo_pipe_server_path = get_random_pipe_path();
        DEFER(echo_pipe_server_path = "");
        ASSERT_TRUE(cat_socket_bind(&server, echo_pipe_server_path.c_str(), echo_pipe_server_path.length(), 0));
        ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
        echo_pipe_server = cat_coroutine_get_current();
        DEFER(echo_pipe_server = nullptr;);
        cat_socket_set_read_timeout(&server, -1);

        echo_stream_server_connection_handler(&server);
    });
    ASSERT_NE(echo_pipe_server, nullptr);
}

TEST_REQUIREMENT_DTOR(cat_socket, echo_pipe_server)
{
    ASSERT_NE(echo_pipe_server, nullptr);
    cat_coroutine_resume(echo_pipe_server, nullptr, nullptr);
    ASSERT_EQ(echo_pipe_server, nullptr);
}

#ifdef CAT_OS_UNIX_LIKE
cat_coroutine_t *echo_udg_server;
std::string echo_udg_server_path;

TEST_REQUIREMENT(cat_socket, echo_udg_server)
{
    co([] {
        cat_socket_t server;

        ASSERT_NE(cat_socket_create(&server, CAT_SOCKET_TYPE_UDG), nullptr);
        DEFER(cat_socket_close(&server));
        echo_udg_server_path = get_random_pipe_path();
        DEFER(echo_udg_server_path = "");
        ASSERT_TRUE(cat_socket_bind(&server, echo_udg_server_path.c_str(), echo_udg_server_path.length(), 0));
        echo_udg_server = cat_coroutine_get_current();
        DEFER(echo_udg_server = nullptr;);
        cat_socket_set_read_timeout(&server, -1);

        while (true) {
            char read_buffer[TEST_BUFFER_SIZE_STD];
            cat_sockaddr_union_t address;
            cat_socklen_t address_length = sizeof(address);
            ssize_t read_n = cat_socket_recvfrom(
                &server, CAT_STRS(read_buffer),
                &address.common,
                &address_length
            );
            if (read_n <= 0) {
                break;
            }
            ASSERT_TRUE(cat_socket_sendto(
                &server, read_buffer, read_n,
                &address.common, address_length
            ));
            continue;
        }
    });
    ASSERT_NE(echo_udg_server, nullptr);
}

TEST_REQUIREMENT_DTOR(cat_socket, echo_udg_server)
{
    ASSERT_NE(echo_udg_server, nullptr);
    cat_coroutine_resume(echo_udg_server, nullptr, nullptr);
    ASSERT_EQ(echo_udg_server, nullptr);
}
#endif

TEST(cat_socket, get_local_free_port)
{
    ASSERT_GT(cat_socket_get_local_free_port(), 0);
}

TEST(cat_socket, cat_sockaddr_af_name)
{
    ASSERT_STREQ("UNSPEC", cat_sockaddr_af_name(AF_UNSPEC));
    ASSERT_STREQ("INET", cat_sockaddr_af_name(AF_INET));
    ASSERT_STREQ("INET6", cat_sockaddr_af_name(AF_INET6));
#ifdef CAT_OS_UNIX_LIKE
    ASSERT_STREQ("UNIX", cat_sockaddr_af_name(AF_LOCAL));
#else
    ASSERT_STREQ("LOCAL", cat_sockaddr_af_name(AF_LOCAL));
#endif
    ASSERT_STREQ("UNKNOWN", cat_sockaddr_af_name(-1));
}

TEST(cat_socket, cat_sockaddr_get_address_ip)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;
    char buffer[CAT_SOCKET_IP_BUFFER_SIZE];
    size_t buffer_size = CAT_SOCKET_IP_BUFFER_SIZE;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_NE(nullptr, info);

    ASSERT_TRUE(cat_sockaddr_get_address(&info->address.common, info->length, buffer, &buffer_size));
    ASSERT_STREQ(TEST_LISTEN_IPV4, buffer);
    ASSERT_EQ(CAT_STRLEN(TEST_LISTEN_IPV4), buffer_size);
}

TEST(cat_socket, cat_sockaddr_get_address_ip4)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;
    char buffer[CAT_SOCKET_IP_BUFFER_SIZE];
    size_t buffer_size = CAT_SOCKET_IP_BUFFER_SIZE;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP4));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_NE(nullptr, info);

    ASSERT_TRUE(cat_sockaddr_get_address(&info->address.common, info->length, buffer, &buffer_size));
    ASSERT_STREQ(TEST_LISTEN_IPV4, buffer);
    ASSERT_EQ(CAT_STRLEN(TEST_LISTEN_IPV4), buffer_size);
}

TEST(cat_socket, cat_sockaddr_get_address_ip6)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;
    char buffer[CAT_SOCKET_IP_BUFFER_SIZE];
    size_t buffer_size = CAT_SOCKET_IP_BUFFER_SIZE;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP6));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV6), 0));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_NE(nullptr, info);

    ASSERT_TRUE(cat_sockaddr_get_address(&info->address.common, info->length, buffer, &buffer_size));
    ASSERT_STREQ(TEST_LISTEN_IPV6, buffer);
    ASSERT_EQ(CAT_STRLEN(TEST_LISTEN_IPV6), buffer_size);
}

TEST(cat_socket, cat_sockaddr_get_address_pipe)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;
    char buffer[CAT_SOCKADDR_MAX_PATH];
    size_t buffer_size = CAT_SOCKADDR_MAX_PATH;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_PIPE));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_PIPE_PATH), 0));
    DEFER(unlink(TEST_PIPE_PATH));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_NE(nullptr, info);

    ASSERT_TRUE(cat_sockaddr_get_address(&info->address.common, info->length, buffer, &buffer_size));
    ASSERT_STREQ(TEST_PIPE_PATH, buffer);
    ASSERT_EQ(CAT_STRLEN(TEST_PIPE_PATH), buffer_size);
}

TEST(cat_socket, cat_sockaddr_get_port_ip)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;
    int port;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_NE(nullptr, info);

    port = cat_sockaddr_get_port(&info->address.common);
    ASSERT_GT(port, 0);
}

TEST(cat_socket, cat_sockaddr_get_port_ip4)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;
    int port;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP4));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_NE(nullptr, info);

    port = cat_sockaddr_get_port(&info->address.common);
    ASSERT_GT(port, 0);
}

TEST(cat_socket, cat_sockaddr_get_port_ip6)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;
    int port;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP6));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV6), 0));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_NE(nullptr, info);

    port = cat_sockaddr_get_port(&info->address.common);
    ASSERT_GT(port, 0);
}

TEST(cat_socket, cat_sockaddr_get_port_pipe)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;
    int port;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_PIPE));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_PIPE_PATH), 0));
    DEFER(unlink(TEST_PIPE_PATH));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_NE(nullptr, info);

    port = cat_sockaddr_get_port(&info->address.common);
    ASSERT_EQ(port, 0);
}

TEST(cat_socket, cat_sockaddr_set_port_ip4)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;
    int actual_port;
    int expect_port;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP4));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_NE(nullptr, info);
    expect_port = cat_socket_get_local_free_port();
    ASSERT_TRUE(cat_sockaddr_set_port((cat_sockaddr_t *) &info->address.common, expect_port));
    actual_port = cat_sockaddr_get_port(&info->address.common);
    ASSERT_EQ(expect_port, actual_port);
}

TEST(cat_socket, cat_sockaddr_set_port_ip6)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;
    int actual_port;
    int expect_port;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP6));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV6), 0));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_NE(nullptr, info);
    expect_port = cat_socket_get_local_free_port();
    ASSERT_TRUE(cat_sockaddr_set_port((cat_sockaddr_t *) &info->address.common, expect_port));
    actual_port = cat_sockaddr_get_port(&info->address.common);
    ASSERT_EQ(expect_port, actual_port);
}

TEST(cat_socket, cat_sockaddr_to_name)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;
    char name[CAT_SOCKET_IP_BUFFER_SIZE];
    size_t name_length = CAT_SOCKET_IP_BUFFER_SIZE;
    int port;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP4));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_TRUE(cat_sockaddr_to_name(&info->address.common, info->length, name, &name_length, &port));
    ASSERT_STREQ(TEST_LISTEN_IPV4, name);
    ASSERT_EQ(CAT_STRLEN(TEST_LISTEN_IPV4), name_length);
    ASSERT_GT(port, 0);
}

TEST(cat_socket, cat_sockaddr_to_name_address_length_eq_zero)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;
    char name[CAT_SOCKET_IP_BUFFER_SIZE];
    size_t name_length = CAT_SOCKET_IP_BUFFER_SIZE;
    int port;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP4));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_TRUE(cat_sockaddr_to_name(&info->address.common, 0, name, &name_length, &port));
    ASSERT_STREQ("", name);
    ASSERT_EQ(0, name_length);
    ASSERT_EQ(port, 0);
}

TEST(cat_socket, cat_sockaddr_copy)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *from;
    cat_sockaddr_info_t to = { };

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP4));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    /* from will be free in cat_socket_close callback */
    from = cat_socket_getname_fast(&socket, cat_false);
    to.length = from->length;
    ASSERT_EQ(0, cat_sockaddr_copy(&to.address.common, &to.length, &from->address.common, from->length));
}

TEST(cat_socket, cat_sockaddr_copy_to_length_eq_zero)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *from;
    cat_sockaddr_info_t to = { };

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP4));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    /* from will be free in cat_socket_close callback */
    from = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_EQ(CAT_ENOSPC, cat_sockaddr_copy(&to.address.common, &to.length, &from->address.common, from->length));
}

TEST(cat_socket, cat_sockaddr_check_ip4)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP4));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_FALSE(cat_sockaddr_check(&info->address.common, sizeof(cat_sockaddr_in_t) - 1));
    ASSERT_EQ(CAT_EINVAL, cat_get_last_error_code());
    ASSERT_TRUE(cat_sockaddr_check(&info->address.common, sizeof(cat_sockaddr_in_t)));
}

TEST(cat_socket, cat_sockaddr_check_ip6)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP6));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV6), 0));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_FALSE(cat_sockaddr_check(&info->address.common, sizeof(cat_sockaddr_in6_t) - 1));
    ASSERT_EQ(CAT_EINVAL, cat_get_last_error_code());
    ASSERT_TRUE(cat_sockaddr_check(&info->address.common, sizeof(cat_sockaddr_in6_t)));
}

TEST(cat_socket, cat_sockaddr_check_pipe_error)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_PIPE));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_PIPE_PATH), 0));
    DEFER(unlink(TEST_PIPE_PATH));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_FALSE(cat_sockaddr_check(&info->address.common, 0));
    ASSERT_EQ(CAT_EINVAL, cat_get_last_error_code());
    ASSERT_TRUE(cat_sockaddr_check(&info->address.common, info->length));
}

TEST(cat_socket, init)
{
    cat_socket_t socket;
    cat_socket_init(&socket);
    ASSERT_EQ(CAT_SOCKET_TYPE_ANY, socket.type);
    ASSERT_EQ(nullptr, socket.internal);
}

TEST(cat_socket, create_tcp)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
}

TEST(cat_socket, create_udp)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_UDP));
    DEFER(cat_socket_close(&socket));
}

TEST(cat_socket, type_name)
{
    ASSERT_STREQ("TCP", cat_socket_type_name(CAT_SOCKET_TYPE_TCP));
    ASSERT_STREQ("TCP4", cat_socket_type_name(CAT_SOCKET_TYPE_TCP | CAT_SOCKET_TYPE_FLAG_IPV4));
    ASSERT_STREQ("TCP6", cat_socket_type_name(CAT_SOCKET_TYPE_TCP | CAT_SOCKET_TYPE_FLAG_IPV6));

    ASSERT_STREQ("UDP", cat_socket_type_name(CAT_SOCKET_TYPE_UDP));
    ASSERT_STREQ("UDP4", cat_socket_type_name(CAT_SOCKET_TYPE_UDP | CAT_SOCKET_TYPE_FLAG_IPV4));
    ASSERT_STREQ("UDP6", cat_socket_type_name(CAT_SOCKET_TYPE_UDP | CAT_SOCKET_TYPE_FLAG_IPV6));

#ifdef CAT_OS_UNIX_LIKE
    ASSERT_STREQ("UNIX", cat_socket_type_name(CAT_SOCKET_TYPE_PIPE));
#else
    ASSERT_STREQ("PIPE", cat_socket_type_name(CAT_SOCKET_TYPE_PIPE));
#endif
    ASSERT_STREQ("TTY", cat_socket_type_name(CAT_SOCKET_TYPE_TTY));
#ifdef CAT_OS_UNIX_LIKE
    ASSERT_STREQ("UDG", cat_socket_type_name(CAT_SOCKET_TYPE_UDG));
#endif
    ASSERT_STREQ("UNKNOWN", cat_socket_type_name(INT_MIN));
}

TEST(cat_socket, get_type_name)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_STREQ("TCP", cat_socket_get_type_name(&socket));
}

TEST(cat_socket, get_fd_fast)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;
    cat_socket_fd_t fd;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP4));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_NE(nullptr, info);
    fd = cat_socket_get_fd_fast(&socket);
    ASSERT_GE((cat_os_fd_t) fd, 0);
}

TEST(cat_socket, get_fd)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;
    cat_socket_fd_t fd;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP4));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getname_fast(&socket, cat_false);
    ASSERT_NE(nullptr, info);
    fd = cat_socket_get_fd(&socket);
    ASSERT_GE((cat_os_fd_t) fd, 0);
}

TEST(cat_socket, bind)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP4));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
}

TEST(cat_socket, bind_ex_ip4)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP4));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind_ex(&socket,
        CAT_STRL(TEST_LISTEN_IPV4),
        0,
        CAT_SOCKET_BIND_FLAG_REUSEADDR | CAT_SOCKET_BIND_FLAG_REUSEPORT)
    );
}

TEST(cat_socket, bind_ex_ip6)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP6));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind_ex(&socket,
        CAT_STRL(TEST_LISTEN_IPV6),
        0,
        CAT_SOCKET_BIND_FLAG_IPV6ONLY | CAT_SOCKET_BIND_FLAG_REUSEADDR | CAT_SOCKET_BIND_FLAG_REUSEPORT)
    );
}

TEST(cat_socket, getsockname_fast)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getsockname_fast(&socket);
    ASSERT_NE(nullptr, info);
}

TEST(cat_socket, getpeername)
{
    cat_socket_t socket;
    cat_sockaddr_info_t info;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    info.length = sizeof(info.address);
    ASSERT_FALSE(cat_socket_getpeername(&socket, &info.address.common, &info.length));
}

TEST(cat_socket, getpeername_fast)
{
    cat_socket_t socket;
    const cat_sockaddr_info_t *info;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    /* info will be free in cat_socket_close callback */
    info = cat_socket_getpeername_fast(&socket);
    ASSERT_EQ(nullptr, info);
}

TEST(cat_socket, is_available)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_is_available(&socket) && cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_is_available(&socket));
    ASSERT_TRUE(cat_socket_close(&socket));
    ASSERT_FALSE(cat_socket_is_available(&socket));
}

TEST(cat_socket, is_open)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_FALSE(cat_socket_is_open(&socket));
    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_is_open(&socket));
}

TEST(cat_socket, is_established)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_FALSE(cat_socket_is_established(&socket));
}

TEST(cat_socket, is_server)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_FALSE(cat_socket_is_server(&socket));
}

TEST(cat_socket, is_client)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_FALSE(cat_socket_is_client(&socket));
}

TEST(cat_socket, check_liveness)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_FALSE(cat_socket_check_liveness(&socket));
    ASSERT_TRUE(cat_socket_connect(&socket, echo_tcp_server_ip, echo_tcp_server_ip_length, echo_tcp_server_port));
    ASSERT_TRUE(cat_socket_check_liveness(&socket));
    ASSERT_TRUE(cat_socket_send(&socket, CAT_STRL("RESET")));
    ASSERT_EQ(cat_poll_one(cat_socket_get_fd_fast(&socket), POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
    ASSERT_FALSE(cat_socket_check_liveness(&socket));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ECONNRESET);
}

TEST(cat_socket, is_eof_error)
{
    ASSERT_FALSE(cat_socket_is_eof_error(CAT_ETIMEDOUT));
    ASSERT_FALSE(cat_socket_is_eof_error(CAT_ECANCELED));
    ASSERT_TRUE(cat_socket_is_eof_error(CAT_EIO));
}

TEST(cat_socket, io_state_name)
{
    ASSERT_STREQ("read", cat_socket_io_state_name(CAT_SOCKET_IO_FLAG_READ));
    ASSERT_STREQ("connect", cat_socket_io_state_name(CAT_SOCKET_IO_FLAG_CONNECT));
    ASSERT_STREQ("accept", cat_socket_io_state_name(CAT_SOCKET_IO_FLAG_ACCEPT));
    ASSERT_STREQ("write", cat_socket_io_state_name(CAT_SOCKET_IO_FLAG_WRITE));
    ASSERT_STREQ("read or write", cat_socket_io_state_name(CAT_SOCKET_IO_FLAG_RDWR));
    ASSERT_STREQ("bind", cat_socket_io_state_name(CAT_SOCKET_IO_FLAG_BIND));
    ASSERT_STREQ("idle", cat_socket_io_state_name(CAT_SOCKET_IO_FLAG_NONE));
}

TEST(cat_socket, io_state_naming)
{
    ASSERT_STREQ("reading", cat_socket_io_state_naming(CAT_SOCKET_IO_FLAG_READ));
    ASSERT_STREQ("connecting", cat_socket_io_state_naming(CAT_SOCKET_IO_FLAG_CONNECT));
    ASSERT_STREQ("accepting", cat_socket_io_state_naming(CAT_SOCKET_IO_FLAG_ACCEPT));
    ASSERT_STREQ("writing", cat_socket_io_state_naming(CAT_SOCKET_IO_FLAG_WRITE));
    ASSERT_STREQ("reading and writing", cat_socket_io_state_naming(CAT_SOCKET_IO_FLAG_RDWR));
    ASSERT_STREQ("binding", cat_socket_io_state_naming(CAT_SOCKET_IO_FLAG_BIND));
    ASSERT_STREQ("idle", cat_socket_io_state_naming(CAT_SOCKET_IO_FLAG_NONE));
}

TEST(cat_socket, get_io_state)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_EQ(CAT_SOCKET_IO_FLAG_NONE, cat_socket_get_io_state(&socket));
}

TEST(cat_socket, get_io_state_name)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_STREQ("idle", cat_socket_get_io_state_name(&socket));
}

TEST(cat_socket, get_io_state_naming)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_STREQ("idle", cat_socket_get_io_state_naming(&socket));
}

TEST(cat_socket, get_role_name)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_STREQ(cat_socket_get_role_name(&socket), "none");
    ASSERT_TRUE(cat_socket_connect(&socket, echo_tcp_server_ip, echo_tcp_server_ip_length, echo_tcp_server_port));
    ASSERT_STREQ(cat_socket_get_role_name(&socket), "client");
}

TEST(cat_socket, buffer_size)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));

    ASSERT_EQ(cat_socket_get_recv_buffer_size(&socket), -1);
    ASSERT_EQ(cat_socket_get_send_buffer_size(&socket), -1);

    ASSERT_TRUE(cat_socket_bind(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));

    /* get/set/align-memory/clear-cache/get/get-cache */
    ASSERT_GT(cat_socket_get_recv_buffer_size(&socket), 0);
    ASSERT_TRUE(cat_socket_set_recv_buffer_size(&socket, 0));
    ASSERT_GE(cat_socket_get_recv_buffer_size(&socket), (int) cat_getpagesize());
    ASSERT_LE(cat_socket_get_recv_buffer_size(&socket), (int) (cat_getpagesize() * 2));
    /* send buffer again */
    ASSERT_GT(cat_socket_get_send_buffer_size(&socket), 0);
    ASSERT_TRUE(cat_socket_set_send_buffer_size(&socket, 0));
    ASSERT_GE(cat_socket_get_send_buffer_size(&socket), (int) cat_getpagesize());
    ASSERT_LE(cat_socket_get_send_buffer_size(&socket), (int) (cat_getpagesize() * 2));
}

TEST(cat_socket, set_tcp_nodelay)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_set_tcp_nodelay(&socket, cat_true));
    ASSERT_TRUE(cat_socket_connect(&socket, echo_tcp_server_ip, echo_tcp_server_ip_length, echo_tcp_server_port));
    ASSERT_EQ(socket.internal->u.tcp.flags & UV_HANDLE_TCP_NODELAY, UV_HANDLE_TCP_NODELAY);
    ASSERT_TRUE(cat_socket_set_tcp_nodelay(&socket, cat_true));
    ASSERT_EQ(socket.internal->u.tcp.flags & UV_HANDLE_TCP_NODELAY, UV_HANDLE_TCP_NODELAY);
    ASSERT_TRUE(cat_socket_set_tcp_nodelay(&socket, cat_false));
    ASSERT_EQ(socket.internal->u.tcp.flags & UV_HANDLE_TCP_NODELAY, 0);
    ASSERT_TRUE(cat_socket_set_tcp_nodelay(&socket, cat_true));
}

TEST(cat_socket, set_tcp_keepalive)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_set_tcp_keepalive(&socket, cat_true, 1));
    ASSERT_TRUE(cat_socket_connect(&socket, echo_tcp_server_ip, echo_tcp_server_ip_length, echo_tcp_server_port));
    ASSERT_EQ(socket.internal->u.tcp.flags & UV_HANDLE_TCP_KEEPALIVE, UV_HANDLE_TCP_KEEPALIVE);
    ASSERT_TRUE(cat_socket_set_tcp_keepalive(&socket, cat_true, 0));
    ASSERT_EQ(socket.internal->u.tcp.flags & UV_HANDLE_TCP_KEEPALIVE, UV_HANDLE_TCP_KEEPALIVE);
    ASSERT_TRUE(cat_socket_set_tcp_keepalive(&socket, cat_false, 0));
    ASSERT_EQ(socket.internal->u.tcp.flags & UV_HANDLE_TCP_KEEPALIVE, 0);
    ASSERT_TRUE(cat_socket_set_tcp_keepalive(&socket, cat_true, 30));
    ASSERT_EQ(socket.internal->u.tcp.flags & UV_HANDLE_TCP_KEEPALIVE, UV_HANDLE_TCP_KEEPALIVE);
}

TEST(cat_socket, set_tcp_accept_balance)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_set_tcp_accept_balance(&socket, cat_true));
}

static void echo_stream_client_tests(cat_socket_t *echo_client)
{
    size_t n;
    ssize_t ret;

    /* normal loop */
    for (n = TEST_MAX_REQUESTS; n--;) {
        char read_buffer[TEST_BUFFER_SIZE_STD];
        char write_buffer[TEST_BUFFER_SIZE_STD];
        /* send request */
        cat_snrand(CAT_STRS(write_buffer));
        ASSERT_TRUE(cat_socket_send(echo_client, CAT_STRS(write_buffer)));
        /* recv response */
        ret = cat_socket_read(echo_client, CAT_STRS(read_buffer));
        ASSERT_EQ(ret, (ssize_t) sizeof(read_buffer));
        read_buffer[sizeof(read_buffer) - 1] = '\0';
        write_buffer[sizeof(write_buffer) - 1] = '\0';
        ASSERT_STREQ(read_buffer, write_buffer);
    }

    /* pipeline */
    do {
        char **read_buffers = (char **) cat_malloc(TEST_MAX_REQUESTS * sizeof(*read_buffers));
        char **write_buffers = (char **) cat_malloc(TEST_MAX_REQUESTS * sizeof(*write_buffers));
        bool done = false, wait = false;
        ASSERT_NE(read_buffers, nullptr);
        ASSERT_NE(write_buffers, nullptr);
        /* generate write data */
        for (n = 0; n < TEST_MAX_REQUESTS; n++) {
            write_buffers[n] = (char *) cat_malloc(TEST_BUFFER_SIZE_STD);
            ASSERT_NE(write_buffers[n], nullptr);
            cat_snrand(write_buffers[n], TEST_BUFFER_SIZE_STD);
        }
        /* send requests */
        cat_coroutine_t *coroutine = cat_coroutine_get_current();
        co([&] {
            for (size_t n = 0; n < TEST_MAX_REQUESTS; n++) {
                ASSERT_TRUE(cat_socket_send(echo_client, write_buffers[n], TEST_BUFFER_SIZE_STD));
            }
            done = true;
            if (wait) {
                ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
            }
        });
        /* recv responses */
        for (n = 0; n < TEST_MAX_REQUESTS; n++) {
            read_buffers[n] = (char *) cat_malloc(TEST_BUFFER_SIZE_STD);
            ASSERT_NE(read_buffers[n], nullptr);
            ret = cat_socket_read(echo_client, read_buffers[n], TEST_BUFFER_SIZE_STD);
            ASSERT_EQ(ret, TEST_BUFFER_SIZE_STD);
            read_buffers[n][TEST_BUFFER_SIZE_STD - 1] = '\0';
            write_buffers[n][TEST_BUFFER_SIZE_STD - 1] = '\0';
            ASSERT_STREQ(read_buffers[n], write_buffers[n]);
            cat_free(read_buffers[n]);
            cat_free(write_buffers[n]);
        }
        cat_free(read_buffers);
        cat_free(write_buffers);
        if (!done) {
            wait = true;
            ASSERT_TRUE(cat_coroutine_yield(nullptr, nullptr));
        }
        ASSERT_TRUE(done);
    } while (0);
}

TEST(cat_socket, echo_tcp_client)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    cat_socket_t echo_client;

    ASSERT_NE(cat_socket_create(&echo_client, CAT_SOCKET_TYPE_TCP), nullptr);
    DEFER(cat_socket_close(&echo_client));

    ASSERT_TRUE(cat_socket_connect(&echo_client, echo_tcp_server_ip, echo_tcp_server_ip_length, echo_tcp_server_port));
    {
        char ip[CAT_SOCKET_IP_BUFFER_SIZE];
        size_t ip_length = sizeof(ip);
        ASSERT_TRUE(cat_socket_get_peer_address(&echo_client, ip, &ip_length));
        ASSERT_EQ(std::string(ip, ip_length), std::string(echo_tcp_server_ip, echo_tcp_server_ip_length));
    }

    echo_stream_client_tests(&echo_client);
}

TEST(cat_socket, echo_tcp_client_localhost)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    cat_socket_t echo_client_v6, echo_client_localhost;

    ASSERT_NE(cat_socket_create(&echo_client_v6, CAT_SOCKET_TYPE_FLAG_UNSPEC | CAT_SOCKET_TYPE_TCP), nullptr);
    DEFER(cat_socket_close(&echo_client_v6));
    ASSERT_NE(cat_socket_create(&echo_client_localhost, CAT_SOCKET_TYPE_FLAG_UNSPEC | CAT_SOCKET_TYPE_TCP), nullptr);
    DEFER(cat_socket_close(&echo_client_localhost));

    // ::1 is not bound
    ASSERT_FALSE(cat_socket_connect(&echo_client_v6, CAT_STRL("::1"), echo_tcp_server_port));
    // localhost should work
    // try ::1 first, then 127.0.0.1
    ASSERT_TRUE(cat_socket_connect(&echo_client_localhost, CAT_STRL("localhost"), echo_tcp_server_port));
    {
        char ip[CAT_SOCKET_IP_BUFFER_SIZE];
        size_t ip_length = sizeof(ip);
        ASSERT_TRUE(cat_socket_get_peer_address(&echo_client_localhost, ip, &ip_length));
        ASSERT_EQ(std::string(ip, ip_length), std::string(echo_tcp_server_ip, echo_tcp_server_ip_length));
    }

    echo_stream_client_tests(&echo_client_localhost);
}

TEST(cat_socket, echo_udp_client)
{
    TEST_REQUIRE(echo_udp_server != nullptr, cat_socket, echo_udp_server);
    cat_socket_t echo_client;
    ssize_t ret;

    ASSERT_NE(cat_socket_create(&echo_client, CAT_SOCKET_TYPE_UDP), nullptr);
    DEFER(cat_socket_close(&echo_client));

    /* normal loop */
    for (int type = 0; type < 2; type++) {
        if (type == 1) {
            ASSERT_TRUE(cat_socket_connect(
                &echo_client,
                echo_udp_server_ip,
                echo_udp_server_ip_length, echo_udp_server_port
            ));
        }
        for (size_t n = TEST_MAX_REQUESTS; n--;) {
            char read_buffer[TEST_BUFFER_SIZE_STD];
            char write_buffer[TEST_BUFFER_SIZE_STD];
            /* send request */
            cat_snrand(CAT_STRS(write_buffer));
            if (type == 0) {
                ASSERT_TRUE(cat_socket_send_to(
                    &echo_client, CAT_STRS(write_buffer),
                    echo_udp_server_ip, echo_udp_server_ip_length, echo_udp_server_port
                ));
            } else {
                ASSERT_TRUE(cat_socket_send(&echo_client, CAT_STRS(write_buffer)));
            }
            /* recv response */
            cat_sockaddr_union_t address;
            cat_socklen_t address_length = sizeof(address);
            if (type == 0) {
                ret = cat_socket_recvfrom(
                    &echo_client, CAT_STRS(read_buffer),
                    &address.common, &address_length
                );
            } else {
                ret = cat_socket_recv(&echo_client, CAT_STRS(read_buffer));
            }
            ASSERT_EQ(ret, (ssize_t) sizeof(read_buffer));
            read_buffer[sizeof(read_buffer) - 1] = '\0';
            write_buffer[sizeof(write_buffer) - 1] = '\0';
            ASSERT_STREQ(read_buffer, write_buffer);
            if (type == 0) {
                char name[CAT_SOCKET_IP_BUFFER_SIZE];
                size_t name_length = sizeof(name);
                int port;
                cat_sockaddr_to_name(&address.common, address_length, name, &name_length, &port);
                ASSERT_EQ(std::string(name, name_length), echo_udp_server_ip);
                ASSERT_EQ(port, echo_udp_server_port);
            }
        }
    }
}

TEST(cat_socket, echo_pipe_client)
{
    TEST_REQUIRE(echo_pipe_server != nullptr, cat_socket, echo_pipe_server);
    cat_socket_t echo_client;

    ASSERT_NE(cat_socket_create(&echo_client, CAT_SOCKET_TYPE_PIPE), nullptr);
    DEFER(cat_socket_close(&echo_client));

    ASSERT_TRUE(cat_socket_connect(&echo_client, echo_pipe_server_path.c_str(), echo_pipe_server_path.length(), 0));
    {
        char path[CAT_SOCKADDR_MAX_PATH];
        size_t path_length = sizeof(path);
        ASSERT_TRUE(cat_socket_get_peer_address(&echo_client, path, &path_length));
        ASSERT_EQ(std::string(path, path_length), echo_pipe_server_path);
    }

    echo_stream_client_tests(&echo_client);
}

#ifdef CAT_OS_UNIX_LIKE
TEST(cat_socket, echo_udg_client)
{
    TEST_REQUIRE(echo_udg_server != nullptr, cat_socket, echo_udg_server);
    cat_socket_t echo_client;
    ssize_t ret;

    ASSERT_NE(cat_socket_create(&echo_client, CAT_SOCKET_TYPE_UDG), nullptr);
    DEFER(cat_socket_close(&echo_client));
    std::string path = get_random_pipe_path();
    ASSERT_TRUE(cat_socket_bind(&echo_client, path.c_str(), path.length(), 0));

    /* normal loop */
    for (int type = 0; type < 2; type++) {
        if (type == 1) {
            ASSERT_TRUE(cat_socket_connect(
                &echo_client,
                echo_udg_server_path.c_str(),
                echo_udg_server_path.length(), 0
            ));
        }
        for (size_t n = TEST_MAX_REQUESTS; n--;) {
            /* TEST_BUFFER_SIZE_STD is too long for MacOS */
            char read_buffer[1024];
            char write_buffer[1024];
            /* send request */
            cat_snrand(CAT_STRS(write_buffer));
            if (type == 0) {
                ASSERT_TRUE(cat_socket_send_to(
                    &echo_client, CAT_STRS(write_buffer),
                    echo_udg_server_path.c_str(),
                    echo_udg_server_path.length(), 0
                ));
            } else {
                ASSERT_TRUE(cat_socket_send(&echo_client, CAT_STRS(write_buffer)));
            }
            /* recv response */
            cat_sockaddr_union_t address;
            cat_socklen_t address_length = sizeof(address);
            if (type == 0) {
                ret = cat_socket_recvfrom(
                    &echo_client, CAT_STRS(read_buffer),
                    &address.common, &address_length
                );
            } else {
                ret = cat_socket_recv(&echo_client, CAT_STRS(read_buffer));
            }
            ASSERT_EQ(ret, (ssize_t) sizeof(read_buffer));
            read_buffer[sizeof(read_buffer) - 1] = '\0';
            write_buffer[sizeof(write_buffer) - 1] = '\0';
            ASSERT_STREQ(read_buffer, write_buffer);
            if (type == 0) {
                char name[CAT_SOCKET_IP_BUFFER_SIZE];
                size_t name_length = sizeof(name);
                int port;
                cat_sockaddr_to_name(&address.common, address_length, name, &name_length, &port);
                ASSERT_EQ(std::string(name, name_length), echo_udg_server_path);
                ASSERT_EQ(port, 0);
            }
        }
    }
}
#endif

TEST(cat_socket, send_yield)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    cat_socket_t client;

    ASSERT_NE(cat_socket_create(&client, CAT_SOCKET_TYPE_TCP), nullptr);
    DEFER(cat_socket_close(&client));

    ASSERT_TRUE(cat_socket_connect(&client, echo_tcp_server_ip, echo_tcp_server_ip_length, echo_tcp_server_port));

    int send_buffer_size = cat_socket_get_send_buffer_size(&client);
    ASSERT_GT(send_buffer_size, 0);
    size_t buffer_size = send_buffer_size * (!is_valgrind() ? 8 : 2);
    char *write_buffer = (char *) cat_malloc(buffer_size);
    ASSERT_NE(write_buffer, nullptr);
    cat_snrand(write_buffer, buffer_size);
    do {
        cat_coroutine_t write_coroutine;
        struct write_context_s {
            cat_socket_t *socket;
            const char *buffer;
            size_t length;
        } write_context = {
            &client,
            write_buffer,
            buffer_size
        };
        cat_coroutine_run(&write_coroutine, [](cat_data_t *data)->cat_data_t* {
            cat_coroutine_round_t round = cat_coroutine_get_current_round();
            struct write_context_s *context = (struct write_context_s *) data;
            EXPECT_TRUE(cat_socket_send(context->socket, context->buffer, context->length));
            EXPECT_GT(cat_coroutine_get_current_round(), round);
            return nullptr;
        }, &write_context);
    } while (0);
    char *read_buffer = (char *) cat_malloc(buffer_size);
    ASSERT_NE(read_buffer, nullptr);
    ASSERT_EQ(cat_socket_read(&client, read_buffer, buffer_size), (ssize_t) buffer_size);
    read_buffer[buffer_size - 1] = '\0';
    write_buffer[buffer_size - 1] = '\0';
    ASSERT_STREQ(read_buffer, write_buffer);
    cat_free(read_buffer);
    cat_free(write_buffer);
}

TEST(cat_socket, query_remote_http_server)
{
    SKIP_IF_OFFLINE();
    cat_socket_t socket;

    ASSERT_NE(cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP), nullptr);
    DEFER(cat_socket_close(&socket));

    /* invaild connect */
    cat_coroutine_run(nullptr, [](cat_data_t *data)->cat_data_t* {
        cat_socket_t *socket = (cat_socket_t *) data;
        cat_time_sleep(0);
        EXPECT_FALSE(cat_socket_connect(socket, CAT_STRL("8.8.8.8"), 12345));
        EXPECT_EQ(cat_get_last_error_code(), CAT_ELOCKED);
        return nullptr;
    }, &socket);

    ASSERT_TRUE(cat_socket_connect(&socket, TEST_REMOTE_HTTP_SERVER));

    char *request = cat_sprintf(
        "GET / HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: %s\r\n"
        "\r\n",
        TEST_REMOTE_HTTP_SERVER_HOST,
        TEST_HTTP_CLIENT_FAKE_USERAGENT
    );
    ASSERT_NE(request, nullptr);
    DEFER(cat_free(request));
    ASSERT_TRUE(cat_socket_send(&socket, request, strlen(request)));

    /* invaild read */
    cat_coroutine_run(nullptr, [](cat_data_t *data)->cat_data_t* {
        cat_socket_t *socket = (cat_socket_t *) data;
        char read_buffer[1];
        cat_time_sleep(0);
        EXPECT_EQ(cat_socket_recv(socket, CAT_STRS(read_buffer)), -1);
        EXPECT_EQ(cat_get_last_error_code(), CAT_ELOCKED);
        return nullptr;
    }, &socket);

    do {
        llhttp_t parser;
        llhttp_settings_t settings;
        size_t total_bytes = 0;

        llhttp_settings_init(&settings);
        settings.on_message_complete = [](llhttp_t *parser) {
            parser->data = (void *) 1;
            return 0;
        };
        llhttp_init(&parser, HTTP_RESPONSE, &settings);
        parser.data = nullptr;

        do {
            char read_buffer[TEST_BUFFER_SIZE_STD];
            ssize_t n;
            enum llhttp_errno error;

            n = cat_socket_recv(&socket, CAT_STRS(read_buffer));
            ASSERT_GT(n, 0);
            total_bytes += (size_t) n;

            error = llhttp_execute(&parser, read_buffer, n);
            ASSERT_EQ(error, HPE_OK);

            cat_debug(SOCKET, "Data[%zd]: %.*s", n, (int) n, read_buffer);

            if (parser.data != nullptr) {
                break;
            }
        } while (true);

        EXPECT_TRUE(parser.status_code == TEST_HTTP_STATUS_CODE_OK);

        cat_debug(SOCKET, "total_bytes: %zu", total_bytes);
    } while (0);
}

TEST(cat_socket, cross_close_when_dns_resolve)
{
    SKIP_IF_OFFLINE();
    bool exited = false;
    DEFER(exited = true);
    cat_socket_t _socket, *socket = cat_socket_create(&_socket, CAT_SOCKET_TYPE_TCP);
    DEFER(cat_socket_close(socket));
    co([&] {
        cat_time_sleep(0);
        if (!exited) {
            cat_socket_close(socket);
        }
    });
    ASSERT_FALSE(cat_socket_connect(socket, TEST_REMOTE_HTTP_SERVER));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
}

static const char *test_remote_http_server_ip;

TEST(cat_socket, get_test_remote_http_server_ip)
{
    SKIP_IF_OFFLINE();
    static char ip[CAT_SOCKET_IP_BUFFER_SIZE];
    ASSERT_TRUE(cat_dns_get_ip(CAT_STRS(ip), TEST_REMOTE_HTTP_SERVER_HOST, AF_UNSPEC));
    test_remote_http_server_ip = ip;
}

TEST(cat_socket, cross_close_when_connecting)
{
    SKIP_IF(test_remote_http_server_ip == nullptr);
    bool exited = false;
    DEFER(exited = true);
    cat_socket_t _socket, *socket = cat_socket_create(&_socket, CAT_SOCKET_TYPE_TCP);
    DEFER(cat_socket_close(socket));
    co([&] {
        cat_time_sleep(0);
        ASSERT_FALSE(exited);
        cat_socket_close(socket);
    });
    ASSERT_FALSE(cat_socket_connect(socket, test_remote_http_server_ip, strlen(test_remote_http_server_ip), TEST_REMOTE_HTTP_SERVER_PORT));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
}

TEST(cat_socket, cancel_connect)
{
    SKIP_IF(test_remote_http_server_ip == nullptr);
    cat_coroutine_t *waiter = cat_coroutine_get_current();
    bool exited;
    exited = false;
    DEFER(exited = true);
    cat_socket_t _socket, *socket = cat_socket_create(&_socket, CAT_SOCKET_TYPE_TCP);
    DEFER(cat_socket_close(socket));
    co([&] {
        cat_time_sleep(0);
        ASSERT_FALSE(exited);
        cat_coroutine_resume(waiter, nullptr, nullptr);
    });
    ASSERT_FALSE(cat_socket_connect(socket, test_remote_http_server_ip, strlen(test_remote_http_server_ip), TEST_REMOTE_HTTP_SERVER_PORT));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
}

TEST(cat_socket, connreset)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    cat_socket_t client;
    char buffer[1];
    ssize_t n;

    ASSERT_NE(cat_socket_create(&client, CAT_SOCKET_TYPE_TCP), nullptr);
    DEFER(cat_socket_close(&client));
    ASSERT_TRUE(cat_socket_connect(&client, echo_tcp_server_ip, echo_tcp_server_ip_length, echo_tcp_server_port));
    ASSERT_TRUE(cat_socket_send(&client, CAT_STRL("RESET")));
    n = cat_socket_recv(&client, CAT_STRS(buffer));
    ASSERT_LE(n, 0);
    if (n < 0) {
        ASSERT_EQ(cat_get_last_error_code(), CAT_ECONNRESET);
    }
}

TEST(cat_socket, timeout)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    cat_socket_t client;
    char buffer[1];
    ssize_t n;

    ASSERT_NE(cat_socket_create(&client, CAT_SOCKET_TYPE_TCP), nullptr);
    DEFER(cat_socket_close(&client));
    ASSERT_TRUE(cat_socket_connect(&client, echo_tcp_server_ip, echo_tcp_server_ip_length, echo_tcp_server_port));
    ASSERT_TRUE(cat_socket_send(&client, CAT_STRL("TIMEOUT")));
    n = cat_socket_recv_ex(&client, CAT_STRS(buffer), 1);
    ASSERT_LT(n, 0);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
}

TEST(cat_socket, peek)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    cat_socket_t client;
    char buffer[CAT_BUFFER_DEFAULT_SIZE];
    ssize_t n;

    ASSERT_NE(cat_socket_create(&client, CAT_SOCKET_TYPE_TCP), nullptr);
    DEFER(cat_socket_close(&client));
    ASSERT_TRUE(cat_socket_connect(&client, echo_tcp_server_ip, echo_tcp_server_ip_length, echo_tcp_server_port));
    ASSERT_TRUE(cat_socket_send(&client, CAT_STRL("Hello libcat")));
    ASSERT_EQ(cat_poll_one(cat_socket_get_fd_fast(&client), POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
    ASSERT_TRUE(cat_socket_check_liveness(&client));
    n = cat_socket_peek(&client, CAT_STRS(buffer));
    ASSERT_EQ(n, CAT_STRLEN("Hello libcat"));
    ASSERT_EQ(std::string(buffer, n), std::string("Hello libcat"));
    (void) cat_socket_read(&client, buffer, CAT_STRLEN("Hello libcat"));
}

TEST(cat_socket, peekfrom)
{
    TEST_REQUIRE(echo_udp_server != nullptr, cat_socket, echo_udp_server);
    cat_socket_t client;
    char buffer[CAT_BUFFER_DEFAULT_SIZE];
    ssize_t n;
    char ip[CAT_SOCKET_IP_BUFFER_SIZE];
    size_t ip_length;
    int port;

    ASSERT_NE(cat_socket_create(&client, CAT_SOCKET_TYPE_UDP), nullptr);
    DEFER(cat_socket_close(&client));

    ASSERT_TRUE(cat_socket_send_to(&client, CAT_STRL("Hello libcat"), echo_udp_server_ip, echo_udp_server_ip_length, echo_udp_server_port));
    {
        cat_sockaddr_union_t address;
        cat_socklen_t address_length = sizeof(address);
        ASSERT_EQ(cat_poll_one(cat_socket_get_fd_fast(&client), POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
        n = cat_socket_peekfrom(&client, CAT_STRS(buffer), &address.common, &address_length);
        ASSERT_EQ(n, CAT_STRLEN("Hello libcat"));
        ASSERT_EQ(std::string(buffer, n), std::string("Hello libcat"));
        ip_length = sizeof(ip);
        ASSERT_TRUE(cat_sockaddr_to_name(&address.common, address_length, ip, &ip_length, &port));
        ASSERT_EQ(std::string(ip, ip_length), std::string(echo_udp_server_ip, echo_udp_server_ip_length));
        ASSERT_EQ(port, echo_udp_server_port);
        (void) cat_socket_read_from(&client, CAT_STRS(buffer), nullptr, 0, nullptr);
    }

    ASSERT_TRUE(cat_socket_send_to(&client, CAT_STRL("Hello libcat"), echo_udp_server_ip, echo_udp_server_ip_length, echo_udp_server_port));
    {
        ASSERT_EQ(cat_poll_one(cat_socket_get_fd_fast(&client), POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
        ip_length = sizeof(ip);
        n = cat_socket_peek_from(&client, CAT_STRS(buffer), ip, &ip_length, &port);
        ASSERT_EQ(n, CAT_STRLEN("Hello libcat"));
        ASSERT_EQ(std::string(buffer, n), std::string("Hello libcat"));
        ASSERT_EQ(std::string(ip, ip_length), std::string(echo_udp_server_ip, echo_udp_server_ip_length));
        ASSERT_EQ(port, echo_udp_server_port);
        (void) cat_socket_read_from(&client, CAT_STRS(buffer), nullptr, 0, nullptr);
    }
}

static inline void test_tty(int fd)
{
    const char* msg;
    cat_socket_type_t type;
    switch(fd){
        case 0:
            msg = "stdin is not a tty";
            type = CAT_SOCKET_TYPE_STDIN;
            break;
        case 1:
            msg = "stdout is not a tty";
            type = CAT_SOCKET_TYPE_STDOUT;
            break;
        case 2:
            msg = "stderr is not a tty";
            type = CAT_SOCKET_TYPE_STDERR;
            break;
        default:
            GTEST_FATAL_FAILURE_("bad file descriptor");
    }
    cat_socket_t client;
    char buffer[TEST_BUFFER_SIZE_STD];
    cat_snrand(CAT_STRS(buffer) - 1);
    buffer[TEST_BUFFER_SIZE_STD - 1] = '\0';

    cat_socket_t *ptr = cat_socket_create(&client, type);
    if (ptr == nullptr) {
        SKIP_IF_(true, msg);
    }
    ASSERT_EQ(ptr, &client);
    DEFER(cat_socket_close(&client));
    ASSERT_TRUE(cat_socket_is_available(&client));
}

TEST(cat_socket, tty_stdin)
{
    ASSERT_NO_FATAL_FAILURE(test_tty(0));
}

TEST(cat_socket, tty_stdout)
{
    ASSERT_NO_FATAL_FAILURE(test_tty(1));
}

TEST(cat_socket, tty_stderr)
{
    ASSERT_NO_FATAL_FAILURE(test_tty(2));
}

TEST(cat_socket, dump_all_and_close_all)
{
    bool closed_all = false;

    // TODO: now all sockets are unavailable
    cat_socket_t *tcp_socket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_TCP);
    ASSERT_NE(nullptr, tcp_socket);
    DEFER(if (!closed_all) { cat_socket_close(tcp_socket); });

    cat_socket_t *udp_socket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_UDP);
    ASSERT_NE(nullptr, udp_socket);
    DEFER(if (!closed_all) { cat_socket_close(udp_socket); });

    cat_socket_t *pipe_socket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_PIPE);
    ASSERT_NE(nullptr, pipe_socket);
    DEFER(if (!closed_all) { cat_socket_close(pipe_socket); });

    if (uv_guess_handle(STDOUT_FILENO) == UV_TTY) {
        cat_socket_t *tty_socket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_STDOUT);
        ASSERT_NE(nullptr, tty_socket);
        DEFER(if (!closed_all) { cat_socket_close(tty_socket); });
    }

    /* test dump all */
    {
        testing::internal::CaptureStdout();
        cat_socket_dump_all();
        std::string output = testing::internal::GetCapturedStdout();
        ASSERT_NE(output.find("TCP"), std::string::npos);
        ASSERT_NE(output.find("UDP"), std::string::npos);
        ASSERT_TRUE(output.find("PIPE") != std::string::npos || output.find("UNIX") != std::string::npos);
        if (uv_guess_handle(STDOUT_FILENO) == UV_TTY) {
            ASSERT_TRUE(output.find("TTY") != std::string::npos || output.find("STDOUT") != std::string::npos);
        }
    }

    /* test close all */
    {
        cat_socket_close_all();
        closed_all = true;
        cat_event_wait();
        testing::internal::CaptureStdout();
        cat_socket_dump_all();
        std::string output = testing::internal::GetCapturedStdout();
        ASSERT_EQ(output.find("TCP"), std::string::npos);
        ASSERT_EQ(output.find("UDP"), std::string::npos);
        ASSERT_TRUE(output.find("PIPE") == std::string::npos && output.find("UNIX") == std::string::npos);
        if (uv_guess_handle(STDOUT_FILENO) == UV_TTY) {
            ASSERT_TRUE(output.find("TTY") == std::string::npos && output.find("STDOUT") == std::string::npos);
        }
    }
}
