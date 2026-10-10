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

TEST(cat_buffer, dump_content_and_capacity)
{
    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_TEST_DEFAULT_BUFFER_SIZE));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_append(&buffer, CAT_STRL("abc")));
    testing::internal::CaptureStdout();
    cat_buffer_dump(&buffer);
    const std::string output = testing::internal::GetCapturedStdout();
    ASSERT_NE(output.find("string(3) \"abc\""), std::string::npos);
    ASSERT_NE(output.find("size_t(3)"), std::string::npos);
    ASSERT_NE(output.find(string_format("size_t(%zu)", buffer.size)), std::string::npos);
    ASSERT_EQ(buffer.length, 3);
    ASSERT_EQ(std::string(buffer.value, buffer.length), "abc");
}
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
    memset(src, 'a', CAT_TEST_DEFAULT_BUFFER_SIZE);

    dst = cat_buffer_allocator.realloc_function(src, CAT_TEST_DEFAULT_BUFFER_SIZE * 2);
    DEFER(cat_buffer_allocator.free_function(dst));
    ASSERT_NE(nullptr, dst);
    // realloc may retain the original address; verify data and writable capacity.
    ASSERT_EQ(std::string(dst, CAT_TEST_DEFAULT_BUFFER_SIZE), std::string(CAT_TEST_DEFAULT_BUFFER_SIZE, 'a'));
    memset(dst + CAT_TEST_DEFAULT_BUFFER_SIZE, 'b', CAT_TEST_DEFAULT_BUFFER_SIZE);
    ASSERT_EQ(std::string(dst + CAT_TEST_DEFAULT_BUFFER_SIZE, CAT_TEST_DEFAULT_BUFFER_SIZE), std::string(CAT_TEST_DEFAULT_BUFFER_SIZE, 'b'));
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

    dst = cat_buffer_allocator.realloc_function(src, CAT_TEST_DEFAULT_BUFFER_SIZE + 1);
    DEFER(cat_buffer_allocator.free_function(dst));
    ASSERT_NE(nullptr, dst);

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

    dst = cat_buffer_allocator.realloc_function(src, CAT_TEST_DEFAULT_BUFFER_SIZE / 2 + 1);
    DEFER(cat_buffer_allocator.free_function(dst));
    ASSERT_NE(nullptr, dst);

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

TEST(cat_buffer, truncate_value_not_allocated)
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

    cat_buffer_truncate_from(&buffer, 1, SIZE_MAX);
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

TEST(cat_buffer, clear_not_allocated)
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

TEST(cat_buffer, close_not_allocated)
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
    cat_buffer_t read_buffer = { };
    cat_buffer_t write_buffer = { };

    ASSERT_TRUE(cat_buffer_make_pair(&read_buffer, CAT_TEST_DEFAULT_BUFFER_SIZE, &write_buffer, CAT_TEST_DEFAULT_BUFFER_SIZE));
    DEFER(cat_buffer_close(&read_buffer));
    DEFER(cat_buffer_close(&write_buffer));
    ASSERT_EQ(read_buffer, write_buffer);
}

TEST(cat_buffer, append_formatted_values)
{
    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 1));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_append_unsigned(&buffer, 42));
    ASSERT_TRUE(cat_buffer_append_char(&buffer, ':'));
    ASSERT_TRUE(cat_buffer_append_signed(&buffer, -17));
    ASSERT_TRUE(cat_buffer_append_str(&buffer, ":"));
    ASSERT_TRUE(cat_buffer_append_double(&buffer, 1.5));
    ASSERT_TRUE(cat_buffer_append_printf(&buffer, ":%s:%d", "item", 3));
    ASSERT_TRUE(cat_buffer_zero_terminate(&buffer));
    ASSERT_STREQ(buffer.value, "42:-17:1.500000:item:3");
    ASSERT_EQ(buffer.length, strlen(buffer.value));
    ASSERT_EQ(cat_buffer_export_str(&buffer), buffer.value);
}

TEST(cat_buffer, append_with_padding)
{
    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_append_str_with_padding(&buffer, "abc", '.', 8));
    ASSERT_EQ(std::string(buffer.value, buffer.length), "..abc...");
    cat_buffer_clear(&buffer);
    ASSERT_TRUE(cat_buffer_append_with_padding(&buffer, CAT_STRL("ab"), '-', 2));
    ASSERT_EQ(std::string(buffer.value, buffer.length), "ab");
}

TEST(cat_buffer, realloc_failure_preserves_data)
{
    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 16));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_append(&buffer, CAT_STRL("content")));
    char *value = buffer.value;
    size_t size = buffer.size;
    size_t length = buffer.length;
    cat_buffer_allocator_t allocator = cat_buffer_allocator;
    DEFER(cat_buffer_register_allocator(&allocator));
    cat_buffer_allocator_t failing_allocator = allocator;
    failing_allocator.realloc_function = [](char *, size_t) -> char * { return nullptr; };
    ASSERT_TRUE(cat_buffer_register_allocator(&failing_allocator));

    // 扩容失败不能丢弃已经写入的内容，也不能提前更新 size／length。
    ASSERT_FALSE(cat_buffer_realloc(&buffer, size * 2));
    ASSERT_FALSE(cat_buffer_write(&buffer, size, CAT_STRL("more")));
    ASSERT_FALSE(cat_buffer_append_printf(&buffer, "%032d", 1));
    ASSERT_EQ(buffer.value, value);
    ASSERT_EQ(buffer.size, size);
    ASSERT_EQ(buffer.length, length);
    ASSERT_EQ(std::string(buffer.value, buffer.length), "content");
}

TEST(cat_buffer, pair_allocation_failure)
{
    cat_buffer_allocator_t allocator = cat_buffer_allocator;
    DEFER(cat_buffer_register_allocator(&allocator));
    cat_buffer_allocator_t failing_allocator = allocator;
    // 只让第二个 buffer 的分配失败，验证第一个 buffer 会被释放。
    failing_allocator.alloc_function = [](size_t size) -> char * {
        return size == 16 ? nullptr : static_cast<char *>(cat_malloc(size));
    };
    failing_allocator.free_function = [](char *value) { cat_free(value); };
    ASSERT_TRUE(cat_buffer_register_allocator(&failing_allocator));
    cat_buffer_t first = {}, second = {};
    ASSERT_FALSE(cat_buffer_make_pair(&first, 8, &second, 16));
    ASSERT_EQ(first.value, nullptr);
    ASSERT_EQ(first.length, 0);
    ASSERT_EQ(first.size, 0);
    ASSERT_EQ(second.value, nullptr);
}

TEST(cat_buffer, allocator_receives_length_updates)
{
    const unsigned int debug_level = CAT_LOG_G(debug_level);
    DEFER(CAT_LOG_G(debug_level) = debug_level);
    CAT_LOG_G(debug_level) = 0;
    const cat_buffer_allocator_t original = cat_buffer_allocator;
    DEFER(cat_buffer_register_allocator(&original));
    static char *updated_value;
    static size_t updated_length, updates;
    updated_value = nullptr;
    updated_length = updates = 0;
    cat_buffer_allocator_t allocator = original;
    allocator.update_function = [](char *value, size_t length) {
        updated_value = value;
        updated_length = length;
        updates++;
    };
    ASSERT_TRUE(cat_buffer_register_allocator(&allocator));
    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, CAT_TEST_DEFAULT_BUFFER_SIZE));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_append(&buffer, CAT_STRL("abc")));
    ASSERT_EQ(updates, 1);
    ASSERT_EQ(updated_value, buffer.value);
    ASSERT_EQ(updated_length, buffer.length);
    ASSERT_EQ(updated_length, 3);
    // Overwriting existing bytes does not change the length reported to the allocator.
    ASSERT_TRUE(cat_buffer_write(&buffer, 1, CAT_STRL("Z")));
    ASSERT_EQ(updates, 1);
    ASSERT_EQ(std::string(buffer.value, buffer.length), "aZc");
    cat_buffer_truncate(&buffer, 1);
    ASSERT_EQ(updates, 2);
    ASSERT_EQ(updated_length, 1);
    ASSERT_EQ(updated_value, buffer.value);
    cat_buffer_clear(&buffer);
    ASSERT_EQ(updates, 3);
    ASSERT_EQ(updated_length, 0);
    ASSERT_EQ(updated_value, buffer.value);
}
