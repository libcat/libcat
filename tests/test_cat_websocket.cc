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
