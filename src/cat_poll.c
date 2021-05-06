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

#include "cat_poll.h"

#include "cat_coroutine.h"
#include "cat_event.h"
#include "cat_time.h"

typedef struct
{
    union {
        cat_coroutine_t *coroutine;
        uv_handle_t handle;
        uv_poll_t poll;
    } u;
    int status;
    int revents;
} cat_poll_one_t;

static cat_always_inline cat_pollfd_events_t cat_poll_translate_to_sysno(int ievents, int uv_revents)
{
    cat_pollfd_events_t revents = 0;

    if (uv_revents & UV_READABLE) {
        if (ievents & POLLIN) {
            revents |= POLLIN;
        } else {
            CAT_ASSERT(ievents & POLLERR);
            revents |= POLLERR;
        }
    }
    if (uv_revents & UV_WRITABLE) {
        revents |= POLLOUT;
    }
    if (uv_revents & UV_DISCONNECT) {
        revents |= POLLHUP;
    }
    if (uv_revents & UV_PRIORITIZED) {
        revents |= POLLPRI;
    }

    return revents;
}

static cat_always_inline int cat_poll_translate_from_sysno(int ievents)
{
    int uv_events = 0;

    if ((ievents & (POLLIN | POLLERR))) {
        uv_events |= UV_READABLE;
    }
    if (ievents & POLLOUT) {
        uv_events |= UV_WRITABLE;
    }
    if (ievents & POLLHUP) {
        uv_events |= UV_DISCONNECT;
    }
    if (ievents & POLLPRI) {
        uv_events |= UV_PRIORITIZED;
    }

    return uv_events;
}

static void cat_poll_one_callback(uv_poll_t* handle, int status, int events)
{
    cat_poll_one_t *poll = (cat_poll_one_t *) handle;

    poll->status = status;
    poll->revents = events;

    cat_coroutine_schedule(poll->u.coroutine, EVENT, "Poll one");
}

CAT_API cat_ret_t cat_poll_one(cat_os_socket_t fd, int events, int *revents, cat_timeout_t timeout)
{
    cat_poll_one_t *poll;
    cat_ret_t ret;
    int error, _revents;

    cat_debug(EVENT, "poll_one(fd=" CAT_OS_SOCKET_FMT ", events=%d, timeout=" CAT_TIMEOUT_FMT ")", fd, events, timeout);

    if (revents == NULL) {
        revents = &_revents;
    }
    *revents = 0;

    poll = (cat_poll_one_t *) cat_malloc(sizeof(*poll));;
    if (unlikely(poll == NULL)) {
        cat_update_last_error_of_syscall("Malloc for poll failed");
        return CAT_RET_ERROR;
    }

    error = uv_poll_init_socket(cat_event_loop, &poll->u.poll, fd);
    if (unlikely(error != 0)) {
        cat_update_last_error_with_reason(error, "Poll init failed");
        cat_free(poll);
        return CAT_RET_ERROR;
    }
    error = uv_poll_start(&poll->u.poll, cat_poll_translate_from_sysno(events), cat_poll_one_callback);
    if (unlikely(error != 0)) {
        cat_update_last_error_with_reason(error, "Poll start failed");
        uv_close(&poll->u.handle, (uv_close_cb) cat_free_function);
        return CAT_RET_ERROR;
    }
    poll->status = CAT_ECANCELED;
    poll->revents = 0;
    poll->u.coroutine = CAT_COROUTINE_G(current);

    ret = cat_time_delay(timeout);

    uv_close(&poll->u.handle, (uv_close_cb) cat_free_function);

    switch (ret) {
        /* cancelled */
        case CAT_RET_NONE: {
            if (unlikely(poll->status < 0)) {
                if (poll->status == CAT_ECANCELED) {
                    cat_update_last_error(CAT_ECANCELED, "Poll has been canceled");
                } else {
                    cat_update_last_error(poll->status, "Poll failed");
                }
                ret = CAT_RET_ERROR;
            }
            ret = CAT_RET_OK;
            *revents = cat_poll_translate_to_sysno(events, poll->revents);
            break;
        }
        /* timedout */
        case CAT_RET_OK:
            ret = CAT_RET_NONE;
            break;
        /* error */
        case CAT_RET_ERROR:
            cat_update_last_error_with_previous("Poll wait failed");
            break;
        default:
            CAT_NEVER_HERE("Impossible");
            break;
    }
    cat_debug(EVENT, "poll_one(fd=" CAT_OS_SOCKET_FMT ", *revents=%u) = %d", fd, *revents, ret);

    return ret;
}

typedef struct {
    cat_coroutine_t *coroutine;
    cat_bool_t deferred;
} cat_poll_context_t;

typedef struct {
    union {
        cat_poll_context_t *context;
        uv_handle_t handle;
        uv_poll_t poll;
    } u;
    int status;
    int revents;
    cat_bool_t is_head;
} cat_poll_t;

/* double defer here to avoid use-after-free */

static void cat_poll_free_function(void *data)
{
    cat_event_defer(cat_free_function, data);
}

static void cat_poll_close_function(uv_handle_t *handle)
{
    cat_poll_t *poll = (cat_poll_t *) handle;

    if (poll->is_head) {
        cat_event_defer(cat_poll_free_function, poll);
    }
}

static void cat_poll_done_callback(cat_data_t *data)
{
    cat_poll_t *poll = (cat_poll_t *) data;
    cat_poll_context_t *context = poll->u.context;

    if (unlikely(context == NULL)) {
        // cancalled
        return;
    }
    cat_coroutine_schedule(context->coroutine, EVENT, "Poll");
}

static void cat_poll_callback(uv_poll_t* handle, int status, int revents)
{
    cat_poll_t *poll = (cat_poll_t *) handle;
    cat_poll_context_t *context = poll->u.context;

    poll->status = status;
    poll->revents = revents;

    if (!context->deferred) {
        if (!cat_event_defer_ex(cat_poll_done_callback, poll, cat_true)) {
            cat_warn_with_last(EVENT, "Poll defer failed");
            return;
        }
        context->deferred = cat_true;
    }
}

CAT_API int cat_poll(cat_pollfd_t *fds, cat_nfds_t nfds, cat_timeout_t timeout)
{
    cat_poll_context_t context;
    cat_poll_t *polls;
    cat_nfds_t i, n = 0;
    cat_ret_t ret;
    int error;

    cat_debug(EVENT, "poll(fds=%p, nfds=%zu, timeout=" CAT_TIMEOUT_FMT ")", fds, (size_t) nfds, timeout);

    polls = (cat_poll_t *) cat_malloc(sizeof(*polls) * nfds);;
    if (unlikely(polls == NULL)) {
        cat_update_last_error_of_syscall("Malloc for polls failed");
        return CAT_RET_ERROR;
    }

    context.coroutine = CAT_COROUTINE_G(current);
    context.deferred = cat_false;
    for (i = 0; i < nfds;) {
        cat_pollfd_t *fd = &fds[i];
        cat_poll_t *poll = &polls[i];
        CAT_ASSERT(fd->fd != CAT_OS_INVALID_SOCKET); // TODO: shall we support negative fd?
        fd->revents = 0; // clear it
        cat_debug(EVENT, "poll_add(fd=" CAT_OS_SOCKET_FMT ", event=%d)", fd->fd, fd->events);
        error = uv_poll_init_socket(cat_event_loop, &poll->u.poll, fd->fd);
        if (unlikely(error != 0)) {
            cat_update_last_error_with_reason(error, "Poll init failed");
            goto _error;
        }
        i++;
        error = uv_poll_start(&poll->u.poll, cat_poll_translate_from_sysno(fd->events), cat_poll_callback);
        if (unlikely(error != 0)) {
            cat_update_last_error_with_reason(error, "Poll start failed");
            goto _error;
        }
        poll->status = CAT_ECANCELED;
        poll->revents = 0;
        poll->is_head = cat_false;
        poll->u.context = &context;
    }
    polls[0].is_head = cat_true;

    ret = cat_time_delay(timeout);

    if (unlikely(ret == CAT_RET_ERROR)) {
        cat_update_last_error_with_previous("Poll wait failed");
        goto _error;
    }

    for (; i-- > 0;) {
        cat_pollfd_t *fd = &fds[i];
        cat_poll_t *poll = &polls[i];
        poll->u.context = NULL; // let defer know it has been cancalled
        uv_close(&poll->u.handle, cat_poll_close_function);
        if (unlikely(ret == CAT_RET_NONE && poll->status < 0)) {
            if (poll->status == CAT_ECANCELED) {
                fd->revents = 0;
            } else {
                fd->revents = POLLERR;
                n++;
            }
        } else {
            fd->revents = cat_poll_translate_to_sysno(fd->events, poll->revents);
            if (fd->revents != POLLNONE) {
                n++;
            }
        }
        cat_debug(EVENT, "poll_del(fd=" CAT_OS_SOCKET_FMT ") = %d", fd->fd, fd->revents);
    }

    cat_debug(EVENT, "poll(fds=%p, nfds=%zu, timeout=" CAT_TIMEOUT_FMT ") = %zu", fds, (size_t) nfds, timeout, (size_t) n);

    return n;

    _error:
    cat_debug(EVENT, "poll(fds=%p, nfds=%zu, timeout=" CAT_TIMEOUT_FMT ") failed", fds, (size_t) nfds, timeout);
    for (; i > 0; i--) {
        cat_poll_t *poll = &polls[i];
        CAT_ASSERT(!context.deferred);
        uv_close(&poll->u.handle, cat_poll_close_function);
    }
    return CAT_RET_ERROR;
}
