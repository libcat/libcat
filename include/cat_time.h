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

#ifndef CAT_TIME_H
#define CAT_TIME_H
#ifdef __cplusplus
extern "C" {
#endif

#include "cat.h"

/* powered by hr_time() */
CAT_API cat_nsec_t cat_time_nsec(void);
CAT_API cat_msec_t cat_time_msec(void);
/* powered by event loop */
CAT_API cat_msec_t cat_time_msec_cached(void);

/* powered by gettimeofday() */
CAT_API cat_nsec_t cat_time_nsec2(void);
CAT_API cat_msec_t cat_time_msec2(void);
CAT_API double cat_microtime(void);

CAT_API char *cat_time_format_msec(cat_msec_t msec);

/*
 * A timer that reports expiry through a callback. It does not resume a coroutine.
 *
 * A waiter needs this, because a wakeup from another thread and its own timeout
 * both arrive at the same coroutine. Only one of them may resume it.
 */
typedef struct cat_time_timeout_s cat_time_timeout_t;
typedef void (*cat_time_timeout_callback_t)(cat_time_timeout_t *timer, cat_data_t *data);

/*
 * after is in msec. A negative value arms nothing and returns NULL. A failure
 * also returns NULL.
 *
 * The caller owns the returned object, which stays valid until
 * cat_time_timeout_close. The object can therefore outlive the variable that the
 * caller used on the stack.
 */
CAT_API cat_time_timeout_t *cat_time_timeout_init(cat_time_timeout_callback_t callback,
                                                  cat_data_t *data, cat_timeout_t after); CAT_INTERNAL
/* Safe to call with NULL and safe to call twice through the same pointer. */
CAT_API void cat_time_timeout_close(cat_time_timeout_t *timer); CAT_INTERNAL

/*
 * cat_time_wait(), cat_time_delay() and cat_time_msleep() wait for the whole
 * time even when a signal arrives, because they are CAT_TIME_FLAG_NONE. Their
 * _ex variants take the flags below.
 */
typedef enum cat_time_flag_e {
    CAT_TIME_FLAG_NONE = 0,
    /* a signal ends the wait early; the caller sees CAT_EINTR or the remaining time */
    CAT_TIME_FLAG_INTERRUPTIBLE = 1 << 0,
} cat_time_flag_t;

typedef int cat_time_flags_t;

/* cat_false: yield failed or sleep failed or timeout, cat_true: cancelled */
CAT_API cat_bool_t cat_time_wait(cat_timeout_t timeout);
/* An interrupt reports cat_false with CAT_EINTR; an expired timeout still
 * reports the timeout when both happen in the same round */
CAT_API cat_bool_t cat_time_wait_ex(cat_timeout_t timeout, cat_time_flags_t flags);

#define CAT_TIME_WAIT_START() do { \
    cat_msec_t __time_cached = cat_time_msec_cached(); \

#define CAT_TIME_WAIT_END(timeout) \
    if (timeout == CAT_TIMEOUT_FOREVER) { \
        break; \
    } \
    timeout -= (cat_time_msec_cached() - __time_cached); \
    if (unlikely(timeout < 0)) { \
        timeout = 0; \
    } \
} while (0)

/* OK: timeout, NONE: cancelled, ERROR: error occured */
CAT_API cat_ret_t cat_time_delay(cat_timeout_t timeout);
/* An interrupt reports CAT_RET_ERROR with CAT_EINTR */
CAT_API cat_ret_t cat_time_delay_ex(cat_timeout_t timeout, cat_time_flags_t flags);

CAT_API unsigned int cat_time_sleep(unsigned int seconds);
/* behaviour is same with sleep() but use msec */
CAT_API cat_msec_t cat_time_msleep(cat_msec_t millisecond);
/* An interrupt reports the unslept milliseconds, like sleep(), with CAT_EINTR */
CAT_API cat_msec_t cat_time_msleep_ex(cat_msec_t msec, cat_time_flags_t flags);
CAT_API int cat_time_usleep(cat_usec_t microseconds);
CAT_API int cat_time_nanosleep(const struct cat_timespec *req, struct cat_timespec *rem);

/* timeval to timeout */
CAT_API cat_timeout_t cat_time_tv2to(const struct timeval *tv);

#ifdef __cplusplus
}
#endif
#endif /* CAT_TIME_H */
