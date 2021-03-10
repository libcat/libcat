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
    ASSERT_FALSE(cat_sockaddr_check(&info->address.common, info->length - 1));
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
    ASSERT_GT(fd, 0);
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
    ASSERT_GT(fd, 0);
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
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_FALSE(cat_socket_check_liveness(&socket));
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
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_set_tcp_nodelay(&socket, cat_true));
}

TEST(cat_socket, set_tcp_keepalive)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_set_tcp_keepalive(&socket, cat_true, 1));
}

TEST(cat_socket, set_tcp_accept_balance)
{
    cat_socket_t socket;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_set_tcp_accept_balance(&socket, cat_true));
}

cat_coroutine_t *echo_tcp_server;
char echo_tcp_server_ip[CAT_SOCKET_IPV6_BUFFER_SIZE];
size_t echo_tcp_server_ip_length = CAT_SOCKET_IPV6_BUFFER_SIZE;
int echo_tcp_server_port;

TEST(cat_socket, echo_tcp_server)
{
    co([] {
        cat_socket_t server;

        ASSERT_NE(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP), nullptr);
        ASSERT_TRUE(cat_socket_bind(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
        ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
        ASSERT_TRUE(cat_socket_get_sock_address(&server, echo_tcp_server_ip, &echo_tcp_server_ip_length));
        ASSERT_GT(echo_tcp_server_port = cat_socket_get_sock_port(&server), 0);

        echo_tcp_server = cat_coroutine_get_current();
        cat_socket_set_read_timeout(&server, -1);

        while (1) {
            cat_socket_t *client = cat_socket_accept(&server, nullptr);

            if (client == nullptr) {
                EXPECT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
                break;
            }
            cat_coroutine_run(nullptr, [](cat_data_t *data)->cat_data_t* {
                cat_socket_t *client = (cat_socket_t *) data;

                while (true) {
                    char read_buffer[TEST_BUFFER_SIZE_STD];
                    ssize_t read_n = cat_socket_recv(client, CAT_STRS(read_buffer));
                    if (read_n > 0) {
                        bool send_ok = cat_socket_send(client, read_buffer, read_n);
                        EXPECT_TRUE(send_ok);
                        if (send_ok) {
                            continue;
                        }
                    } else {
                        EXPECT_EQ(read_n, 0);
                    }
                    break;
                }

                cat_socket_close(client);

                return nullptr;
            }, client);
        }

        cat_socket_close(&server);
        echo_tcp_server = nullptr;
    });
    ASSERT_NE(echo_tcp_server, nullptr);
}

TEST(cat_socket, echo_tcp_client)
{
    SKIP_IF(echo_tcp_server == nullptr);
    cat_socket_t echo_client;
    size_t n;
    ssize_t ret;

    ASSERT_NE(cat_socket_create(&echo_client, CAT_SOCKET_TYPE_TCP), nullptr);
    DEFER(cat_socket_close(&echo_client));

    ASSERT_TRUE(cat_socket_connect(&echo_client, CAT_STRL(TEST_LISTEN_IPV4), echo_tcp_server_port));

    /* normal loop */
    for (n = TEST_MAX_REQUESTS; n--;) {
        char read_buffer[TEST_BUFFER_SIZE_STD];
        char write_buffer[TEST_BUFFER_SIZE_STD];
        /* send request */
        cat_snrand(CAT_STRS(write_buffer));
        ASSERT_TRUE(cat_socket_send(&echo_client, CAT_STRS(write_buffer)));
        /* recv response */
        ret = cat_socket_read(&echo_client, CAT_STRS(read_buffer));
        ASSERT_EQ(ret, (ssize_t) sizeof(read_buffer));
        read_buffer[sizeof(read_buffer) - 1] = '\0';
        write_buffer[sizeof(write_buffer) - 1] = '\0';
        ASSERT_STREQ(read_buffer, write_buffer);
    }

    /* pipeline */
    do {
        char **read_buffers = (char **) cat_malloc(TEST_MAX_REQUESTS * sizeof(*read_buffers));
        char **write_buffers = (char **) cat_malloc(TEST_MAX_REQUESTS * sizeof(*write_buffers));
        bool done = false;
        ASSERT_NE(read_buffers, nullptr);
        ASSERT_NE(write_buffers, nullptr);
        /* generate write data */
        for (n = 0; n < TEST_MAX_REQUESTS; n++) {
            write_buffers[n] = (char *) cat_malloc(TEST_BUFFER_SIZE_STD);
            ASSERT_NE(write_buffers[n], nullptr);
            cat_snrand(write_buffers[n], TEST_BUFFER_SIZE_STD);
        }
        /* send requests */
        co([&] {
            for (size_t n = 0; n < TEST_MAX_REQUESTS; n++) {
                ASSERT_TRUE(cat_socket_send(&echo_client, write_buffers[n], TEST_BUFFER_SIZE_STD));
            }
            done = true;
        });
        /* recv responses */
        for (n = 0; n < TEST_MAX_REQUESTS; n++) {
            read_buffers[n] = (char *) cat_malloc(TEST_BUFFER_SIZE_STD);
            ASSERT_NE(read_buffers[n], nullptr);
            ret = cat_socket_read(&echo_client, read_buffers[n], TEST_BUFFER_SIZE_STD);
            ASSERT_EQ(ret, TEST_BUFFER_SIZE_STD);
            read_buffers[n][TEST_BUFFER_SIZE_STD - 1] = '\0';
            write_buffers[n][TEST_BUFFER_SIZE_STD - 1] = '\0';
            ASSERT_STREQ(read_buffers[n], write_buffers[n]);
            cat_free(read_buffers[n]);
            cat_free(write_buffers[n]);
        }
        cat_free(read_buffers);
        cat_free(write_buffers);
        ASSERT_TRUE(done);
    } while (0);
}

TEST(cat_socket, send_yield)
{
    cat_socket_t client;

    ASSERT_NE(cat_socket_create(&client, CAT_SOCKET_TYPE_TCP), nullptr);
    DEFER(cat_socket_close(&client));

    ASSERT_TRUE(cat_socket_connect(&client, CAT_STRL(TEST_LISTEN_IPV4), echo_tcp_server_port));

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
    SKIP_IF(is_offline());
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

    ASSERT_TRUE(cat_socket_send(&socket, CAT_STRL(
        "GET / HTTP/1.1\r\n"
        "Host: " TEST_REMOTE_HTTP_SERVER_HOST "\r\n"
        "User-Agent: " TEST_HTTP_CLIENT_FAKE_USERAGENT "\r\n"
        "\r\n"
    )));

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
    SKIP_IF(is_offline());
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
    SKIP_IF(is_offline());
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

TEST(cat_socket, dump_all)
{
    // TODO: now all sockets are unavailable
    cat_socket_t *tcp_socket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_TCP);
    ASSERT_NE(nullptr, tcp_socket);
    DEFER(cat_socket_close(tcp_socket));

    cat_socket_t *udp_socket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_UDP);
    ASSERT_NE(nullptr, udp_socket);
    DEFER(cat_socket_close(udp_socket));

    cat_socket_t *pipe_socket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_PIPE);
    ASSERT_NE(nullptr, pipe_socket);
    DEFER(cat_socket_close(pipe_socket));

    if (uv_guess_handle(STDOUT_FILENO) == UV_TTY) {
        cat_socket_t *tty_socket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_STDOUT);
        ASSERT_NE(nullptr, tty_socket);
        DEFER(cat_socket_close(tty_socket));
    }

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

TEST(cat_socket, echo_tcp_server_shutdown)
{
    SKIP_IF(echo_tcp_server == nullptr);
    cat_coroutine_resume(echo_tcp_server, nullptr, nullptr);
    ASSERT_EQ(echo_tcp_server, nullptr);
}
