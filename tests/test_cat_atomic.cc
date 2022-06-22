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

#include "cat_atomic.h"

TEST(cat_atomic, base)
{
#define CAT_ATOMIC_BASE_TEST_MAP(XX) \
    XX(int8, 100) \
    XX(uint8, 100) \
    XX(int16, 10000) \
    XX(uint16, 10000) \
    XX(int32, 10000) \
    XX(uint32, 10000) \
    XX(int64, 10000) \
    XX(uint64, 10000) \

#define CAT_ATOMIC_BASE_TEST_GEN(type_name, max_value) do { \
    cat_atomic_##type_name##_t atomic; \
    type_name##_t max = max_value; \
    cat_atomic_##type_name##_init(&atomic, 0); \
    DEFER(cat_atomic_##type_name##_destroy(&atomic)); \
    ASSERT_EQ(cat_atomic_##type_name##_load(&atomic), 0); \
    cat_atomic_##type_name##_store(&atomic, 123); \
    ASSERT_EQ(cat_atomic_##type_name##_exchange(&atomic, max - 1), 123); \
    ASSERT_EQ(cat_atomic_##type_name##_fetch_add(&atomic, 1), max - 1); \
    ASSERT_EQ(cat_atomic_##type_name##_load(&atomic), max); \
    for (auto is_add : std::array<bool, 2>{ false, true }) { \
        type_name##_t value = is_add ? 0 : max; \
        cat_atomic_##type_name##_store(&atomic, value); \
        ASSERT_EQ(cat_atomic_##type_name##_load(&atomic), value); \
        wait_group wg; \
        for (auto c = 0; c < 4; c++) { \
            co([&] { \
                wg++; \
                work(CAT_WORK_KIND_CPU, [&] { \
                    for (type_name##_t i = 0; i < max / 5; i++) { \
                        if (is_add) { \
                            (void) cat_atomic_##type_name##_fetch_add(&atomic, 1); \
                        } else { \
                            (void) cat_atomic_##type_name##_fetch_sub(&atomic, 1); \
                        } \
                        cat_sys_usleep(1); \
                    } \
                }, TEST_IO_TIMEOUT); \
                wg--; \
            }); \
        } \
        for (type_name##_t i = 0; i < max / 5; i++) { \
            if (is_add) { \
                (void) cat_atomic_##type_name##_fetch_add(&atomic, 1); \
            } else { \
                (void) cat_atomic_##type_name##_fetch_sub(&atomic, 1); \
            } \
            cat_sys_usleep(1); \
        } \
        wg(); \
        ASSERT_EQ(cat_atomic_##type_name##_load(&atomic), is_add ? max : 0); \
    } \
} while (0);

    CAT_ATOMIC_BASE_TEST_MAP(CAT_ATOMIC_BASE_TEST_GEN)
}

TEST(cat_atomic, bool)
{
    cat_atomic_bool_t atomic;
    cat_atomic_bool_init(&atomic, cat_false);
    DEFER(cat_atomic_bool_destroy(&atomic));
    ASSERT_FALSE(cat_atomic_bool_exchange(&atomic, cat_true));
    ASSERT_TRUE(cat_atomic_bool_exchange(&atomic, cat_false));
    ASSERT_FALSE(cat_atomic_bool_load(&atomic));
    cat_atomic_bool_store(&atomic, cat_true);
    ASSERT_TRUE(cat_atomic_bool_load(&atomic));
}
