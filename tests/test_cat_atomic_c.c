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
#if defined(CAT_OS_UNIX_LIKE)
# include <pthread.h>
#endif
#if 0 /* Mach threads: see the provider section below */
# include <mach/mach.h>
# include <mach/thread_act.h>
# if defined(__aarch64__)
#  include <mach/arm/thread_status.h>
# elif defined(__x86_64__)
#  include <mach/i386/thread_status.h>
# endif
#endif
#ifdef CAT_OS_WIN
# include <windows.h>
# include <process.h>
#endif

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

int cat_test_atomic_c_validates_orders(void)
{
#if defined(CAT_HAVE_C11_ATOMIC) || defined(CAT_HAVE_GNUC_ATOMIC)
    return 1;
#else
    return 0;
#endif
}

int cat_test_atomic_c_rejects_exchange_consume(void)
{
#ifdef CAT_HAVE_C11_ATOMIC
    return 1;
#else
    return 0;
#endif
}

/*
 * Drives the bare-address layer into an order it must reject, so the death test
 * of the C++ side can see the process stop.
 */
void cat_test_atomic_c_invalid_order_at(int operation, cat_atomic_memory_order_t success,
    cat_atomic_memory_order_t failure)
{
    int64_t value = 1;
    int64_t expected = 1;
    switch (operation) {
        case 0:
            cat_atomic_int64_store_at_explicit(&value, 2, success);
            break;
        case 1:
            (void) cat_atomic_int64_load_at_explicit(&value, success);
            break;
        case 2:
            (void) cat_atomic_int64_exchange_at_explicit(&value, 2, success);
            break;
        case 3:
            (void) cat_atomic_int64_fetch_add_at_explicit(&value, 1, success);
            break;
        case 4:
            (void) cat_atomic_int64_fetch_sub_at_explicit(&value, 1, success);
            break;
        case 5:
            (void) cat_atomic_int64_compare_exchange_strong_at_explicit(&value, &expected, 2,
                success, failure);
            break;
        case 6:
            (void) cat_atomic_int64_compare_exchange_weak_at_explicit(&value, &expected, 2,
                success, failure);
            break;
        case 7:
            cat_atomic_fence_explicit(success);
            break;
    }
}

void cat_test_atomic_c_invalid_order(int operation, cat_atomic_memory_order_t order)
{
    cat_atomic_uint32_t atomic;
    cat_atomic_uint32_init(&atomic, 1);
    switch (operation) {
        case 0:
            cat_atomic_uint32_store_explicit(&atomic, 2, order);
            break;
        case 1:
            (void) cat_atomic_uint32_load_explicit(&atomic, order);
            break;
        case 2:
            (void) cat_atomic_uint32_exchange_explicit(&atomic, 2, order);
            break;
    }
}

/*
 * Bare-address atomics under a foreign thread provider.
 *
 * The bare-address layer takes an address from the caller, so a thread that
 * libcat did not create must be able to use it. The driver below runs one
 * platform thread function under one provider at a time. Every provider feeds
 * the same driver, so adding a provider does not touch the driver.
 *
 * The provider table is closed: a provider that is not in it is not tested.
 * Only pthread is in it now.
 */

enum {
    CAT_TEST_ATOMIC_BARE_THREADS = 4,
    CAT_TEST_ATOMIC_BARE_SLOTS = CAT_TEST_ATOMIC_BARE_THREADS,
    CAT_TEST_ATOMIC_BARE_ROUNDS = 1000,
};

static const cat_atomic_memory_order_t cat_test_atomic_bare_load_orders[] = {
    CAT_ATOMIC_MEMORY_ORDER_RELAXED, CAT_ATOMIC_MEMORY_ORDER_CONSUME,
    CAT_ATOMIC_MEMORY_ORDER_ACQUIRE, CAT_ATOMIC_MEMORY_ORDER_SEQ_CST
};

static const cat_atomic_memory_order_t cat_test_atomic_bare_store_orders[] = {
    CAT_ATOMIC_MEMORY_ORDER_RELAXED, CAT_ATOMIC_MEMORY_ORDER_RELEASE,
    CAT_ATOMIC_MEMORY_ORDER_SEQ_CST
};

static const cat_atomic_memory_order_t cat_test_atomic_bare_rmw_orders[] = {
    CAT_ATOMIC_MEMORY_ORDER_RELAXED, CAT_ATOMIC_MEMORY_ORDER_ACQUIRE,
    CAT_ATOMIC_MEMORY_ORDER_RELEASE, CAT_ATOMIC_MEMORY_ORDER_ACQ_REL,
    CAT_ATOMIC_MEMORY_ORDER_SEQ_CST
};

/*
 * The legal success and failure order pairs of a compare-exchange. The failure
 * order can not be release or acq_rel, and it can not be stronger than the
 * success order, so not every combination of the two lists above is legal.
 */
static const struct {
    cat_atomic_memory_order_t success;
    cat_atomic_memory_order_t failure;
} cat_test_atomic_bare_cas_orders[] = {
    { CAT_ATOMIC_MEMORY_ORDER_RELAXED, CAT_ATOMIC_MEMORY_ORDER_RELAXED },
    { CAT_ATOMIC_MEMORY_ORDER_ACQUIRE, CAT_ATOMIC_MEMORY_ORDER_RELAXED },
    { CAT_ATOMIC_MEMORY_ORDER_ACQUIRE, CAT_ATOMIC_MEMORY_ORDER_CONSUME },
    { CAT_ATOMIC_MEMORY_ORDER_RELEASE, CAT_ATOMIC_MEMORY_ORDER_RELAXED },
    { CAT_ATOMIC_MEMORY_ORDER_ACQ_REL, CAT_ATOMIC_MEMORY_ORDER_RELAXED },
    { CAT_ATOMIC_MEMORY_ORDER_ACQ_REL, CAT_ATOMIC_MEMORY_ORDER_ACQUIRE },
    { CAT_ATOMIC_MEMORY_ORDER_SEQ_CST, CAT_ATOMIC_MEMORY_ORDER_ACQUIRE },
};

static const cat_atomic_memory_order_t cat_test_atomic_bare_fence_orders[] = {
    CAT_ATOMIC_MEMORY_ORDER_ACQUIRE, CAT_ATOMIC_MEMORY_ORDER_RELEASE,
    CAT_ATOMIC_MEMORY_ORDER_ACQ_REL, CAT_ATOMIC_MEMORY_ORDER_SEQ_CST
};

typedef struct cat_test_atomic_bare_slot_s {
    uintptr_t index;
    uint64_t value;
} cat_test_atomic_bare_slot_t;

typedef struct cat_test_atomic_bare_context_s {
    cat_atomic_int32_t count;     /* increments from every thread */
    cat_atomic_int32_t sequence;  /* slot allocation order */
    cat_atomic_int32_t published; /* completed stores, read before the timeout */
    cat_atomic_int64_t ticks;     /* increments from every thread, checked at the end */
    cat_test_atomic_bare_slot_t *slots;
    uint32_t thread_count;
    cat_nsec_t timeout_nsec;
} cat_test_atomic_bare_context_t;

typedef struct cat_test_atomic_bare_thread_s {
    union {
#if defined(CAT_OS_UNIX_LIKE)
        pthread_t pthread;
#endif
#if 0 /* Mach threads */
        mach_port_t mach;
#endif
#ifdef CAT_OS_WIN
        void *win32;
#endif
        uv_thread_t uv;
    } handle;
#if 0 /* Mach threads */
    char mach_stack[64 * 1024];
#endif
    cat_test_atomic_bare_context_t *context;
    uint32_t index;
    const char *failure;
} cat_test_atomic_bare_thread_t;

typedef struct cat_test_atomic_bare_provider_s {
    int (*create)(cat_test_atomic_bare_thread_t *thread);
    int (*join)(cat_test_atomic_bare_thread_t *thread);
} cat_test_atomic_bare_provider_t;

#define CAT_TEST_ATOMIC_BARE_CHECK(condition, message) do { \
    if (!(condition)) { \
        return message; \
    } \
} while (0)

/* Each thread owns its buffer, so the sequences below never contend. */
#define CAT_TEST_ATOMIC_BARE_SEQUENCE(buffer) do { \
    int64_t expected; \
    cat_atomic_int64_store_at(&(buffer), 1); \
    CAT_TEST_ATOMIC_BARE_CHECK(cat_atomic_int64_load_at(&(buffer)) == 1, "store then load"); \
    CAT_TEST_ATOMIC_BARE_CHECK(cat_atomic_int64_exchange_at(&(buffer), 2) == 1, "exchange returns the old value"); \
    CAT_TEST_ATOMIC_BARE_CHECK(cat_atomic_int64_fetch_add_at(&(buffer), 3) == 2, "fetch_add returns the old value"); \
    CAT_TEST_ATOMIC_BARE_CHECK(cat_atomic_int64_fetch_sub_at(&(buffer), 1) == 5, "fetch_sub returns the old value"); \
    /* the buffer holds 4 here */ \
    expected = 3; \
    CAT_TEST_ATOMIC_BARE_CHECK( \
        !cat_atomic_int64_compare_exchange_strong_at(&(buffer), &expected, 7), \
        "compare_exchange must fail when expected is stale"); \
    CAT_TEST_ATOMIC_BARE_CHECK(expected == 4, "a failing compare_exchange publishes the actual value"); \
    CAT_TEST_ATOMIC_BARE_CHECK(cat_atomic_int64_load_at(&(buffer)) == 4, "a failing compare_exchange writes nothing"); \
    CAT_TEST_ATOMIC_BARE_CHECK( \
        cat_atomic_int64_compare_exchange_strong_at(&(buffer), &expected, 7), \
        "compare_exchange must succeed when expected matches"); \
    CAT_TEST_ATOMIC_BARE_CHECK(expected == 4, "a succeeding compare_exchange leaves expected alone"); \
    CAT_TEST_ATOMIC_BARE_CHECK(cat_atomic_int64_load_at(&(buffer)) == 7, "a succeeding compare_exchange writes"); \
    /* explicit orders: every store order must publish 2 */ \
    for (size_t order = 0; order < CAT_ARRAY_SIZE(cat_test_atomic_bare_store_orders); order++) { \
        cat_atomic_int64_store_at_explicit(&(buffer), 2, cat_test_atomic_bare_store_orders[order]); \
        CAT_TEST_ATOMIC_BARE_CHECK(cat_atomic_int64_load_at(&(buffer)) == 2, "store with an explicit order"); \
    } \
    /* every load order must see 2 */ \
    for (size_t order = 0; order < CAT_ARRAY_SIZE(cat_test_atomic_bare_load_orders); order++) { \
        CAT_TEST_ATOMIC_BARE_CHECK( \
            cat_atomic_int64_load_at_explicit(&(buffer), cat_test_atomic_bare_load_orders[order]) == 2, \
            "load with an explicit order"); \
    } \
    /* every read-modify-write order must return the old value and publish the new one */ \
    for (size_t order = 0; order < CAT_ARRAY_SIZE(cat_test_atomic_bare_rmw_orders); order++) { \
        cat_atomic_int64_store_at(&(buffer), 2); \
        CAT_TEST_ATOMIC_BARE_CHECK( \
            cat_atomic_int64_exchange_at_explicit(&(buffer), 3, cat_test_atomic_bare_rmw_orders[order]) == 2, \
            "exchange with an explicit order"); \
        CAT_TEST_ATOMIC_BARE_CHECK( \
            cat_atomic_int64_fetch_add_at_explicit(&(buffer), 1, cat_test_atomic_bare_rmw_orders[order]) == 3, \
            "fetch_add with an explicit order"); \
        CAT_TEST_ATOMIC_BARE_CHECK( \
            cat_atomic_int64_fetch_sub_at_explicit(&(buffer), 1, cat_test_atomic_bare_rmw_orders[order]) == 4, \
            "fetch_sub with an explicit order"); \
        CAT_TEST_ATOMIC_BARE_CHECK(cat_atomic_int64_load_at(&(buffer)) == 3, "read-modify-write result"); \
    } \
    /* every legal success and failure order pair must work, from a value of 3 */ \
    for (size_t pair = 0; pair < CAT_ARRAY_SIZE(cat_test_atomic_bare_cas_orders); pair++) { \
        cat_atomic_int64_store_at(&(buffer), 3); \
        expected = 3; \
        CAT_TEST_ATOMIC_BARE_CHECK( \
            cat_atomic_int64_compare_exchange_strong_at_explicit(&(buffer), &expected, 4, \
                cat_test_atomic_bare_cas_orders[pair].success, cat_test_atomic_bare_cas_orders[pair].failure), \
            "compare_exchange with explicit orders must succeed from a matching value"); \
        CAT_TEST_ATOMIC_BARE_CHECK(cat_atomic_int64_load_at(&(buffer)) == 4, "compare_exchange explicit result"); \
        CAT_TEST_ATOMIC_BARE_CHECK( \
            !cat_atomic_int64_compare_exchange_strong_at_explicit(&(buffer), &expected, 5, \
                cat_test_atomic_bare_cas_orders[pair].success, cat_test_atomic_bare_cas_orders[pair].failure), \
            "compare_exchange with explicit orders must fail on a stale value"); \
        CAT_TEST_ATOMIC_BARE_CHECK(expected == 4, "compare_exchange explicit failure publishes the actual value"); \
    } \
    /* the fence must not change the value */ \
    cat_atomic_int64_store_at(&(buffer), 9); \
    cat_atomic_fence(); \
    for (size_t order = 0; order < CAT_ARRAY_SIZE(cat_test_atomic_bare_fence_orders); order++) { \
        cat_atomic_fence_explicit(cat_test_atomic_bare_fence_orders[order]); \
    } \
    CAT_TEST_ATOMIC_BARE_CHECK(cat_atomic_int64_load_at(&(buffer)) == 9, "fence preserves the value"); \
} while (0)

static const char *cat_test_atomic_bare_worker(cat_test_atomic_bare_thread_t *self)
{
    cat_test_atomic_bare_context_t *context = self->context;
    /* the per-thread macros below require a buffer that no other thread touches */
    int64_t buffer = 0;
    intptr_t marker = (intptr_t) 0x40000000 + (intptr_t) self->index * 0x1000;
    for (int round = 0; round < CAT_TEST_ATOMIC_BARE_ROUNDS; round++) {
        cat_atomic_int32_fetch_add_at_explicit(&context->count, 1, CAT_ATOMIC_MEMORY_ORDER_ACQ_REL);
        cat_atomic_int64_fetch_add_at_explicit(&context->ticks, 1, CAT_ATOMIC_MEMORY_ORDER_RELAXED);
        cat_atomic_intptr_store_at_explicit(&marker, marker, CAT_ATOMIC_MEMORY_ORDER_RELAXED);
        if (round == 0) {
            CAT_TEST_ATOMIC_BARE_SEQUENCE(buffer);
        }
    }
    CAT_TEST_ATOMIC_BARE_CHECK(cat_atomic_intptr_load_at(&marker) == marker, "intptr round trip");
    const int32_t index = cat_atomic_int32_fetch_add_at_explicit(&context->sequence, 1, CAT_ATOMIC_MEMORY_ORDER_ACQ_REL);
    if (index < 0 || index >= (int32_t) context->thread_count) {
        return "sequence range";
    }
    context->slots[index].index = self->index;
    context->slots[index].value = (uint64_t) self->index ^ UINT64_C(0xa5a5a5a5);
    cat_atomic_int32_store_at_explicit(&context->published, index + 1, CAT_ATOMIC_MEMORY_ORDER_RELEASE);
    for (int round = 0; round < CAT_TEST_ATOMIC_BARE_ROUNDS; round++) {
        cat_atomic_int32_fetch_add_at_explicit(&context->count, 1, CAT_ATOMIC_MEMORY_ORDER_ACQ_REL);
    }
    return NULL;
}

/*
 * pthread and CreateThread take cat_test_atomic_bare_thread_main directly.
 * uv_thread_create carries its own function type, so it needs one more step.
 */
static void *cat_test_atomic_bare_thread_main(void *data)
{
    cat_test_atomic_bare_thread_t *thread = (cat_test_atomic_bare_thread_t *) data;
    thread->failure = cat_test_atomic_bare_worker(thread);
    return NULL;
}

static void cat_test_atomic_bare_uv_main(void *data)
{
    (void) cat_test_atomic_bare_thread_main(data);
}

static const char *cat_test_atomic_bare_run(const cat_test_atomic_bare_provider_t *provider, cat_timeout_t timeout)
{
    cat_test_atomic_bare_context_t context;
    cat_test_atomic_bare_thread_t threads[CAT_TEST_ATOMIC_BARE_THREADS];
    cat_test_atomic_bare_slot_t slots[CAT_TEST_ATOMIC_BARE_SLOTS];
    const cat_nsec_t start = cat_time_nsec();
    uint32_t started = 0;
    memset(&context, 0, sizeof(context));
    memset(slots, 0, sizeof(slots));
    memset(threads, 0, sizeof(threads));
    context.slots = slots;
    context.thread_count = CAT_TEST_ATOMIC_BARE_THREADS;
    context.timeout_nsec = (cat_nsec_t) timeout * 1000 * 1000;
    for (uint32_t i = 0; i < CAT_TEST_ATOMIC_BARE_THREADS; i++) {
        threads[i].context = &context;
        threads[i].index = i;
    }
    for (uint32_t i = 0; i < CAT_TEST_ATOMIC_BARE_THREADS; i++) {
        if (provider->create(&threads[i]) != 0) {
            break;
        }
        started++;
    }
    /* The threads are still running here, so this proves that a foreign thread
     * reaches its own stack and publishes. Joining first would make the check
     * below true by construction. A plain sleep would prove nothing. */
    while (cat_atomic_int32_load_at_explicit(&context.sequence, CAT_ATOMIC_MEMORY_ORDER_ACQUIRE) !=
           (int32_t) started) {
        if (cat_time_nsec() - start >= context.timeout_nsec) {
            break;
        }
        cat_sys_usleep(1000);
    }
    cat_atomic_fence();
    const cat_bool_t incomplete =
        cat_atomic_int32_load_at(&context.sequence) != (int32_t) started;
    /* join every started thread before any failure returns */
    for (uint32_t i = 0; i < started; i++) {
        (void) provider->join(&threads[i]);
    }
    if (started != CAT_TEST_ATOMIC_BARE_THREADS) {
        return "thread create";
    }
    CAT_TEST_ATOMIC_BARE_CHECK(!incomplete, "publication timeout");
    for (uint32_t i = 0; i < started; i++) {
        CAT_TEST_ATOMIC_BARE_CHECK(threads[i].failure == NULL, threads[i].failure);
    }
    CAT_TEST_ATOMIC_BARE_CHECK(cat_atomic_int32_load_at(&context.published) == (int32_t) context.thread_count,
        "published count");
    for (uint32_t i = 0; i < context.thread_count; i++) {
        CAT_TEST_ATOMIC_BARE_CHECK(slots[i].value == ((uint64_t) slots[i].index ^ UINT64_C(0xa5a5a5a5)),
            "slot payload");
    }
    CAT_TEST_ATOMIC_BARE_CHECK(
        cat_atomic_int64_load_at(&context.ticks) == (int64_t) context.thread_count * CAT_TEST_ATOMIC_BARE_ROUNDS,
        "ticks");
    CAT_TEST_ATOMIC_BARE_CHECK(
        cat_atomic_int32_load_at(&context.count) == (int32_t) context.thread_count * CAT_TEST_ATOMIC_BARE_ROUNDS * 2,
        "count");
    return NULL;
}

/* One provider table per platform thread function. The entry point of each
 * provider is generated from the table, so the two cannot drift apart. */
#define CAT_TEST_ATOMIC_BARE_PROVIDERS(tag, entry) \
    static const cat_test_atomic_bare_provider_t cat_test_atomic_bare_providers_ ## tag[] = { \
        cat_test_atomic_bare_provider_ ## tag, \
    }; \
    const char *cat_test_atomic_bare_ ## entry(cat_timeout_t timeout) \
    { \
        return cat_test_atomic_bare_run(&cat_test_atomic_bare_providers_ ## tag[0], timeout); \
    }

#if defined(CAT_OS_UNIX_LIKE)
static int cat_test_atomic_bare_pthread_create(cat_test_atomic_bare_thread_t *thread)
{
    return pthread_create(&thread->handle.pthread, NULL, cat_test_atomic_bare_thread_main, thread);
}

static int cat_test_atomic_bare_pthread_join(cat_test_atomic_bare_thread_t *thread)
{
    return pthread_join(thread->handle.pthread, NULL);
}

#define cat_test_atomic_bare_provider_pthread \
    { \
        .create = cat_test_atomic_bare_pthread_create, \
        .join = cat_test_atomic_bare_pthread_join, \
    }
CAT_TEST_ATOMIC_BARE_PROVIDERS(pthread, pthread)
#endif

#if 0 /* Mach threads
       * A Mach thread gets no start routine: the caller installs the program
       * counter and the stack pointer itself in the machine state.
       *
       * This reports success from thread_create, thread_set_state and
       * thread_resume, yet the thread faults before its entry point on macOS
       * 15.5 with the Command Line Tools SDK. A standalone program with the same
       * three calls does run, so the fault belongs to this combination. The
       * likely cause is pointer authentication on arm64e, which that SDK does
       * not expose (no ptrauth.h), so the entry point cannot be signed here.
       *
       * Enabling this needs a host with the full Xcode toolchain. The shape is
       * kept because it is correct and the diagnosis is worth keeping.
       *
       * Two constraints if this is ever enabled:
       *   - the entry must not return, because the kernel starts the thread with
       *     an empty link register and a return jumps to address zero
       *   - the thread needs its own stack, and thread_create_running wants the
       *     stack pointer non-zero even though the state flavor carries it
       */
static void *cat_test_atomic_bare_mach_main(void *data)
{
    cat_test_atomic_bare_thread_t *thread = (cat_test_atomic_bare_thread_t *) data;
    thread->failure = cat_test_atomic_bare_worker(thread);
    for (;;) {
        cat_sys_sleep(3600);
    }
    return NULL;
}

static int cat_test_atomic_bare_mach_create(cat_test_atomic_bare_thread_t *thread)
{
    kern_return_t result;
#if defined(__aarch64__)
    arm_thread_state64_t state;
    memset(&state, 0, sizeof(state));
    state.__pc = (uint64_t) (uintptr_t) cat_test_atomic_bare_mach_main;
    state.__x[0] = (uint64_t) (uintptr_t) thread;
    state.__sp = (uint64_t) (((uintptr_t) thread->mach_stack + sizeof(thread->mach_stack)) & ~(uintptr_t) 15);
#elif defined(__x86_64__)
    x86_thread_state64_t state;
    memset(&state, 0, sizeof(state));
    state.__rip = (uint64_t) (uintptr_t) cat_test_atomic_bare_mach_main;
    state.__rdi = (uint64_t) (uintptr_t) thread;
    state.__rsp = (uint64_t) (((uintptr_t) thread->mach_stack + sizeof(thread->mach_stack)) & ~(uintptr_t) 15);
#endif
    result = thread_create(mach_task_self(), &thread->handle.mach);
    if (result == KERN_SUCCESS) {
#if defined(__aarch64__)
        result = thread_set_state(thread->handle.mach, ARM_THREAD_STATE64,
            (thread_state_t) &state, ARM_THREAD_STATE64_COUNT);
#elif defined(__x86_64__)
        result = thread_set_state(thread->handle.mach, x86_THREAD_STATE64,
            (thread_state_t) &state, x86_THREAD_STATE64_COUNT);
#endif
    }
    if (result == KERN_SUCCESS) {
        result = thread_resume(thread->handle.mach);
    }
    return result == KERN_SUCCESS ? 0 : (int) result;
}

static int cat_test_atomic_bare_mach_join(cat_test_atomic_bare_thread_t *thread)
{
    const kern_return_t result = thread_terminate(thread->handle.mach);
    (void) mach_port_deallocate(mach_task_self(), thread->handle.mach);
    return result == KERN_SUCCESS ? 0 : (int) result;
}

#define cat_test_atomic_bare_provider_mach \
    { \
        .create = cat_test_atomic_bare_mach_create, \
        .join = cat_test_atomic_bare_mach_join, \
    }
CAT_TEST_ATOMIC_BARE_PROVIDERS(mach, mach)
#endif /* Mach threads */

#ifdef CAT_OS_WIN
/*
 * _beginthreadex rather than CreateThread: the thread runs C runtime code, and
 * the runtime needs its own per-thread bookkeeping. It also returns the handle
 * directly, so no integer-to-pointer conversion is needed.
 */
static unsigned int __stdcall cat_test_atomic_bare_win32_main(void *data)
{
    (void) cat_test_atomic_bare_thread_main(data);
    return 0;
}

static int cat_test_atomic_bare_win32_create(cat_test_atomic_bare_thread_t *thread)
{
    const uintptr_t handle = _beginthreadex(NULL, 0, cat_test_atomic_bare_win32_main, thread, 0, NULL);
    if (handle == 0) {
        return -1;
    }
    thread->handle.win32 = (void *) handle;
    return 0;
}

static int cat_test_atomic_bare_win32_join(cat_test_atomic_bare_thread_t *thread)
{
    const DWORD wait = WaitForSingleObject((HANDLE) thread->handle.win32, INFINITE);
    (void) CloseHandle((HANDLE) thread->handle.win32);
    return wait == WAIT_OBJECT_0 ? 0 : -1;
}

#define cat_test_atomic_bare_provider_win32 \
    { \
        .create = cat_test_atomic_bare_win32_create, \
        .join = cat_test_atomic_bare_win32_join, \
    }
CAT_TEST_ATOMIC_BARE_PROVIDERS(win32, win32)
#endif

/* libuv threads exist on every platform, so this provider has no guard. */
static int cat_test_atomic_bare_uv_create(cat_test_atomic_bare_thread_t *thread)
{
    return uv_thread_create(&thread->handle.uv, cat_test_atomic_bare_uv_main, thread);
}

static int cat_test_atomic_bare_uv_join(cat_test_atomic_bare_thread_t *thread)
{
    return uv_thread_join(&thread->handle.uv);
}

#define cat_test_atomic_bare_provider_uv \
    { \
        .create = cat_test_atomic_bare_uv_create, \
        .join = cat_test_atomic_bare_uv_join, \
    }
CAT_TEST_ATOMIC_BARE_PROVIDERS(uv, uv)
