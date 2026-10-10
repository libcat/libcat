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
#include <algorithm>

#include "cat_atomic.h"

extern "C" const char *cat_test_atomic_c_operations(cat_timeout_t timeout);
extern "C" const char *cat_test_atomic_c_publication(cat_timeout_t timeout);
extern "C" int cat_test_atomic_c_validates_orders(void);
extern "C" int cat_test_atomic_c_rejects_exchange_consume(void);
extern "C" void cat_test_atomic_c_invalid_order(int operation, cat_atomic_memory_order_t order);

TEST(cat_atomic, c_operations)
{
    const char *failure = cat_test_atomic_c_operations(TEST_IO_TIMEOUT);
    ASSERT_EQ(failure, nullptr) << failure;
}

TEST(cat_atomic, c_release_acquire_publication)
{
    const char *failure = cat_test_atomic_c_publication(TEST_IO_TIMEOUT);
    ASSERT_EQ(failure, nullptr) << failure;
}

#if GTEST_HAS_DEATH_TEST
TEST(cat_atomic, c_invalid_memory_orders)
{
    SKIP_IF_(cat_test_atomic_c_validates_orders() == 0, "This atomic backend accepts stronger memory orders");
    const struct {
        int operation;
        cat_atomic_memory_order_t order;
        const char *message;
    } cases[] = {
        {0, CAT_ATOMIC_MEMORY_ORDER_ACQUIRE, "Invalid memory order for store operation"},
        {0, CAT_ATOMIC_MEMORY_ORDER_CONSUME, "Invalid memory order for store operation"},
        {0, CAT_ATOMIC_MEMORY_ORDER_ACQ_REL, "Invalid memory order for store operation"},
        {1, CAT_ATOMIC_MEMORY_ORDER_RELEASE, "Invalid memory order for load operation"},
        {1, CAT_ATOMIC_MEMORY_ORDER_ACQ_REL, "Invalid memory order for load operation"},
        {0, static_cast<cat_atomic_memory_order_t>(INT_MAX), "Unknown memory order"},
        {1, static_cast<cat_atomic_memory_order_t>(INT_MAX), "Unknown memory order"},
        {2, static_cast<cat_atomic_memory_order_t>(INT_MAX), "Unknown memory order"},
    };
    for (const auto &entry : cases) {
        SCOPED_TRACE(entry.operation);
        SCOPED_TRACE(static_cast<int>(entry.order));
        ASSERT_DEATH(cat_test_atomic_c_invalid_order(entry.operation, entry.order), entry.message);
    }
    if (cat_test_atomic_c_rejects_exchange_consume()) {
        ASSERT_DEATH(cat_test_atomic_c_invalid_order(2, CAT_ATOMIC_MEMORY_ORDER_CONSUME),
            "Invalid memory order for exchange operation");
    }
}
#endif

TEST(cat_atomic, base)
{
#define CAT_ATOMIC_NUMERIC_TEST_MAP(XX) \
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
    ASSERT_EQ(cat_atomic_##type_name##_load(&atomic), 0); \
    cat_atomic_##type_name##_store(&atomic, 123); \
    ASSERT_EQ(cat_atomic_##type_name##_exchange(&atomic, max - 1), 123); \
    ASSERT_EQ(cat_atomic_##type_name##_fetch_add(&atomic, 1), max - 1); \
    ASSERT_EQ(cat_atomic_##type_name##_load(&atomic), max); \
    for (auto is_add : std::array<bool, 2>{ false, true }) { \
        /* FIXME: enhance work(), prefer dynamic expansion, \
         * there are not always enough available workers, \
         * some while it lead to dead-lock.  */    \
        const size_t c_max = 4; \
        type_name##_t value = is_add ? 0 : max; \
        cat_atomic_##type_name##_store(&atomic, value); \
        ASSERT_EQ(cat_atomic_##type_name##_load(&atomic), value); \
        wait_group wg; \
        uv_cond_t cond; \
        uv_mutex_t mutex; \
        ASSERT_EQ(uv_cond_init(&cond), 0); \
        DEFER2(uv_cond_destroy(&cond), 0); \
        ASSERT_EQ(uv_mutex_init(&mutex), 0); \
        DEFER2(uv_mutex_destroy(&mutex), 1); \
        for (auto c = 0; c < c_max; c++) { \
            co([&] { \
                wg++; \
                work(CAT_WORK_KIND_CPU, [&] { \
                    uv_mutex_lock(&mutex); \
                    uv_cond_timedwait(&cond, &mutex, 100 * 1000 * 1000); \
                    uv_mutex_unlock(&mutex); \
                    for (type_name##_t i = 0; i < max / (c_max + 1); i++) { \
                        if (is_add) { \
                            (void) cat_atomic_##type_name##_fetch_add(&atomic, 1); \
                        } else { \
                            (void) cat_atomic_##type_name##_fetch_sub(&atomic, 1); \
                        } \
                    } \
                }, TEST_IO_TIMEOUT); \
                wg--; \
            }); \
        } \
        ASSERT_EQ(cat_sys_usleep(10 * 1000), 0);  \
        uv_cond_broadcast(&cond); \
        for (type_name##_t i = 0; i < max / (c_max + 1); i++) { \
            if (is_add) { \
                (void) cat_atomic_##type_name##_fetch_add(&atomic, 1); \
            } else { \
                (void) cat_atomic_##type_name##_fetch_sub(&atomic, 1); \
            } \
        } \
        wg(); \
        ASSERT_EQ(cat_atomic_##type_name##_load(&atomic), is_add ? max : 0); \
    } \
} while (0);

    CAT_ATOMIC_NUMERIC_TEST_MAP(CAT_ATOMIC_BASE_TEST_GEN)
}

typedef cat_bool_t (*atomic_bool_compare_exchange_t)(cat_atomic_bool_t *atomic, cat_bool_t *expected, cat_bool_t desired);

TEST(cat_atomic, bool)
{
    cat_atomic_bool_t atomic;
    cat_atomic_bool_init(&atomic, cat_false);
    ASSERT_FALSE(cat_atomic_bool_exchange(&atomic, cat_true));
    ASSERT_TRUE(cat_atomic_bool_exchange(&atomic, cat_false));
    ASSERT_FALSE(cat_atomic_bool_load(&atomic));
    cat_atomic_bool_store(&atomic, cat_true);
    ASSERT_TRUE(cat_atomic_bool_load(&atomic));

    for (auto atomic_bool_compare_exchange_function : std::array<atomic_bool_compare_exchange_t, 2>{
            cat_atomic_bool_compare_exchange_strong,
            cat_atomic_bool_compare_exchange_weak
    }) {
        cat_bool_t expected = cat_false;
        cat_atomic_bool_store(&atomic, cat_false);
        ASSERT_TRUE(atomic_bool_compare_exchange_function(&atomic, &expected, cat_false));
        ASSERT_EQ(cat_atomic_bool_load(&atomic), cat_false);
        expected = cat_false;
        ASSERT_TRUE(atomic_bool_compare_exchange_function(&atomic, &expected, cat_true));
        ASSERT_EQ(cat_atomic_bool_load(&atomic), cat_true);
        ASSERT_FALSE(atomic_bool_compare_exchange_function(&atomic, &expected, cat_false));
        ASSERT_EQ(expected, cat_true);
        ASSERT_EQ(cat_atomic_bool_load(&atomic), cat_true);
    }
}

typedef cat_bool_t (*atomic_ptr_compare_exchange_t)(cat_atomic_ptr_t *atomic, cat_ptr_t *expected, cat_ptr_t desired);

TEST(cat_atomic, ptr)
{
    cat_atomic_ptr_t atomic;
    char buffer1[1], buffer2[1];
    cat_atomic_ptr_init(&atomic, nullptr);
    ASSERT_EQ(cat_atomic_ptr_exchange(&atomic, buffer1), nullptr);
    ASSERT_EQ(cat_atomic_ptr_exchange(&atomic, buffer2), buffer1);
    ASSERT_EQ(cat_atomic_ptr_load(&atomic), buffer2);
    cat_atomic_ptr_store(&atomic, nullptr);

    for (auto atomic_ptr_compare_exchange_function : std::array<atomic_ptr_compare_exchange_t, 2>{
            cat_atomic_ptr_compare_exchange_strong,
            cat_atomic_ptr_compare_exchange_weak
    }) {
        cat_ptr_t expected = nullptr;
        cat_atomic_ptr_store(&atomic, nullptr);
        ASSERT_TRUE(atomic_ptr_compare_exchange_function(&atomic, &expected, buffer1));
        ASSERT_EQ(cat_atomic_ptr_load(&atomic), buffer1);
        expected = buffer1;
        ASSERT_TRUE(atomic_ptr_compare_exchange_function(&atomic, &expected, buffer2));
        ASSERT_EQ(cat_atomic_ptr_load(&atomic), buffer2);
        ASSERT_FALSE(atomic_ptr_compare_exchange_function(&atomic, &expected, nullptr));
        ASSERT_EQ(expected, buffer2);
        ASSERT_EQ(cat_atomic_ptr_load(&atomic), buffer2);
    }
}

TEST(cat_atomic, concurrent_exchange_preserves_submitted_values)
{
    constexpr size_t worker_count = 2;
    const size_t iterations = TEST_MAX_REQUESTS;
    ASSERT_GT(iterations, 0);
    ASSERT_LE(iterations, (UINT32_MAX - 1) / worker_count);
    cat_atomic_uint32_t atomic;
    cat_atomic_uint32_init(&atomic, 0);
    uv_sem_t ready, start;
    ASSERT_EQ(uv_sem_init(&ready, 0), 0);
    DEFER(uv_sem_destroy(&ready));
    ASSERT_EQ(uv_sem_init(&start, 0), 0);
    DEFER(uv_sem_destroy(&start));
    std::vector<uint32_t> results(worker_count * iterations);
    struct context_t {
        cat_atomic_uint32_t *atomic;
        uv_sem_t *ready;
        uv_sem_t *start;
        uint32_t first;
        size_t count;
        uint32_t *results;
    };
    std::array<context_t, worker_count> contexts;
    std::array<uv_thread_t, worker_count> threads;
    size_t created = 0;
    DEFER(
        for (size_t i = 0; i < created; i++) { uv_sem_post(&start); }
        for (size_t i = 0; i < created; i++) { uv_thread_join(&threads[i]); }
    );
    for (size_t i = 0; i < worker_count; i++) {
        contexts[i] = {&atomic, &ready, &start, static_cast<uint32_t>(i * iterations + 1),
            iterations, results.data() + i * iterations};
        ASSERT_EQ(uv_thread_create(&threads[i], [](void *data) -> void {
            auto *context = static_cast<context_t *>(data);
            uv_sem_post(context->ready);
            uv_sem_wait(context->start);
            for (size_t i = 0; i < context->count; i++) {
                context->results[i] = cat_atomic_uint32_exchange(context->atomic,
                    context->first + static_cast<uint32_t>(i));
            }
        }, &contexts[i]), 0);
        created++;
    }
    for (size_t i = 0; i < worker_count; i++) {
        uv_sem_wait(&ready);
    }
    for (size_t i = 0; i < worker_count; i++) {
        uv_sem_post(&start);
    }
    while (created > 0) {
        ASSERT_EQ(uv_thread_join(&threads[created - 1]), 0);
        created--;
    }
    // Unique submissions mean returned values plus the final value must contain 0..N exactly once.
    // A one-shot CAS that fails to write duplicates an old value and loses its submitted value.
    results.push_back(cat_atomic_uint32_load(&atomic));
    std::sort(results.begin(), results.end());
    for (size_t i = 0; i < results.size(); i++) {
        ASSERT_EQ(results[i], i) << "value index " << i;
    }
}
