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
