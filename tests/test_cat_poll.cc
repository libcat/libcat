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

#define PREPARE_TCP_SOCKET(socket) \
    cat_socket_t socket; \
    cat_socket_fd_t fd; \
    \
    ASSERT_NE(nullptr, cat_socket_create(&socket, CAT_SOCKET_TYPE_TCP)); \
    DEFER(cat_socket_close(&socket)); \
    ASSERT_TRUE(cat_socket_connect_to(&socket, echo_tcp_server_ip, echo_tcp_server_ip_length, echo_tcp_server_port)); \
    fd = cat_socket_get_fd_fast(&socket);

TEST(cat_poll, base)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    PREPARE_TCP_SOCKET(socket);

    ASSERT_EQ(cat_poll_one(fd, POLLOUT, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
    ASSERT_TRUE(cat_socket_send(&socket, CAT_STRL("Hello libcat")));
    ASSERT_EQ(cat_poll_one(fd, POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
    do {
        /** @record: test multi events callback bug */
        cat_pollfd_events_t revents;
        ASSERT_EQ(cat_poll_one(fd, POLLIN|POLLOUT, &revents, TEST_IO_TIMEOUT), CAT_RET_OK);
        ASSERT_EQ(revents, POLLIN|POLLOUT);
    } while (0);
    char buffer[CAT_STRLEN("Hello libcat")];
    ASSERT_EQ(cat_socket_read(&socket, buffer, sizeof(buffer)), sizeof(buffer));
    ASSERT_EQ(cat_poll_one(fd, POLLOUT, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
    ASSERT_TRUE(cat_socket_send(&socket, CAT_STRL("RESET")));
    ASSERT_EQ(cat_poll_one(fd, POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
    ASSERT_FALSE(cat_socket_check_liveness(&socket));
}

// FIXME
TEST(cat_poll, server)
{
    SKIP_IF_(true, "Need to fix");
    cat_socket_t server;
    ASSERT_NE(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP), nullptr);
    DEFER(cat_socket_close(&server));
    ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
    ASSERT_EQ(cat_poll_one(cat_socket_get_fd(&server), POLLIN, nullptr, 1), CAT_RET_NONE);
    cat_socket_t client;
    ASSERT_NE(cat_socket_create(&client, CAT_SOCKET_TYPE_TCP), nullptr);
    DEFER(cat_socket_close(&client));
    ASSERT_TRUE(cat_socket_connect_to(&client, TEST_LISTEN_IPV4, CAT_STRLEN(TEST_LISTEN_IPV4), cat_socket_get_sock_port(&server)));
    ASSERT_EQ(cat_time_usleep(10 * 1000), 0);
    ASSERT_EQ(cat_poll_one(cat_socket_get_fd(&server), POLLIN, nullptr, 1), CAT_RET_OK);
}

TEST(cat_poll, duplicate)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    PREPARE_TCP_SOCKET(socket);

    {
        wait_group wg;
        for (size_t n = 10; n--;) {
            co([&] {
                wg++;
                DEFER(wg--);
                ASSERT_EQ(cat_poll_one(fd, POLLOUT, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
            });
        }
    }

    {
        wait_group wg;
        for (size_t n = 10; n--;) {
            co([&] {
                wg++;
                DEFER(wg--);
                cat_pollfd_t pollfd;
                pollfd.fd = fd;
                pollfd.events = POLLOUT;
                pollfd.revents = 0;
                ASSERT_EQ(cat_poll(&pollfd, 1, TEST_IO_TIMEOUT), 1);
                ASSERT_EQ(pollfd.revents, POLLOUT);
            });
        }
    }
}

TEST(cat_poll, defer_bug)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    PREPARE_TCP_SOCKET(socket);

    ASSERT_EQ(cat_poll_one(fd, POLLOUT, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
    ASSERT_TRUE(cat_socket_send(&socket, CAT_STRL("Hello libcat")));
    // make sure it's readable before real test
    ASSERT_EQ(cat_poll_one(fd, POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);

    wait_group wg;
    cat_coroutine_t *poller = NULL;
    co([&] {
        wg++;
        DEFER(wg--);
        ASSERT_EQ(cat_poll_one(fd, POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
        ASSERT_NE(poller, nullptr);
        /* it must be done before poller, cancel the poller,
         * poll_one_callback() of poller has been called,
         * but defer task not yet. */
        cat_coroutine_resume(poller, nullptr, nullptr);
    });
    // cancel it soon
    poller = co([&] {
        wg++;
        DEFER(wg--);
        /* although poller was cancelled by other coroutine,
         * but status and revents has been set, so this call is success. */
        ASSERT_EQ(cat_poll_one(fd, POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
    });
    /* we expect that poll_one_done_callback() will not be called,
     * otherwise, memory error will occur. */
    ASSERT_TRUE(wg());
    char buffer[CAT_STRLEN("Hello libcat")];
    ASSERT_EQ(cat_socket_read(&socket, buffer, sizeof(buffer)), sizeof(buffer));
}

static cat_ret_t select_is_able(cat_socket_fd_t fd, int type)
{
    struct timeval timeout;
    fd_set readfds, writefds, exceptfds;
    timeout.tv_sec = (cat_timeval_sec_t) TEST_IO_TIMEOUT;
    timeout.tv_usec = 0;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    switch (type) {
        case 0:
            FD_SET(fd, &readfds);
            break;
        case 1:
            FD_SET(fd, &writefds);
            break;
    }
    int ret = cat_select((int) (fd + 1), &readfds, &writefds, nullptr, &timeout);
    if (ret < 0) {
        return CAT_RET_ERROR;
    }
    if (ret == 0) {
        return CAT_RET_NONE;
    }
    switch (type) {
        case 0:
            return FD_ISSET(fd, &readfds) ? CAT_RET_OK : CAT_RET_NONE;
        case 1:
            return FD_ISSET(fd, &writefds) ? CAT_RET_OK : CAT_RET_NONE;
    }
    CAT_NEVER_HERE("Invalid type");
}

#define select_is_readable(fd) select_is_able(fd, 0)
#define select_is_writable(fd) select_is_able(fd, 1)

TEST(cat_select, base)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    PREPARE_TCP_SOCKET(socket);

    ASSERT_TRUE(select_is_writable(fd));
    ASSERT_TRUE(cat_socket_send(&socket, CAT_STRL("Hello libcat")));
    ASSERT_TRUE(select_is_readable(fd));
    char buffer[CAT_STRLEN("Hello libcat")];
    ASSERT_EQ(cat_socket_read(&socket, buffer, sizeof(buffer)), sizeof(buffer));
    ASSERT_TRUE(select_is_writable(fd));
    ASSERT_TRUE(cat_socket_send(&socket, CAT_STRL("RESET")));
    ASSERT_TRUE(select_is_readable(fd));
    ASSERT_FALSE(cat_socket_check_liveness(&socket));
}

TEST(cat_select, error)
{
    ASSERT_LT(cat_select(-1, nullptr, nullptr, nullptr, nullptr), 0);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

TEST(cat_select, empty)
{
    ASSERT_EQ(cat_select(0, nullptr, nullptr, nullptr, nullptr), 0);
}
