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

#if defined(CAT_OS_LINUX) && GTEST_HAS_DEATH_TEST
#include <sys/resource.h>
#endif

TEST(cat_socket, open_native_udp_socket_preserves_datagrams)
{
    cat_socket_t receiver, sender;
    ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_UDP4), &receiver);
    DEFER(cat_socket_close(&receiver));
    ASSERT_TRUE(cat_socket_bind_to(&receiver, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_EQ(cat_socket_create(&sender, CAT_SOCKET_TYPE_UDP), &sender);
    DEFER(cat_socket_close(&sender));
    cat_os_socket_t fd = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_NE(fd, CAT_OS_INVALID_SOCKET);
    bool transferred = false;
#ifdef CAT_OS_WIN
    DEFER(if (!transferred) { closesocket(fd); });
#else
    DEFER(if (!transferred) { close(fd); });
#endif
    ASSERT_TRUE(cat_socket_open_os_socket(&sender, fd));
    transferred = true;
    ASSERT_EQ(cat_socket_get_fd(&sender), fd);
    ASSERT_FALSE(cat_socket_is_established(&sender));
    ASSERT_TRUE(cat_socket_send_to(&sender, CAT_STRL("native-udp"), CAT_STRL(TEST_LISTEN_IPV4), cat_socket_get_sock_port(&receiver)));
    char buffer[CAT_STRLEN("native-udp")];
    ASSERT_EQ(cat_socket_recv(&receiver, buffer, sizeof(buffer)), sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "native-udp");
}

TEST(cat_socket, lazy_udp_errors_preserve_socket)
{
    cat_socket_t sender, receiver;
    ASSERT_EQ(cat_socket_create(&sender, CAT_SOCKET_TYPE_UDP), &sender);
    DEFER(cat_socket_close(&sender));
    char buffer[3] = {'x', 'x', 'x'};
    cat_set_last_error_code(CAT_EBUSY);
    ASSERT_EQ(cat_socket_try_recv(&sender, buffer, sizeof(buffer)), CAT_EBADF);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBUSY);
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "xxx");
    ASSERT_EQ(cat_socket_peek(&sender, buffer, sizeof(buffer)), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "xxx");
    char name[CAT_SOCKET_IPV4_BUFFER_SIZE] = "unchanged";
    size_t name_length = sizeof(name);
    int port = 1;
    ASSERT_EQ(cat_socket_peek_from(&sender, buffer, sizeof(buffer), name, &name_length, &port), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_EQ(name_length, 0);
    ASSERT_EQ(port, 0);
    ASSERT_FALSE(cat_socket_connect_to(&sender, CAT_STRL(""), 0));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_TRUE(cat_socket_is_available(&sender));
    ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_UDP4), &receiver);
    DEFER(cat_socket_close(&receiver));
    ASSERT_TRUE(cat_socket_bind_to(&receiver, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_bind_to(&sender, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_connect_to(&sender, CAT_STRL("localhost"), cat_socket_get_sock_port(&receiver)));
    ASSERT_TRUE(cat_socket_send(&sender, CAT_STRL("abc")));
    name_length = sizeof(name);
    ASSERT_EQ(cat_socket_peek_from_ex(&receiver, buffer, sizeof(buffer), name, &name_length, &port, TEST_IO_TIMEOUT), sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "abc");
    ASSERT_EQ(std::string(name, name_length), TEST_LISTEN_IPV4);
    ASSERT_EQ(port, cat_socket_get_sock_port(&sender));
    ASSERT_EQ(cat_socket_recv_ex(&receiver, buffer, sizeof(buffer), TEST_IO_TIMEOUT), sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "abc");
}

TEST(cat_socket, empty_datagram_receive_timeout_preserves_socket)
{
    for (cat_socket_type_t type : {
        CAT_SOCKET_TYPE_UDP4,
#ifdef CAT_OS_UNIX_LIKE
        CAT_SOCKET_TYPE_UDG,
#endif
    }) {
        SCOPED_TRACE(type);
        cat_socket_t receiver;
        ASSERT_EQ(cat_socket_create(&receiver, type), &receiver);
        DEFER(cat_socket_close(&receiver));
        const std::string name = type == CAT_SOCKET_TYPE_UDP4 ? TEST_LISTEN_IPV4 : get_random_path();
        ASSERT_TRUE(cat_socket_bind_to(&receiver, name.data(), name.size(), 0));
        char buffer[1] = {'x'};
        ASSERT_EQ(cat_socket_recv_ex(&receiver, buffer, sizeof(buffer), 1), -1);
        ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
        ASSERT_EQ(buffer[0], 'x');
        ASSERT_TRUE(cat_socket_is_available(&receiver));
        cat_socket_t sender;
        ASSERT_EQ(cat_socket_create(&sender, type), &sender);
        DEFER(cat_socket_close(&sender));
        ASSERT_TRUE(cat_socket_connect_to(&sender, name.data(), name.size(),
            type == CAT_SOCKET_TYPE_UDP4 ? cat_socket_get_sock_port(&receiver) : 0));
        ASSERT_TRUE(cat_socket_send(&sender, CAT_STRL("y")));
        ASSERT_EQ(cat_socket_recv(&receiver, buffer, sizeof(buffer)), sizeof(buffer));
        ASSERT_EQ(buffer[0], 'y');
    }
}

TEST(cat_socket, pending_ipv6_datagram_receive_preserves_source)
{
    cat_socket_t receiver, sender;
    ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_UDP6), &receiver);
    DEFER(cat_socket_close(&receiver));
    if (!cat_socket_bind_to(&receiver, CAT_STRL(TEST_LISTEN_IPV6), 0)) {
        if (cat_get_last_error_code() == CAT_EAFNOSUPPORT || cat_get_last_error_code() == CAT_EADDRNOTAVAIL) {
            GTEST_SKIP() << "IPv6 loopback is unavailable";
        }
        FAIL() << cat_get_last_error_message();
    }
    ASSERT_EQ(cat_socket_create(&sender, CAT_SOCKET_TYPE_UDP6), &sender);
    DEFER(cat_socket_close(&sender));
    ASSERT_TRUE(cat_socket_bind_to(&sender, CAT_STRL(TEST_LISTEN_IPV6), 0));
    char buffer[3] = {};
    cat_sockaddr_info_t source = {};
    source.length = sizeof(source.address);
    wait_group wg;
    ssize_t received = -1;
    co([&] {
        wg++;
        DEFER(wg--);
        received = cat_socket_recvfrom_ex(&receiver, buffer, sizeof(buffer), &source.address.common,
            &source.length, TEST_IO_TIMEOUT);
    });
    DEFER(if (cat_socket_get_io_state(&receiver) == CAT_SOCKET_IO_FLAG_READ) { cat_socket_close(&receiver); } wg(TEST_IO_TIMEOUT));
    ASSERT_EQ(cat_socket_get_io_state(&receiver), CAT_SOCKET_IO_FLAG_READ);
    ASSERT_TRUE(cat_socket_send_to(&sender, CAT_STRL("abc"), CAT_STRL(TEST_LISTEN_IPV6), cat_socket_get_sock_port(&receiver)));
    ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
    ASSERT_EQ(received, sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "abc");
    ASSERT_EQ(source.address.common.sa_family, AF_INET6);
    ASSERT_EQ(source.length, sizeof(cat_sockaddr_in6_t));
    ASSERT_EQ(cat_sockaddr_get_port(&source.address.common), cat_socket_get_sock_port(&sender));
}

TEST(cat_socket, datagram_vector_destinations)
{
    cat_socket_t receiver, sender;
    ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_UDP4), &receiver);
    DEFER(cat_socket_close(&receiver));
    ASSERT_TRUE(cat_socket_bind_to(&receiver, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_EQ(cat_socket_create(&sender, CAT_SOCKET_TYPE_UDP4), &sender);
    DEFER(cat_socket_close(&sender));
    ASSERT_TRUE(cat_socket_bind_to(&sender, CAT_STRL(TEST_LISTEN_IPV4), 0));
    const cat_sockaddr_info_t *address = cat_socket_getsockname_fast(&receiver);
    ASSERT_NE(address, nullptr);
    int port = cat_socket_get_sock_port(&receiver);
    const cat_socket_write_vector_t vector[] = {
        cat_socket_write_vector_init("a\0", 2),
        cat_socket_write_vector_init("bc", 2),
    };
    for (unsigned int method = 0; method < 3; method++) {
        SCOPED_TRACE(method);
        if (method == 0) {
            ASSERT_TRUE(cat_socket_write_to(&sender, vector, CAT_ARRAY_SIZE(vector), CAT_STRL(TEST_LISTEN_IPV4), port));
        } else if (method == 1) {
            ASSERT_EQ(cat_socket_try_writeto(&sender, vector, CAT_ARRAY_SIZE(vector),
                &address->address.common, address->length), 4);
        } else {
            ASSERT_EQ(cat_socket_try_write_to(&sender, vector, CAT_ARRAY_SIZE(vector), CAT_STRL(TEST_LISTEN_IPV4), port), 4);
        }
        char buffer[4], name[CAT_SOCKADDR_MAX_PATH];
        size_t name_length = sizeof(name);
        int source_port = 0;
        ASSERT_EQ(cat_socket_recv_from(&receiver, buffer, sizeof(buffer), name, &name_length, &source_port), sizeof(buffer));
        ASSERT_EQ(std::string(buffer, sizeof(buffer)), std::string("a\0bc", 4));
        ASSERT_EQ(std::string(name, name_length), TEST_LISTEN_IPV4);
        ASSERT_EQ(source_port, cat_socket_get_sock_port(&sender));
    }
    ASSERT_TRUE(cat_socket_connect_to(&sender, CAT_STRL(TEST_LISTEN_IPV4), port));
    ASSERT_TRUE(cat_socket_write(&sender, vector, CAT_ARRAY_SIZE(vector)));
    char buffer[4];
    ASSERT_EQ(cat_socket_recv(&receiver, buffer, sizeof(buffer)), sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), std::string("a\0bc", 4));
}

#ifdef CAT_OS_LINUX
TEST(cat_socket, udg_queued_writes_preserve_order_and_cancel)
{
    enum class action { send, cancel_head, cancel_tail, timeout_active, timeout_queued, close_one, close_all };
    for (action operation : {action::send, action::cancel_head, action::cancel_tail,
        action::timeout_active, action::timeout_queued, action::close_one, action::close_all}) {
        SCOPED_TRACE(static_cast<int>(operation));
        std::string path = get_random_path();
        cat_socket_t receiver, sender;
        ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_UDG), &receiver);
        DEFER(cat_socket_close(&receiver));
        ASSERT_TRUE(cat_socket_bind_to(&receiver, path.data(), path.length(), 0));
        DEFER(cat_fs_unlink(path.c_str()));
        ASSERT_EQ(cat_socket_create(&sender, CAT_SOCKET_TYPE_UDG), &sender);
        DEFER(if (cat_socket_is_available(&sender)) { cat_socket_close(&sender); });
        ASSERT_TRUE(cat_socket_connect_to(&sender, path.data(), path.length(), 0));
        ASSERT_TRUE(cat_socket_set_write_timeout(&sender, TEST_IO_TIMEOUT));

        std::string packet(1024, 'f');
        int capacity = cat_socket_get_send_buffer_size(&sender);
        ASSERT_GT(capacity, 0);
        size_t limit = static_cast<size_t>(capacity) / packet.length() + 1;
        size_t filled = 0;
        ssize_t n = 0;
        while (filled < limit) {
            n = cat_socket_try_send(&sender, packet.data(), packet.length());
            if (n == CAT_EAGAIN) {
                break;
            }
            ASSERT_EQ(n, static_cast<ssize_t>(packet.length()));
            filled++;
        }
        ASSERT_EQ(n, CAT_EAGAIN);
        ASSERT_GT(filled, 0);

        std::array<cat_bool_t, 3> sent = {};
        std::array<cat_errno_t, 3> errors = {};
        std::array<bool, 3> done = {};
        std::array<cat_coroutine_t *, 3> writers = {};
        wait_group wg;
        wait_group timed;
        DEFER(if (cat_socket_is_available(&sender)) { cat_socket_close(&sender); });
        // The full peer queue makes co() yield with the first writer active and the others queued.
        size_t writer_count = operation == action::close_one ? 1 : writers.size();
        size_t timed_index = operation == action::timeout_active ? 0
            : operation == action::timeout_queued ? 1 : writers.size();
        for (size_t i = 0; i < writer_count; i++) {
            writers[i] = co([&, i] {
                wg++;
                DEFER(wg--);
                if (i == timed_index) {
                    timed++;
                }
                DEFER(if (i == timed_index) { timed--; });
                char value = static_cast<char>('a' + i);
                sent[i] = cat_socket_send_ex(&sender, &value, sizeof(value), i == timed_index ? 0 : TEST_IO_TIMEOUT);
                errors[i] = sent[i] ? 0 : cat_get_last_error_code();
                done[i] = true;
            });
            ASSERT_FALSE(done[i]);
            ASSERT_EQ(cat_socket_get_io_state(&sender), CAT_SOCKET_IO_FLAG_WRITE);
        }
        size_t canceled = writers.size();
        if (timed_index < writers.size()) {
            ASSERT_TRUE(timed(TEST_IO_TIMEOUT));
            canceled = timed_index;
            ASSERT_TRUE(done[canceled]);
            ASSERT_FALSE(sent[canceled]);
            ASSERT_EQ(errors[canceled], CAT_ETIMEDOUT);
            ASSERT_EQ(cat_socket_get_io_state(&sender), CAT_SOCKET_IO_FLAG_WRITE);
        }
        if (operation == action::close_one || operation == action::close_all) {
            ASSERT_TRUE(cat_socket_close(&sender));
            ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
            ASSERT_FALSE(cat_socket_is_available(&sender));
            for (size_t i = 0; i < writer_count; i++) {
                ASSERT_TRUE(done[i]);
                ASSERT_FALSE(sent[i]);
                ASSERT_EQ(errors[i], CAT_ECANCELED);
            }
            // Closing must cancel blocked writes rather than delivering their packets.
            std::string buffer(packet.length(), '\0');
            for (size_t i = 0; i < filled; i++) {
                ASSERT_EQ(cat_socket_recv_ex(&receiver, buffer.data(), buffer.length(), TEST_IO_TIMEOUT),
                    static_cast<ssize_t>(packet.length()));
                ASSERT_EQ(buffer, packet);
            }
            ASSERT_EQ(cat_socket_try_recv(&receiver, buffer.data(), buffer.length()), CAT_EAGAIN);
            continue;
        }
        if (operation == action::cancel_head || operation == action::cancel_tail) {
            canceled = operation == action::cancel_head ? 1 : 2;
            ASSERT_TRUE(cat_coroutine_resume(writers[canceled], nullptr, nullptr));
            ASSERT_TRUE(done[canceled]);
            ASSERT_FALSE(sent[canceled]);
            ASSERT_EQ(errors[canceled], CAT_ECANCELED);
            ASSERT_FALSE(done[0]);
            ASSERT_EQ(cat_socket_get_io_state(&sender), CAT_SOCKET_IO_FLAG_WRITE);
        }
        std::string buffer(packet.length(), '\0');
        for (size_t i = 0; i < filled; i++) {
            ASSERT_EQ(cat_socket_recv_ex(&receiver, buffer.data(), buffer.length(), TEST_IO_TIMEOUT),
                static_cast<ssize_t>(packet.length()));
            ASSERT_EQ(buffer, packet);
        }
        ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
        for (size_t i = 0; i < writers.size(); i++) {
            if (i == canceled) {
                continue;
            }
            ASSERT_TRUE(sent[i]) << cat_strerror(errors[i]);
            char value;
            ASSERT_EQ(cat_socket_recv_ex(&receiver, &value, sizeof(value), TEST_IO_TIMEOUT), 1);
            ASSERT_EQ(value, static_cast<char>('a' + i));
        }
        ASSERT_EQ(cat_socket_get_io_state(&sender), CAT_SOCKET_IO_FLAG_NONE);
        ASSERT_EQ(cat_socket_try_recv(&receiver, buffer.data(), buffer.length()), CAT_EAGAIN);
    }
}

#if GTEST_HAS_DEATH_TEST
TEST(cat_socket, udg_descriptor_limit_failure_and_recovery)
{
    // Re-exec can repeat the time-seeded random path; keep parent and child endpoints distinct.
    const std::string path = get_random_path() + "-" + std::to_string(getpid());
    cat_socket_t receiver, sender;
    ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_UDG), &receiver);
    DEFER(cat_socket_close(&receiver));
    ASSERT_TRUE(cat_socket_bind_to(&receiver, path.data(), path.length(), 0));
    DEFER(cat_fs_unlink(path.c_str()));
    ASSERT_EQ(cat_socket_create(&sender, CAT_SOCKET_TYPE_UDG), &sender);
    DEFER(if (cat_socket_is_available(&sender)) { cat_socket_close(&sender); });
    ASSERT_TRUE(cat_socket_connect_to(&sender, path.data(), path.length(), 0));
    ASSERT_TRUE(cat_socket_set_write_timeout(&sender, TEST_IO_TIMEOUT));

    std::string packet(1024, 'f');
    int capacity = cat_socket_get_send_buffer_size(&sender);
    ASSERT_GT(capacity, 0);
    size_t limit = static_cast<size_t>(capacity) / packet.length() + 1;
    size_t filled = 0;
    ssize_t n = 0;
    while (filled < limit) {
        n = cat_socket_try_send(&sender, packet.data(), packet.length());
        if (n == CAT_EAGAIN) {
            break;
        }
        ASSERT_EQ(n, static_cast<ssize_t>(packet.length()));
        filled++;
    }
    ASSERT_EQ(n, CAT_EAGAIN);
    ASSERT_GT(filled, 0);

    // Restrict only the isolated child's descriptors, then restore them before recovery.
    ASSERT_EXIT({
        struct rlimit original;
        if (getrlimit(RLIMIT_NOFILE, &original) != 0) {
            std::exit(1);
        }
        struct rlimit restricted = original;
        restricted.rlim_cur = 0;
        if (setrlimit(RLIMIT_NOFILE, &restricted) != 0) {
            std::exit(2);
        }
        bool sent = cat_socket_send_ex(&sender, CAT_STRL("pending"), TEST_IO_TIMEOUT);
        cat_errno_t error = cat_get_last_error_code();
        if (setrlimit(RLIMIT_NOFILE, &original) != 0) {
            std::exit(3);
        }
        if (sent || error != CAT_EMFILE || cat_socket_get_io_state(&sender) != CAT_SOCKET_IO_FLAG_NONE) {
            std::exit(4);
        }
        std::string buffer(packet.size(), '\0');
        for (size_t i = 0; i < filled; i++) {
            if (cat_socket_recv_ex(&receiver, buffer.data(), buffer.size(), TEST_IO_TIMEOUT) !=
                static_cast<ssize_t>(packet.size()) || buffer != packet) {
                std::exit(5);
            }
        }
        if (setrlimit(RLIMIT_NOFILE, &restricted) != 0) {
            std::exit(6);
        }
        ssize_t received = cat_socket_recv_ex(&receiver, buffer.data(), buffer.size(), TEST_IO_TIMEOUT);
        error = cat_get_last_error_code();
        if (setrlimit(RLIMIT_NOFILE, &original) != 0) {
            std::exit(7);
        }
        if (received != -1 || error != CAT_EMFILE || buffer != packet ||
            cat_socket_get_io_state(&receiver) != CAT_SOCKET_IO_FLAG_NONE) {
            std::exit(8);
        }
        if (!cat_socket_send_ex(&sender, CAT_STRL("reuse"), TEST_IO_TIMEOUT) ||
            cat_socket_recv_ex(&receiver, buffer.data(), buffer.size(), TEST_IO_TIMEOUT) != CAT_STRLEN("reuse") ||
            buffer.compare(0, CAT_STRLEN("reuse"), "reuse") != 0) {
            std::exit(9);
        }
        std::exit(0);
    }, testing::ExitedWithCode(0), "");
}
#endif
#endif

TEST(cat_socket, datagram_try_recv_address)
{
    cat_socket_t receiver, sender;
    ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_UDP4), &receiver);
    DEFER(cat_socket_close(&receiver));
    ASSERT_TRUE(cat_socket_bind_to(&receiver, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_EQ(cat_socket_create(&sender, CAT_SOCKET_TYPE_UDP4), &sender);
    DEFER(cat_socket_close(&sender));
    ASSERT_TRUE(cat_socket_bind_to(&sender, CAT_STRL(TEST_LISTEN_IPV4), 0));
    int receiver_port = cat_socket_get_sock_port(&receiver);
    ASSERT_TRUE(cat_socket_send_to(&sender, CAT_STRL("payload"), CAT_STRL(TEST_LISTEN_IPV4), receiver_port));
    ASSERT_EQ(cat_poll_one(cat_socket_get_fd(&receiver), POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
    char buffer[CAT_STRLEN("payload")], name[CAT_SOCKADDR_MAX_PATH];
    size_t name_length = sizeof(name);
    int port = 0;
    ASSERT_EQ(cat_socket_try_recv_from(&receiver, buffer, sizeof(buffer), name, &name_length, &port), sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "payload");
    ASSERT_EQ(std::string(name, name_length), TEST_LISTEN_IPV4);
    ASSERT_EQ(port, cat_socket_get_sock_port(&sender));
    ASSERT_EQ(cat_socket_try_recv_from(&receiver, buffer, sizeof(buffer), name, &name_length, &port), CAT_EAGAIN);
}

TEST(cat_socket, bind_invalid_flags)
{
    for (cat_socket_type_t type : {CAT_SOCKET_TYPE_TCP4, CAT_SOCKET_TYPE_UDP4}) {
        cat_socket_t socket;
        ASSERT_EQ(cat_socket_create(&socket, type), &socket);
        DEFER(cat_socket_close(&socket));
        ASSERT_FALSE(cat_socket_bind_to_ex(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0,
            static_cast<cat_socket_bind_flags_t>(~CAT_SOCKET_BIND_FLAG_NONE)));
        ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
        ASSERT_TRUE(cat_socket_bind_to(&socket, CAT_STRL(TEST_LISTEN_IPV4), 0));
        ASSERT_GT(cat_socket_get_sock_port(&socket), 0);
    }
}

TEST(cat_socket, datagram_peek_preserves_packet)
{
    cat_socket_t receiver, sender;
    ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_UDP4), &receiver);
    DEFER(cat_socket_close(&receiver));
    ASSERT_TRUE(cat_socket_bind_to(&receiver, CAT_STRL("localhost"), 0));
    ASSERT_EQ(cat_socket_create(&sender, CAT_SOCKET_TYPE_UDP4), &sender);
    DEFER(cat_socket_close(&sender));
    ASSERT_TRUE(cat_socket_bind_to(&sender, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_send_to(&sender, CAT_STRL("packet"), CAT_STRL("localhost"), cat_socket_get_sock_port(&receiver)));
    ASSERT_EQ(cat_poll_one(cat_socket_get_fd(&receiver), POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
    char buffer[CAT_STRLEN("packet")], name[CAT_SOCKADDR_MAX_PATH];
    size_t name_length = sizeof(name);
    int port = 0;
    ASSERT_EQ(cat_socket_peek_from(&receiver, buffer, sizeof(buffer), name, &name_length, &port), sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "packet");
    ASSERT_EQ(std::string(name, name_length), TEST_LISTEN_IPV4);
    ASSERT_EQ(port, cat_socket_get_sock_port(&sender));
    ASSERT_EQ(cat_socket_recv(&receiver, buffer, sizeof(buffer)), sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "packet");
    name_length = sizeof(name);
    ASSERT_EQ(cat_socket_peek_from_ex(&receiver, buffer, sizeof(buffer), name, &name_length, &port, 1), 0);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
}

TEST(cat_socket, datagram_reconnect_rejected)
{
    cat_socket_t receiver, sender;
    ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_UDP4), &receiver);
    DEFER(cat_socket_close(&receiver));
    ASSERT_TRUE(cat_socket_bind_to(&receiver, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_EQ(cat_socket_create(&sender, CAT_SOCKET_TYPE_UDP4), &sender);
    DEFER(cat_socket_close(&sender));
    int port = cat_socket_get_sock_port(&receiver);
    ASSERT_TRUE(cat_socket_connect_to(&sender, CAT_STRL(TEST_LISTEN_IPV4), port));
    ASSERT_EQ(cat_socket_get_peer_port(&sender), port);
    ASSERT_FALSE(cat_socket_connect_to(&sender, "", 0, 0));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EISCONN);
    ASSERT_EQ(cat_socket_get_peer_port(&sender), port);
    ASSERT_TRUE(cat_socket_send(&sender, CAT_STRL("reconnect")));
    char buffer[CAT_STRLEN("reconnect")];
    ASSERT_EQ(cat_socket_recv(&receiver, buffer, sizeof(buffer)), sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "reconnect");
}

TEST(cat_socket, missing_datagram_destination_preserves_socket)
{
    cat_socket_t sender, receiver;
    ASSERT_EQ(cat_socket_create(&sender, CAT_SOCKET_TYPE_UDP4), &sender);
    DEFER(cat_socket_close(&sender));
    ASSERT_FALSE(cat_socket_send(&sender, CAT_STRL("packet")));
    ASSERT_NE(cat_get_last_error_code(), 0);
    ASSERT_TRUE(cat_socket_is_available(&sender));
    ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_UDP4), &receiver);
    DEFER(cat_socket_close(&receiver));
    ASSERT_TRUE(cat_socket_bind_to(&receiver, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_send_to(&sender, CAT_STRL("packet"), CAT_STRL(TEST_LISTEN_IPV4), cat_socket_get_sock_port(&receiver)));
    char buffer[CAT_STRLEN("packet")];
    ASSERT_EQ(cat_socket_recv(&receiver, buffer, sizeof(buffer)), sizeof(buffer));
    ASSERT_EQ(std::string(buffer, sizeof(buffer)), "packet");
}

TEST(cat_socket, datagram_cancel_receive_then_reuse)
{
    std::vector<cat_socket_type_t> types = {CAT_SOCKET_TYPE_UDP4};
#ifdef CAT_OS_UNIX_LIKE
    types.push_back(CAT_SOCKET_TYPE_UDG);
#endif
    for (cat_socket_type_t type : types) {
        SCOPED_TRACE(cat_socket_type_get_name(type));
        cat_socket_t receiver, sender;
        ASSERT_EQ(cat_socket_create(&receiver, type), &receiver);
        DEFER(cat_socket_close(&receiver));
        ASSERT_EQ(cat_socket_create(&sender, type), &sender);
        DEFER(cat_socket_close(&sender));
        std::string address = type == CAT_SOCKET_TYPE_UDP4 ? TEST_LISTEN_IPV4 : get_random_path();
        std::string sender_address = type == CAT_SOCKET_TYPE_UDP4 ? TEST_LISTEN_IPV4 : get_random_path();
        ASSERT_TRUE(cat_socket_bind_to(&receiver, address.data(), address.length(), 0));
        DEFER(if (type != CAT_SOCKET_TYPE_UDP4) { cat_fs_unlink(address.c_str()); });
        ASSERT_TRUE(cat_socket_bind_to(&sender, sender_address.data(), sender_address.length(), 0));
        DEFER(if (type != CAT_SOCKET_TYPE_UDP4) { cat_fs_unlink(sender_address.c_str()); });
        int port = type == CAT_SOCKET_TYPE_UDP4 ? cat_socket_get_sock_port(&receiver) : 0;
        for (bool peek : {false, true}) {
            SCOPED_TRACE(peek);
            ssize_t result = -2;
            cat_errno_t error = 0;
            bool done = false;
            wait_group wg;
            cat_coroutine_t *reader = co([&] {
                wg++;
                DEFER(wg--);
                char buffer[8];
                cat_sockaddr_union_t source;
                cat_socklen_t source_length = sizeof(source);
                if (peek) {
                    result = cat_socket_peekfrom_ex(&receiver, buffer, sizeof(buffer),
                        &source.common, &source_length, TEST_IO_TIMEOUT);
                } else {
                    result = cat_socket_recvfrom_ex(&receiver, buffer, sizeof(buffer),
                        &source.common, &source_length, TEST_IO_TIMEOUT);
                }
                error = cat_get_last_error_code();
                done = true;
            });
            DEFER(if (!done) { cat_coroutine_resume(reader, nullptr, nullptr); });
            ASSERT_FALSE(done);
            ASSERT_TRUE(cat_coroutine_resume(reader, nullptr, nullptr));
            ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
            ASSERT_TRUE(done);
            ASSERT_EQ(result, peek ? 0 : -1);
            ASSERT_EQ(error, CAT_ECANCELED);
            ASSERT_TRUE(cat_socket_is_available(&receiver));
            ASSERT_EQ(cat_socket_get_io_state(&receiver), CAT_SOCKET_IO_FLAG_NONE);
            ASSERT_TRUE(cat_socket_send_to(&sender, CAT_STRL("reuse"), address.data(), address.length(), port));
            char buffer[CAT_STRLEN("reuse")];
            ASSERT_EQ(cat_socket_recv_ex(&receiver, buffer, sizeof(buffer), TEST_IO_TIMEOUT), sizeof(buffer));
            ASSERT_EQ(std::string(buffer, sizeof(buffer)), "reuse");
        }
    }
}

#ifdef CAT_OS_LINUX
TEST(cat_socket, udp_peer_error_then_reuse)
{
    for (const auto &[peek, pending] : std::array<std::pair<bool, bool>, 3>{
        {{false, false}, {true, false}, {false, true}}}) {
        SCOPED_TRACE(peek);
        SCOPED_TRACE(pending);
        cat_socket_t receiver, sender;
        ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_UDP4), &receiver);
        DEFER(if (cat_socket_is_available(&receiver)) { cat_socket_close(&receiver); });
        ASSERT_TRUE(cat_socket_bind_to(&receiver, CAT_STRL(TEST_LISTEN_IPV4), 0));
        const int port = cat_socket_get_sock_port(&receiver);
        ASSERT_GT(port, 0);
        ASSERT_EQ(cat_socket_create(&sender, CAT_SOCKET_TYPE_UDP4), &sender);
        DEFER(cat_socket_close(&sender));
        ASSERT_TRUE(cat_socket_connect_to(&sender, CAT_STRL(TEST_LISTEN_IPV4), port));
        ASSERT_TRUE(cat_socket_close(&receiver));
        char buffer[8];
        memset(buffer, 'x', sizeof(buffer));
        ssize_t result = -2;
        cat_errno_t error = 0;
        bool completed = false;
        wait_group wg;
        cat_coroutine_t *reader = nullptr;
        auto read = [&] {
            result = peek ?
                cat_socket_peek_ex(&sender, buffer, sizeof(buffer), TEST_IO_TIMEOUT) :
                cat_socket_recv_ex(&sender, buffer, sizeof(buffer), TEST_IO_TIMEOUT);
            error = cat_get_last_error_code();
            completed = true;
        };
        DEFER(if (reader != nullptr && !completed) { cat_coroutine_resume(reader, nullptr, nullptr); } wg());
        if (pending) {
            reader = co([&] {
                wg++;
                DEFER(wg--);
                read();
            });
            ASSERT_FALSE(completed);
        }
        // Linux reports the closed local UDP peer to either an inline or pending reader.
        ASSERT_TRUE(cat_socket_send(&sender, CAT_STRL("packet")));
        if (!pending) {
            read();
        }
        ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
        ASSERT_TRUE(completed);
        ASSERT_EQ(result, -1);
        ASSERT_EQ(error, CAT_ECONNREFUSED);
        ASSERT_EQ(std::string(buffer, sizeof(buffer)), std::string(sizeof(buffer), 'x'));
        ASSERT_TRUE(cat_socket_is_available(&sender));
        ASSERT_EQ(cat_socket_get_io_state(&sender), CAT_SOCKET_IO_FLAG_NONE);
        ASSERT_EQ(cat_socket_create(&receiver, CAT_SOCKET_TYPE_UDP4), &receiver);
        ASSERT_TRUE(cat_socket_bind_to(&receiver, CAT_STRL(TEST_LISTEN_IPV4), port));
        ASSERT_TRUE(cat_socket_send(&sender, CAT_STRL("reuse")));
        ASSERT_EQ(cat_socket_recv_ex(&receiver, buffer, sizeof(buffer), TEST_IO_TIMEOUT), CAT_STRLEN("reuse"));
        ASSERT_EQ(std::string(buffer, CAT_STRLEN("reuse")), "reuse");
    }
}
#endif


#ifdef CAT_OS_UNIX_LIKE

/* The address that recvfrom reports is handed back to sendto, so it has to fit the
 * address structure the caller passed. The kernel may report a length larger than the
 * structure, and using that length reads past the caller buffer */

TEST(cat_socket, udg_received_address_fits_the_buffer)
{
    std::string server_path = get_random_path();
    std::string client_path = get_random_path();
    cat_socket_t server, client;
    ASSERT_EQ(cat_socket_create(&server, CAT_SOCKET_TYPE_UDG), &server);
    DEFER(cat_socket_close(&server));
    ASSERT_TRUE(cat_socket_bind_to(&server, server_path.data(), server_path.length(), 0));
    DEFER(cat_fs_unlink(server_path.c_str()));
    ASSERT_EQ(cat_socket_create(&client, CAT_SOCKET_TYPE_UDG), &client);
    DEFER(cat_socket_close(&client));
    ASSERT_TRUE(cat_socket_bind_to(&client, client_path.data(), client_path.length(), 0));
    DEFER(cat_fs_unlink(client_path.c_str()));
    ASSERT_TRUE(cat_socket_connect_to(&client, server_path.data(), server_path.length(), 0));

    ASSERT_TRUE(cat_socket_send(&client, CAT_STRL("hello")));

    cat_sockaddr_union_t address;
    cat_socklen_t address_length = sizeof(address);
    char buffer[64];
    ssize_t read_n = cat_socket_recvfrom(&server, CAT_STRS(buffer), &address.common, &address_length);
    ASSERT_EQ(read_n, (ssize_t) CAT_STRLEN("hello"));

    /* the reported length must stay inside what the caller offered, otherwise the address
     * cannot be used to reply */
    ASSERT_LE(address_length, (cat_socklen_t) sizeof(address));

    /* reply to the reported address, which is the path that reads past the buffer when the
     * length is wrong */
    ASSERT_TRUE(cat_socket_sendto(&server, CAT_STRL("reply"), &address.common, address_length));
    ASSERT_EQ(cat_socket_read(&client, CAT_STRS(buffer)), (ssize_t) CAT_STRLEN("reply"));
    ASSERT_EQ(std::string(buffer, CAT_STRLEN("reply")), "reply");
}

#endif /* CAT_OS_UNIX_LIKE */
