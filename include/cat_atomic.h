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

#ifndef CAT_ATOMIC_H
#define CAT_ATOMIC_H
#ifdef __cplusplus
extern "C" {
#endif

#include "cat.h"

/* TODO: Fixed implementation selection
 * or provided C API for external use. */
#if __has_feature(c_atomic) && defined(__clang__)
# define CAT_HAVE_C11_ATOMIC 1
#elif CAT_GCC_VERSION >= 4007
# define CAT_HAVE_GNUC_ATOMIC 1
#elif defined(__GNUC__)
# define CAT_HAVE_SYNC_ATOMIC 1
#elif defined(CAT_OS_WIN)
# define CAT_HAVE_INTERLOCK_ATOMIC 1
#else
# error "No atomics support detected, that's terrible!"
#endif

#ifdef CAT_HAVE_INTERLOCK_ATOMIC
# include <intrin.h>
#endif

#if defined(CAT_HAVE_C11_ATOMIC)
# define CAT_ATOMIC_C11_CASE(...) __VA_ARGS__
# define CAT_ATOMIC_GNUC_CASE(...)
# define CAT_ATOMIC_INTERLOCK_CASE(...)
# define CAT_ATOMIC_SYNC_CASE(...)
# define CAT_ATOMIC_NO_CASE(...)
#elif defined(CAT_HAVE_GNUC_ATOMIC)
# define CAT_ATOMIC_C11_CASE(...)
# define CAT_ATOMIC_GNUC_CASE(...) __VA_ARGS__
# define CAT_ATOMIC_INTERLOCK_CASE(...)
# define CAT_ATOMIC_SYNC_CASE(...)
# define CAT_ATOMIC_NO_CASE(...)
#elif defined(CAT_HAVE_INTERLOCK_ATOMIC)
# define CAT_ATOMIC_C11_CASE(...)
# define CAT_ATOMIC_GNUC_CASE(...)
# define CAT_ATOMIC_INTERLOCK_CASE(...) __VA_ARGS__
# define CAT_ATOMIC_SYNC_CASE(...)
# define CAT_ATOMIC_NO_CASE(...)
#elif defined(CAT_HAVE_SYNC_ATOMIC)
# define CAT_ATOMIC_C11_CASE(...)
# define CAT_ATOMIC_GNUC_CASE(...)
# define CAT_ATOMIC_INTERLOCK_CASE(...)
# define CAT_ATOMIC_SYNC_CASE(...) __VA_ARGS__
# define CAT_ATOMIC_NO_CASE(...)
#else
# define CAT_ATOMIC_C11_CASE(...)
# define CAT_ATOMIC_GNUC_CASE(...)
# define CAT_ATOMIC_INTERLOCK_CASE(...)
# define CAT_ATOMIC_SYNC_CASE(...)
# define CAT_ATOMIC_NO_CASE(...) __VA_ARGS__
#endif

#define CAT_ATOMIC_COMMON_OPERATION_FUNCTIONS_MAP(XX) \
        XX(bool, cat_bool_t, 8,       char) \
        XX(ptr,  cat_ptr_t,  Pointer, PVOID) \
        XX(uintptr, uintptr_t, Pointer, PVOID) \

#define CAT_ATOMIC_NUMERIC_OPERATION_FUNCTIONS_MAP(XX) \
        XX(int8,   int8_t,   8,  char) \
        XX(uint8,  uint8_t,  8,  char) \
        XX(int16,  int16_t,  16, short) \
        XX(uint16, uint16_t, 16, short) \
        XX(int32,  int32_t,  32, long) \
        XX(uint32, uint32_t, 32, long) \
        XX(int64,  int64_t,  64, __int64) \
        XX(uint64, uint64_t, 64, __int64) \

# if defined(CAT_HAVE_GNUC_ATOMIC)
# define __atomic_compare_exchange_strong(atomic, expected, desired) \
         __atomic_compare_exchange_n(atomic, expected, desired, 0 /* not weak */, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
# define __atomic_compare_exchange_weak(atomic, expected, desired) \
         __atomic_compare_exchange_n(atomic, expected, desired, 1 /* weak */, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
# elif defined(CAT_HAVE_INTERLOCK_ATOMIC)
/* alias */
# define _InterlockedOr32                          _InterlockedOr
# define _InterlockedExchange32                    _InterlockedExchange
# define _InterlockedExchangeAdd32                 _InterlockedExchangeAdd
# define _InterlockedCompareExchange32             _InterlockedCompareExchange
# define _InterlockedLoad8(value)                  _InterlockedOr8(value, 0)
# define _InterlockedLoad16(value)                 _InterlockedOr16(value, 0)
# define _InterlockedLoad32(value)                 _InterlockedOr(value, 0)
# define _InterlockedLoad64(value)                 _InterlockedOr64(value, 0)
# define _InterlockedLoadPointer(value)            _InterlockedCompareExchangePointer(value, NULL, NULL)
# define _InterlockedExchangeSub8(value, operand)  _InterlockedExchangeAdd8(value, -operand)
# define _InterlockedExchangeSub16(value, operand) _InterlockedExchangeAdd16(value, -operand)
# define _InterlockedExchangeSub32(value, operand) _InterlockedExchangeAdd32(value, -operand)
# define _InterlockedExchangeSub64(value, operand) _InterlockedExchangeAdd64(value, -operand)
/* not exist at any platform */
# define _InterlockedExchange32_acq                _InterlockedExchange
# define _InterlockedExchange32_rel                _InterlockedExchange
# define _InterlockedExchange32_nf                 _InterlockedExchange
# define _InterlockedLoad8_acq(value)              _InterlockedOr8_acq(value, 0)
# define _InterlockedLoad16_acq(value)             _InterlockedOr16_acq(value, 0)
# define _InterlockedLoad32_acq(value)             _InterlockedOr_acq(value, 0)
# define _InterlockedLoad64_acq(value)             _InterlockedOr64_acq(value, 0)
# define _InterlockedLoad8_nf(value)               _InterlockedOr8_nf(value, 0)
# define _InterlockedLoad16_nf(value)              _InterlockedOr16_nf(value, 0)
# define _InterlockedLoad32_nf(value)              _InterlockedOr_nf(value, 0)
# define _InterlockedLoad64_nf(value)              _InterlockedOr64_nf(value, 0)
# define _InterlockedLoadPointer_acq               _InterlockedLoadPointer
# define _InterlockedLoadPointer_nf                _InterlockedLoadPointer
# ifdef _M_X64
/* not exist at x86 */
#  define _InterlockedExchange8_acq _InterlockedExchange8
#  define _InterlockedExchange8_rel _InterlockedExchange8
#  define _InterlockedExchange8_nf _InterlockedExchange8
#  define _InterlockedExchange16_acq _InterlockedExchange16
#  define _InterlockedExchange16_rel _InterlockedExchange16
#  define _InterlockedExchange16_nf _InterlockedExchange16
#  define _InterlockedExchange_acq _InterlockedExchange
#  define _InterlockedExchange_rel _InterlockedExchange
#  define _InterlockedExchange_nf _InterlockedExchange
#  define _InterlockedExchange64_acq _InterlockedExchange64
#  define _InterlockedExchange64_rel _InterlockedExchange64
#  define _InterlockedExchange64_nf _InterlockedExchange64
#  define _InterlockedOr8_acq _InterlockedOr8
#  define _InterlockedOr8_nf _InterlockedOr8
#  define _InterlockedOr16_acq _InterlockedOr16
#  define _InterlockedOr16_nf _InterlockedOr16
#  define _InterlockedOr_acq _InterlockedOr
#  define _InterlockedOr_nf _InterlockedOr
#  define _InterlockedOr64_acq _InterlockedOr64
#  define _InterlockedOr64_nf _InterlockedOr64
#  define _InterlockedExchangePointer_acq _InterlockedExchangePointer
#  define _InterlockedExchangePointer_nf _InterlockedExchangePointer
#  define _InterlockedExchangePointer_rel _InterlockedExchangePointer
# endif
#endif

#define CAT_ATOMIC_COMMON_OPERATION_FUNCTIONS_GEN(name, type_name_t, interlocked_suffix, interlocked_type_t) \
typedef struct cat_atomic_##name##_s { \
    CAT_ATOMIC_C11_CASE( \
        _Atomic(type_name_t) value; \
    ) \
    CAT_ATOMIC_GNUC_CASE( \
        volatile type_name_t value; \
    ) \
    CAT_ATOMIC_INTERLOCK_CASE( \
        volatile interlocked_type_t value; \
    ) \
    CAT_ATOMIC_SYNC_CASE( \
        volatile type_name_t value; \
    ) \
    CAT_ATOMIC_NO_CASE( \
        volatile type_name_t value; \
    ) \
} cat_atomic_##name##_t; \
\
CAT_ATOMIC_SYNC_CASE( \
static cat_always_inline type_name_t cat_atomic_##name##_exchange_sync_impl(cat_atomic_##name##_t *atomic, type_name_t desired) \
{ \
    /* A competing writer can change the expected value; retry until this call writes its value. */ \
    type_name_t expected = desired; \
    while (1) { \
        type_name_t actual = __sync_val_compare_and_swap(&atomic->value, expected, desired); \
        if (actual == expected) { \
            return actual; \
        } \
        expected = actual; \
    } \
} \
) \
\
static cat_always_inline void cat_atomic_##name##_init(cat_atomic_##name##_t *atomic, type_name_t desired) \
{ \
    CAT_ATOMIC_C11_CASE({ \
        __c11_atomic_init(&atomic->value, desired); \
    }) \
    CAT_ATOMIC_GNUC_CASE({ \
        atomic->value = desired; \
    }) \
    CAT_ATOMIC_INTERLOCK_CASE({ \
        atomic->value = (interlocked_type_t) desired; \
    }) \
    CAT_ATOMIC_SYNC_CASE({ \
        atomic->value = desired; \
    }) \
    CAT_ATOMIC_NO_CASE({ \
        atomic->value = desired; \
    }) \
} \
\
static cat_always_inline void cat_atomic_##name##_store(cat_atomic_##name##_t *atomic, type_name_t desired) \
{ \
    CAT_ATOMIC_C11_CASE({ \
        __c11_atomic_store(&atomic->value, desired, __ATOMIC_SEQ_CST); \
    }) \
    CAT_ATOMIC_GNUC_CASE({ \
        __atomic_store(&atomic->value, &desired, __ATOMIC_SEQ_CST); \
    }) \
    CAT_ATOMIC_INTERLOCK_CASE({ \
        (void) _InterlockedExchange##interlocked_suffix(&atomic->value, (interlocked_type_t) desired); \
    }) \
    CAT_ATOMIC_SYNC_CASE({ \
        (void) cat_atomic_##name##_exchange_sync_impl(atomic, desired); \
    }) \
    CAT_ATOMIC_NO_CASE({ \
        atomic->value = desired; \
    }) \
} \
\
static cat_always_inline void cat_atomic_##name##_store_explicit(cat_atomic_##name##_t *atomic, type_name_t desired, cat_atomic_memory_order_t order) \
{ \
    CAT_ATOMIC_C11_CASE({ \
        switch (order) { \
            case CAT_ATOMIC_MEMORY_ORDER_RELAXED: \
                __c11_atomic_store(&atomic->value, desired, __ATOMIC_RELAXED); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_RELEASE: \
                __c11_atomic_store(&atomic->value, desired, __ATOMIC_RELEASE); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_SEQ_CST: \
                __c11_atomic_store(&atomic->value, desired, __ATOMIC_SEQ_CST); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_ACQUIRE: \
            case CAT_ATOMIC_MEMORY_ORDER_CONSUME: \
            case CAT_ATOMIC_MEMORY_ORDER_ACQ_REL: \
                CAT_NEVER_HERE("Invalid memory order for store operation"); \
            default: \
                CAT_NEVER_HERE("Unknown memory order"); \
        } \
    }) \
    CAT_ATOMIC_GNUC_CASE({ \
        switch (order) { \
            case CAT_ATOMIC_MEMORY_ORDER_RELAXED: \
                __atomic_store(&atomic->value, &desired, __ATOMIC_RELAXED); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_RELEASE: \
                __atomic_store(&atomic->value, &desired, __ATOMIC_RELEASE); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_SEQ_CST: \
                __atomic_store(&atomic->value, &desired, __ATOMIC_SEQ_CST); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_ACQUIRE: \
            case CAT_ATOMIC_MEMORY_ORDER_CONSUME: \
            case CAT_ATOMIC_MEMORY_ORDER_ACQ_REL: \
                CAT_NEVER_HERE("Invalid memory order for store operation"); \
            default: \
                CAT_NEVER_HERE("Unknown memory order"); \
        } \
    }) \
    CAT_ATOMIC_INTERLOCK_CASE({ \
        switch (order) { \
            case CAT_ATOMIC_MEMORY_ORDER_ACQ_REL: \
            case CAT_ATOMIC_MEMORY_ORDER_SEQ_CST: \
                (void) _InterlockedExchange##interlocked_suffix(&atomic->value, (interlocked_type_t) desired); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_ACQUIRE: \
            case CAT_ATOMIC_MEMORY_ORDER_CONSUME: \
                (void) _InterlockedExchange##interlocked_suffix##_acq(&atomic->value, (interlocked_type_t) desired); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_RELEASE: \
                (void) _InterlockedExchange##interlocked_suffix##_rel(&atomic->value, (interlocked_type_t) desired); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_RELAXED: \
                (void) _InterlockedExchange##interlocked_suffix##_nf(&atomic->value, (interlocked_type_t) desired); \
                break; \
            default: \
                CAT_NEVER_HERE("Unknown memory order"); \
        } \
    }) \
    CAT_ATOMIC_SYNC_CASE({ \
        (void) cat_atomic_##name##_exchange_sync_impl(atomic, desired); \
    }) \
    CAT_ATOMIC_NO_CASE({ \
        atomic->value = desired; \
    }) \
} \
\
static cat_always_inline type_name_t cat_atomic_##name##_load(const cat_atomic_##name##_t *atomic) \
{ \
    CAT_ATOMIC_C11_CASE({ \
        return __c11_atomic_load(&atomic->value, __ATOMIC_SEQ_CST); \
    }) \
    CAT_ATOMIC_GNUC_CASE({ \
        type_name_t ret; \
        __atomic_load(&atomic->value, &ret, __ATOMIC_SEQ_CST); \
        return ret; \
    }) \
    CAT_ATOMIC_INTERLOCK_CASE({ \
        return (type_name_t) _InterlockedLoad##interlocked_suffix(&(((cat_atomic_##name##_t *) atomic)->value)); \
    }) \
    CAT_ATOMIC_SYNC_CASE({ \
        return __sync_fetch_and_or(&(((cat_atomic_##name##_t *) atomic)->value), 0); \
    }) \
    CAT_ATOMIC_NO_CASE({ \
        return atomic->value; \
    }) \
} \
\
static cat_always_inline type_name_t cat_atomic_##name##_load_explicit(const cat_atomic_##name##_t *atomic, cat_atomic_memory_order_t order) \
{ \
    CAT_ATOMIC_C11_CASE({ \
        switch (order) { \
            case CAT_ATOMIC_MEMORY_ORDER_RELAXED: \
                return __c11_atomic_load(&atomic->value, __ATOMIC_RELAXED); \
            case CAT_ATOMIC_MEMORY_ORDER_CONSUME: \
                return __c11_atomic_load(&atomic->value, __ATOMIC_CONSUME); \
            case CAT_ATOMIC_MEMORY_ORDER_ACQUIRE: \
                return __c11_atomic_load(&atomic->value, __ATOMIC_ACQUIRE); \
            case CAT_ATOMIC_MEMORY_ORDER_SEQ_CST: \
                return __c11_atomic_load(&atomic->value, __ATOMIC_SEQ_CST); \
            case CAT_ATOMIC_MEMORY_ORDER_RELEASE: \
            case CAT_ATOMIC_MEMORY_ORDER_ACQ_REL: \
                CAT_NEVER_HERE("Invalid memory order for load operation"); \
            default: \
                CAT_NEVER_HERE("Unknown memory order"); \
        } \
    }) \
    CAT_ATOMIC_GNUC_CASE({ \
        type_name_t ret; \
        switch (order) { \
            case CAT_ATOMIC_MEMORY_ORDER_RELAXED: \
                __atomic_load(&atomic->value, &ret, __ATOMIC_RELAXED); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_CONSUME: \
                __atomic_load(&atomic->value, &ret, __ATOMIC_CONSUME); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_ACQUIRE: \
                __atomic_load(&atomic->value, &ret, __ATOMIC_ACQUIRE); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_SEQ_CST: \
                __atomic_load(&atomic->value, &ret, __ATOMIC_SEQ_CST); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_RELEASE: \
            case CAT_ATOMIC_MEMORY_ORDER_ACQ_REL: \
                CAT_NEVER_HERE("Invalid memory order for load operation"); \
            default: \
                CAT_NEVER_HERE("Unknown memory order"); \
        } \
        return ret; \
    }) \
    CAT_ATOMIC_INTERLOCK_CASE({ \
        switch (order) { \
            case CAT_ATOMIC_MEMORY_ORDER_ACQUIRE: \
            case CAT_ATOMIC_MEMORY_ORDER_CONSUME: \
                return (type_name_t) _InterlockedLoad##interlocked_suffix##_acq(&(((cat_atomic_##name##_t *) atomic)->value)); \
            case CAT_ATOMIC_MEMORY_ORDER_RELAXED: \
                return (type_name_t) _InterlockedLoad##interlocked_suffix##_nf(&(((cat_atomic_##name##_t *) atomic)->value)); \
            case CAT_ATOMIC_MEMORY_ORDER_RELEASE: \
            case CAT_ATOMIC_MEMORY_ORDER_ACQ_REL: \
            case CAT_ATOMIC_MEMORY_ORDER_SEQ_CST: \
                return (type_name_t) _InterlockedLoad##interlocked_suffix(&(((cat_atomic_##name##_t *) atomic)->value)); \
            default: \
                CAT_NEVER_HERE("Unknown memory order"); \
        } \
    }) \
    CAT_ATOMIC_SYNC_CASE({ \
        return __sync_fetch_and_or(&(((cat_atomic_##name##_t *) atomic)->value), 0); \
    }) \
    CAT_ATOMIC_NO_CASE({ \
        return atomic->value; \
    }) \
} \
\
static cat_always_inline type_name_t cat_atomic_##name##_exchange(cat_atomic_##name##_t *atomic, type_name_t desired) \
{ \
    CAT_ATOMIC_C11_CASE({ \
        return __c11_atomic_exchange(&atomic->value, desired, __ATOMIC_SEQ_CST); \
    }) \
    CAT_ATOMIC_GNUC_CASE({ \
        type_name_t ret; \
        __atomic_exchange(&atomic->value, &desired, &ret, __ATOMIC_SEQ_CST); \
        return ret; \
    }) \
    CAT_ATOMIC_INTERLOCK_CASE({ \
        return (type_name_t) _InterlockedExchange##interlocked_suffix(&atomic->value, (interlocked_type_t) desired); \
    }) \
    CAT_ATOMIC_SYNC_CASE({ \
        return cat_atomic_##name##_exchange_sync_impl(atomic, desired); \
    }) \
    CAT_ATOMIC_NO_CASE({ \
        type_name_t ret = atomic->value; \
        atomic->value = desired; \
        return ret; \
    }) \
} \
\
static cat_always_inline type_name_t cat_atomic_##name##_exchange_explicit(cat_atomic_##name##_t *atomic, type_name_t desired, cat_atomic_memory_order_t order) \
{ \
    CAT_ATOMIC_C11_CASE({ \
        switch (order) { \
            case CAT_ATOMIC_MEMORY_ORDER_RELAXED: \
                return __c11_atomic_exchange(&atomic->value, desired, __ATOMIC_RELAXED); \
            case CAT_ATOMIC_MEMORY_ORDER_ACQUIRE: \
                return __c11_atomic_exchange(&atomic->value, desired, __ATOMIC_ACQUIRE); \
            case CAT_ATOMIC_MEMORY_ORDER_RELEASE: \
                return __c11_atomic_exchange(&atomic->value, desired, __ATOMIC_RELEASE); \
            case CAT_ATOMIC_MEMORY_ORDER_ACQ_REL: \
                return __c11_atomic_exchange(&atomic->value, desired, __ATOMIC_ACQ_REL); \
            case CAT_ATOMIC_MEMORY_ORDER_SEQ_CST: \
                return __c11_atomic_exchange(&atomic->value, desired, __ATOMIC_SEQ_CST); \
            case CAT_ATOMIC_MEMORY_ORDER_CONSUME: \
                CAT_NEVER_HERE("Invalid memory order for exchange operation"); \
            default: \
                CAT_NEVER_HERE("Unknown memory order"); \
        } \
    }) \
    CAT_ATOMIC_GNUC_CASE({ \
        type_name_t ret; \
        switch (order) { \
            case CAT_ATOMIC_MEMORY_ORDER_RELAXED: \
                __atomic_exchange(&atomic->value, &desired, &ret, __ATOMIC_RELAXED); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_CONSUME: \
                __atomic_exchange(&atomic->value, &desired, &ret, __ATOMIC_CONSUME); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_ACQUIRE: \
                __atomic_exchange(&atomic->value, &desired, &ret, __ATOMIC_ACQUIRE); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_RELEASE: \
                __atomic_exchange(&atomic->value, &desired, &ret, __ATOMIC_RELEASE); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_ACQ_REL: \
                __atomic_exchange(&atomic->value, &desired, &ret, __ATOMIC_ACQ_REL); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_SEQ_CST: \
                __atomic_exchange(&atomic->value, &desired, &ret, __ATOMIC_SEQ_CST); \
                break; \
            default: \
                CAT_NEVER_HERE("Unknown memory order"); \
        } \
        return ret; \
    }) \
    CAT_ATOMIC_INTERLOCK_CASE({ \
        type_name_t ret; \
        switch (order) { \
            case CAT_ATOMIC_MEMORY_ORDER_ACQ_REL: \
            case CAT_ATOMIC_MEMORY_ORDER_SEQ_CST: \
                ret = (type_name_t) _InterlockedExchange##interlocked_suffix(&atomic->value, (interlocked_type_t) desired); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_ACQUIRE: \
            case CAT_ATOMIC_MEMORY_ORDER_CONSUME: \
                ret = (type_name_t) _InterlockedExchange##interlocked_suffix##_acq(&atomic->value, (interlocked_type_t) desired); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_RELEASE: \
                ret = (type_name_t) _InterlockedExchange##interlocked_suffix##_rel(&atomic->value, (interlocked_type_t) desired); \
                break; \
            case CAT_ATOMIC_MEMORY_ORDER_RELAXED: \
                ret = (type_name_t) _InterlockedExchange##interlocked_suffix##_nf(&atomic->value, (interlocked_type_t) desired); \
                break; \
            default: \
                CAT_NEVER_HERE("Unknown memory order"); \
        } \
        return ret; \
    }) \
    CAT_ATOMIC_SYNC_CASE({ \
        return cat_atomic_##name##_exchange_sync_impl(atomic, desired); \
    }) \
    CAT_ATOMIC_NO_CASE({ \
        type_name_t ret = atomic->value; \
        atomic->value = desired; \
        return ret; \
    }) \
} \
\
static cat_always_inline cat_bool_t cat_atomic_##name##_compare_exchange_strong(cat_atomic_##name##_t *atomic, type_name_t *expected, type_name_t desired) \
{ \
    CAT_ATOMIC_C11_CASE({ \
        return __c11_atomic_compare_exchange_strong(&atomic->value, expected, desired, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); \
    }) \
    CAT_ATOMIC_GNUC_CASE({ \
        return __atomic_compare_exchange_strong(&atomic->value, expected, desired); \
    }) \
    CAT_ATOMIC_INTERLOCK_CASE({ \
        type_name_t value = (type_name_t) _InterlockedCompareExchange##interlocked_suffix(&atomic->value, (interlocked_type_t) desired, (interlocked_type_t) (*expected)); \
        if (value == *expected) { \
            return cat_true; \
        } else { \
            *expected = value; \
            return cat_false; \
        } \
    }) \
    CAT_ATOMIC_SYNC_CASE({ \
        type_name_t value = __sync_val_compare_and_swap(&atomic->value, *expected, desired); \
         if (value == *expected) { \
            return cat_true; \
        } else { \
            *expected = value; \
            return cat_false; \
        } \
    }) \
    CAT_ATOMIC_NO_CASE({ \
        if (atomic->value == *expected) { \
            atomic->value = desired; \
            return cat_true; \
        } else { \
            *expected = atomic->value; \
            return cat_false; \
        } \
    }) \
} \
\
static cat_always_inline cat_bool_t cat_atomic_##name##_compare_exchange_weak(cat_atomic_##name##_t *atomic, type_name_t *expected, type_name_t desired) \
{ \
    CAT_ATOMIC_C11_CASE({ \
        return __c11_atomic_compare_exchange_weak(&atomic->value, expected, desired, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); \
    }) \
    CAT_ATOMIC_GNUC_CASE({ \
        return __atomic_compare_exchange_weak(&atomic->value, expected, desired); \
    }) \
    CAT_ATOMIC_INTERLOCK_CASE({ \
        return cat_atomic_##name##_compare_exchange_strong(atomic, expected, desired); \
    }) \
    CAT_ATOMIC_SYNC_CASE({ \
        return cat_atomic_##name##_compare_exchange_strong(atomic, expected, desired); \
    }) \
    CAT_ATOMIC_NO_CASE({ \
        return cat_atomic_##name##_compare_exchange_strong(atomic, expected, desired); \
    }) \
}

#define CAT_ATOMIC_NUMERIC_OPERATION_FUNCTIONS_GEN(name, type_name_t, interlocked_suffix, interlocked_type_t) \
static cat_always_inline type_name_t cat_atomic_##name##_fetch_add(cat_atomic_##name##_t *atomic, type_name_t operand) \
{ \
    CAT_ATOMIC_C11_CASE({ \
        return __c11_atomic_fetch_add(&atomic->value, operand, __ATOMIC_SEQ_CST); \
    }) \
    CAT_ATOMIC_GNUC_CASE({ \
        return __atomic_fetch_add(&atomic->value, operand, __ATOMIC_SEQ_CST); \
    }) \
    CAT_ATOMIC_INTERLOCK_CASE({ \
        return (type_name_t) _InterlockedExchangeAdd##interlocked_suffix(&atomic->value, (interlocked_type_t) operand); \
    }) \
    CAT_ATOMIC_SYNC_CASE({ \
        return __sync_fetch_and_add(&atomic->value, operand); \
    }) \
    CAT_ATOMIC_NO_CASE({ \
        type_name_t ret = atomic->value; \
        atomic->value += operand; \
        return ret; \
    }) \
} \
\
static cat_always_inline type_name_t cat_atomic_##name##_fetch_sub(cat_atomic_##name##_t *atomic, type_name_t operand) \
{ \
    CAT_ATOMIC_C11_CASE({ \
        return __c11_atomic_fetch_sub(&atomic->value, operand, __ATOMIC_SEQ_CST); \
    }) \
    CAT_ATOMIC_GNUC_CASE({ \
        return __atomic_fetch_sub(&atomic->value, operand, __ATOMIC_SEQ_CST); \
    }) \
    CAT_ATOMIC_INTERLOCK_CASE({ \
        return (type_name_t) _InterlockedExchangeSub##interlocked_suffix(&atomic->value, (interlocked_type_t) operand); \
    }) \
    CAT_ATOMIC_SYNC_CASE({ \
        return __sync_fetch_and_sub(&atomic->value, operand); \
    }) \
    CAT_ATOMIC_NO_CASE({ \
        type_name_t ret = atomic->value; \
        atomic->value -= operand; \
        return ret; \
    }) \
}

#define CAT_ATOMIC_OPERATION_FUNCTIONS_GEN(name, type_name_t, interlocked_suffix, interlocked_type_t) \
        CAT_ATOMIC_COMMON_OPERATION_FUNCTIONS_GEN(name, type_name_t, interlocked_suffix, interlocked_type_t) \
        CAT_ATOMIC_NUMERIC_OPERATION_FUNCTIONS_GEN(name, type_name_t, interlocked_suffix, interlocked_type_t) \

typedef enum cat_atomic_memory_order_e {
   CAT_ATOMIC_MEMORY_ORDER_SEQ_CST,
   CAT_ATOMIC_MEMORY_ORDER_ACQUIRE,
   CAT_ATOMIC_MEMORY_ORDER_RELEASE,
   CAT_ATOMIC_MEMORY_ORDER_RELAXED,
   CAT_ATOMIC_MEMORY_ORDER_ACQ_REL,
   CAT_ATOMIC_MEMORY_ORDER_CONSUME,
} cat_atomic_memory_order_t;

CAT_ATOMIC_COMMON_OPERATION_FUNCTIONS_MAP(CAT_ATOMIC_COMMON_OPERATION_FUNCTIONS_GEN)
CAT_ATOMIC_NUMERIC_OPERATION_FUNCTIONS_MAP(CAT_ATOMIC_OPERATION_FUNCTIONS_GEN)

/*
 * Bare-address atomics.
 *
 * The caller supplies the address, so a value the caller owns needs no library
 * type around it. A waiter can then be indexed by an address, which the
 * generated layer above cannot do.
 *
 * Two points differ from C11 <stdatomic.h>. The address is void *, so the
 * compiler does not check its type. A compare-exchange defaults to seq_cst, and
 * the _explicit form takes the success and failure orders.
 *
 * Alignment is the caller's duty: align the address to the size of the access.
 * The library does not check it.
 *
 * The generated layer above does not forward here yet. Until it does, the two
 * layers validate memory orders differently on the backend whose intrinsics
 * carry a full barrier.
 */

#define CAT_ATOMIC_LOAD_ORDERS_VALID(order) \
    ((order) == CAT_ATOMIC_MEMORY_ORDER_RELAXED || \
     (order) == CAT_ATOMIC_MEMORY_ORDER_CONSUME || \
     (order) == CAT_ATOMIC_MEMORY_ORDER_ACQUIRE || \
     (order) == CAT_ATOMIC_MEMORY_ORDER_SEQ_CST)

#define CAT_ATOMIC_STORE_ORDERS_VALID(order) \
    ((order) == CAT_ATOMIC_MEMORY_ORDER_RELAXED || \
     (order) == CAT_ATOMIC_MEMORY_ORDER_RELEASE || \
     (order) == CAT_ATOMIC_MEMORY_ORDER_SEQ_CST)

#define CAT_ATOMIC_RMW_ORDERS_VALID(order) \
    ((order) == CAT_ATOMIC_MEMORY_ORDER_RELAXED || \
     (order) == CAT_ATOMIC_MEMORY_ORDER_ACQUIRE || \
     (order) == CAT_ATOMIC_MEMORY_ORDER_RELEASE || \
     (order) == CAT_ATOMIC_MEMORY_ORDER_ACQ_REL || \
     (order) == CAT_ATOMIC_MEMORY_ORDER_SEQ_CST)

#define CAT_ATOMIC_FENCE_ORDERS_VALID(order) \
    ((order) == CAT_ATOMIC_MEMORY_ORDER_ACQUIRE || \
     (order) == CAT_ATOMIC_MEMORY_ORDER_RELEASE || \
     (order) == CAT_ATOMIC_MEMORY_ORDER_ACQ_REL || \
     (order) == CAT_ATOMIC_MEMORY_ORDER_SEQ_CST)

/* the relative strength of a memory order, used to compare two of them */
#define CAT_ATOMIC_ORDER_STRENGTH(order) \
    ((order) == CAT_ATOMIC_MEMORY_ORDER_RELAXED ? 1 : \
     (order) == CAT_ATOMIC_MEMORY_ORDER_CONSUME ? 2 : \
     (order) == CAT_ATOMIC_MEMORY_ORDER_ACQUIRE ? 3 : \
     (order) == CAT_ATOMIC_MEMORY_ORDER_RELEASE ? 4 : \
     (order) == CAT_ATOMIC_MEMORY_ORDER_ACQ_REL ? 5 : \
     (order) == CAT_ATOMIC_MEMORY_ORDER_SEQ_CST ? 6 : 0)

/*
 * C11 constrains the failure order three ways: it can not be release or acq_rel,
 * it can not be consume when the success order is consume, and it can not be
 * stronger than the success order. Call this only after the success order passed
 * the read-modify-write check.
 */
#define CAT_ATOMIC_FAILURE_ORDERS_VALID(success, failure) \
    ((failure) != CAT_ATOMIC_MEMORY_ORDER_RELEASE && \
     (failure) != CAT_ATOMIC_MEMORY_ORDER_ACQ_REL && \
     !((failure) == CAT_ATOMIC_MEMORY_ORDER_CONSUME && (success) == CAT_ATOMIC_MEMORY_ORDER_CONSUME) && \
     CAT_ATOMIC_ORDER_STRENGTH(failure) <= CAT_ATOMIC_ORDER_STRENGTH(success))

#define CAT_ATOMIC_LOAD_ORDERS_TO_NATIVE(order) \
    ((order) == CAT_ATOMIC_MEMORY_ORDER_RELAXED ? __ATOMIC_RELAXED : \
     (order) == CAT_ATOMIC_MEMORY_ORDER_CONSUME ? __ATOMIC_CONSUME : \
     (order) == CAT_ATOMIC_MEMORY_ORDER_ACQUIRE ? __ATOMIC_ACQUIRE : \
     __ATOMIC_SEQ_CST)

#define CAT_ATOMIC_STORE_ORDERS_TO_NATIVE(order) \
    ((order) == CAT_ATOMIC_MEMORY_ORDER_RELAXED ? __ATOMIC_RELAXED : \
     (order) == CAT_ATOMIC_MEMORY_ORDER_RELEASE ? __ATOMIC_RELEASE : \
     __ATOMIC_SEQ_CST)

#define CAT_ATOMIC_RMW_ORDERS_TO_NATIVE(order) \
    ((order) == CAT_ATOMIC_MEMORY_ORDER_RELAXED ? __ATOMIC_RELAXED : \
     (order) == CAT_ATOMIC_MEMORY_ORDER_ACQUIRE ? __ATOMIC_ACQUIRE : \
     (order) == CAT_ATOMIC_MEMORY_ORDER_RELEASE ? __ATOMIC_RELEASE : \
     (order) == CAT_ATOMIC_MEMORY_ORDER_ACQ_REL ? __ATOMIC_ACQ_REL : \
     __ATOMIC_SEQ_CST)

/* a full barrier is the strongest fence order, so it maps to itself */
#define CAT_ATOMIC_FENCE_ORDERS_TO_NATIVE(order) CAT_ATOMIC_RMW_ORDERS_TO_NATIVE(order)

/*
 * The plain-memory operations. The two compiler families need different
 * spellings, and the pointee type travels with the call.
 */

#ifdef CAT_HAVE_C11_ATOMIC
/*
 * The C11 builtins of clang take a pointer to an _Atomic type and reject a
 * pointer to plain memory, so the call declares the pointee type with a cast.
 * GCC has no such requirement and its cast is the identity. Neither form puts
 * an _Atomic member into any structure.
 */
# define CAT_ATOMIC_ATOMIC_CAST(type_name_t, address) ((_Atomic(type_name_t) *) (address))
# define CAT_ATOMIC_PLAIN_LOAD(address, type_name_t) \
        __c11_atomic_load(address, __ATOMIC_SEQ_CST)
# define CAT_ATOMIC_PLAIN_LOAD_ORDERED(address, type_name_t, order) \
        __c11_atomic_load(address, CAT_ATOMIC_LOAD_ORDERS_TO_NATIVE(order))
# define CAT_ATOMIC_PLAIN_STORE(address, desired, type_name_t) \
        __c11_atomic_store(address, (desired), __ATOMIC_SEQ_CST)
# define CAT_ATOMIC_PLAIN_STORE_ORDERED(address, desired, type_name_t, order) \
        __c11_atomic_store(address, (desired), CAT_ATOMIC_STORE_ORDERS_TO_NATIVE(order))
# define CAT_ATOMIC_PLAIN_EXCHANGE(address, desired, type_name_t) \
        __c11_atomic_exchange(address, (desired), __ATOMIC_SEQ_CST)
# define CAT_ATOMIC_PLAIN_EXCHANGE_ORDERED(address, desired, type_name_t, order) \
        __c11_atomic_exchange(address, (desired), CAT_ATOMIC_RMW_ORDERS_TO_NATIVE(order))
# define CAT_ATOMIC_PLAIN_FETCH_ADD(address, operand, type_name_t) \
        __c11_atomic_fetch_add(address, (operand), __ATOMIC_SEQ_CST)
# define CAT_ATOMIC_PLAIN_FETCH_ADD_ORDERED(address, operand, type_name_t, order) \
        __c11_atomic_fetch_add(address, (operand), CAT_ATOMIC_RMW_ORDERS_TO_NATIVE(order))
# define CAT_ATOMIC_PLAIN_FETCH_SUB(address, operand, type_name_t) \
        __c11_atomic_fetch_sub(address, (operand), __ATOMIC_SEQ_CST)
# define CAT_ATOMIC_PLAIN_FETCH_SUB_ORDERED(address, operand, type_name_t, order) \
        __c11_atomic_fetch_sub(address, (operand), CAT_ATOMIC_RMW_ORDERS_TO_NATIVE(order))
# define CAT_ATOMIC_PLAIN_COMPARE_EXCHANGE(address, expected, desired, type_name_t) \
        (cat_bool_t) __c11_atomic_compare_exchange_strong(address, (expected), (desired), __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
# define CAT_ATOMIC_PLAIN_COMPARE_EXCHANGE_ORDERED(address, expected, desired, type_name_t, success, failure) \
        (cat_bool_t) __c11_atomic_compare_exchange_strong(address, (expected), (desired), \
            CAT_ATOMIC_RMW_ORDERS_TO_NATIVE(success), CAT_ATOMIC_LOAD_ORDERS_TO_NATIVE(failure))
# define CAT_ATOMIC_PLAIN_FENCE() \
        __c11_atomic_thread_fence(__ATOMIC_SEQ_CST)
# define CAT_ATOMIC_PLAIN_FENCE_ORDERED(order) \
        __c11_atomic_thread_fence(CAT_ATOMIC_FENCE_ORDERS_TO_NATIVE(order))
#else
# define CAT_ATOMIC_ATOMIC_CAST(type_name_t, address) (address)
# define CAT_ATOMIC_PLAIN_LOAD(address, type_name_t) \
        __atomic_load_n(address, __ATOMIC_SEQ_CST)
# define CAT_ATOMIC_PLAIN_LOAD_ORDERED(address, type_name_t, order) \
        __atomic_load_n(address, CAT_ATOMIC_LOAD_ORDERS_TO_NATIVE(order))
# define CAT_ATOMIC_PLAIN_STORE(address, desired, type_name_t) \
        __atomic_store_n(address, (desired), __ATOMIC_SEQ_CST)
# define CAT_ATOMIC_PLAIN_STORE_ORDERED(address, desired, type_name_t, order) \
        __atomic_store_n(address, (desired), CAT_ATOMIC_STORE_ORDERS_TO_NATIVE(order))
# define CAT_ATOMIC_PLAIN_EXCHANGE(address, desired, type_name_t) \
        __atomic_exchange_n(address, (desired), __ATOMIC_SEQ_CST)
# define CAT_ATOMIC_PLAIN_EXCHANGE_ORDERED(address, desired, type_name_t, order) \
        __atomic_exchange_n(address, (desired), CAT_ATOMIC_RMW_ORDERS_TO_NATIVE(order))
# define CAT_ATOMIC_PLAIN_FETCH_ADD(address, operand, type_name_t) \
        __atomic_fetch_add(address, (operand), __ATOMIC_SEQ_CST)
# define CAT_ATOMIC_PLAIN_FETCH_ADD_ORDERED(address, operand, type_name_t, order) \
        __atomic_fetch_add(address, (operand), CAT_ATOMIC_RMW_ORDERS_TO_NATIVE(order))
# define CAT_ATOMIC_PLAIN_FETCH_SUB(address, operand, type_name_t) \
        __atomic_fetch_sub(address, (operand), __ATOMIC_SEQ_CST)
# define CAT_ATOMIC_PLAIN_FETCH_SUB_ORDERED(address, operand, type_name_t, order) \
        __atomic_fetch_sub(address, (operand), CAT_ATOMIC_RMW_ORDERS_TO_NATIVE(order))
# define CAT_ATOMIC_PLAIN_COMPARE_EXCHANGE(address, expected, desired, type_name_t) \
        (cat_bool_t) __atomic_compare_exchange_n(address, (expected), (desired), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
# define CAT_ATOMIC_PLAIN_COMPARE_EXCHANGE_ORDERED(address, expected, desired, type_name_t, success, failure) \
        (cat_bool_t) __atomic_compare_exchange_n(address, (expected), (desired), 0, \
            CAT_ATOMIC_RMW_ORDERS_TO_NATIVE(success), CAT_ATOMIC_LOAD_ORDERS_TO_NATIVE(failure))
# define CAT_ATOMIC_PLAIN_FENCE() \
        __atomic_thread_fence(__ATOMIC_SEQ_CST)
# define CAT_ATOMIC_PLAIN_FENCE_ORDERED(order) \
        __atomic_thread_fence(CAT_ATOMIC_FENCE_ORDERS_TO_NATIVE(order))
#endif


/*
 * Widths the bare-address layer covers. They are the same on every platform,
 * so this list is not the numeric map above.
 */
#define CAT_ATOMIC_BARE_OPERATIONS_MAP(XX) \
        XX(int32, int32_t, 32) \
        XX(int64, int64_t, 64) \
        XX(intptr, intptr_t, POINTER) \
        XX(uintptr, uintptr_t, POINTER) \

/* The plain-memory path does not need the interlocked marker, so it ignores it. */
#define CAT_ATOMIC_BARE_CAST(type_name_t, interlocked_suffix, address) CAT_ATOMIC_ATOMIC_CAST(type_name_t, address)
#define CAT_ATOMIC_BARE_LOAD(address, type_name_t, interlocked_suffix) \
        CAT_ATOMIC_PLAIN_LOAD(CAT_ATOMIC_BARE_CAST(type_name_t, interlocked_suffix, address), type_name_t)
#define CAT_ATOMIC_BARE_LOAD_ORDERED(address, type_name_t, interlocked_suffix, order) \
        CAT_ATOMIC_PLAIN_LOAD_ORDERED(CAT_ATOMIC_BARE_CAST(type_name_t, interlocked_suffix, address), type_name_t, order)
#define CAT_ATOMIC_BARE_STORE(address, desired, type_name_t, interlocked_suffix) \
        CAT_ATOMIC_PLAIN_STORE(CAT_ATOMIC_BARE_CAST(type_name_t, interlocked_suffix, address), desired, type_name_t)
#define CAT_ATOMIC_BARE_STORE_ORDERED(address, desired, type_name_t, interlocked_suffix, order) \
        CAT_ATOMIC_PLAIN_STORE_ORDERED(CAT_ATOMIC_BARE_CAST(type_name_t, interlocked_suffix, address), desired, type_name_t, order)
#define CAT_ATOMIC_BARE_EXCHANGE(address, desired, type_name_t, interlocked_suffix) \
        CAT_ATOMIC_PLAIN_EXCHANGE(CAT_ATOMIC_BARE_CAST(type_name_t, interlocked_suffix, address), desired, type_name_t)
#define CAT_ATOMIC_BARE_EXCHANGE_ORDERED(address, desired, type_name_t, interlocked_suffix, order) \
        CAT_ATOMIC_PLAIN_EXCHANGE_ORDERED(CAT_ATOMIC_BARE_CAST(type_name_t, interlocked_suffix, address), desired, type_name_t, order)
#define CAT_ATOMIC_BARE_FETCH_ADD(address, operand, type_name_t, interlocked_suffix) \
        CAT_ATOMIC_PLAIN_FETCH_ADD(CAT_ATOMIC_BARE_CAST(type_name_t, interlocked_suffix, address), operand, type_name_t)
#define CAT_ATOMIC_BARE_FETCH_ADD_ORDERED(address, operand, type_name_t, interlocked_suffix, order) \
        CAT_ATOMIC_PLAIN_FETCH_ADD_ORDERED(CAT_ATOMIC_BARE_CAST(type_name_t, interlocked_suffix, address), operand, type_name_t, order)
#define CAT_ATOMIC_BARE_FETCH_SUB(address, operand, type_name_t, interlocked_suffix) \
        CAT_ATOMIC_PLAIN_FETCH_SUB(CAT_ATOMIC_BARE_CAST(type_name_t, interlocked_suffix, address), operand, type_name_t)
#define CAT_ATOMIC_BARE_FETCH_SUB_ORDERED(address, operand, type_name_t, interlocked_suffix, order) \
        CAT_ATOMIC_PLAIN_FETCH_SUB_ORDERED(CAT_ATOMIC_BARE_CAST(type_name_t, interlocked_suffix, address), operand, type_name_t, order)
#define CAT_ATOMIC_BARE_COMPARE_EXCHANGE(address, expected, desired, type_name_t, interlocked_suffix) \
        CAT_ATOMIC_PLAIN_COMPARE_EXCHANGE(CAT_ATOMIC_BARE_CAST(type_name_t, interlocked_suffix, address), expected, desired, type_name_t)
#define CAT_ATOMIC_BARE_COMPARE_EXCHANGE_ORDERED(address, expected, desired, type_name_t, interlocked_suffix, success, failure) \
        CAT_ATOMIC_PLAIN_COMPARE_EXCHANGE_ORDERED(CAT_ATOMIC_BARE_CAST(type_name_t, interlocked_suffix, address), expected, desired, type_name_t, success, failure)
#define CAT_ATOMIC_BARE_FENCE() CAT_ATOMIC_PLAIN_FENCE()
#define CAT_ATOMIC_BARE_FENCE_ORDERED(order) CAT_ATOMIC_PLAIN_FENCE_ORDERED(order)
#if defined(CAT_HAVE_INTERLOCK_ATOMIC)
/*
 * Three things break on 32-bit Windows. The 64-bit columns need Or64, Exchange64
 * and ExchangeAdd64, which x86 does not have, and compare-exchange needs a loop
 * around the 64-bit form. The pointer column would also write 8 bytes into a
 * 4-byte object, which corrupts memory. A 32-bit path needs all three handled,
 * so a 32-bit build stops here until someone writes it.
 */
# if !defined(_WIN64)
#  error "The bare-address atomics need a 64-bit target on MSVC"
# endif
# undef CAT_ATOMIC_BARE_CAST
# undef CAT_ATOMIC_BARE_LOAD
# undef CAT_ATOMIC_BARE_LOAD_ORDERED
# undef CAT_ATOMIC_BARE_STORE
# undef CAT_ATOMIC_BARE_STORE_ORDERED
# undef CAT_ATOMIC_BARE_EXCHANGE
# undef CAT_ATOMIC_BARE_EXCHANGE_ORDERED
# undef CAT_ATOMIC_BARE_FETCH_ADD
# undef CAT_ATOMIC_BARE_FETCH_ADD_ORDERED
# undef CAT_ATOMIC_BARE_FETCH_SUB
# undef CAT_ATOMIC_BARE_FETCH_SUB_ORDERED
# undef CAT_ATOMIC_BARE_COMPARE_EXCHANGE
# undef CAT_ATOMIC_BARE_COMPARE_EXCHANGE_ORDERED
# undef CAT_ATOMIC_BARE_FENCE
# undef CAT_ATOMIC_BARE_FENCE_ORDERED
/*
 * One macro per width instead of a type token pasted into the intrinsic name.
 * MSVC does not expand a macro argument that is an operand of ##, so a pasted
 * type name would arrive at the intrinsic unexpanded. The marker selects the
 * width here, and every operation pattern is written once in the two generic
 * macros below.
 *
 * The intrinsics also take exact pointer types (long *, __int64 * or void **),
 * so the address is cast to the interlocked width before it reaches them.
 */
/*
 * The intrinsic names below are checked against the MSVC lists for ARM64, x64
 * and x86. Two names do not exist anywhere: there is no _InterlockedLoad, and a
 * zero-valued Or is the load form, and there is no _InterlockedExchangeSubtract,
 * and a negated ExchangeAdd is the subtract form. The list marks a base name as
 * a full barrier, so a relaxed order still costs a barrier here.
 */
#define CAT_ATOMIC_INTERLOCK_LOAD_32(address, il_t) \
        ((il_t) _InterlockedOr((volatile long *) (address), 0))
#define CAT_ATOMIC_INTERLOCK_LOAD_64(address, il_t) \
        ((il_t) _InterlockedOr64((volatile __int64 *) (address), 0))
#define CAT_ATOMIC_INTERLOCK_LOAD_POINTER(address, il_t) \
        ((il_t) _InterlockedCompareExchangePointer((void *volatile *) (address), NULL, NULL))
#define CAT_ATOMIC_INTERLOCK_STORE_32(address, desired, il_t) \
        ((void) _InterlockedExchange((volatile long *) (address), (long) (desired)))
#define CAT_ATOMIC_INTERLOCK_STORE_64(address, desired, il_t) \
        ((void) _InterlockedExchange64((volatile __int64 *) (address), (__int64) (desired)))
#define CAT_ATOMIC_INTERLOCK_STORE_POINTER(address, desired, il_t) \
        ((void) _InterlockedExchangePointer((void *volatile *) (address), (void *) (desired)))
#define CAT_ATOMIC_INTERLOCK_EXCHANGE_32(address, desired, il_t) \
        ((il_t) _InterlockedExchange((volatile long *) (address), (long) (desired)))
#define CAT_ATOMIC_INTERLOCK_EXCHANGE_64(address, desired, il_t) \
        ((il_t) _InterlockedExchange64((volatile __int64 *) (address), (__int64) (desired)))
#define CAT_ATOMIC_INTERLOCK_EXCHANGE_POINTER(address, desired, il_t) \
        ((il_t) _InterlockedExchangePointer((void *volatile *) (address), (void *) (desired)))
#define CAT_ATOMIC_INTERLOCK_FETCH_ADD_32(address, operand, il_t) \
        ((il_t) _InterlockedExchangeAdd((volatile long *) (address), (long) (operand)))
#define CAT_ATOMIC_INTERLOCK_FETCH_ADD_64(address, operand, il_t) \
        ((il_t) _InterlockedExchangeAdd64((volatile __int64 *) (address), (__int64) (operand)))
#define CAT_ATOMIC_INTERLOCK_FETCH_SUB_32(address, operand, il_t) \
        ((il_t) _InterlockedExchangeAdd((volatile long *) (address), -(long) (operand)))
#define CAT_ATOMIC_INTERLOCK_FETCH_SUB_64(address, operand, il_t) \
        ((il_t) _InterlockedExchangeAdd64((volatile __int64 *) (address), -(__int64) (operand)))
#define CAT_ATOMIC_INTERLOCK_COMPARE_EXCHANGE_32(address, expected, desired, il_t) \
        cat_atomic_interlock_cas_long((volatile long *) (address), (long *) (expected), (long) (desired))
#define CAT_ATOMIC_INTERLOCK_COMPARE_EXCHANGE_64(address, expected, desired, il_t) \
        cat_atomic_interlock_cas___int64((volatile __int64 *) (address), (__int64 *) (expected), (__int64) (desired))
#define CAT_ATOMIC_INTERLOCK_COMPARE_EXCHANGE_POINTER(address, expected, desired, il_t) \
        cat_atomic_interlock_cas_pointer((void *volatile *) (address), (void **) (expected), (void *) (desired))
/* fetch_sub has no pointer form: void * has no arithmetic */
#define CAT_ATOMIC_INTERLOCK_FETCH_ADD_POINTER(address, operand, il_t) \
        ((il_t) _InterlockedExchangeAdd64((volatile __int64 *) (address), (__int64) (operand)))
#define CAT_ATOMIC_INTERLOCK_FETCH_SUB_POINTER(address, operand, il_t) \
        ((il_t) _InterlockedExchangeAdd64((volatile __int64 *) (address), -(__int64) (operand)))
#define CAT_ATOMIC_IL_TYPE_32        long
#define CAT_ATOMIC_IL_TYPE_64        __int64
#define CAT_ATOMIC_IL_TYPE_POINTER   void *
/*
 * The marker selects one macro above, so each operation pattern is written once
 * per width instead of once per width and operation.
 */
#define CAT_ATOMIC_BARE_LOAD(address, type_name_t, suffix) \
        CAT_ATOMIC_INTERLOCK_LOAD_ ## suffix(address, CAT_ATOMIC_IL_TYPE_ ## suffix)
#define CAT_ATOMIC_BARE_STORE(address, desired, type_name_t, suffix) \
        CAT_ATOMIC_INTERLOCK_STORE_ ## suffix(address, desired, CAT_ATOMIC_IL_TYPE_ ## suffix)
#define CAT_ATOMIC_BARE_EXCHANGE(address, desired, type_name_t, suffix) \
        CAT_ATOMIC_INTERLOCK_EXCHANGE_ ## suffix(address, desired, CAT_ATOMIC_IL_TYPE_ ## suffix)
#define CAT_ATOMIC_BARE_FETCH_ADD(address, operand, type_name_t, suffix) \
        CAT_ATOMIC_INTERLOCK_FETCH_ADD_ ## suffix(address, operand, CAT_ATOMIC_IL_TYPE_ ## suffix)
#define CAT_ATOMIC_BARE_FETCH_SUB(address, operand, type_name_t, suffix) \
        CAT_ATOMIC_INTERLOCK_FETCH_SUB_ ## suffix(address, operand, CAT_ATOMIC_IL_TYPE_ ## suffix)
#define CAT_ATOMIC_BARE_COMPARE_EXCHANGE(address, expected, desired, type_name_t, suffix) \
        CAT_ATOMIC_INTERLOCK_COMPARE_EXCHANGE_ ## suffix(address, expected, desired, CAT_ATOMIC_IL_TYPE_ ## suffix)
#define CAT_ATOMIC_BARE_LOAD_ORDERED(address, type_name_t, suffix, order) \
        CAT_ATOMIC_BARE_LOAD(address, type_name_t, suffix)
#define CAT_ATOMIC_BARE_STORE_ORDERED(address, desired, type_name_t, suffix, order) \
        CAT_ATOMIC_BARE_STORE(address, desired, type_name_t, suffix)
#define CAT_ATOMIC_BARE_EXCHANGE_ORDERED(address, desired, type_name_t, suffix, order) \
        CAT_ATOMIC_BARE_EXCHANGE(address, desired, type_name_t, suffix)
#define CAT_ATOMIC_BARE_FETCH_ADD_ORDERED(address, operand, type_name_t, suffix, order) \
        CAT_ATOMIC_BARE_FETCH_ADD(address, operand, type_name_t, suffix)
#define CAT_ATOMIC_BARE_FETCH_SUB_ORDERED(address, operand, type_name_t, suffix, order) \
        CAT_ATOMIC_BARE_FETCH_SUB(address, operand, type_name_t, suffix)
#define CAT_ATOMIC_BARE_COMPARE_EXCHANGE_ORDERED(address, expected, desired, type_name_t, suffix, success, failure) \
        CAT_ATOMIC_BARE_COMPARE_EXCHANGE(address, expected, desired, type_name_t, suffix)
#if defined(_M_ARM64) || defined(_M_ARM64EC)
/* __dmb and _ARM64_BARRIER_SY come from winnt.h */
# define CAT_ATOMIC_BARE_FENCE() __dmb(_ARM64_BARRIER_SY)
#else
/* _mm_mfence comes from intrin.h and is a full barrier on x86 and x64 */
# define CAT_ATOMIC_BARE_FENCE() _mm_mfence()
#endif
#define CAT_ATOMIC_BARE_FENCE_ORDERED(order) CAT_ATOMIC_BARE_FENCE()
static cat_always_inline cat_bool_t cat_atomic_interlock_cas_long(
    volatile long *address, long *expected, long desired)
{
    const long previous = _InterlockedCompareExchange(address, desired, *expected);
    if (previous == *expected) {
        return cat_true;
    }
    *expected = previous;
    return cat_false;
}
static cat_always_inline cat_bool_t cat_atomic_interlock_cas___int64(
    volatile __int64 *address, __int64 *expected, __int64 desired)
{
    const __int64 previous = _InterlockedCompareExchange64(address, desired, *expected);
    if (previous == *expected) {
        return cat_true;
    }
    *expected = previous;
    return cat_false;
}
static cat_always_inline cat_bool_t cat_atomic_interlock_cas_pointer(
    void *volatile *address, void **expected, void *desired)
{
    void *previous = _InterlockedCompareExchangePointer(address, desired, *expected);
    if (previous == *expected) {
        return cat_true;
    }
    *expected = previous;
    return cat_false;
}
#endif /* CAT_HAVE_INTERLOCK_ATOMIC overrides */

#define CAT_ATOMIC_BARE_OPERATIONS_GEN(name, type_name_t, interlocked_suffix) \
static cat_always_inline type_name_t cat_atomic_##name##_load_at(const void *address) \
{ \
    return (type_name_t) CAT_ATOMIC_BARE_LOAD((const type_name_t *) address, type_name_t, interlocked_suffix); \
} \
static cat_always_inline type_name_t cat_atomic_##name##_load_at_explicit( \
    const void *address, cat_atomic_memory_order_t order) \
{ \
    if (!CAT_ATOMIC_LOAD_ORDERS_VALID(order)) { \
        CAT_NEVER_HERE("Invalid memory order for load operation"); \
    } \
    return (type_name_t) CAT_ATOMIC_BARE_LOAD_ORDERED((const type_name_t *) address, type_name_t, interlocked_suffix, order); \
} \
static cat_always_inline void cat_atomic_##name##_store_at(void *address, type_name_t desired) \
{ \
    CAT_ATOMIC_BARE_STORE((type_name_t *) address, desired, type_name_t, interlocked_suffix); \
} \
static cat_always_inline void cat_atomic_##name##_store_at_explicit( \
    void *address, type_name_t desired, cat_atomic_memory_order_t order) \
{ \
    if (!CAT_ATOMIC_STORE_ORDERS_VALID(order)) { \
        CAT_NEVER_HERE("Invalid memory order for store operation"); \
    } \
    CAT_ATOMIC_BARE_STORE_ORDERED((type_name_t *) address, desired, type_name_t, interlocked_suffix, order); \
} \
static cat_always_inline type_name_t cat_atomic_##name##_exchange_at(void *address, type_name_t desired) \
{ \
    return (type_name_t) CAT_ATOMIC_BARE_EXCHANGE((type_name_t *) address, desired, type_name_t, interlocked_suffix); \
} \
static cat_always_inline type_name_t cat_atomic_##name##_exchange_at_explicit( \
    void *address, type_name_t desired, cat_atomic_memory_order_t order) \
{ \
    if (!CAT_ATOMIC_RMW_ORDERS_VALID(order)) { \
        CAT_NEVER_HERE("Invalid memory order for exchange operation"); \
    } \
    return (type_name_t) CAT_ATOMIC_BARE_EXCHANGE_ORDERED((type_name_t *) address, desired, type_name_t, interlocked_suffix, order); \
} \
static cat_always_inline type_name_t cat_atomic_##name##_fetch_add_at(void *address, type_name_t operand) \
{ \
    return (type_name_t) CAT_ATOMIC_BARE_FETCH_ADD((type_name_t *) address, operand, type_name_t, interlocked_suffix); \
} \
static cat_always_inline type_name_t cat_atomic_##name##_fetch_add_at_explicit( \
    void *address, type_name_t operand, cat_atomic_memory_order_t order) \
{ \
    if (!CAT_ATOMIC_RMW_ORDERS_VALID(order)) { \
        CAT_NEVER_HERE("Invalid memory order for fetch_add operation"); \
    } \
    return (type_name_t) CAT_ATOMIC_BARE_FETCH_ADD_ORDERED((type_name_t *) address, operand, type_name_t, interlocked_suffix, order); \
} \
static cat_always_inline type_name_t cat_atomic_##name##_fetch_sub_at(void *address, type_name_t operand) \
{ \
    return (type_name_t) CAT_ATOMIC_BARE_FETCH_SUB((type_name_t *) address, operand, type_name_t, interlocked_suffix); \
} \
static cat_always_inline type_name_t cat_atomic_##name##_fetch_sub_at_explicit( \
    void *address, type_name_t operand, cat_atomic_memory_order_t order) \
{ \
    if (!CAT_ATOMIC_RMW_ORDERS_VALID(order)) { \
        CAT_NEVER_HERE("Invalid memory order for fetch_sub operation"); \
    } \
    return (type_name_t) CAT_ATOMIC_BARE_FETCH_SUB_ORDERED((type_name_t *) address, operand, type_name_t, interlocked_suffix, order); \
} \
static cat_always_inline cat_bool_t cat_atomic_##name##_compare_exchange_strong_at( \
    void *address, type_name_t *expected, type_name_t desired) \
{ \
    return CAT_ATOMIC_BARE_COMPARE_EXCHANGE((type_name_t *) address, expected, desired, type_name_t, interlocked_suffix); \
} \
static cat_always_inline cat_bool_t cat_atomic_##name##_compare_exchange_strong_at_explicit( \
    void *address, type_name_t *expected, type_name_t desired, \
    cat_atomic_memory_order_t success_order, cat_atomic_memory_order_t failure_order) \
{ \
    if (!CAT_ATOMIC_RMW_ORDERS_VALID(success_order)) { \
        CAT_NEVER_HERE("Invalid memory order for compare_exchange operation"); \
    } \
    if (!CAT_ATOMIC_FAILURE_ORDERS_VALID(success_order, failure_order)) { \
        CAT_NEVER_HERE("Invalid failure memory order for compare_exchange operation"); \
    } \
    return CAT_ATOMIC_BARE_COMPARE_EXCHANGE_ORDERED( \
        (type_name_t *) address, expected, desired, type_name_t, interlocked_suffix, success_order, failure_order); \
} \
static cat_always_inline cat_bool_t cat_atomic_##name##_compare_exchange_weak_at( \
    void *address, type_name_t *expected, type_name_t desired) \
{ \
    return CAT_ATOMIC_BARE_COMPARE_EXCHANGE((type_name_t *) address, expected, desired, type_name_t, interlocked_suffix); \
} \
static cat_always_inline cat_bool_t cat_atomic_##name##_compare_exchange_weak_at_explicit( \
    void *address, type_name_t *expected, type_name_t desired, \
    cat_atomic_memory_order_t success_order, cat_atomic_memory_order_t failure_order) \
{ \
    if (!CAT_ATOMIC_RMW_ORDERS_VALID(success_order)) { \
        CAT_NEVER_HERE("Invalid memory order for compare_exchange operation"); \
    } \
    if (!CAT_ATOMIC_FAILURE_ORDERS_VALID(success_order, failure_order)) { \
        CAT_NEVER_HERE("Invalid failure memory order for compare_exchange operation"); \
    } \
    return CAT_ATOMIC_BARE_COMPARE_EXCHANGE_ORDERED( \
        (type_name_t *) address, expected, desired, type_name_t, interlocked_suffix, success_order, failure_order); \
}

CAT_ATOMIC_BARE_OPERATIONS_MAP(CAT_ATOMIC_BARE_OPERATIONS_GEN)

static cat_always_inline void cat_atomic_fence(void)
{
    CAT_ATOMIC_BARE_FENCE();
}

static cat_always_inline void cat_atomic_fence_explicit(cat_atomic_memory_order_t order)
{
    if (!CAT_ATOMIC_FENCE_ORDERS_VALID(order)) {
        CAT_NEVER_HERE("Invalid memory order for fence operation");
    }
    CAT_ATOMIC_BARE_FENCE_ORDERED(order);
}

#ifdef __cplusplus
}
#endif
#endif /* CAT_ATOMIC_H */
