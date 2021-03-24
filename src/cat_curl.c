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

#include "cat_curl.h"
#include "cat_coroutine.h"
#include "cat_event.h"
#include "cat_poll.h"
#include "cat_time.h"

#ifdef CAT_CURL

typedef struct {
    CURLM *multi;
    cat_coroutine_t *coroutine;
    curl_socket_t sockfd;
    int events;
    long timeout;
} cat_curl_easy_context_t;

typedef struct {
    cat_queue_node_t node;
    CURLM *multi;
    cat_coroutine_t *coroutine;
    cat_queue_t fds;
    cat_nfds_t nfds;
    long timeout;
} cat_curl_multi_context_t;

typedef struct {
    cat_queue_node_t node;
    curl_socket_t sockfd;
    int action;
} cat_curl_pollfd_t;

CAT_GLOBALS_STRUCT_BEGIN(cat_curl)
    cat_queue_t multi_map;
CAT_GLOBALS_STRUCT_END(cat_curl)

CAT_GLOBALS_DECLARE(cat_curl)

#define CAT_CURL_G(x) CAT_GLOBALS_GET(cat_curl, x)

CAT_GLOBALS_CTOR_DECLARE_SZ(cat_curl)

static cat_always_inline void cat_curl_multi_configure(CURLM *multi, void *socket_function, void *timer_function, void *context)
{
    curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, socket_function);
    curl_multi_setopt(multi, CURLMOPT_SOCKETDATA, context);
    curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, timer_function);
    curl_multi_setopt(multi, CURLMOPT_TIMERDATA, context);
}

static cat_always_inline void cat_curl_multi_unconfigure(CURLM *multi)
{
    curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, NULL);
    curl_multi_setopt(multi, CURLMOPT_SOCKETDATA, NULL);
    curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, NULL);
    curl_multi_setopt(multi, CURLMOPT_TIMERDATA, NULL);
}

static cat_always_inline int cat_curl_translate_poll_flags_from_sys(int revents)
{
    int action = CURL_POLL_NONE;

    if (revents & POLLIN) {
        action |= CURL_CSELECT_IN;
    }
    if (revents & POLLOUT) {
        action |= CURL_CSELECT_OUT;
    }

    return action;
}

static cat_always_inline int cat_curl_translate_poll_flags_to_sys(int action)
{
    int events = POLLNONE;

    if (action != CURL_POLL_REMOVE) {
        if (action != CURL_POLL_IN) {
            events |= POLLOUT;
        }
        if (action != CURL_POLL_OUT) {
            events |= POLLIN;
        }
    }

    return events;
}

#ifdef CAT_DEBUG
static const char *cat_curl_translate_action_name(int action)
{
    switch (action) {
        case CURL_POLL_NONE:
            return "CURL_POLL_NONE";
        case CURL_POLL_IN:
            return "CURL_POLL_IN";
        case CURL_POLL_OUT:
            return "CURL_POLL_OUT";
        case CURL_POLL_INOUT:
            return "CURL_POLL_INOUT";
        case CURL_POLL_REMOVE:
            return "CURL_POLL_REMOVE";
        default:
            CAT_NEVER_HERE("Non-exist");
    }
}
#endif

static cat_always_inline long cat_curl_timeout_min(long timeout1, long timeout2)
{
    if (timeout1 < timeout2 && timeout1 >= 0) {
        return timeout1;
    }
    return timeout2;
}

static int cat_curl_easy_socket_function(CURL *ch, curl_socket_t sockfd, int action, cat_curl_easy_context_t *context, void *unused)
{
    cat_debug(EXT, "curl_easy_socket_function(multi=%p, sockfd=%d, action=%s), timeout=%ld",
        context->multi, sockfd, cat_curl_translate_action_name(action), context->timeout);

    context->sockfd = sockfd;
    context->events = cat_curl_translate_poll_flags_to_sys(action);

    return 0;
}

static int cat_curl_easy_timeout_function(CURLM *multi, long timeout, cat_curl_easy_context_t *context)
{
    cat_debug(EXT, "curl_easy_timeout_function(multi=%p, timeout=%ld)", multi, timeout);

    context->timeout = timeout;

    return 0;
}

static int cat_curl_multi_socket_function(
    CURL *ch, curl_socket_t sockfd, int action,
    cat_curl_multi_context_t *context, cat_curl_pollfd_t *fd)
{
    CURLM *multi = context->multi;

    cat_debug(EXT, "curl_multi_socket_function(multi=%p, sockfd=%d, action=%s), nfds=%lu, timeout=%ld",
        multi, sockfd, cat_curl_translate_action_name(action), context->nfds, context->timeout);

    if (action != CURL_POLL_REMOVE) {
        if (fd == NULL) {
            fd = (cat_curl_pollfd_t *) cat_malloc(sizeof(*fd));
            if (unlikely(fd == NULL)) {
                return CURLM_OUT_OF_MEMORY;
            }
            cat_queue_push_back(&context->fds, &fd->node);
            context->nfds++;
            fd->sockfd = sockfd;
            curl_multi_assign(multi, sockfd, fd);
        }
        fd->action = action;
    } else {
        cat_queue_remove(&fd->node);
        cat_free(fd);
        context->nfds--;
        curl_multi_assign(multi, sockfd, NULL);
    }

    return CURLM_OK;
}

static int cat_curl_multi_timeout_function(CURLM *multi, long timeout, cat_curl_multi_context_t *context)
{
    cat_debug(EXT, "curl_multi_timeout_function(multi=%p, timeout=%ld)", multi, timeout);

    context->timeout = timeout;

    return 0;
}

static cat_curl_multi_context_t *cat_curl_multi_context_create(CURLM *multi)
{
    cat_curl_multi_context_t *context;

    cat_debug(EXT, "curl_multi_context_create(multi=%p)", multi);

    context = (cat_curl_multi_context_t *) cat_malloc(sizeof(*context));

    if (unlikely(context == NULL)) {
        return NULL;
    }

    context->multi = multi;
    context->coroutine = NULL;
    cat_queue_init(&context->fds);
    context->nfds = 0;
    context->timeout = -1;
    /* latest multi has higher priority
     * (previous may leak and they would be free'd in shutdown) */
    cat_queue_push_front(&CAT_CURL_G(multi_map), &context->node);

    cat_curl_multi_configure(
        multi,
        cat_curl_multi_socket_function,
        cat_curl_multi_timeout_function,
        context
    );

    return context;
}

static cat_curl_multi_context_t *cat_curl_multi_context_get(CURLM *multi)
{
    size_t i = 0;

    CAT_QUEUE_FOREACH_DATA_START(&CAT_CURL_G(multi_map), cat_curl_multi_context_t, node, context) {
        if (context->multi == NULL) {
            return NULL; // eof
        }
        if (context->multi == multi) {
            return context; // hit
        }
        i++;
    } CAT_QUEUE_FOREACH_DATA_END();

    return NULL;
}

static void cat_curl_multi_context_close(CURLM *multi)
{
    cat_curl_multi_context_t *context = cat_curl_multi_context_get(multi);
    cat_curl_pollfd_t *fd;

    CAT_ASSERT(context != NULL);
    while ((fd = cat_queue_front_data(&context->fds, cat_curl_pollfd_t, node))) {
        cat_queue_remove(&fd->node);
        cat_free(fd);
        context->nfds--;
    }
    CAT_ASSERT(context->nfds == 0);
    cat_queue_remove(&context->node);
    cat_free(context);
}

CAT_API cat_bool_t cat_curl_module_init(void)
{
    CAT_GLOBALS_REGISTER(cat_curl, CAT_GLOBALS_CTOR(cat_curl), NULL);

    cat_queue_init(&CAT_CURL_G(multi_map));

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        cat_warn_with_reason(EXT, CAT_UNKNOWN, "Curl init failed");
        return cat_false;
    }

    return cat_true;
}

CAT_API cat_bool_t cat_curl_module_shutdown(void)
{
    CAT_ASSERT(cat_queue_empty(&CAT_CURL_G(multi_map)));

    curl_global_cleanup();

    return cat_true;
}

CAT_API CURLcode cat_curl_easy_perform(CURL *ch)
{
    cat_curl_easy_context_t context;
    CURLMsg *message = NULL;
    CURLMcode mcode = CURLM_INTERNAL_ERROR;
    CURLcode code = CURLE_RECV_ERROR;
    int running_handles;

    /* check if CURL is busy */
    do {
        cat_coroutine_t *coroutine = NULL;
        curl_easy_getinfo(ch, CURLINFO_PRIVATE, &coroutine);
        if (unlikely(coroutine != NULL)) {
            /* can not find appropriate error code */
            return CURLE_AGAIN;
        }
    } while (0);

    curl_easy_setopt(ch, CURLOPT_PRIVATE, CAT_COROUTINE_G(current));
    context.multi = curl_multi_init();
    if (unlikely(context.multi == NULL)) {
        return CURLM_OUT_OF_MEMORY;
    }
    context.coroutine = CAT_COROUTINE_G(current);
    context.sockfd = -1;
    context.timeout = -1;
    context.events = POLLNONE;
    cat_curl_multi_configure(
        context.multi,
        cat_curl_easy_socket_function,
        cat_curl_easy_timeout_function,
        &context
    );
    curl_multi_add_handle(context.multi, ch);

    while (1) {
        if (context.events == POLLNONE) {
            cat_ret_t ret = cat_time_delay(context.timeout);
            if (unlikely(ret != CAT_RET_OK)) {
                mcode = CURLE_RECV_ERROR;
                break;
            }
            mcode = curl_multi_socket_action(context.multi, CURL_SOCKET_TIMEOUT, 0, &running_handles);
        } else {
            int action, revents;
            cat_ret_t ret;
            ret = cat_poll_one(context.sockfd, context.events, &revents, context.timeout);
            if (unlikely(ret == CAT_RET_ERROR)) {
                mcode = CURLE_RECV_ERROR;
                break;
            }
            action = cat_curl_translate_poll_flags_from_sys(revents);
            mcode = curl_multi_socket_action(
                context.multi,
                action != 0 ? context.sockfd : CURL_SOCKET_TIMEOUT,
                action,
                &running_handles
            );
        }
        if (unlikely(mcode != CURLM_OK)) {
            break;
        }
        message = curl_multi_info_read(context.multi, &running_handles);
        if (message != NULL) {
            CAT_ASSERT(message->msg == CURLMSG_DONE);
            CAT_ASSERT(running_handles == 0);
#ifdef CAT_DEBUG
            char *done_url;
            curl_easy_getinfo(message->easy_handle, CURLINFO_EFFECTIVE_URL, &done_url);
            cat_debug(EXT, "curl_easy_perform(multi=%p, url='%s') DONE", context.multi, done_url);
#endif
            code = message->data.result;
            break;
        }
        CAT_ASSERT(running_handles == 0 || running_handles == 1);
    }

    curl_multi_remove_handle(context.multi, ch);
    //cat_curl_multi_unconfigure(context.multi);
    curl_multi_cleanup(context.multi);
    curl_easy_setopt(ch, CURLOPT_PRIVATE, NULL);

    return code;
}

CAT_API CURLM *cat_curl_multi_init(void)
{
    CURLM *multi = curl_multi_init();
    cat_curl_multi_context_t *context;

    if (unlikely(multi == NULL)) {
        return NULL;
    }

    context = cat_curl_multi_context_create(multi);
    if (unlikely(context == NULL)) {
        (void) curl_multi_cleanup(multi);
        return NULL;
    }

    return multi;
}

CAT_API CURLMcode cat_curl_multi_cleanup(CURLM *multi)
{
    CURLMcode mcode;

    mcode = curl_multi_cleanup(multi);
    /* we do not know whether libcurl would do somthing during cleanup,
     * so we close the context later */
    cat_curl_multi_context_close(multi);

    return mcode;
}

CAT_API CURLMcode cat_curl_multi_perform(CURLM *multi, int *running_handles)
{
    cat_debug(EXT, "curl_multi_perform(multi=%p)", multi);

    /* this way even can solve the problem of CPU 100% if we perform in while loop */
    return cat_curl_multi_wait(multi, NULL, 0, 0, running_handles);
}

CAT_API CURLMcode cat_curl_multi_wait(
    CURLM *multi,
    struct curl_waitfd *extra_fds, unsigned int extra_nfds,
    int timeout_ms, int *running_handles
)
{
    cat_curl_multi_context_t *context;
    cat_pollfd_t *fds = NULL;
    CURLMcode mcode = CURLM_OK;
    cat_msec_t t = cat_time_msec_cached();

    cat_debug(EXT, "curl_multi_wait(multi=%p, timeout_ms=%d)", multi, timeout_ms);

    CAT_ASSERT(extra_fds == NULL && "Not support");
    CAT_ASSERT(extra_nfds == 0 && "Not support");

    if (running_handles != NULL) {
        *running_handles = 0;
    }

    context = cat_curl_multi_context_get(multi);

    CAT_ASSERT(context != NULL);

    while (1) {
        if (context->nfds == 0) {
            cat_debug(EXT, "curl_time_delay(multi=%p, timeout=%ld)", multi, cat_curl_timeout_min(context->timeout, timeout_ms));
            cat_ret_t ret = cat_time_delay(cat_curl_timeout_min(context->timeout, timeout_ms));
            if (unlikely(ret != CAT_RET_OK)) {
                goto _out;
            }
            cat_debug(EXT, "curl_multi_socket_action(multi=%p, CURL_SOCKET_TIMEOUT) after delay", multi);
            mcode = curl_multi_socket_action(multi, CURL_SOCKET_TIMEOUT, 0, running_handles);
            if (unlikely(mcode != CURLM_OK) || *running_handles == 0) {
                goto _out;
            }
        } else {
            cat_nfds_t i;
            int ret;
            fds = (cat_pollfd_t *) cat_malloc(sizeof(*fds) * context->nfds);
            if (unlikely(fds == NULL)) {
                mcode = CURLM_OUT_OF_MEMORY;
                goto _out;
            }
            i = 0;
            CAT_QUEUE_FOREACH_DATA_START(&context->fds, cat_curl_pollfd_t, node, curl_fd) {
                cat_pollfd_t *fd = &fds[i];
                fd->fd = curl_fd->sockfd;
                fd->events = cat_curl_translate_poll_flags_to_sys(curl_fd->action);
                i++;
            } CAT_QUEUE_FOREACH_DATA_END();
            ret = cat_poll(fds, context->nfds, cat_curl_timeout_min(context->timeout, timeout_ms));
            if (unlikely(ret == CAT_RET_ERROR)) {
                goto _out;
            }
            if (ret != 0) {
                for (i = 0; i < context->nfds; i++) {
                    cat_pollfd_t *fd = &fds[i];
                    int action;
                    if (fd->revents == POLLNONE) {
                        continue;
                    }
                    action = cat_curl_translate_poll_flags_from_sys(fd->revents);
                    cat_debug(EXT, "curl_multi_socket_action(multi=%p, fd=%d, %s) after poll", multi, fd->fd, cat_curl_translate_action_name(action));
                    mcode = curl_multi_socket_action(multi, fd->fd, action, running_handles);
                    if (unlikely(mcode != CURLM_OK)) {
                        continue; // shall we handle it?
                    }
                    if (*running_handles == 0) {
                        goto _out;
                    }
                }
            } else {
                cat_debug(EXT, "curl_multi_socket_action(multi=%p, CURL_SOCKET_TIMEOUT) after poll return 0", multi);
                mcode = curl_multi_socket_action(multi, CURL_SOCKET_TIMEOUT, 0, running_handles);
                if (unlikely(mcode != CURLM_OK) || *running_handles == 0) {
                    goto _out;
                }
            }
            goto _out;
        }
        if (cat_time_msec_cached() - t > timeout_ms) {
            /* timeout */
            break;
        }
    }

    _out:
    if (fds != NULL) {
        cat_free(fds);
    }
    return mcode;
}

#endif /* CAT_CURL */
