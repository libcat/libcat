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
  +--------------------------------------------------------------------------+
 */

#include "test.h"

#define CAT_TEST_DEFAULT_MEMORY_SIZE    16

TEST(cat_memory, malloc_function)
{
    void *ptr = cat_malloc_function(CAT_TEST_DEFAULT_MEMORY_SIZE);

    ASSERT_NE(nullptr, ptr);

    cat_free_function(ptr);
}

TEST(cat_memory, calloc_function)
{
    void *ptr = cat_calloc_function(1, CAT_TEST_DEFAULT_MEMORY_SIZE);

    ASSERT_NE(nullptr, ptr);

    cat_free_function(ptr);
}

TEST(cat_memory, realloc_function)
{
    void *ptr = cat_malloc_function(CAT_TEST_DEFAULT_MEMORY_SIZE);

    ASSERT_NE(nullptr, ptr);
    ptr = cat_realloc_function(ptr, CAT_TEST_DEFAULT_MEMORY_SIZE * 2);
    ASSERT_NE(nullptr, ptr);

    cat_free_function(ptr);
}

TEST(cat_memory, free_function)
{
    void *ptr = cat_malloc_function(CAT_TEST_DEFAULT_MEMORY_SIZE);

    ASSERT_NE(nullptr, ptr);

    cat_free_function(ptr);
    cat_free_function(nullptr);
}

TEST(cat_memory, freep_function)
{
    void *ptr[1];

    ptr[0] = cat_malloc_function(CAT_TEST_DEFAULT_MEMORY_SIZE);
    ASSERT_NE(nullptr, ptr[0]);
    cat_freep_function(ptr);
    cat_freep_function(nullptr);
}

TEST(cat_memory, strdup_function)
{
    char *src;
    char *dst;

    for (int n = 0; n < 2; n++) {
        src = (char *) cat_malloc_function(CAT_TEST_DEFAULT_MEMORY_SIZE);
        DEFER(cat_free_function(src));
        ASSERT_NE(nullptr, src);
        ASSERT_EQ(src, cat_srand(src, CAT_TEST_DEFAULT_MEMORY_SIZE));
        dst = n ? cat_sys_strdup(src) : cat_strdup(src);
        DEFER(if (n) { cat_sys_free(dst); } else { cat_free(dst); });
        ASSERT_NE(nullptr, dst);
        ASSERT_NE(dst, src);
        ASSERT_STREQ(dst, src);
    }
}

TEST(cat_memory, strndup_function)
{
    char src[] = "abcdef";
    char *dst;

    for (int n = 0; n < 2; n++) {
        dst = n ? cat_sys_strndup(src, CAT_STRLEN(src) / 2) : cat_strndup(src, CAT_STRLEN(src) / 2);
        DEFER(if (n) { cat_sys_free(dst); } else { cat_free(dst); });
        ASSERT_NE(nullptr, dst);
        ASSERT_NE(dst, src);
        ASSERT_EQ(std::string(dst), std::string(src, CAT_STRLEN(src) / 2));
    }
}

TEST(cat_memory, getpagesize_function)
{
    size_t size1;
    size_t size2;

    size1 = cat_getpagesize();
    ASSERT_GT(size1, 0);
    size2 = cat_getpagesize();
    ASSERT_GT(size2, 0);
    ASSERT_EQ(size1, size2);
}

TEST(cat_memory, bit_count)
{
    unsigned int count;

    count = cat_bit_count(0B10100101);
    ASSERT_EQ(4, count);

    count = cat_bit_count(0B11111111);
    ASSERT_EQ(8, count);

    count = cat_bit_count(0B00000000);
    ASSERT_EQ(0, count);
}

TEST(cat_memory, bit_pos)
{
    int pos;

    pos = cat_bit_pos(0B10100101);
    ASSERT_EQ(7, pos);

    pos = cat_bit_pos(0B00000001);
    ASSERT_EQ(0, pos);

    pos = cat_bit_pos(0B00000000);
    ASSERT_EQ(-1, pos);
}

TEST(cat_memory, hton64)
{
    uint64_t num;

    num = cat_hton64(1024);
    ASSERT_EQ(1125899906842624ULL, num);
}

TEST(cat_memory, ntoh64)
{
    uint64_t num;

    num = cat_ntoh64(1125899906842624);
    ASSERT_EQ(1024, num);
}
