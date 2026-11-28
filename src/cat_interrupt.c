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

#include "cat_interrupt.h"
#include "cat_event.h"

static void cat_interrupt_poll_free_callback(uv_handle_t *handle)
{
    cat_free(handle);
}

typedef enum cat_interrupt_drain_ret_e {
    CAT_INTERRUPT_DRAINED = 0,
    CAT_INTERRUPT_DRAIN_FAILED = 1,
    /* the writer closed its end, so the descriptor stays readable and the
     * watcher has to stop instead of waking every round */
    CAT_INTERRUPT_DRAIN_EOF = 2,
} cat_interrupt_drain_ret_t;

static cat_interrupt_drain_ret_t cat_interrupt_drain(cat_os_socket_t fd)
{
    char buffer[64];

    for (;;) {
        ssize_t nread;
#ifdef CAT_OS_WIN
        /* uv_poll only accepts sockets on Windows, where a descriptor has no
         * plain read() */
        nread = recv(fd, buffer, sizeof(buffer), 0);
#else
        nread = read(fd, buffer, sizeof(buffer));
#endif
        if (nread > 0) {
            continue;
        }
        if (nread == 0) {
            return CAT_INTERRUPT_DRAIN_EOF;
        }
        if (cat_sys_errno == EINTR) {
            continue;
        }
#ifdef CAT_OS_WIN
        if (cat_sys_errno == WSAEWOULDBLOCK) {
#else
        if (cat_sys_errno == EAGAIN || cat_sys_errno == EWOULDBLOCK) {
#endif
            return CAT_INTERRUPT_DRAINED;
        }
        return CAT_INTERRUPT_DRAIN_FAILED;
    }
}

/* Keep the watcher referenced while at least one waiter depends on it, so a
 * wait with no handle of its own does not let the loop return. The reference
 * stays while such a waiter is registered, so the embedder must end those waits
 * before it shuts the runtime down */
static void cat_interrupt_poll_ref(cat_bool_t wanted)
{
    uv_poll_t *poll = CAT_EVENT_G(interrupt_poll);

    if (poll == NULL) {
        return;
    }
    if (cat_queue_empty(&CAT_EVENT_G(interrupt_waits))) {
        wanted = cat_false;
    }
    if (wanted) {
        uv_ref((uv_handle_t *) poll);
    } else {
        uv_unref((uv_handle_t *) poll);
    }
}

static void cat_interrupt_wake_waiters(void)
{
    cat_queue_t *waiters = &CAT_EVENT_G(interrupt_waits);
    cat_queue_t pending;
    cat_queue_node_t *node;

    /* A wake callback resumes a coroutine, and that coroutine can register a
     * waiter, remove one, or clear the source. Take the current set out of the
     * shared queue first, so a callback can change the queue in any way. A
     * waiter that a callback registers belongs to a later signal and stays out
     * of this walk */
    cat_queue_init(&pending);
    while ((node = cat_queue_front(waiters)) != NULL) {
        cat_queue_remove(node);
        cat_queue_push_back(&pending, node);
    }

    while ((node = cat_queue_front(&pending)) != NULL) {
        cat_interrupt_waiter_t *waiter = cat_queue_data(node, cat_interrupt_waiter_t, node);

        /* The node leaves the walk before the callback runs, so a callback that
         * removes it, or a nested walk over the shared queue, never sees it
         * twice. The owner's later unregister then does nothing */
        waiter->registered = cat_false;
        cat_queue_remove(node);

        waiter->interrupted = cat_true;
        waiter->wake(waiter);
    }
    cat_interrupt_poll_ref(cat_false);
}

/* Stop the watcher and give its handle back now. The handle must not stay
 * allocated: a closed but unclosed watcher still holds the descriptor number it
 * was initialised with, and cat_event_runtime_shutdown() or a later
 * cat_interrupt_source_set() would close it and remove that number from the
 * kernel, although another handle may use the number by then */
static void cat_interrupt_poll_retire(void)
{
    uv_poll_t *poll = CAT_EVENT_G(interrupt_poll);

    if (poll == NULL) {
        return;
    }

    CAT_EVENT_G(interrupt_poll) = NULL;
    CAT_EVENT_G(interrupt_fd) = CAT_OS_INVALID_SOCKET;
    /* closing a handle from its own callback is allowed, and the close callback
     * only frees the allocation */
    uv_close((uv_handle_t *) poll, cat_interrupt_poll_free_callback);
}

static void cat_interrupt_poll_callback(uv_poll_t *handle, int status, int events)
{
    /** @note: the watcher is stopped and the descriptor forgotten below, so
     * this callback never runs again until cat_interrupt_source_set() */
    cat_interrupt_drain_ret_t drain_ret;
    (void) events;

    if (unlikely(status != 0)) {
        CAT_WARN_WITH_REASON(EVENT, status, "Interrupt source poll failed");
        return;
    }

    drain_ret = cat_interrupt_drain(CAT_EVENT_G(interrupt_fd));
    if (unlikely(drain_ret == CAT_INTERRUPT_DRAIN_FAILED)) {
        /* The descriptor can not be drained, so its readiness would wake this
         * callback again in every following round */
        CAT_SYSCALL_FAILURE(WARNING, EVENT, "Interrupt source drain failed");
    }

    /* A failed or closed source keeps the descriptor readable forever. Retire it
     * before the waiters run, because one of them can install a fresh source.
     * Close the handle now: a stopped handle still names its descriptor, and the
     * later close would then tell the kernel to forget a number that another
     * handle may already use */
    if (unlikely(drain_ret != CAT_INTERRUPT_DRAINED)) {
        if (CAT_EVENT_G(interrupt_poll) == handle) {
            cat_interrupt_poll_retire();
        }
    }

    cat_interrupt_wake_waiters();
}

CAT_API cat_bool_t cat_interrupt_source_set(cat_os_socket_t fd)
{
    uv_poll_t *poll;
    int error;

    if (unlikely(fd == CAT_OS_INVALID_SOCKET)) {
        cat_update_last_error(CAT_EINVAL, "Invalid interrupt source descriptor");
        return cat_false;
    }

    if (CAT_EVENT_G(interrupt_poll) != NULL && CAT_EVENT_G(interrupt_fd) == fd) {
        /* The same descriptor is already watched and armed */
        return cat_true;
    }

    poll = (uv_poll_t *) cat_malloc_unrecoverable(sizeof(*poll));

    error = uv_poll_init_socket(&CAT_EVENT_G(loop), poll, fd);

    if (unlikely(error != 0)) {
        /* The working source stays, so a failed replacement does not stop the
         * signals that the caller still waits for */
        cat_free(poll);
        cat_update_last_error_with_reason(error, "Interrupt source init failed");
        return cat_false;
    }

    /* The watcher stays armed for the whole runtime, but it must not keep the
     * loop alive: the scheduler returns when nothing else is active, and the
     * runtime shutdown would otherwise block in the poll forever */
    uv_unref((uv_handle_t *) poll);

    error = uv_poll_start(poll, UV_READABLE, cat_interrupt_poll_callback);

    if (unlikely(error != 0)) {
        /* The handle is initialized now, so it has to be closed instead of
         * freed, and the close callback releases it after the loop runs */
        uv_close((uv_handle_t *) poll, cat_interrupt_poll_free_callback);
        cat_update_last_error_with_reason(error, "Interrupt source start failed");
        return cat_false;
    }

    /* The old watcher is retired only now, so a failed start keeps the working
     * source and the number it still owns */
    cat_interrupt_poll_retire();
    CAT_EVENT_G(interrupt_poll) = poll;
    CAT_EVENT_G(interrupt_fd) = fd;


    return cat_true;
}

CAT_API void cat_interrupt_source_clear(void)
{
    uv_poll_t *poll = CAT_EVENT_G(interrupt_poll);

    if (poll == NULL) {
        /* There is no source, so there is no wait that it could end. Waking a
         * waiter here would report an interrupt that never happened */
        return;
    }

    /* A watcher that was already cleared keeps its handle, so the armed state
     * is the descriptor and not the handle: clearing twice must not report an
     * interrupt that no source produced */
    if (CAT_EVENT_G(interrupt_fd) == CAT_OS_INVALID_SOCKET) {
        return;
    }

    cat_interrupt_poll_retire();

    /* A waiter that is still registered waits for a source that will not fire
     * again, so every one of them observes the interrupt and stops waiting */
    cat_interrupt_wake_waiters();
}

CAT_API void cat_interrupt_waiter_register(cat_interrupt_waiter_t *waiter)
{
    CAT_ASSERT(waiter->wake != NULL);

    waiter->interrupted = cat_false;
    waiter->registered = cat_true;
    cat_queue_push_back(&CAT_EVENT_G(interrupt_waits), &waiter->node);
    /* A wait that has no handle of its own would let the loop return at once,
     * and the scheduler would report a deadlock before the source fires */
    cat_interrupt_poll_ref(cat_true);
}

CAT_API void cat_interrupt_waiter_unregister(cat_interrupt_waiter_t *waiter)
{
    /* The dispatch may have removed the waiter already, and cat_queue_remove()
     * writes through the links of the node, so it must run exactly once */
    if (!waiter->registered) {
        return;
    }
    waiter->registered = cat_false;
    cat_queue_remove(&waiter->node);
    cat_interrupt_poll_ref(cat_false);
}

CAT_API cat_bool_t cat_interrupt_waiter_is_interrupted(const cat_interrupt_waiter_t *waiter)
{
    return waiter->interrupted;
}

CAT_API void cat_interrupt_poll_close(void)
{
    uv_poll_t *poll = CAT_EVENT_G(interrupt_poll);

    if (poll == NULL) {
        return;
    }

    CAT_EVENT_G(interrupt_poll) = NULL;
    CAT_EVENT_G(interrupt_fd) = CAT_OS_INVALID_SOCKET;

    uv_close((uv_handle_t *) poll, cat_interrupt_poll_free_callback);
}
