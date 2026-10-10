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

#include "cat_atomic.h"
#include "cat_time.h"
#include <string.h>

#define CAT_TEST_ATOMIC_CHECK(condition, message) do { \
    if (!(condition)) { \
        return message; \
    } \
} while (0)

const char *cat_test_atomic_c_operations(cat_timeout_t timeout)
{
    const cat_atomic_memory_order_t store_orders[] = {
        CAT_ATOMIC_MEMORY_ORDER_RELAXED, CAT_ATOMIC_MEMORY_ORDER_RELEASE, CAT_ATOMIC_MEMORY_ORDER_SEQ_CST
    };
    const cat_atomic_memory_order_t load_orders[] = {
        CAT_ATOMIC_MEMORY_ORDER_RELAXED, CAT_ATOMIC_MEMORY_ORDER_CONSUME,
        CAT_ATOMIC_MEMORY_ORDER_ACQUIRE, CAT_ATOMIC_MEMORY_ORDER_SEQ_CST
    };
    const cat_atomic_memory_order_t exchange_orders[] = {
        CAT_ATOMIC_MEMORY_ORDER_RELAXED, CAT_ATOMIC_MEMORY_ORDER_ACQUIRE,
        CAT_ATOMIC_MEMORY_ORDER_RELEASE, CAT_ATOMIC_MEMORY_ORDER_ACQ_REL, CAT_ATOMIC_MEMORY_ORDER_SEQ_CST
    };
    const cat_nsec_t timeout_nsec = (cat_nsec_t) timeout * 1000 * 1000;

#define CAT_TEST_ATOMIC_COMMON(name, type_name_t, first, second) do { \
    cat_atomic_##name##_t atomic; \
    type_name_t expected; \
    cat_atomic_##name##_init(&atomic, first); \
    CAT_TEST_ATOMIC_CHECK(cat_atomic_##name##_load(&atomic) == first, #name " init/load"); \
    cat_atomic_##name##_store(&atomic, second); \
    CAT_TEST_ATOMIC_CHECK(cat_atomic_##name##_exchange(&atomic, first) == second, #name " store/exchange"); \
    expected = second; \
    CAT_TEST_ATOMIC_CHECK(!cat_atomic_##name##_compare_exchange_strong(&atomic, &expected, second), #name " strong mismatch"); \
    CAT_TEST_ATOMIC_CHECK(expected == first && cat_atomic_##name##_load(&atomic) == first, #name " strong expected"); \
    CAT_TEST_ATOMIC_CHECK(cat_atomic_##name##_compare_exchange_strong(&atomic, &expected, second), #name " strong match"); \
    expected = first; \
    CAT_TEST_ATOMIC_CHECK(!cat_atomic_##name##_compare_exchange_weak(&atomic, &expected, first), #name " weak mismatch"); \
    CAT_TEST_ATOMIC_CHECK(expected == second && cat_atomic_##name##_load(&atomic) == second, #name " weak expected"); \
    cat_nsec_t start = cat_time_nsec(); \
    /* Weak CAS may fail spuriously even without a competing writer. */ \
    while (!cat_atomic_##name##_compare_exchange_weak(&atomic, &expected, first)) { \
        CAT_TEST_ATOMIC_CHECK(expected == second && cat_atomic_##name##_load(&atomic) == second, #name " weak retry"); \
        CAT_TEST_ATOMIC_CHECK(cat_time_nsec() - start < timeout_nsec, #name " weak timeout"); \
    } \
    CAT_TEST_ATOMIC_CHECK(cat_atomic_##name##_load(&atomic) == first, #name " weak result"); \
    for (size_t i = 0; i < CAT_ARRAY_SIZE(store_orders); i++) { \
        cat_atomic_##name##_store_explicit(&atomic, second, store_orders[i]); \
        CAT_TEST_ATOMIC_CHECK(cat_atomic_##name##_load(&atomic) == second, #name " store_explicit"); \
    } \
    for (size_t i = 0; i < CAT_ARRAY_SIZE(load_orders); i++) { \
        CAT_TEST_ATOMIC_CHECK(cat_atomic_##name##_load_explicit(&atomic, load_orders[i]) == second, #name " load_explicit"); \
    } \
    for (size_t i = 0; i < CAT_ARRAY_SIZE(exchange_orders); i++) { \
        CAT_TEST_ATOMIC_CHECK(cat_atomic_##name##_exchange_explicit(&atomic, first, exchange_orders[i]) == second, #name " exchange_explicit"); \
        CAT_TEST_ATOMIC_CHECK(cat_atomic_##name##_load(&atomic) == first, #name " exchange result"); \
        cat_atomic_##name##_store(&atomic, second); \
    } \
} while (0)

#define CAT_TEST_ATOMIC_NUMERIC(name, type_name_t, interlocked_suffix, interlocked_type_t) do { \
    CAT_TEST_ATOMIC_COMMON(name, type_name_t, (type_name_t) 1, (type_name_t) 2); \
    cat_atomic_##name##_t atomic; \
    cat_atomic_##name##_init(&atomic, 2); \
    CAT_TEST_ATOMIC_CHECK(cat_atomic_##name##_fetch_add(&atomic, 3) == 2, #name " fetch_add return"); \
    CAT_TEST_ATOMIC_CHECK(cat_atomic_##name##_load(&atomic) == 5, #name " fetch_add result"); \
    CAT_TEST_ATOMIC_CHECK(cat_atomic_##name##_fetch_sub(&atomic, 1) == 5, #name " fetch_sub return"); \
    CAT_TEST_ATOMIC_CHECK(cat_atomic_##name##_load(&atomic) == 4, #name " fetch_sub result"); \
} while (0);

    CAT_ATOMIC_NUMERIC_OPERATION_FUNCTIONS_MAP(CAT_TEST_ATOMIC_NUMERIC)
    int token;
    CAT_TEST_ATOMIC_COMMON(bool, cat_bool_t, cat_false, cat_true);
    CAT_TEST_ATOMIC_COMMON(ptr, cat_ptr_t, NULL, (cat_ptr_t) &token);
    CAT_TEST_ATOMIC_COMMON(uintptr, uintptr_t, (uintptr_t) 0, (uintptr_t) &token);
#undef CAT_TEST_ATOMIC_NUMERIC
#undef CAT_TEST_ATOMIC_COMMON
    return NULL;
}

typedef struct cat_test_atomic_publication_s {
    cat_atomic_bool_t ready;
    uint32_t payload[32];
} cat_test_atomic_publication_t;

static void cat_test_atomic_publish(void *data)
{
    cat_test_atomic_publication_t *publication = (cat_test_atomic_publication_t *) data;
    for (size_t i = 0; i < CAT_ARRAY_SIZE(publication->payload); i++) {
        publication->payload[i] = (uint32_t) i + 1;
    }
    cat_atomic_bool_store_explicit(&publication->ready, cat_true, CAT_ATOMIC_MEMORY_ORDER_RELEASE);
}

const char *cat_test_atomic_c_publication(cat_timeout_t timeout)
{
    cat_test_atomic_publication_t publication;
    cat_atomic_bool_init(&publication.ready, cat_false);
    memset(publication.payload, 0, sizeof(publication.payload));
    uv_thread_t producer;
    if (uv_thread_create(&producer, cat_test_atomic_publish, &publication) != 0) {
        return "publication thread create";
    }
    const char *failure = NULL;
    const cat_nsec_t start = cat_time_nsec();
    const cat_nsec_t timeout_nsec = (cat_nsec_t) timeout * 1000 * 1000;
    while (!cat_atomic_bool_load_explicit(&publication.ready, CAT_ATOMIC_MEMORY_ORDER_ACQUIRE)) {
        if (cat_time_nsec() - start >= timeout_nsec) {
            failure = "publication timeout";
            break;
        }
        cat_sys_usleep(1);
    }
    // Read before join: visibility must come from release/acquire, not thread completion.
    if (failure == NULL) {
        for (size_t i = 0; i < CAT_ARRAY_SIZE(publication.payload); i++) {
            if (publication.payload[i] != (uint32_t) i + 1) {
                failure = "publication payload";
                break;
            }
        }
    }
    if (uv_thread_join(&producer) != 0) {
        return "publication thread join";
    }
    return failure;
}

#undef CAT_TEST_ATOMIC_CHECK
