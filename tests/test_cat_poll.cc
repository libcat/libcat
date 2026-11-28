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

#ifdef CAT_OS_UNIX_LIKE
#include <sys/resource.h>
#include <unistd.h>
/* cat_api.h does not aggregate the interrupt header yet */
#include "cat_interrupt.h"
#endif

#ifdef CAT_OS_LINUX
TEST(cat_poll, tcp_priority_events_and_select_exceptfds)
{
    cat_socket_t server, sender, receiver;
    ASSERT_EQ(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP4), &server);
    DEFER(cat_socket_close(&server));
    ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
    ASSERT_EQ(cat_socket_create(&sender, CAT_SOCKET_TYPE_TCP4), &sender);
    DEFER(cat_socket_close(&sender));
    ASSERT_TRUE(cat_socket_connect_to(&sender, CAT_STRL(TEST_LISTEN_IPV4), cat_socket_get_sock_port(&server)));
    ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_TCP), &receiver);
    DEFER(cat_socket_close(&receiver));
    ASSERT_TRUE(cat_socket_accept(&server, &receiver));
    int fd = cat_socket_get_fd(&receiver);
    ASSERT_GE(fd, 0);
    ASSERT_LT(fd, FD_SETSIZE);
    ASSERT_EQ(send(cat_socket_get_fd(&sender), "!", 1, MSG_OOB), 1);
    cat_pollfd_events_t events = 0;
    ASSERT_EQ(cat_poll_one(fd, POLLPRI, &events, TEST_IO_TIMEOUT), CAT_RET_OK);
    ASSERT_NE(events & POLLPRI, 0);
    cat_pollfd_t descriptor = {fd, POLLPRI, 0};
    ASSERT_EQ(cat_poll(&descriptor, 1, TEST_IO_TIMEOUT), 1);
    ASSERT_NE(descriptor.revents & POLLPRI, 0);
    fd_set exceptfds;
    FD_ZERO(&exceptfds);
    FD_SET(fd, &exceptfds);
    struct timeval timeout = {1, 0};
    ASSERT_EQ(cat_select(fd + 1, nullptr, nullptr, &exceptfds, &timeout), 1);
    ASSERT_TRUE(FD_ISSET(fd, &exceptfds));
    ASSERT_TRUE(cat_socket_send(&sender, CAT_STRL("normal")));
    ASSERT_EQ(cat_poll_one(fd, POLLIN, &events, TEST_IO_TIMEOUT), CAT_RET_OK);
    fd_set readfds, writefds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    FD_SET(fd, &readfds);
    FD_SET(fd, &writefds);
    FD_SET(fd, &exceptfds);
    // select counts ready bits across all three sets, even for one descriptor.
    ASSERT_EQ(cat_select(fd + 1, &readfds, &writefds, &exceptfds, &timeout), 3);
    ASSERT_TRUE(FD_ISSET(fd, &readfds));
    ASSERT_TRUE(FD_ISSET(fd, &writefds));
    ASSERT_TRUE(FD_ISSET(fd, &exceptfds));
    char byte = 0;
    ASSERT_EQ(recv(fd, &byte, 1, MSG_OOB), 1);
    ASSERT_EQ(byte, '!');
    ASSERT_EQ(cat_poll_one(fd, POLLPRI, &events, 0), CAT_RET_NONE);
    ASSERT_EQ(events, 0);
    char buffer[CAT_STRLEN("normal")];
    ASSERT_EQ(cat_socket_read(&receiver, buffer, sizeof(buffer)), sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "normal");
    int closed_fd = dup(fd);
    ASSERT_GE(closed_fd, 0);
    ASSERT_LT(closed_fd, FD_SETSIZE);
    ASSERT_EQ(close(closed_fd), 0);
    FD_ZERO(&readfds);
    FD_SET(closed_fd, &readfds);
    ASSERT_EQ(cat_select(closed_fd + 1, &readfds, nullptr, nullptr, &timeout), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
}
#endif

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

#define PREPARE_TCP_SOCKET(socket, fd) \
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
    PREPARE_TCP_SOCKET(socket, fd);

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

// FIXME: hard to fix on Windows
TEST(cat_poll, server)
{
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
    cat_pollfd_t descriptor = {cat_socket_get_fd(&server), POLLIN, POLLNONE};
    ASSERT_EQ(cat_poll(&descriptor, 1, 0), 1);
    ASSERT_EQ(descriptor.revents, POLLIN);
    cat_socket_t connection;
    ASSERT_EQ(cat_socket_create(&connection, CAT_SOCKET_TYPE_TCP), &connection);
    DEFER(cat_socket_close(&connection));
    ASSERT_TRUE(cat_socket_accept(&server, &connection));
    ASSERT_TRUE(cat_socket_send(&client, CAT_STRL("data")));
    char buffer[CAT_STRLEN("data")];
    ASSERT_EQ(cat_socket_read(&connection, buffer, sizeof(buffer)), sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "data");
    ASSERT_EQ(cat_poll(&descriptor, 1, 0), 0);
    ASSERT_EQ(descriptor.revents, POLLNONE);
}

TEST(cat_poll, listeners_registered_in_reverse_descriptor_order)
{
    cat_socket_t first, second;
    ASSERT_EQ(cat_socket_create(&first, CAT_SOCKET_TYPE_TCP4), &first);
    DEFER(cat_socket_close(&first));
    ASSERT_EQ(cat_socket_create(&second, CAT_SOCKET_TYPE_TCP4), &second);
    DEFER(cat_socket_close(&second));
    ASSERT_TRUE(cat_socket_bind_to(&first, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_bind_to(&second, CAT_STRL(TEST_LISTEN_IPV4), 0));
    std::array<cat_socket_t *, 2> listeners = {&first, &second};
    if (cat_socket_get_fd(listeners[0]) > cat_socket_get_fd(listeners[1])) {
        std::swap(listeners[0], listeners[1]);
    }
    ASSERT_LT(cat_socket_get_fd(listeners[0]), cat_socket_get_fd(listeners[1]));
    ASSERT_TRUE(cat_socket_listen(listeners[1], TEST_SERVER_BACKLOG));
    ASSERT_TRUE(cat_socket_listen(listeners[0], TEST_SERVER_BACKLOG));
    for (cat_socket_t *listener : listeners) {
    for (bool multiple : {false, true}) {
        SCOPED_TRACE(multiple);
        cat_socket_t client, connection;
        ASSERT_EQ(cat_socket_create(&client, CAT_SOCKET_TYPE_TCP4), &client);
        DEFER(cat_socket_close(&client));
        ASSERT_EQ(cat_socket_create(&connection, CAT_SOCKET_TYPE_TCP), &connection);
        DEFER(cat_socket_close(&connection));
        cat_pollfd_t descriptor = {cat_socket_get_fd(listener), POLLIN, POLLNONE};
        cat_pollfd_events_t revents = POLLNONE;
        ASSERT_TRUE(cat_socket_connect_to(&client, CAT_STRL(TEST_LISTEN_IPV4), cat_socket_get_sock_port(listener)));
        // Windows AcceptEx can consume readiness before uv_poll observes it.
        // Wait for a pending accept, then exercise the listener's descriptor lookup.
        const cat_msec_t deadline = cat_time_msec2() + TEST_IO_TIMEOUT;
        int ready;
        do {
            ready = multiple ? cat_poll(&descriptor, 1, 0)
                : cat_poll_one(descriptor.fd, POLLIN, &revents, 0);
            ASSERT_GE(ready, 0);
            if (ready == 0) {
                ASSERT_LT(cat_time_msec2(), deadline);
                ASSERT_EQ(cat_time_msleep(0), 0);
            }
        } while (ready == 0);
        ASSERT_EQ(ready, 1);
        ASSERT_EQ(multiple ? descriptor.revents : revents, POLLIN);
        ASSERT_TRUE(cat_socket_accept(listener, &connection));
        ASSERT_TRUE(cat_socket_send(&client, CAT_STRL("data")));
        char buffer[CAT_STRLEN("data")];
        ASSERT_EQ(cat_socket_read(&connection, buffer, sizeof(buffer)), sizeof(buffer));
        ASSERT_EQ(std::string(buffer, sizeof(buffer)), "data");
    }
    }
}

TEST(cat_poll, duplicate)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    PREPARE_TCP_SOCKET(socket, fd);

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
    PREPARE_TCP_SOCKET(socket, fd);

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
        /* although poller was canceled by other coroutine,
         * but status and revents has been set, so this call is success. */
        ASSERT_EQ(cat_poll_one(fd, POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
    });
    /* we expect that poll_one_done_callback() will not be called,
     * otherwise, memory error will occur. */
    ASSERT_TRUE(wg());
    char buffer[CAT_STRLEN("Hello libcat")];
    ASSERT_EQ(cat_socket_read(&socket, buffer, sizeof(buffer)), sizeof(buffer));
}

typedef int (*test_select_function_t)(cat_os_socket_t nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

static cat_ret_t select_is_xxx_able(test_select_function_t select_function, cat_socket_fd_t fd, int type)
{
    struct timeval timeout;
    fd_set readfds, writefds, exceptfds;
    timeout.tv_sec = (cat_timeval_sec_t) (TEST_IO_TIMEOUT / 1000);
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
    int ret = select_function((int) (fd + 1), &readfds, &writefds, nullptr, &timeout);
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

#define select_is_readable(select_function, fd) select_is_xxx_able(select_function, fd, 0)
#define select_is_writable(select_function, fd) select_is_xxx_able(select_function, fd, 1)

TEST(cat_select, base)
{
    TEST_REQUIRE(echo_tcp_server != nullptr, cat_socket, echo_tcp_server);
    PREPARE_TCP_SOCKET(socket, fd);
    ASSERT_TRUE(select_is_writable(cat_select, fd));
    ASSERT_TRUE(select_is_writable(cat_sys_select, fd));
    ASSERT_TRUE(cat_socket_send(&socket, CAT_STRL("Hello libcat")));
    ASSERT_TRUE(select_is_readable(cat_select, fd));
    ASSERT_TRUE(select_is_readable(cat_sys_select, fd));
    fd_set readfds, writefds;
    for (test_select_function_t select_function : {cat_sys_select, cat_select}) {
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_SET(fd, &readfds);
        FD_SET(fd, &writefds);
        struct timeval timeout = {1, 0};
        ASSERT_EQ(select_function(fd + 1, &readfds, &writefds, nullptr, &timeout), 2);
        ASSERT_TRUE(FD_ISSET(fd, &readfds));
        ASSERT_TRUE(FD_ISSET(fd, &writefds));
    }
    char buffer[CAT_STRLEN("Hello libcat")];
    ASSERT_EQ(cat_socket_read(&socket, buffer, sizeof(buffer)), sizeof(buffer));
    ASSERT_TRUE(select_is_writable(cat_select, fd));
    ASSERT_TRUE(select_is_writable(cat_sys_select, fd));
    ASSERT_TRUE(cat_socket_send(&socket, CAT_STRL("RESET")));
    ASSERT_TRUE(select_is_readable(cat_select, fd));
    ASSERT_TRUE(select_is_readable(cat_sys_select, fd));
    ASSERT_FALSE(cat_socket_check_liveness(&socket));
}

TEST(cat_select, error)
{
    ASSERT_LT(cat_select((cat_os_socket_t) -1, nullptr, nullptr, nullptr, nullptr), 0);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_LT(cat_sys_select((cat_os_socket_t) -1, nullptr, nullptr, nullptr, nullptr), 0);
    ASSERT_EQ(cat_translate_sys_error(cat_sys_errno), CAT_EINVAL);
}

TEST(cat_select, empty)
{
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000;
    ASSERT_EQ(cat_select(0, nullptr, nullptr, nullptr, &timeout), 0);
    ASSERT_EQ(cat_sys_select((cat_os_socket_t) 0, nullptr, nullptr, nullptr, &timeout), 0);
}

TEST(cat_select, canceled_empty_wait)
{
    struct timeval timeout;
    timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(TEST_IO_TIMEOUT / 1000);
    timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>((TEST_IO_TIMEOUT % 1000) * 1000);
    int result = 0;
    cat_errno_t error = 0;
    bool done = false;
    wait_group wg;
    cat_coroutine_t *waiter = co([&] {
        wg++;
        DEFER(wg--);
        cat_clear_last_error();
        result = cat_select(0, nullptr, nullptr, nullptr, &timeout);
        error = cat_get_last_error_code();
        done = true;
    });
    DEFER(if (!done) { cat_coroutine_resume(waiter, nullptr, nullptr); } wg(TEST_IO_TIMEOUT));
    ASSERT_FALSE(done);
    ASSERT_TRUE(cat_coroutine_resume(waiter, nullptr, nullptr));
    ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
    ASSERT_TRUE(done);
    ASSERT_EQ(result, -1);
    ASSERT_EQ(error, CAT_ECANCELED);
    timeout = {0, 0};
    ASSERT_EQ(cat_select(0, nullptr, nullptr, nullptr, &timeout), 0);
}

TEST(cat_poll, invalid_descriptors)
{
    cat_pollfd_t descriptors[] = {
        {CAT_OS_INVALID_SOCKET, POLLIN, POLLNONE},
        {CAT_OS_INVALID_SOCKET, POLLOUT, POLLNONE},
    };
    ASSERT_EQ(cat_poll(descriptors, CAT_ARRAY_SIZE(descriptors), 0), 2);
    // Invalid-handle events vary across libuv platforms; they must report errors, not readiness.
    ASSERT_NE(descriptors[0].revents & (POLLERR | POLLNVAL), 0);
    ASSERT_NE(descriptors[1].revents & (POLLERR | POLLNVAL), 0);
}

/* A count whose allocation size wraps must be rejected before the descriptors
 * are read; the caller only owns the one descriptor below. */
TEST(cat_poll, a_wrapped_descriptor_count_is_rejected)
{
    cat_pollfd_t descriptor = { CAT_OS_INVALID_SOCKET, POLLIN, POLLNONE };
    /* one past the int result range; the caller array holds one descriptor */
    cat_nfds_t nfds = (cat_nfds_t) INT_MAX + 1;
    ASSERT_EQ(cat_poll(&descriptor, nfds, 0), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EOVERFLOW);
}

TEST(cat_poll, datagram_ready_and_timeout)
{
    cat_socket_t socket;
    ASSERT_EQ(cat_socket_create(&socket, CAT_SOCKET_TYPE_UDP4), &socket);
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind_to(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    cat_pollfd_t descriptor = {cat_socket_get_fd(&socket), POLLOUT, POLLNONE};
    ASSERT_EQ(cat_poll(&descriptor, 1, TEST_IO_TIMEOUT), 1);
    ASSERT_NE(descriptor.revents & POLLOUT, 0);
    descriptor.events = POLLIN;
    ASSERT_EQ(cat_poll(&descriptor, 1, 1), 0);
    ASSERT_EQ(descriptor.revents, POLLNONE);
}

TEST(cat_poll, canceled_wait_then_descriptor_reuse)
{
    for (bool multiple : {false, true}) {
        SCOPED_TRACE(multiple);
        cat_socket_t first, second;
        ASSERT_EQ(cat_socket_create(&first, CAT_SOCKET_TYPE_UDP4), &first);
        DEFER(cat_socket_close(&first));
        ASSERT_EQ(cat_socket_create(&second, CAT_SOCKET_TYPE_UDP4), &second);
        DEFER(cat_socket_close(&second));
        ASSERT_TRUE(cat_socket_bind_to(&first, CAT_STRL(TEST_LISTEN_IPV4), 0));
        ASSERT_TRUE(cat_socket_bind_to(&second, CAT_STRL(TEST_LISTEN_IPV4), 0));
        cat_pollfd_t descriptors[] = {
            {cat_socket_get_fd(&first), POLLIN, POLLNONE},
            {cat_socket_get_fd(&second), POLLIN, POLLNONE},
        };
        int result = 0;
        cat_pollfd_events_t revents = POLLNONE;
        cat_errno_t error = 0;
        bool done = false;
        wait_group wg;
        cat_coroutine_t *poller = co([&] {
            wg++;
            DEFER(wg--);
            cat_clear_last_error();
            result = multiple ? cat_poll(descriptors, CAT_ARRAY_SIZE(descriptors), TEST_IO_TIMEOUT)
                : cat_poll_one(descriptors[0].fd, POLLIN, &revents, TEST_IO_TIMEOUT);
            error = cat_get_last_error_code();
            done = true;
        });
        DEFER(if (!done) { cat_coroutine_resume(poller, nullptr, nullptr); });
        ASSERT_FALSE(done);
        ASSERT_TRUE(cat_coroutine_resume(poller, nullptr, nullptr));
        ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
        ASSERT_TRUE(done);
        // Multi-poll returns the ready count on cancellation, including zero.
        ASSERT_EQ(result, multiple ? 0 : CAT_RET_ERROR);
        ASSERT_EQ(error, multiple ? 0 : CAT_ECANCELED);
        ASSERT_EQ(revents, POLLNONE);
        ASSERT_EQ(descriptors[0].revents, POLLNONE);
        ASSERT_EQ(descriptors[1].revents, POLLNONE);
        ASSERT_TRUE(cat_socket_send_to(&first, CAT_STRL("first"), CAT_STRL(TEST_LISTEN_IPV4), cat_socket_get_sock_port(&first)));
        ASSERT_TRUE(cat_socket_send_to(&second, CAT_STRL("second"), CAT_STRL(TEST_LISTEN_IPV4), cat_socket_get_sock_port(&second)));
        // A completed UDP send does not guarantee both packets are readable in the same event-loop iteration.
        ASSERT_EQ(cat_poll_one(descriptors[0].fd, POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
        ASSERT_EQ(cat_poll_one(descriptors[1].fd, POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
        ASSERT_EQ(cat_poll(descriptors, CAT_ARRAY_SIZE(descriptors), TEST_IO_TIMEOUT), 2);
        ASSERT_EQ(descriptors[0].revents, POLLIN);
        ASSERT_EQ(descriptors[1].revents, POLLIN);
        char buffer[TEST_BUFFER_SIZE_STD];
        ASSERT_EQ(cat_socket_recv(&first, buffer, sizeof(buffer)), CAT_STRLEN("first"));
        ASSERT_EQ(std::string(buffer, CAT_STRLEN("first")), "first");
        ASSERT_EQ(cat_socket_recv(&second, buffer, sizeof(buffer)), CAT_STRLEN("second"));
        ASSERT_EQ(std::string(buffer, CAT_STRLEN("second")), "second");
    }
}

#ifdef CAT_OS_UNIX_LIKE
TEST(cat_poll, regular_file_readiness_preserves_descriptor)
{
    std::string path = get_random_path();
    ASSERT_EQ(cat_fs_put_contents(path.c_str(), CAT_STRL("file-data")), CAT_STRLEN("file-data"));
    DEFER(cat_fs_unlink(path.c_str()));
    cat_file_t fd = cat_fs_open(path.c_str(), CAT_FS_OPEN_FLAG_RDONLY);
    ASSERT_GE(fd, 0);
    DEFER(cat_fs_close(fd));
    int next_fd = dup(fd);
    ASSERT_GE(next_fd, 0);
    ASSERT_EQ(close(next_fd), 0);
    cat_pollfd_events_t revents = POLLNONE;
    cat_ret_t ready = cat_poll_one(fd, POLLIN, &revents, TEST_IO_TIMEOUT);
    if (ready == CAT_RET_ERROR) {
        ASSERT_NE(cat_get_last_error_code(), 0);
    } else {
        ASSERT_EQ(ready, CAT_RET_OK);
    }
    ASSERT_NE(revents & POLLIN, 0);
    cat_pollfd_t descriptors[] = {
        {fd, POLLIN, POLLNONE},
        {fd, POLLIN, POLLNONE},
    };
    ASSERT_EQ(cat_poll(descriptors, CAT_ARRAY_SIZE(descriptors), TEST_IO_TIMEOUT), 2);
    ASSERT_NE(descriptors[0].revents & POLLIN, 0);
    ASSERT_NE(descriptors[1].revents & POLLIN, 0);
    if (ready == CAT_RET_ERROR) {
        // Initialization failures must release duplicates without closing the caller's fd.
        int probe = dup(fd);
        ASSERT_GE(probe, 0);
        DEFER(close(probe));
        ASSERT_EQ(probe, next_fd);
    }
    char buffer[CAT_STRLEN("file-data")];
    ASSERT_EQ(cat_fs_read(fd, buffer, sizeof(buffer)), sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "file-data");
}

TEST(cat_poll, descriptor_limit_errors_preserve_original_fd)
{
    std::string path = get_random_path();
    ASSERT_EQ(cat_fs_put_contents(path.c_str(), CAT_STRL("data")), CAT_STRLEN("data"));
    DEFER(cat_fs_unlink(path.c_str()));
    int fd = open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    DEFER(close(fd));
    ASSERT_EXIT({
        struct rlimit limit;
        if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
            exit(1);
        }
        rlim_t original_limit = limit.rlim_cur;
        // Restrict only this test child; the inherited original fd remains open.
        limit.rlim_cur = static_cast<rlim_t>(fd) + 1;
        if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
            exit(1);
        }
        cat_socket_t *socket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_TCP4);
        bool valid = socket == nullptr && cat_get_last_error_code() == CAT_EMFILE;
        if (socket != nullptr) {
            cat_socket_close(socket);
        }
        valid = valid && cat_socket_get_local_free_port() == -1 && cat_get_last_error_code() == CAT_EMFILE;
        cat_os_fd_t pipe_fds[2];
        valid = valid && !cat_pipe(pipe_fds, CAT_PIPE_FLAG_NONE, CAT_PIPE_FLAG_NONE) && cat_get_last_error_code() == CAT_EMFILE;
        valid = valid && cat_poll_one(fd, POLLIN, nullptr, 0) == CAT_RET_ERROR && cat_get_last_error_code() == CAT_EMFILE;
        cat_pollfd_t descriptor;
        descriptor.fd = fd;
        descriptor.events = POLLIN;
        descriptor.revents = POLLNONE;
        valid = valid && cat_poll(&descriptor, 1, 0) == 1 && descriptor.revents == POLLERR;
        char buffer[CAT_STRLEN("data")];
        valid = valid && read(fd, buffer, sizeof(buffer)) == static_cast<ssize_t>(sizeof(buffer)) && memcmp(buffer, "data", sizeof(buffer)) == 0;
        limit.rlim_cur = original_limit;
        int restored = setrlimit(RLIMIT_NOFILE, &limit);
        valid = valid && restored == 0;
        valid = valid && cat_coroutine_wait_all();
        exit(valid ? 0 : 1);
    }, ::testing::ExitedWithCode(0), "");
}

/* A descriptor that fails to initialize must not stop the rest of the batch,
 * and poll(2) counts every descriptor with nonzero revents. */
TEST(cat_poll, batch_reports_ready_descriptors_after_failure)
{
    std::string path = get_random_path();
    ASSERT_EQ(cat_fs_put_contents(path.c_str(), CAT_STRL("file-data")), CAT_STRLEN("file-data"));
    DEFER(cat_fs_unlink(path.c_str()));
    cat_file_t file = cat_fs_open(path.c_str(), CAT_FS_OPEN_FLAG_RDONLY);
    ASSERT_GE(file, 0);
    DEFER(cat_fs_close(file));
    cat_socket_t socket;
    ASSERT_EQ(cat_socket_create(&socket, CAT_SOCKET_TYPE_UDP4), &socket);
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind_to(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_send_to(&socket, CAT_STRL("data"),
        CAT_STRL(TEST_LISTEN_IPV4), cat_socket_get_sock_port(&socket)));
    // A completed UDP send does not guarantee readability in the same event-loop
    // iteration, so make the socket ready before the batch is measured
    ASSERT_EQ(cat_poll_one(cat_socket_get_fd(&socket), POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
    cat_pollfd_t descriptors[] = {
        {file, POLLIN, POLLNONE},
        {cat_socket_get_fd(&socket), POLLIN, POLLNONE},
        {CAT_OS_INVALID_SOCKET, POLLIN, POLLNONE},
    };
    ASSERT_EQ(cat_poll(descriptors, CAT_ARRAY_SIZE(descriptors), TEST_IO_TIMEOUT), 3);
    // poll(2) defines regular files as always ready; uv reaches that result
    // through the initialization-error fallback because epoll and kqueue
    // reject them as poll targets.
    ASSERT_NE(descriptors[0].revents & POLLIN, 0);
    ASSERT_EQ(descriptors[0].revents & (POLLOUT | POLLHUP | POLLNVAL), 0);
    ASSERT_EQ(descriptors[1].revents, POLLIN);
    ASSERT_NE(descriptors[2].revents & (POLLERR | POLLNVAL), 0);
}

/* The failed descriptor is ready at once, but the registered socket still has
 * to be waited for; a zero timeout makes that observable as a single result. */
TEST(cat_poll, batch_timeout_reports_failed_descriptor_only)
{
    std::string path = get_random_path();
    ASSERT_EQ(cat_fs_put_contents(path.c_str(), CAT_STRL("file-data")), CAT_STRLEN("file-data"));
    DEFER(cat_fs_unlink(path.c_str()));
    cat_file_t file = cat_fs_open(path.c_str(), CAT_FS_OPEN_FLAG_RDONLY);
    ASSERT_GE(file, 0);
    DEFER(cat_fs_close(file));
    cat_socket_t socket;
    ASSERT_EQ(cat_socket_create(&socket, CAT_SOCKET_TYPE_UDP4), &socket);
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind_to(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    cat_pollfd_t descriptors[] = {
        {file, POLLIN, POLLNONE},
        {cat_socket_get_fd(&socket), POLLIN, POLLNONE},
    };
    ASSERT_EQ(cat_poll(descriptors, CAT_ARRAY_SIZE(descriptors), 0), 1);
    ASSERT_NE(descriptors[0].revents & POLLIN, 0);
    ASSERT_EQ(descriptors[1].revents, POLLNONE);
}

/* poll(2) returns at once when any descriptor is ready, so a regular file in
 * the batch must not wait out the timeout for the idle socket */
TEST(cat_poll, batch_regular_file_returns_before_timeout)
{
    std::string path = get_random_path();
    ASSERT_EQ(cat_fs_put_contents(path.c_str(), CAT_STRL("file-data")), CAT_STRLEN("file-data"));
    DEFER(cat_fs_unlink(path.c_str()));
    cat_file_t file = cat_fs_open(path.c_str(), CAT_FS_OPEN_FLAG_RDONLY);
    ASSERT_GE(file, 0);
    DEFER(cat_fs_close(file));
    cat_socket_t socket;
    ASSERT_EQ(cat_socket_create(&socket, CAT_SOCKET_TYPE_UDP4), &socket);
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind_to(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    cat_pollfd_t descriptors[] = {
        {file, POLLIN, POLLNONE},
        {cat_socket_get_fd(&socket), POLLIN, POLLNONE},
    };
    cat_timeout_t timeout = 500;
    cat_msec_t start = cat_time_msec2();
    ASSERT_EQ(cat_poll(descriptors, CAT_ARRAY_SIZE(descriptors), timeout), 1);
    ASSERT_LT(cat_time_msec2() - start, timeout / 2);
    ASSERT_NE(descriptors[0].revents & POLLIN, 0);
    ASSERT_EQ(descriptors[1].revents, POLLNONE);
}

/* The immediate return still collects the registered descriptors that are
 * already ready in that round */
TEST(cat_poll, batch_regular_file_collects_ready_socket)
{
    std::string path = get_random_path();
    ASSERT_EQ(cat_fs_put_contents(path.c_str(), CAT_STRL("file-data")), CAT_STRLEN("file-data"));
    DEFER(cat_fs_unlink(path.c_str()));
    cat_file_t file = cat_fs_open(path.c_str(), CAT_FS_OPEN_FLAG_RDONLY);
    ASSERT_GE(file, 0);
    DEFER(cat_fs_close(file));
    cat_socket_t socket;
    ASSERT_EQ(cat_socket_create(&socket, CAT_SOCKET_TYPE_UDP4), &socket);
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind_to(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_send_to(&socket, CAT_STRL("data"),
        CAT_STRL(TEST_LISTEN_IPV4), cat_socket_get_sock_port(&socket)));
    ASSERT_EQ(cat_poll_one(cat_socket_get_fd(&socket), POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
    cat_pollfd_t descriptors[] = {
        {file, POLLIN, POLLNONE},
        {cat_socket_get_fd(&socket), POLLIN, POLLNONE},
    };
    cat_timeout_t timeout = 500;
    cat_msec_t start = cat_time_msec2();
    ASSERT_EQ(cat_poll(descriptors, CAT_ARRAY_SIZE(descriptors), timeout), 2);
    ASSERT_LT(cat_time_msec2() - start, timeout / 2);
    ASSERT_NE(descriptors[0].revents & POLLIN, 0);
    ASSERT_EQ(descriptors[1].revents, POLLIN);
}
#endif

#if defined(CAT_OS_LINUX) || defined(CAT_OS_WIN)
/* poll(2) reports hangup for a descriptor that requested nothing, and an
 * already pending hangup must not make the batch wait for the timeout */
TEST(cat_poll, empty_events_reports_hangup_without_blocking_batch)
{
    cat_socket_t server;
    ASSERT_EQ(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP4), &server);
    DEFER(cat_socket_close(&server));
    ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
    cat_socket_t client;
    ASSERT_EQ(cat_socket_create(&client, CAT_SOCKET_TYPE_TCP4), &client);
    bool client_closed = false;
    DEFER(if (!client_closed) { cat_socket_close(&client); });
    ASSERT_TRUE(cat_socket_connect_to(&client, CAT_STRL(TEST_LISTEN_IPV4), cat_socket_get_sock_port(&server)));
    cat_socket_t connection;
    ASSERT_EQ(cat_socket_create(&connection, CAT_SOCKET_TYPE_TCP), &connection);
    DEFER(cat_socket_close(&connection));
    ASSERT_TRUE(cat_socket_accept(&server, &connection));
    ASSERT_TRUE(cat_socket_close(&client));
    client_closed = true;
    cat_socket_t idle;
    ASSERT_EQ(cat_socket_create(&idle, CAT_SOCKET_TYPE_UDP4), &idle);
    DEFER(cat_socket_close(&idle));
    ASSERT_TRUE(cat_socket_bind_to(&idle, CAT_STRL(TEST_LISTEN_IPV4), 0));
    cat_pollfd_t descriptors[] = {
        {cat_socket_get_fd(&connection), POLLNONE, POLLNONE},
        {cat_socket_get_fd(&idle), POLLIN, POLLNONE},
    };
    cat_msec_t start = cat_time_msec2();
    ASSERT_EQ(cat_poll(descriptors, CAT_ARRAY_SIZE(descriptors), 1000), 1);
    ASSERT_LT(cat_time_msec2() - start, 1000);
    // only hangup and errors survive the empty request
    ASSERT_NE(descriptors[0].revents & POLLHUP, 0);
    ASSERT_EQ(descriptors[0].revents & (POLLIN | POLLOUT), 0);
    ASSERT_EQ(descriptors[1].revents, POLLNONE);
}
#endif

/* An empty request must not subscribe to readability, so pending data alone
 * must not end the wait even while the peer stays open */
TEST(cat_poll, empty_events_does_not_report_readability)
{
    cat_socket_t socket;
    ASSERT_EQ(cat_socket_create(&socket, CAT_SOCKET_TYPE_UDP4), &socket);
    DEFER(cat_socket_close(&socket));
    ASSERT_TRUE(cat_socket_bind_to(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_send_to(&socket, CAT_STRL("data"),
        CAT_STRL(TEST_LISTEN_IPV4), cat_socket_get_sock_port(&socket)));
    cat_pollfd_t descriptor = {cat_socket_get_fd(&socket), POLLNONE, POLLNONE};
    ASSERT_EQ(cat_poll(&descriptor, 1, 10), 0);
    ASSERT_EQ(descriptor.revents, POLLNONE);
}

#ifndef CAT_OS_WIN

namespace {

/* The read end is the interrupt source, the write end stands in for the signal
 * handler that forwards a signal */
struct poll_pipe_s {
    int fds[2];
    int write_count;

    poll_pipe_s()
    {
        fds[0] = -1;
        fds[1] = -1;
        write_count = 0;
    }

    ~poll_pipe_s()
    {
        if (fds[0] >= 0) {
            close(fds[0]);
        }
        if (fds[1] >= 0) {
            close(fds[1]);
        }
    }

    bool open()
    {
        return pipe(fds) == 0;
    }

    bool write_byte()
    {
        if (write(fds[1], "!", 1) != 1) {
            return false;
        }
        write_count++;
        return true;
    }
};

/* The timer phase runs before the poll phase, so a byte written here reaches a
 * waiter of the same loop round */
void poll_interrupt_write(cat_time_timeout_t *timer, cat_data_t *data)
{
    poll_pipe_s *pipe = (poll_pipe_s *) data;

    (void) timer;
    ASSERT_TRUE(pipe->write_byte());
}

struct poll_interrupt_ready_s {
    poll_pipe_s *signal;
    int ready_fd;
};

/* A signal and a readiness in one round: poll(2) reports only the interrupt and
 * the readiness stays for the retry */
void poll_interrupt_write_ready(cat_time_timeout_t *timer, cat_data_t *data)
{
    poll_interrupt_ready_s *ctx = (poll_interrupt_ready_s *) data;

    (void) timer;
    ASSERT_TRUE(ctx->signal->write_byte());
    ASSERT_EQ(write(ctx->ready_fd, "r", 1), 1);
}

} // namespace

TEST(cat_poll, interrupt_propagates)
{
    poll_pipe_s signal;
    ASSERT_TRUE(signal.open());
    ASSERT_TRUE(cat_interrupt_source_set(signal.fds[0]));
    DEFER(cat_interrupt_source_clear());
    cat_time_timeout_t *writer = cat_time_timeout_init(poll_interrupt_write, &signal, 10);
    ASSERT_NE(writer, nullptr);
    DEFER(cat_time_timeout_close(writer));
    poll_pipe_s ready;
    ASSERT_TRUE(ready.open());
    cat_pollfd_t descriptor = {ready.fds[0], POLLIN, POLLNONE};

    cat_clear_last_error();
    cat_msec_t start = cat_time_msec2();
    int ret = cat_poll(&descriptor, 1, 1000);
    cat_msec_t elapsed = cat_time_msec2() - start;

    ASSERT_EQ(ret, -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINTR);
    ASSERT_EQ(signal.write_count, 1);
    ASSERT_LT(elapsed, 500);
    ASSERT_EQ(descriptor.revents, POLLNONE);
}

TEST(cat_poll, retry_on_interrupt_keeps_the_timeout)
{
    poll_pipe_s signal;
    ASSERT_TRUE(signal.open());
    ASSERT_TRUE(cat_interrupt_source_set(signal.fds[0]));
    DEFER(cat_interrupt_source_clear());
    cat_time_timeout_t *writer = cat_time_timeout_init(poll_interrupt_write, &signal, 10);
    ASSERT_NE(writer, nullptr);
    DEFER(cat_time_timeout_close(writer));
    poll_pipe_s ready;
    ASSERT_TRUE(ready.open());
    cat_pollfd_t descriptor = {ready.fds[0], POLLIN, POLLNONE};

    cat_msec_t start = cat_time_msec2();
    int ret = cat_poll_ex(&descriptor, 1, 200, CAT_POLL_FLAG_RETRY_ON_INTERRUPT);
    cat_msec_t elapsed = cat_time_msec2() - start;

    /* the interrupt must not shorten the wait */
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(signal.write_count, 1);
    ASSERT_GE(elapsed, 150);
    ASSERT_LT(elapsed, 1000);
    ASSERT_EQ(descriptor.revents, POLLNONE);
}

TEST(cat_poll, interrupt_drops_the_ready_events_of_the_round)
{
    poll_pipe_s signal;
    ASSERT_TRUE(signal.open());
    ASSERT_TRUE(cat_interrupt_source_set(signal.fds[0]));
    DEFER(cat_interrupt_source_clear());
    poll_pipe_s ready;
    ASSERT_TRUE(ready.open());
    poll_interrupt_ready_s ctx = {&signal, ready.fds[1]};
    cat_time_timeout_t *writer = cat_time_timeout_init(poll_interrupt_write_ready, &ctx, 10);
    ASSERT_NE(writer, nullptr);
    DEFER(cat_time_timeout_close(writer));
    cat_pollfd_t descriptor = {ready.fds[0], POLLIN, POLLNONE};

    cat_clear_last_error();
    int ret = cat_poll(&descriptor, 1, 1000);

    /* the kernel returns -1 and leaves revents unfilled, so does this */
    ASSERT_EQ(ret, -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINTR);
    ASSERT_EQ(descriptor.revents, POLLNONE);

    /* the data is still there, the retry reports it */
    ASSERT_EQ(cat_poll(&descriptor, 1, 1000), 1);
    ASSERT_EQ(descriptor.revents, POLLIN);
    char byte = 0;
    ASSERT_EQ(read(ready.fds[0], &byte, 1), 1);
    ASSERT_EQ(byte, 'r');
}

TEST(cat_poll_one, interrupt_propagates)
{
    poll_pipe_s signal;
    ASSERT_TRUE(signal.open());
    ASSERT_TRUE(cat_interrupt_source_set(signal.fds[0]));
    DEFER(cat_interrupt_source_clear());
    cat_time_timeout_t *writer = cat_time_timeout_init(poll_interrupt_write, &signal, 10);
    ASSERT_NE(writer, nullptr);
    DEFER(cat_time_timeout_close(writer));
    poll_pipe_s ready;
    ASSERT_TRUE(ready.open());
    cat_pollfd_events_t revents = POLLIN;

    cat_clear_last_error();
    cat_msec_t start = cat_time_msec2();
    cat_ret_t ret = cat_poll_one(ready.fds[0], POLLIN, &revents, 1000);
    cat_msec_t elapsed = cat_time_msec2() - start;

    ASSERT_EQ(ret, CAT_RET_ERROR);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINTR);
    ASSERT_EQ(signal.write_count, 1);
    ASSERT_LT(elapsed, 500);
    ASSERT_EQ(revents, POLLNONE);
}

TEST(cat_poll_one, retry_on_interrupt_keeps_the_timeout)
{
    poll_pipe_s signal;
    ASSERT_TRUE(signal.open());
    ASSERT_TRUE(cat_interrupt_source_set(signal.fds[0]));
    DEFER(cat_interrupt_source_clear());
    cat_time_timeout_t *writer = cat_time_timeout_init(poll_interrupt_write, &signal, 10);
    ASSERT_NE(writer, nullptr);
    DEFER(cat_time_timeout_close(writer));
    poll_pipe_s ready;
    ASSERT_TRUE(ready.open());
    cat_pollfd_events_t revents = POLLNONE;

    cat_msec_t start = cat_time_msec2();
    cat_ret_t ret = cat_poll_one_ex(ready.fds[0], POLLIN, &revents, 200,
        CAT_POLL_FLAG_RETRY_ON_INTERRUPT);
    cat_msec_t elapsed = cat_time_msec2() - start;

    ASSERT_EQ(ret, CAT_RET_NONE);
    ASSERT_EQ(signal.write_count, 1);
    ASSERT_GE(elapsed, 150);
    ASSERT_LT(elapsed, 1000);
    ASSERT_EQ(revents, POLLNONE);
}

TEST(cat_select, interrupt_propagates_with_descriptors)
{
    poll_pipe_s signal;
    ASSERT_TRUE(signal.open());
    ASSERT_TRUE(cat_interrupt_source_set(signal.fds[0]));
    DEFER(cat_interrupt_source_clear());
    cat_time_timeout_t *writer = cat_time_timeout_init(poll_interrupt_write, &signal, 10);
    ASSERT_NE(writer, nullptr);
    DEFER(cat_time_timeout_close(writer));
    poll_pipe_s ready;
    ASSERT_TRUE(ready.open());
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(ready.fds[0], &readfds);
    struct timeval timeout = {1, 0};

    cat_clear_last_error();
    cat_msec_t start = cat_time_msec2();
    int ret = cat_select(ready.fds[0] + 1, &readfds, nullptr, nullptr, &timeout);
    cat_msec_t elapsed = cat_time_msec2() - start;

    ASSERT_EQ(ret, -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINTR);
    ASSERT_EQ(signal.write_count, 1);
    ASSERT_LT(elapsed, 500);
    /* select() leaves the sets unmodified when it fails */
    ASSERT_TRUE(FD_ISSET(ready.fds[0], &readfds));
}

TEST(cat_select, retry_on_interrupt_keeps_the_timeout_with_descriptors)
{
    poll_pipe_s signal;
    ASSERT_TRUE(signal.open());
    ASSERT_TRUE(cat_interrupt_source_set(signal.fds[0]));
    DEFER(cat_interrupt_source_clear());
    cat_time_timeout_t *writer = cat_time_timeout_init(poll_interrupt_write, &signal, 10);
    ASSERT_NE(writer, nullptr);
    DEFER(cat_time_timeout_close(writer));
    poll_pipe_s ready;
    ASSERT_TRUE(ready.open());
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(ready.fds[0], &readfds);
    struct timeval timeout = {0, 200 * 1000};

    cat_msec_t start = cat_time_msec2();
    int ret = cat_select_ex(ready.fds[0] + 1, &readfds, nullptr, nullptr, &timeout,
        CAT_POLL_FLAG_RETRY_ON_INTERRUPT);
    cat_msec_t elapsed = cat_time_msec2() - start;

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(signal.write_count, 1);
    ASSERT_GE(elapsed, 150);
    ASSERT_LT(elapsed, 1000);
}

TEST(cat_select, interrupt_propagates_without_descriptors)
{
    poll_pipe_s signal;
    ASSERT_TRUE(signal.open());
    ASSERT_TRUE(cat_interrupt_source_set(signal.fds[0]));
    DEFER(cat_interrupt_source_clear());
    cat_time_timeout_t *writer = cat_time_timeout_init(poll_interrupt_write, &signal, 10);
    ASSERT_NE(writer, nullptr);
    DEFER(cat_time_timeout_close(writer));
    struct timeval timeout = {1, 0};

    cat_clear_last_error();
    cat_msec_t start = cat_time_msec2();
    int ret = cat_select(0, nullptr, nullptr, nullptr, &timeout);
    cat_msec_t elapsed = cat_time_msec2() - start;

    ASSERT_EQ(ret, -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINTR);
    ASSERT_EQ(signal.write_count, 1);
    ASSERT_LT(elapsed, 500);
}

TEST(cat_select, retry_on_interrupt_keeps_the_timeout_without_descriptors)
{
    poll_pipe_s signal;
    ASSERT_TRUE(signal.open());
    ASSERT_TRUE(cat_interrupt_source_set(signal.fds[0]));
    DEFER(cat_interrupt_source_clear());
    cat_time_timeout_t *writer = cat_time_timeout_init(poll_interrupt_write, &signal, 10);
    ASSERT_NE(writer, nullptr);
    DEFER(cat_time_timeout_close(writer));
    struct timeval timeout = {0, 200 * 1000};

    cat_msec_t start = cat_time_msec2();
    int ret = cat_select_ex(0, nullptr, nullptr, nullptr, &timeout,
        CAT_POLL_FLAG_RETRY_ON_INTERRUPT);
    cat_msec_t elapsed = cat_time_msec2() - start;

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(signal.write_count, 1);
    ASSERT_GE(elapsed, 150);
    ASSERT_LT(elapsed, 1000);
}

TEST(cat_poll, retry_on_interrupt_keeps_the_total_timeout)
{
    poll_pipe_s signal;
    ASSERT_TRUE(signal.open());
    ASSERT_TRUE(cat_interrupt_source_set(signal.fds[0]));
    DEFER(cat_interrupt_source_clear());
    /* two interrupts in the middle of the wait, so a timeout that restarted on
     * each of them would outlast the bound below */
    cat_time_timeout_t *first = cat_time_timeout_init(poll_interrupt_write, &signal, 150);
    ASSERT_NE(first, nullptr);
    DEFER(cat_time_timeout_close(first));
    cat_time_timeout_t *second = cat_time_timeout_init(poll_interrupt_write, &signal, 250);
    ASSERT_NE(second, nullptr);
    DEFER(cat_time_timeout_close(second));
    poll_pipe_s ready;
    ASSERT_TRUE(ready.open());
    cat_pollfd_t descriptor = {ready.fds[0], POLLIN, POLLNONE};

    cat_msec_t start = cat_time_msec2();
    int ret = cat_poll_ex(&descriptor, 1, 400, CAT_POLL_FLAG_RETRY_ON_INTERRUPT);
    cat_msec_t elapsed = cat_time_msec2() - start;

    ASSERT_EQ(ret, 0);
    ASSERT_EQ(signal.write_count, 2);
    ASSERT_GE(elapsed, 350);
    ASSERT_LT(elapsed, 550);
}

/* cat_poll_one() must report a hangup for an empty request as cat_poll() does.
 * kqueue registers a watch only for read and write, so the empty request names
 * nothing there and the hangup is lost */
#if defined(CAT_OS_LINUX) || defined(CAT_OS_WIN)
TEST(cat_poll, one_reports_a_hangup_for_an_empty_request)
{
    cat_socket_t server, client, connection;
    ASSERT_EQ(cat_socket_create(&server, CAT_SOCKET_TYPE_TCP4), &server);
    DEFER(cat_socket_close(&server));
    ASSERT_TRUE(cat_socket_bind_to(&server, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_listen(&server, TEST_SERVER_BACKLOG));
    ASSERT_EQ(cat_socket_create(&client, CAT_SOCKET_TYPE_TCP4), &client);
    DEFER(cat_socket_close(&client));
    ASSERT_TRUE(cat_socket_connect_to(&client, CAT_STRL(TEST_LISTEN_IPV4),
        cat_socket_get_sock_port(&server)));
    ASSERT_EQ(cat_socket_create(&connection, CAT_SOCKET_TYPE_TCP), &connection);
    DEFER(cat_socket_close(&connection));
    ASSERT_TRUE(cat_socket_accept(&server, &connection));
    ASSERT_TRUE(cat_socket_close(&client));

    cat_pollfd_events_t revents = POLLNONE;
    cat_ret_t ret = cat_poll_one(cat_socket_get_fd(&connection), POLLNONE, &revents, 200);
    ASSERT_EQ(ret, CAT_RET_OK);
    ASSERT_NE(revents & POLLHUP, 0);
    /* an empty request must not report readability */
    ASSERT_EQ(revents & (POLLIN | POLLOUT), 0);
}
#endif


TEST(cat_poll, udg_io_survives_repeated_interrupts)
{
    poll_pipe_s signal;
    ASSERT_TRUE(signal.open());
    ASSERT_TRUE(cat_interrupt_source_set(signal.fds[0]));
    DEFER(cat_interrupt_source_clear());

    std::string path = get_random_path();
    cat_socket_t receiver, sender;
    ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_UDG), &receiver);
    DEFER(cat_socket_close(&receiver));
    ASSERT_TRUE(cat_socket_bind_to(&receiver, path.data(), path.length(), 0));
    DEFER(cat_fs_unlink(path.c_str()));
    ASSERT_EQ(cat_socket_create(&sender, CAT_SOCKET_TYPE_UDG), &sender);
    DEFER(cat_socket_close(&sender));
    ASSERT_TRUE(cat_socket_connect_to(&sender, path.data(), path.length(), 0));

    /* A unix datagram read waits through cat_poll_one(), so a signal here ends
     * the wait. The socket must retry it and report the timeout instead */
    wait_group writer_wg;
    co([&] {
        writer_wg++;
        DEFER(writer_wg--);
        for (int i = 0; i < 200; i++) {
            if (cat_time_delay(1) != CAT_RET_OK) {
                return;
            }
            if (!signal.write_byte()) {
                return;
            }
        }
    });

    char probe = 0;
    ASSERT_EQ(cat_socket_read_ex(&receiver, &probe, 1, 100), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);

    /* reads and writes keep working while the source fires */
    std::string payload = get_random_bytes(1024);
    for (int i = 0; i < 20; i++) {
        ASSERT_TRUE(cat_socket_send(&sender, payload.data(), payload.size()));
        std::string received(payload.size(), '\0');
        ssize_t nread = cat_socket_read_ex(&receiver, received.data(), received.size(), 1000);
        ASSERT_EQ(nread, (ssize_t) payload.size());
        ASSERT_EQ(received, payload);
    }

    ASSERT_TRUE(writer_wg(2000));
    ASSERT_GT(signal.write_count, 0);
}

#endif /* CAT_OS_WIN */
