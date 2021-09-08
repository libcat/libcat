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
  | Author: codinghuang <2812240764@qq.com>                                  |
  |         Twosee <twosee@php.net>                                          |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

#define CAT_TEST_DEFAULT_BUFFER_SIZE         16
#define CAT_TEST_MEMORY_DEFAULT_ALIGNED_SIZE (CAT_MEMORY_DEFAULT_ALIGNED_SIZE / 2)

bool operator==(const cat_buffer_allocator_t& lhs, const cat_buffer_allocator_t& rhs)
{
    return lhs.alloc_function == rhs.alloc_function && lhs.free_function == rhs.free_function && lhs.realloc_function == rhs.realloc_function && lhs.update_function == rhs.update_function;
}

bool operator==(const cat_buffer_t& lhs, const cat_buffer_t& rhs)
{
    // TODO: cat_buffer_equal
    return lhs.size == rhs.size && lhs.length == rhs.length && ((lhs.value == nullptr && rhs.value == nullptr) || memcmp(lhs.value, rhs.value, rhs.length) == 0);
}

static const cat_buffer_t empty_buffer = { };

TEST(cat_buffer, alloc_standard)
{
    char *prt;

    prt = cat_buffer_allocator.alloc_function(CAT_TEST_DEFAULT_BUFFER_SIZE);
    ASSERT_NE(nullptr, prt);
    cat_buffer_allocator.free_function(prt);
}

TEST(cat_buffer, realloc_standard)
{
    char *src;
    char *dst;

    src = cat_buffer_allocator.alloc_function(CAT_TEST_DEFAULT_BUFFER_SIZE);
    ASSERT_NE(nullptr, src);

    dst = cat_buffer_allocator.realloc_function(src, CAT_TEST_DEFAULT_BUFFER_SIZE, CAT_TEST_DEFAULT_BUFFER_SIZE * 2);
    DEFER(cat_buffer_allocator.free_function(dst));
    ASSERT_NE(nullptr, dst);
    ASSERT_NE(src, dst);
}

TEST(cat_buffer, realloc_standard_data)
{
    char *src;
    char *dst;
    /* src will be free, so we need src_tmp */
    char src_tmp[CAT_TEST_DEFAULT_BUFFER_SIZE + 1];

    src = cat_buffer_allocator.alloc_function(CAT_TEST_DEFAULT_BUFFER_SIZE + 1);
    ASSERT_NE(nullptr, src);

    for (unsigned char i = 0; i < CAT_TEST_DEFAULT_BUFFER_SIZE; i++) {
        src[i] = 'a' + i;
        src_tmp[i] = src[i];
    }

    dst = cat_buffer_allocator.realloc_function(src, CAT_TEST_DEFAULT_BUFFER_SIZE, CAT_TEST_DEFAULT_BUFFER_SIZE + 1);
    DEFER(cat_buffer_allocator.free_function(dst));
    ASSERT_NE(nullptr, dst);
    ASSERT_NE(src, dst);

    src_tmp[CAT_TEST_DEFAULT_BUFFER_SIZE] = 0;
    dst[CAT_TEST_DEFAULT_BUFFER_SIZE] = 0;
    ASSERT_STREQ(src_tmp, dst);
}

TEST(cat_buffer, realloc_standard_less_data)
{
    char *src;
    char *dst;
    /* src will be free, so we need src_tmp */
    char src_tmp[CAT_TEST_DEFAULT_BUFFER_SIZE + 1];

    src = cat_buffer_allocator.alloc_function(CAT_TEST_DEFAULT_BUFFER_SIZE + 1);
    ASSERT_NE(nullptr, src);

    for (unsigned char i = 0; i < CAT_TEST_DEFAULT_BUFFER_SIZE; i++) {
        src[i] = 'a' + i;
        src_tmp[i] = src[i];
    }

    dst = cat_buffer_allocator.realloc_function(src, CAT_TEST_DEFAULT_BUFFER_SIZE, CAT_TEST_DEFAULT_BUFFER_SIZE / 2 + 1);
    DEFER(cat_buffer_allocator.free_function(dst));
    ASSERT_NE(nullptr, dst);
    ASSERT_NE(src, dst);

    src_tmp[CAT_TEST_DEFAULT_BUFFER_SIZE / 2] = 0;
    dst[CAT_TEST_DEFAULT_BUFFER_SIZE / 2] = 0;
    ASSERT_STREQ(src_tmp, dst);
}

TEST(cat_buffer, free_standard)
{
    char *prt;

    prt = cat_buffer_allocator.alloc_function(CAT_TEST_DEFAULT_BUFFER_SIZE);
    ASSERT_NE(nullptr, prt);
    cat_buffer_allocator.free_function(prt);
}

TEST(cat_buffer, module_init)
{
    ASSERT_TRUE(cat_buffer_module_init());
}

TEST(cat_buffer, register_allocator_not_fill)
{
    cat_buffer_allocator_t allocator = { };

    ASSERT_FALSE(cat_buffer_register_allocator(&allocator));
}

TEST(cat_buffer, register_allocator_fill)
{
    cat_buffer_allocator_t origin_cat_buffer_allocator = cat_buffer_allocator;

    cat_buffer_allocator_t allocator =
            { (cat_buffer_alloc_function_t) 0X1, (cat_buffer_realloc_function_t) 0X1, (cat_buffer_update_function_t) 0X1, (cat_buffer_free_function_t) 0X1 };

    ASSERT_TRUE(cat_buffer_register_allocator(&allocator));
    ASSERT_EQ(cat_buffer_allocator, allocator);

    /* register origin allocator */
    ASSERT_TRUE(cat_buffer_register_allocator(&origin_cat_buffer_allocator));
}

TEST(cat_buffer, align_size)
{
    size_t ret;

    /* CAT_MEMORY_ALIGNED_SIZE(1), align CAT_MEMORY_DEFAULT_ALIGNED_SIZE (sizeof(long)) */
    ret = cat_buffer_align_size(0, 0);
    ASSERT_EQ(ret, CAT_MEMORY_DEFAULT_ALIGNED_SIZE);

    /* CAT_MEMORY_ALIGNED_SIZE(CAT_MEMORY_DEFAULT_ALIGNED_SIZE + 1), align CAT_MEMORY_DEFAULT_ALIGNED_SIZE (sizeof(long)) */
    ret = cat_buffer_align_size(CAT_MEMORY_DEFAULT_ALIGNED_SIZE + 1, 0);
    ASSERT_EQ(ret, CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2);

    /* CAT_MEMORY_ALIGNED_SIZE(CAT_TEST_MEMORY_DEFAULT_ALIGNED_SIZE - 1), align CAT_TEST_MEMORY_DEFAULT_ALIGNED_SIZE (4) */
    ret = cat_buffer_align_size(CAT_TEST_MEMORY_DEFAULT_ALIGNED_SIZE - 1, CAT_TEST_MEMORY_DEFAULT_ALIGNED_SIZE);
    ASSERT_EQ(ret, CAT_TEST_MEMORY_DEFAULT_ALIGNED_SIZE);
}

TEST(cat_buffer, init)
{
    cat_buffer_t buffer = { };

    cat_buffer_init(&buffer);
    ASSERT_EQ(empty_buffer, buffer);
}

TEST(cat_buffer, alloc)
{
    cat_buffer_t buffer = { };

    ASSERT_TRUE(cat_buffer_alloc(&buffer, CAT_TEST_DEFAULT_BUFFER_SIZE));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_EQ(0, buffer.length);
    ASSERT_EQ(CAT_TEST_DEFAULT_BUFFER_SIZE, buffer.size);
    ASSERT_NE(nullptr, buffer.value);

    /* buffer::value has been allocated, don't create a new buffer::value */
    ASSERT_FALSE(cat_buffer_alloc(&buffer, CAT_TEST_DEFAULT_BUFFER_SIZE));
}

TEST(cat_buffer, alloc_zero_size)
{
    cat_buffer_t buffer = { };

    ASSERT_TRUE(cat_buffer_alloc(&buffer, 0));
}

TEST(cat_buffer, create)
{
    cat_buffer_t buffer = { };

    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_TEST_DEFAULT_BUFFER_SIZE));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_EQ(0, buffer.length);
    ASSERT_EQ(CAT_TEST_DEFAULT_BUFFER_SIZE, buffer.size);
    ASSERT_NE(nullptr, buffer.value);
}

TEST(cat_buffer, create_double)
{
    cat_buffer_t buffer = { };
    char *ptr1;
    char *ptr2;

    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_TEST_DEFAULT_BUFFER_SIZE));
    ASSERT_EQ(0, buffer.length);
    ASSERT_EQ(CAT_TEST_DEFAULT_BUFFER_SIZE, buffer.size);
    ASSERT_NE(nullptr, buffer.value);
    cat_buffer_close(&buffer);

    ptr1 = buffer.value;
    /* create a new buffer::value */
    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_TEST_DEFAULT_BUFFER_SIZE));
    ptr2 = buffer.value;
    ASSERT_NE(ptr1, ptr2);
    cat_buffer_close(&buffer);
}

TEST(cat_buffer, realloc_same_size)
{
    cat_buffer_t buffer = { };
    char *ptr1;
    char *ptr2;

    ASSERT_TRUE(cat_buffer_alloc(&buffer, CAT_TEST_DEFAULT_BUFFER_SIZE));
    DEFER(cat_buffer_close(&buffer));
    ptr1 = buffer.value;
    ASSERT_TRUE(cat_buffer_realloc(&buffer, CAT_TEST_DEFAULT_BUFFER_SIZE));
    ptr2 = buffer.value;
    ASSERT_EQ(0, buffer.length);
    ASSERT_EQ(CAT_TEST_DEFAULT_BUFFER_SIZE, buffer.size);
    ASSERT_NE(nullptr, buffer.value);
    ASSERT_EQ(ptr1, ptr2);
}

TEST(cat_buffer, realloc_not_same_size)
{
    cat_buffer_t buffer = { };
    char *ptr1;
    char *ptr2;

    ASSERT_TRUE(cat_buffer_alloc(&buffer, CAT_TEST_DEFAULT_BUFFER_SIZE));
    DEFER(cat_buffer_close(&buffer));
    ptr1 = buffer.value;
    ASSERT_TRUE(cat_buffer_realloc(&buffer, CAT_TEST_DEFAULT_BUFFER_SIZE * 2));
    ptr2 = buffer.value;
    ASSERT_EQ(0, buffer.length);
    ASSERT_EQ(CAT_TEST_DEFAULT_BUFFER_SIZE * 2, buffer.size);
    ASSERT_NE(nullptr, buffer.value);
    ASSERT_NE(ptr1, ptr2);
}

TEST(cat_buffer, realloc_zero_size)
{
    cat_buffer_t buffer = { };

    ASSERT_TRUE(cat_buffer_alloc(&buffer, CAT_TEST_DEFAULT_BUFFER_SIZE));
    ASSERT_TRUE(cat_buffer_realloc(&buffer, 0));
    ASSERT_EQ(empty_buffer, buffer);
}

TEST(cat_buffer, realloc_less_length)
{
    cat_buffer_t buffer = { };
    char data[] = "hello";

    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_write(&buffer, 0, data, sizeof(data)));
    ASSERT_TRUE(cat_buffer_realloc(&buffer, sizeof(data) - 1));
}

TEST(cat_buffer, extend_zero_size)
{
    cat_buffer_t buffer = { };

    ASSERT_TRUE(cat_buffer_extend(&buffer, CAT_TEST_MEMORY_DEFAULT_ALIGNED_SIZE * 2 + 1));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_EQ(0, buffer.length);
    /* if buffer::size is 0, buffer::size will align CAT_MEMORY_DEFAULT_ALIGNED_SIZE */
    ASSERT_EQ(CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2, buffer.size);
    ASSERT_NE(nullptr, buffer.value);
}

TEST(cat_buffer, extend_no_zero_size)
{
    cat_buffer_t buffer = { };

    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_TEST_MEMORY_DEFAULT_ALIGNED_SIZE));
    DEFER(cat_buffer_close(&buffer));

    ASSERT_TRUE(cat_buffer_extend(&buffer, CAT_TEST_MEMORY_DEFAULT_ALIGNED_SIZE * 2 + 1));
    ASSERT_EQ(0, buffer.length);
    ASSERT_EQ(CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2, buffer.size);
    ASSERT_NE(nullptr, buffer.value);
}

TEST(cat_buffer, malloc_trim)
{
    cat_buffer_t buffer = { };

    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_TEST_MEMORY_DEFAULT_ALIGNED_SIZE));
    DEFER(cat_buffer_close(&buffer));
    buffer.length = CAT_TEST_MEMORY_DEFAULT_ALIGNED_SIZE - 1;

    ASSERT_TRUE(cat_buffer_malloc_trim(&buffer));
    ASSERT_EQ(CAT_TEST_MEMORY_DEFAULT_ALIGNED_SIZE - 1, buffer.length);
    ASSERT_EQ(CAT_TEST_MEMORY_DEFAULT_ALIGNED_SIZE - 1, buffer.size);
    ASSERT_NE(nullptr, buffer.value);

    /* do nothing */
    ASSERT_TRUE(cat_buffer_malloc_trim(&buffer));
    ASSERT_EQ(CAT_TEST_MEMORY_DEFAULT_ALIGNED_SIZE - 1, buffer.length);
    ASSERT_EQ(CAT_TEST_MEMORY_DEFAULT_ALIGNED_SIZE - 1, buffer.size);
    ASSERT_NE(nullptr, buffer.value);
}

TEST(cat_buffer, write)
{
    cat_buffer_t buffer = { };
    char data[] = "hello";

    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_TEST_DEFAULT_BUFFER_SIZE * 2));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_write(&buffer, 0, data, sizeof(data)));
    ASSERT_EQ(sizeof(data), buffer.length);
    ASSERT_NE(nullptr, buffer.value);
    ASSERT_STREQ(data, buffer.value);
}

TEST(cat_buffer, write_greater_size)
{
    cat_buffer_t buffer = { };
    char data[CAT_TEST_DEFAULT_BUFFER_SIZE];

    ASSERT_EQ(data, cat_srand(CAT_STRS(data)));
    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_TEST_DEFAULT_BUFFER_SIZE));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_write(&buffer, 0, data, sizeof(data)));
    ASSERT_EQ(sizeof(data), buffer.length);
    ASSERT_NE(nullptr, buffer.value);
    ASSERT_STREQ(data, buffer.value);
}

TEST(cat_buffer, write_not_allocated)
{
    cat_buffer_t buffer = { };
    char data[CAT_TEST_DEFAULT_BUFFER_SIZE];

    ASSERT_EQ(data, cat_srand(CAT_STRS(data)));
    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_TEST_DEFAULT_BUFFER_SIZE));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_write(&buffer, 0, data, sizeof(data)));
    ASSERT_EQ(sizeof(data), buffer.length);
    ASSERT_NE(nullptr, buffer.value);
    ASSERT_STREQ(data, buffer.value);
}

TEST(cat_buffer, append)
{
    cat_buffer_t buffer = { };
    char data[] = "hello";
    char append_data[] = "world";
    char expect[] = "hello\0world";

    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_write(&buffer, 0, data, sizeof(data)));
    ASSERT_TRUE(cat_buffer_append(&buffer, append_data, sizeof(append_data)));
    ASSERT_EQ(sizeof(data) + sizeof(append_data), buffer.length);
    ASSERT_NE(nullptr, buffer.value);
    ASSERT_STREQ(expect, buffer.value);
}

TEST(cat_buffer, truncate_value_not_allocted)
{
    cat_buffer_t buffer = { .value = nullptr, .size = CAT_TEST_DEFAULT_BUFFER_SIZE, .length = 0 };

    /* do nothing */
    cat_buffer_truncate(&buffer, CAT_TEST_DEFAULT_BUFFER_SIZE);
    ASSERT_EQ(CAT_TEST_DEFAULT_BUFFER_SIZE, buffer.size);
    ASSERT_EQ(0, buffer.length);
    ASSERT_EQ(nullptr, buffer.value);
}

TEST(cat_buffer, truncate_start_gt_length)
{
    cat_buffer_t buffer = { };

    char data[] = "hello";

    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_write(&buffer, 0, data, sizeof(data)));

    cat_buffer_truncate_from(&buffer, buffer.length + 1, buffer.length);
    ASSERT_EQ(CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2, buffer.size);
    ASSERT_EQ(0, buffer.length);
    ASSERT_STREQ(data, buffer.value);
}

TEST(cat_buffer, truncate_length_eq_zero)
{
    cat_buffer_t buffer = { };

    char data[] = "hello";

    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_write(&buffer, 0, data, sizeof(data)));

    cat_buffer_truncate_from(&buffer, 1, 0);
    ASSERT_EQ(CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2, buffer.size);
    ASSERT_EQ(sizeof(data), buffer.length + 1);
    ASSERT_STREQ(data + 1, buffer.value);
}

TEST(cat_buffer, truncate_equal_length)
{
    cat_buffer_t buffer = { };
    char data[] = "hello";

    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_write(&buffer, 0, data, sizeof(data)));
    cat_buffer_truncate(&buffer, buffer.length);
    ASSERT_EQ(CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2, buffer.size);
    ASSERT_EQ(6, buffer.length);
    ASSERT_NE(nullptr, buffer.value);
}

TEST(cat_buffer, clear)
{
    cat_buffer_t buffer = { };
    char data[] = "hello";

    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_write(&buffer, 0, data, sizeof(data)));
    cat_buffer_clear(&buffer);
    ASSERT_EQ(CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2, buffer.size);
    ASSERT_EQ(0, buffer.length);
    ASSERT_NE(nullptr, buffer.value);
}

TEST(cat_buffer, clear_not_allocted)
{
    cat_buffer_t buffer = { };

    cat_buffer_clear(&buffer);
    ASSERT_EQ(empty_buffer, buffer);
}

TEST(cat_buffer, fetch)
{
    cat_buffer_t buffer = { };
    char data[] = "hello";
    char *prt;

    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_write(&buffer, 0, data, sizeof(data)));
    prt = cat_buffer_fetch(&buffer);
    DEFER(cat_buffer_allocator.free_function(prt));
    ASSERT_STREQ(prt, data);
    ASSERT_EQ(empty_buffer, buffer);
}

TEST(cat_buffer, dup)
{
    cat_buffer_t src_buffer = { };
    cat_buffer_t dst_buffer = { };
    char data[] = "hello";

    ASSERT_TRUE(cat_buffer_create(&src_buffer, CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2));
    DEFER(cat_buffer_close(&src_buffer));
    ASSERT_TRUE(cat_buffer_write(&src_buffer, 0, data, sizeof(data)));
    cat_buffer_dup(&src_buffer, &dst_buffer);
    DEFER(cat_buffer_close(&dst_buffer));
    ASSERT_EQ(src_buffer.size, dst_buffer.size);
    ASSERT_EQ(src_buffer.length, dst_buffer.length);
    ASSERT_NE(src_buffer.value, dst_buffer.value);
    ASSERT_TRUE(memcmp(src_buffer.value, dst_buffer.value, src_buffer.length) == 0);
}

TEST(cat_buffer, close)
{
    cat_buffer_t buffer = { };
    char data[] = "hello";

    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2));
    ASSERT_TRUE(cat_buffer_write(&buffer, 0, data, sizeof(data)));
    cat_buffer_close(&buffer);
    ASSERT_EQ(empty_buffer, buffer);
}

TEST(cat_buffer, close_not_allocted)
{
    cat_buffer_t buffer = { };

    cat_buffer_close(&buffer);
    ASSERT_EQ(empty_buffer, buffer);
}

TEST(cat_buffer, get_value)
{
    cat_buffer_t buffer = { };
    char data[] = "hello";
    char *ptr;

    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_write(&buffer, 0, data, sizeof(data)));
    ptr = cat_buffer_get_value(&buffer);
    ASSERT_EQ(buffer.value, ptr);
}

TEST(cat_buffer, get_size)
{
    cat_buffer_t buffer = { };
    char data[] = "hello";
    size_t size;

    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_write(&buffer, 0, data, sizeof(data)));
    size = cat_buffer_get_size(&buffer);
    ASSERT_EQ(buffer.size, size);
}

TEST(cat_buffer, get_length)
{
    cat_buffer_t buffer = { };
    char data[] = "hello";
    size_t length;

    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_MEMORY_DEFAULT_ALIGNED_SIZE * 2));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_write(&buffer, 0, data, sizeof(data)));
    length = cat_buffer_get_length(&buffer);
    ASSERT_EQ(buffer.length, length);
}

TEST(cat_buffer, make_pair)
{
    cat_buffer_t rbuffer = { };
    cat_buffer_t wbuffer = { };

    ASSERT_TRUE(cat_buffer_make_pair(&rbuffer, CAT_TEST_DEFAULT_BUFFER_SIZE, &wbuffer, CAT_TEST_DEFAULT_BUFFER_SIZE));
    DEFER(cat_buffer_close(&rbuffer));
    DEFER(cat_buffer_close(&wbuffer));
    ASSERT_EQ(rbuffer, wbuffer);
}
