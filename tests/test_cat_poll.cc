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
  +--------------------------------------------------------------------------+
 */

#include "test.h"

extern cat_coroutine_t *echo_tcp_server;
extern char echo_tcp_server_ip[CAT_SOCKET_IPV6_BUFFER_SIZE];
extern size_t echo_tcp_server_ip_length;
extern int echo_tcp_server_port;

extern TEST_REQUIREMENT_DTOR(cat_socket, echo_tcp_server);
extern TEST_REQUIREMENT(cat_socket, echo_tcp_server);

TEST(cat_poll, init_failed)
{
    ASSERT_EQ(cat_poll_one(CAT_OS_INVALID_FD, POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_ERROR);
#ifdef CAT_OS_UNIX_LIKE
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
#else
    // TODO: ?
#endif
}

#ifdef CAT_OS_UNIX_LIKE
TEST(cat_poll, start_failed)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    cat_socket_t socket;
    cat_socket_fd_t fd;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_connect(&socket, echo_tcp_server_ip, echo_tcp_server_ip_length, echo_tcp_server_port));
    fd = cat_socket_get_fd_fast(&socket);

    co([fd] {
        ASSERT_TRUE(cat_time_delay(0));
        ASSERT_EQ(cat_poll_one(fd, POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_ERROR);
        ASSERT_EQ(cat_get_last_error_code(), CAT_EEXIST);
    });
    ASSERT_EQ(cat_poll_one(fd, POLLIN, nullptr, 1), CAT_RET_NONE);

    co([fd] {
        cat_pollfd_t pollfd;
        pollfd.fd = fd;
        pollfd.events = POLLIN;
        pollfd.revents = 0;
        ASSERT_TRUE(cat_time_delay(0));
        ASSERT_EQ(cat_poll(&pollfd, 1, TEST_IO_TIMEOUT), 1);
        ASSERT_EQ(pollfd.revents, POLLNONE);
    });
    ASSERT_EQ(cat_poll_one(fd, POLLIN, nullptr, 1), CAT_RET_NONE);
}
#endif

TEST(cat_poll, base)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    cat_socket_t socket;
    cat_socket_fd_t fd;

    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP));
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_connect(&socket, echo_tcp_server_ip, echo_tcp_server_ip_length, echo_tcp_server_port));
    fd = cat_socket_get_fd_fast(&socket);

    ASSERT_EQ(cat_poll_one(fd, POLLOUT, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
    ASSERT_TRUE(cat_socket_send(&socket, CAT_STRL("Hello libcat")));
    ASSERT_EQ(cat_poll_one(fd, POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
    char buffer[CAT_STRLEN("Hello libcat")];
    ASSERT_EQ(cat_socket_read(&socket, buffer, sizeof(buffer)), sizeof(buffer));
    ASSERT_EQ(cat_poll_one(fd, POLLOUT, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
    ASSERT_TRUE(cat_socket_send(&socket, CAT_STRL("RESET")));
    ASSERT_EQ(cat_poll_one(fd, POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
    ASSERT_FALSE(cat_socket_check_liveness(&socket));
}
