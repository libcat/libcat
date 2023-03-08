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

#include "cat_fs_notify.h"

static void cat_fs_notify_event_callback(uv_fs_event_t *handle, const char *filename, int events, int status)
{
    cat_fs_notify_event_t *event;
    cat_fs_notify_watch_context_t *watch =  cat_container_of(handle, cat_fs_notify_watch_context_t, handle);

    size_t filename_len = strlen(filename);
    event = (cat_fs_notify_event_t *) cat_malloc(sizeof(*event) + filename_len);

    memcpy(event->filename, filename, filename_len);
    event->filename[filename_len] = 0;
    event->ops = events;

    CAT_LOG_DEBUG(FS_NOTIFIER, "cat_fs_notify_event_callback(filename=%s, events=%d)", filename, events);
    cat_queue_push_back(&watch->events, &event->node);

    if (watch->waiting) {
        cat_coroutine_t *coroutine = watch->coroutine;
        watch->coroutine = NULL;
        watch->waiting = cat_false;
        cat_coroutine_schedule(coroutine, FS, "Fsnotify");
    }
}

static void cat_fs_notify_close_callback(uv_handle_t *handle)
{
    cat_fs_notify_watch_context_t *watch =  cat_container_of(handle, cat_fs_notify_watch_context_t, handle);
    cat_free(watch);
}

CAT_API cat_fs_notify_watch_context_t* cat_fs_notify_watch_context_init(const char *path)
{
    cat_fs_notify_watch_context_t *watch = (cat_fs_notify_watch_context_t *) cat_malloc(sizeof(*watch));
#if CAT_ALLOC_HANDLE_ERRORS
    if (unlikely(watch == NULL)) {
        cat_update_last_error_of_syscall("Malloc for fs_notify_watch_context failed");
        return NULL;
    }
#endif

    cat_queue_init(&watch->events);
    watch->path = path;

    (void) uv_fs_event_init(&CAT_EVENT_G(loop), &watch->handle);
    (void) uv_fs_event_start(&watch->handle, cat_fs_notify_event_callback, watch->path, UV_FS_EVENT_RECURSIVE);

    watch->waiting = cat_false;

    return watch;
}

CAT_API cat_fs_notify_event_t* cat_fs_notify_wait(cat_fs_notify_watch_context_t *watch)
{
    CAT_LOG_DEBUG(FS_NOTIFIER, "cat_fsnotify_wait(path=%s)", watch->path);

    cat_bool_t ret;

    if (cat_queue_empty(&watch->events)) {
        watch->coroutine = CAT_COROUTINE_G(current);
        watch->waiting = cat_true;
        ret = cat_coroutine_yield(NULL, NULL);
        if (unlikely(!ret)) {
            cat_update_last_error_with_previous("Fsnotify watch failed");
            return NULL;
        }
        if (watch->coroutine != NULL) {
            cat_update_last_error(CAT_ECANCELED, "Fsnotify has been canceled");
            return NULL;
        }
    }

    cat_fs_notify_event_t *event = cat_queue_front_data(&watch->events, cat_fs_notify_event_t, node);
    cat_queue_remove(&event->node);

    return event;
}

CAT_API void cat_fs_notify_watch_context_cleanup(cat_fs_notify_watch_context_t *watch)
{
    uv_fs_event_stop(&watch->handle);
    uv_close((uv_handle_t *) &watch->handle, cat_fs_notify_close_callback);

    cat_fs_notify_event_t *event;
    while ((event = cat_queue_front_data(&watch->events, cat_fs_notify_event_t, node))) {
        cat_queue_remove(&event->node);
        cat_free(event);
    }
}
