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
  | Author: Yun Dou <dixyes@gmail.com>                                       |
  +--------------------------------------------------------------------------+
 */

#ifndef CAT_INTERRUPT_H
#define CAT_INTERRUPT_H
#ifdef __cplusplus
extern "C" {
#endif

#include "cat.h"
#include "cat_queue.h"

typedef struct cat_interrupt_waiter_s cat_interrupt_waiter_t;

typedef void (*cat_interrupt_wake_t)(cat_interrupt_waiter_t *waiter);

struct cat_interrupt_waiter_s {
    cat_queue_node_t node;
    cat_interrupt_wake_t wake;
    cat_bool_t interrupted;
    /* The dispatch removes a waiter before it calls the callback. The owner can
     * then remove it again, and the second call does nothing */
    cat_bool_t registered;
};

/**
 * Set the descriptor that an external interrupt source writes to. The
 * descriptor must be readable when it fires, and libcat drains it, so the
 * caller must not read from it.
 *
 * The watcher is armed for the rest of the runtime, not only while a waiter is
 * registered, so a signal that arrives between two waits is not lost. Setting a
 * different descriptor replaces the running watcher.
 *
 * Returns cat_false with CAT_EINVAL for an invalid descriptor, or with the
 * error of the libuv call when the watcher can not start.
 */
CAT_API cat_bool_t cat_interrupt_source_set(cat_os_socket_t fd);

/**
 * Stop watching the source. Every registered waiter is marked interrupted and
 * woken once, so its caller stops waiting for a source that will not fire
 * again. A call without a source changes nothing, because there is no wait that
 * a source could end.
 */
CAT_API void cat_interrupt_source_clear(void);

/*
 * A waiter is registered while its wait runs. Registration clears the
 * interrupted flag and is accepted without a source: such a waiter simply
 * never sees an interrupt. The owner zero-initialises the structure, because
 * the queue membership flag starts out clear, and it unregisters a waiter
 * before it registers that waiter again.
 *
 * The wake callback runs in the poll phase of the event loop. It must be
 * idempotent, because one waiter can be woken more than once in the same loop
 * round: the owner decides how to reschedule, and a repeated wake must not
 * reschedule twice.
 *
 * cat_coroutine_schedule() resumes a coroutine immediately, so a wake callback
 * may switch coroutines. The dispatch takes the waiters out of the queue before
 * it calls any of them, so a callback may register a waiter, unregister one, or
 * clear the source. A waiter that a callback registers belongs to a later
 * signal.
 *
 * A waiter wakes at most once for each registration. The dispatch removes it
 * before the callback runs, so cat_interrupt_waiter_unregister() does nothing
 * when the waiter already left the queue.
 */
CAT_API void cat_interrupt_waiter_register(cat_interrupt_waiter_t *waiter) CAT_INTERNAL;
CAT_API void cat_interrupt_waiter_unregister(cat_interrupt_waiter_t *waiter) CAT_INTERNAL;
CAT_API cat_bool_t cat_interrupt_waiter_is_interrupted(const cat_interrupt_waiter_t *waiter) CAT_INTERNAL;

/* Close the watcher that cat_interrupt_source_set() allocated, the runtime
 * shutdown calls it. It does not interrupt the waiters: a waiter that is still
 * registered is left for the next runtime to discard */
CAT_API void cat_interrupt_poll_close(void) CAT_INTERNAL;

#ifdef __cplusplus
}
#endif
#endif /* CAT_INTERRUPT_H */
