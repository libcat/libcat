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

#include "cat_websocket.h"

TEST(cat_websocket, get_name_for_opcode)
{
#define CAT_WEBSOCKET_OPCODE_NAME_TEST_GEN(name, value) ASSERT_STREQ(cat_websocket_opcode_get_name(value), #name);
        CAT_WEBSOCKET_OPCODE_MAP(CAT_WEBSOCKET_OPCODE_NAME_TEST_GEN);
#undef CAT_WEBSOCKET_OPCODE_NAME_TEST_GEN
    ASSERT_STREQ(cat_websocket_opcode_get_name(-1), "UNKNOWN");
}

TEST(cat_websocket, get_description_for_status)
{
#define CAT_WEBSOCKET_STATUS_DESCRIPTION_GEN(name, value, description) ASSERT_STREQ(cat_websocket_status_get_description(value), description);
        CAT_WEBSOCKET_STATUS_MAP(CAT_WEBSOCKET_STATUS_DESCRIPTION_GEN)
#undef CAT_WEBSOCKET_STATUS_DESCRIPTION_GEN
    ASSERT_STREQ(cat_websocket_status_get_description(-1), "Unknown websocket status code");
}

TEST(cat_websocket, header)
{
    // TODO: add tests for it
}

TEST(cat_websocket, mask_and_unmask)
{
    size_t size = CAT_BUFFER_COMMON_SIZE;
    size_t unaligned_offset = 137;
    const char *masking_key = "abcd";
    char *raw = new char[size + 1];
    DEFER(delete[] raw);
    char *processed = new char[size + 1];
    DEFER(delete[] processed);
    ASSERT_NE(cat_srand(CAT_STRS(raw)), nullptr);
    processed[size] = 0;

    /* mask with empty key */
    cat_websocket_mask(raw, processed, size, CAT_WEBSOCKET_EMPTY_MASKING_KEY);
    ASSERT_STREQ(raw, processed);

    /* mask */
    cat_websocket_mask(raw, processed, size, masking_key);
    ASSERT_STRNE(raw, processed);
    /* unmask */
    cat_websocket_unmask(processed, size, masking_key);
    ASSERT_STREQ(raw, processed);

    /* streaming mask */
    cat_websocket_mask_ex(
        /* from */ raw,
        /* to */ processed,
        /* length */ unaligned_offset,
        /* masking_key */ masking_key,
        /* index */ 0
    );
    cat_websocket_mask_ex(
        /* from */ raw + unaligned_offset,
        /* to */ processed + unaligned_offset,
        /* length */ size - unaligned_offset,
        /* masking_key */ masking_key,
        /* index */ unaligned_offset
    );
    /* streaming unmask */
    cat_websocket_unmask_ex(
        /* data */ processed,
        /* length */ size - unaligned_offset,
        /* masking_key */ masking_key,
        /* index */ 0
    );
    cat_websocket_unmask_ex(
        /* data */ processed + (size - unaligned_offset),
        /* length */ unaligned_offset,
        /* masking_key */ masking_key,
        /* index */ size - unaligned_offset
    );
    ASSERT_STREQ(raw, processed);
}

TEST(cat_websocket, header_payload_lengths)
{
    const struct {
        uint64_t length;
        uint8_t encoded_length;
        uint8_t extension_size;
    } cases[] = {
        {0, 0, 0},
        {125, 125, 0},
        {126, CAT_WEBSOCKET_EXT16_PAYLOAD_LENGTH, 2},
        {65535, CAT_WEBSOCKET_EXT16_PAYLOAD_LENGTH, 2},
        {65536, CAT_WEBSOCKET_EXT64_PAYLOAD_LENGTH, 8},
        {UINT64_C(0x0102030405060708), CAT_WEBSOCKET_EXT64_PAYLOAD_LENGTH, 8},
    };
    const char masking_key[] = {'\0', '\x12', '\x34', '\x56'};

    for (const auto &c : cases) {
        SCOPED_TRACE(c.length);
        for (cat_bool_t mask : {cat_false, cat_true}) {
            cat_websocket_header_t header = {};
            cat_websocket_header_init(&header);
            ASSERT_EQ(header.opcode, 0);
            ASSERT_EQ(header.fin, 0);
            ASSERT_EQ(cat_websocket_header_get_payload_length(&header), 0);
            ASSERT_EQ(cat_websocket_header_get_masking_key(&header), nullptr);
            header.opcode = CAT_WEBSOCKET_OPCODE_BINARY;
            header.fin = 1;
            cat_websocket_header_set_payload_info(&header, c.length, mask ? masking_key : nullptr);

            size_t offset = CAT_WEBSOCKET_HEADER_MIN_SIZE + c.extension_size;
            size_t size = offset + (mask ? CAT_WEBSOCKET_MASKING_KEY_LENGTH : 0);
            ASSERT_EQ(cat_websocket_calculate_masking_key_offset(c.length), offset);
            ASSERT_EQ(cat_websocket_calculate_header_size(c.length, mask), size);
            ASSERT_EQ(cat_websocket_header_get_size(&header), size);
            ASSERT_EQ(cat_websocket_header_get_payload_length(&header), c.length);
            ASSERT_EQ(header.payload_length, c.encoded_length);
            ASSERT_EQ(header.mask, mask);
            ASSERT_EQ(header.opcode, CAT_WEBSOCKET_OPCODE_BINARY);
            ASSERT_EQ(header.fin, 1);
            for (size_t i = 0; i < c.extension_size; i++) {
                ASSERT_EQ(header.extended_payload_length[i],
                    (c.length >> (8 * (c.extension_size - i - 1))) & 0xff);
            }
            const char *key = cat_websocket_header_get_masking_key(&header);
            if (mask) {
                ASSERT_EQ(key, reinterpret_cast<const char *>(&header) + offset);
                ASSERT_EQ(memcmp(key, masking_key, sizeof(masking_key)), 0);
            } else {
                ASSERT_EQ(key, nullptr);
            }
        }
    }
}

TEST(cat_websocket, header_preserves_masking_key)
{
    cat_websocket_header_t header = {};
    cat_websocket_header_init(&header);
    const char key[] = {'\x12', '\0', '\x34', '\x56'};
    cat_websocket_header_set_masking_key(&header, key);

    // Changing length encoding moves the mask past extended lengths and back to the short header.
    for (uint64_t length : {UINT64_C(125), UINT64_C(126), UINT64_C(65536), UINT64_C(0), UINT64_C(65535)}) {
        SCOPED_TRACE(length);
        cat_websocket_header_set_payload_length(&header, length);
        ASSERT_EQ(cat_websocket_header_get_payload_length(&header), length);
        ASSERT_EQ(memcmp(cat_websocket_header_get_masking_key(&header), key, sizeof(key)), 0);
    }
    cat_websocket_header_set_masking_key(&header, nullptr);
    ASSERT_EQ(cat_websocket_header_get_masking_key(&header), nullptr);
    ASSERT_EQ(cat_websocket_header_get_size(&header), 4);
}

TEST(cat_websocket, mask_binary_data)
{
    const char input[] = {'\0', '\x7f', '\x01', '\x02', '\x03', '\0', '\x04'};
    const char key[] = {'\x01', '\0', '\x02', '\x03'};
    for (uint64_t index : {UINT64_C(0), UINT64_C(1), UINT64_C(3), UINT64_C(4)}) {
        char output[sizeof(input)] = {};
        cat_websocket_mask_ex(input, output, sizeof(input), key, index);
        for (size_t i = 0; i < sizeof(input); i++) {
            ASSERT_EQ(output[i], static_cast<char>(input[i] ^ key[(index + i) % sizeof(key)]));
        }
        cat_websocket_unmask_ex(output, sizeof(output), key, index);
        ASSERT_EQ(memcmp(output, input, sizeof(input)), 0);
        cat_websocket_mask_ex(output, output, sizeof(output), key, index);
        cat_websocket_unmask_ex(output, sizeof(output), key, index);
        ASSERT_EQ(memcmp(output, input, sizeof(input)), 0);
        cat_websocket_mask_ex(input, output, 0, key, index);
        ASSERT_EQ(memcmp(output, input, sizeof(input)), 0);
    }
}
