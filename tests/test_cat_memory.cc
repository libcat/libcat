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

#if GTEST_HAS_DEATH_TEST
[[maybe_unused]] static bool cat_test_is_abort_exit(int status)
{
#if defined(GTEST_OS_WINDOWS) || defined(GTEST_OS_FUCHSIA)
    // These platforms report death-test exit codes rather than POSIX signals.
    return status != 0;
#else
    return testing::KilledBySignal(SIGABRT)(status);
#endif
}
#endif

#if GTEST_HAS_DEATH_TEST && !defined(cat_out_of_memory)
TEST(cat_memory, out_of_memory_terminates_process)
{
    ASSERT_EXIT({
        cat_out_of_memory();
    }, cat_test_is_abort_exit, "Out of memory");
}

TEST(cat_memory, unrecoverable_allocators_oom_terminate_process)
{
    char *original_options = cat_env_get_silent("ASAN_OPTIONS", nullptr);
    DEFER({
        if (original_options == nullptr) {
            EXPECT_EQ(uv_os_unsetenv("ASAN_OPTIONS"), 0);
        } else {
            EXPECT_TRUE(cat_env_set("ASAN_OPTIONS", original_options));
        }
        cat_free(original_options);
    });
    std::string options = original_options == nullptr ? "" : original_options;
    // Re-executed death-test children must let allocation failures reach Libcat.
    options += ":allocator_may_return_null=1";
    ASSERT_TRUE(cat_env_set("ASAN_OPTIONS", options.c_str()));
    for (int operation = 0; operation < 6; operation++) {
        SCOPED_TRACE(operation);
        ASSERT_EXIT({
            volatile size_t allocation_size = SIZE_MAX;
            switch (operation) {
                case 0:
                    (void) cat_sys_malloc_unrecoverable(allocation_size);
                    break;
                case 1:
                    (void) cat_sys_calloc_unrecoverable(2, allocation_size);
                    break;
                case 2: {
                    void *memory = cat_sys_malloc_unrecoverable(CAT_TEST_DEFAULT_MEMORY_SIZE);
                    (void) cat_sys_realloc_unrecoverable(memory, allocation_size);
                    break;
                }
                case 3:
                    (void) cat_malloc_unrecoverable(allocation_size);
                    break;
                case 4:
                    (void) cat_calloc_unrecoverable(2, allocation_size);
                    break;
                case 5: {
                    void *memory = cat_malloc_unrecoverable(CAT_TEST_DEFAULT_MEMORY_SIZE);
                    (void) cat_realloc_unrecoverable(memory, allocation_size);
                    break;
                }
            }
            std::exit(1);
        }, cat_test_is_abort_exit, "Out of memory");
    }
}
#endif

TEST(cat_memory, unrecoverable_allocators_zero_size)
{
    // Zero-sized requests may return null or a freeable pointer without being OOM.
    cat_sys_free(cat_sys_malloc_unrecoverable(0));
    cat_sys_free(cat_sys_calloc_unrecoverable(0, 1));
    cat_sys_free(cat_sys_calloc_unrecoverable(1, 0));
    cat_sys_free(cat_sys_realloc_unrecoverable(nullptr, 0));
    cat_free(cat_malloc_unrecoverable(0));
    cat_free(cat_calloc_unrecoverable(0, 1));
    cat_free(cat_calloc_unrecoverable(1, 0));
    cat_free(cat_realloc_unrecoverable(nullptr, 0));
}

TEST(cat_memory, unrecoverable_allocator_preserves_data)
{
    unsigned char *memory = static_cast<unsigned char *>(cat_calloc_unrecoverable(1, CAT_TEST_DEFAULT_MEMORY_SIZE));
    ASSERT_NE(memory, nullptr);
    DEFER(cat_free(memory));
    for (size_t i = 0; i < CAT_TEST_DEFAULT_MEMORY_SIZE; i++) {
        ASSERT_EQ(memory[i], 0);
        memory[i] = static_cast<unsigned char>(i + 1);
    }
    void *resized = cat_realloc_unrecoverable(memory, CAT_TEST_DEFAULT_MEMORY_SIZE * 2);
    ASSERT_NE(resized, nullptr);
    memory = static_cast<unsigned char *>(resized);
    for (size_t i = 0; i < CAT_TEST_DEFAULT_MEMORY_SIZE; i++) {
        ASSERT_EQ(memory[i], i + 1);
    }
}

#ifdef CAT_USE_DYNAMIC_ALLOCATOR
TEST(cat_memory, invalid_allocator_preserves_registered_functions)
{
    const cat_allocator_t original = cat_allocator;
    DEFER(cat_register_allocator(&original));
    for (unsigned int missing = 0; missing < 4; missing++) {
        SCOPED_TRACE(missing);
        cat_allocator_t invalid = original;
        switch (missing) {
            case 0: invalid.malloc = nullptr; break;
            case 1: invalid.calloc = nullptr; break;
            case 2: invalid.realloc = nullptr; break;
            case 3: invalid.free = nullptr; break;
        }
        cat_bool_t registered = cat_register_allocator(&invalid);
        cat_errno_t error = cat_get_last_error_code();
        cat_allocator_t retained = cat_allocator;
        // Restore before assertions: even reporting a failure may allocate memory.
        ASSERT_TRUE(cat_register_allocator(&original));
        EXPECT_FALSE(registered);
        EXPECT_EQ(error, CAT_EINVAL);
        EXPECT_EQ(retained.malloc, original.malloc);
        EXPECT_EQ(retained.calloc, original.calloc);
        EXPECT_EQ(retained.realloc, original.realloc);
        EXPECT_EQ(retained.free, original.free);
    }
    ASSERT_TRUE(cat_register_allocator(nullptr));
    void *memory = cat_malloc(CAT_TEST_DEFAULT_MEMORY_SIZE);
    ASSERT_NE(memory, nullptr);
    cat_free(memory);
}
#endif

TEST(cat_memory, page_alignment_boundaries)
{
    size_t page_size = cat_getpagesize();
    ASSERT_GT(page_size, 0);
    char *buffer = static_cast<char *>(cat_malloc(page_size * 3));
    ASSERT_NE(buffer, nullptr);
    DEFER(cat_free(buffer));
    uintptr_t aligned = reinterpret_cast<uintptr_t>(cat_getpageafter(buffer));
    const void *page = reinterpret_cast<const void *>(aligned);
    ASSERT_EQ(cat_getpageof(page), page);
    ASSERT_EQ(cat_getpageafter(page), page);
    for (size_t offset : {size_t(1), page_size - 1, page_size, page_size + 1}) {
        const void *address = reinterpret_cast<const void *>(aligned + offset);
        uintptr_t lower = aligned + (offset / page_size) * page_size;
        uintptr_t upper = lower + (offset % page_size == 0 ? 0 : page_size);
        ASSERT_EQ(reinterpret_cast<uintptr_t>(cat_getpageof(address)), lower);
        ASSERT_EQ(reinterpret_cast<uintptr_t>(cat_getpageafter(address)), upper);
    }
}

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

#ifdef CAT_USE_DYNAMIC_ALLOCATOR
TEST(cat_memory, dynamic_allocator_obeys_oom_policy)
{
    const cat_allocator_t original = cat_allocator;
    const cat_allocator_t failing = {
        [](size_t) -> void * { errno = ENOMEM; return nullptr; },
        [](size_t, size_t) -> void * { errno = ENOMEM; return nullptr; },
        [](void *, size_t) -> void * { errno = ENOMEM; return nullptr; },
        original.free,
    };
#if CAT_ALLOC_ABORT_ON_OOM && GTEST_HAS_DEATH_TEST
    ASSERT_EXIT({
        cat_register_allocator(&failing);
        cat_malloc(CAT_TEST_DEFAULT_MEMORY_SIZE);
        std::exit(1);
    }, cat_test_is_abort_exit, "Out of memory");
    ASSERT_EXIT({
        cat_register_allocator(&failing);
        cat_calloc(1, CAT_TEST_DEFAULT_MEMORY_SIZE);
        std::exit(1);
    }, cat_test_is_abort_exit, "Out of memory");
    ASSERT_EXIT({
        cat_register_allocator(&failing);
        cat_realloc(nullptr, CAT_TEST_DEFAULT_MEMORY_SIZE);
        std::exit(1);
    }, cat_test_is_abort_exit, "Out of memory");
#elif !CAT_ALLOC_ABORT_ON_OOM
    ASSERT_TRUE(cat_register_allocator(&failing));
    void *allocated = cat_malloc(CAT_TEST_DEFAULT_MEMORY_SIZE);
    void *zeroed = cat_calloc(1, CAT_TEST_DEFAULT_MEMORY_SIZE);
    void *resized = cat_realloc(nullptr, CAT_TEST_DEFAULT_MEMORY_SIZE);
    // Restore before GoogleTest can allocate to report a result.
    ASSERT_TRUE(cat_register_allocator(&original));
    EXPECT_EQ(allocated, nullptr);
    EXPECT_EQ(zeroed, nullptr);
    EXPECT_EQ(resized, nullptr);
#endif
}
#endif
