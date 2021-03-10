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
#include "cat_event.h"

#ifdef CAT_CURL

CAT_GLOBALS_STRUCT_BEGIN(cat_curl)
    CURLM *multi;
    uv_timer_t timer;
CAT_GLOBALS_STRUCT_END(cat_curl)

CAT_API CAT_GLOBALS_DECLARE(cat_curl)

#define CAT_CURL_G(x) CAT_GLOBALS_GET(cat_curl, x)

CAT_API CAT_GLOBALS_DECLARE(cat_curl)

CAT_GLOBALS_CTOR_DECLARE_SZ(cat_curl)

typedef struct cat_curl_context_s {
    uv_poll_t poll_handle;
    curl_socket_t sockfd;
} cat_curl_context_t;

static void cat_curl_check_multi_info(void)
{
    CURLMsg *message;
    int pending;

    while ((message = curl_multi_info_read(CAT_CURL_G(multi), &pending))) {
        switch (message->msg) {
            case CURLMSG_DONE: {
                char *done_url;
#ifdef CAT_DEBUG
                curl_easy_getinfo(message->easy_handle, CURLINFO_EFFECTIVE_URL, &done_url);
                cat_debug(EXT, "Curl query '%s' DONE", done_url);
#endif
                curl_multi_remove_handle(CAT_CURL_G(multi), message->easy_handle);
                // curl_easy_cleanup(message->easy_handle);
                cat_coroutine_t *coroutine;
                curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &coroutine);
                cat_coroutine_resume(coroutine, (cat_data_t *) &message->data.result, NULL);
                break;
            }
            default:
                CAT_NEVER_HERE("Impossible");
        }
    }
}

static void cat_curl_poll_callback(uv_poll_t *request, int status, int events)
{
    cat_curl_context_t *context;
    int flags = 0, running_handles;

    if (events & UV_READABLE) {
        flags |= CURL_CSELECT_IN;
    }
    if (events & UV_WRITABLE) {
        flags |= CURL_CSELECT_OUT;
    }
    context = (cat_curl_context_t *) request->data;
    curl_multi_socket_action(CAT_CURL_G(multi), context->sockfd, flags, &running_handles);
    cat_curl_check_multi_info();
}

static int cat_curl_socket_function(CURL *easy, curl_socket_t sockfd, int action, void *userp, void *socketp)
{
    cat_curl_context_t *context = (cat_curl_context_t *) socketp;
    int events = 0;

    switch(action) {
        case CURL_POLL_IN:
        case CURL_POLL_OUT:
        case CURL_POLL_INOUT:
            if (unlikely(context == NULL)) {
                context = (cat_curl_context_t *) cat_malloc(sizeof(*context));
                context->sockfd = sockfd;
                uv_poll_init_socket(cat_event_loop, &context->poll_handle, sockfd);
                context->poll_handle.data = context;
            }
            curl_multi_assign(CAT_CURL_G(multi), sockfd, (void *) context);
            if (action != CURL_POLL_IN) {
                events |= UV_WRITABLE;
            }
            if (action != CURL_POLL_OUT) {
                events |= UV_READABLE;
            }
            uv_poll_start(&context->poll_handle, events, cat_curl_poll_callback);
            break;
        case CURL_POLL_REMOVE:
            if (socketp) {
                uv_poll_stop(&(context)->poll_handle);
                uv_close((uv_handle_t *) &context->poll_handle, (uv_close_cb) cat_free_function);
                curl_multi_assign(CAT_CURL_G(multi), sockfd, NULL);
            }
            break;
        default:
            CAT_NEVER_HERE("Impossible");
    }

    return 0;
}

static void cat_curl_on_timeout(uv_timer_t *req)
{
    int running_handles;
    curl_multi_socket_action(CAT_CURL_G(multi), CURL_SOCKET_TIMEOUT, 0, &running_handles);
    cat_curl_check_multi_info();
}

static int cat_curl_start_timeout(CURLM *multi, long timeout, void *userp)
{
    if (timeout < 0) {
        uv_timer_stop(&CAT_CURL_G(timer));
    } else {
        if (timeout == 0) {
            timeout = 1; /* 0 means directly call socket_action, but we'll do it in a bit */
        }
        uv_timer_start(&CAT_CURL_G(timer), cat_curl_on_timeout, timeout, 0);
    }

    return 0;
}

CAT_API cat_bool_t cat_curl_module_init(void)
{
    CAT_GLOBALS_REGISTER(cat_curl, CAT_GLOBALS_CTOR(cat_curl), NULL);

    if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
        cat_warn_with_reason(EXT, CAT_UNKNOWN, "Curl init failed");
        return cat_false;
    }

    return cat_true;
}

CAT_API cat_bool_t cat_curl_runtime_init(void)
{
    CAT_CURL_G(multi) = curl_multi_init();
    curl_multi_setopt(CAT_CURL_G(multi), CURLMOPT_SOCKETFUNCTION, cat_curl_socket_function);
    curl_multi_setopt(CAT_CURL_G(multi), CURLMOPT_TIMERFUNCTION, cat_curl_start_timeout);

    int error = uv_timer_init(cat_event_loop, &CAT_CURL_G(timer));
    if (error != 0) {
        cat_warn_with_reason(EXT, error, "Curl Timer init failed");
        return cat_false;
    }

    return cat_true;
}

CAT_API cat_bool_t cat_curl_runtime_shutdown(void)
{
    uv_close((uv_handle_t *) &CAT_CURL_G(timer), NULL);
    curl_multi_cleanup(CAT_CURL_G(multi));

    return cat_true;
}

CAT_API CURLcode cat_curl_easy_perform(CURL *ch)
{
    CURLcode *ret;

    curl_multi_add_handle(CAT_CURL_G(multi), ch);
    curl_easy_setopt(ch, CURLOPT_PRIVATE, CAT_COROUTINE_G(current));
    cat_coroutine_yield(NULL, (cat_data_t **) &ret);

    return *ret;
}

#endif /* CAT_CURL */
