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

#include "cat_fsnotify.h"

static void cat_fsnotify_event_callback(uv_fs_event_t *handle, const char *filename, int events, int status)
{
    cat_fs_notify_watch_context_t *watch =  cat_container_of(handle, cat_fs_notify_watch_context_t, handle);

    watch->event.filename = filename;
    if (events & UV_RENAME) {
        watch->event.event = CAT_FS_NOTIFY_EVENT_KIND_RENAME;
    } else if (events & UV_CHANGE) {
        watch->event.event = CAT_FS_NOTIFY_EVENT_KIND_CHANGE;
    } else {
        watch->event.event = CAT_FS_NOTIFY_EVENT_KIND_UNKNOWN;
    }

    cat_coroutine_t *coroutine = watch->coroutine;
    watch->coroutine = NULL;
    cat_coroutine_schedule(coroutine, FS, "Fsnotify");
}

static void cat_fsnotify_close_callback(uv_handle_t *handle)
{
    cat_fs_notify_watch_context_t *watch =  cat_container_of(handle, cat_fs_notify_watch_context_t, handle);

    cat_free((cat_fs_notifier_t *) watch);
}

void cat_fsnotify_watch_context_init(cat_fs_notify_watch_context_t *watch, const char *path)
{
    watch->path = path;
    watch->event.filename = NULL;
}

CAT_API cat_bool_t cat_fsnotify_wait(const char *path, cat_fs_notify_event_t *event)
{
    CAT_LOG_DEBUG(FS_NOTIFIER, "cat_fsnotify_wait(path=%s)", path);

    cat_bool_t ret;
    cat_fs_notifier_t *fsnotifier = (cat_fs_notifier_t *) cat_malloc(sizeof(*fsnotifier));
#if CAT_ALLOC_HANDLE_ERRORS
    if (unlikely(fsnotifier == NULL)) {
        cat_update_last_error_of_syscall("Malloc for fsnotifier failed");
        return NULL;
    }
#endif

    cat_fs_notify_watch_context_t *watch = &fsnotifier->watch;
    cat_fsnotify_watch_context_init(watch, path);

    (void) uv_fs_event_init(&CAT_EVENT_G(loop), &watch->handle);

    watch->coroutine = CAT_COROUTINE_G(current);
    (void) uv_fs_event_start(&watch->handle, cat_fsnotify_event_callback, watch->path, UV_FS_EVENT_RECURSIVE);
    ret = cat_coroutine_yield(NULL, NULL);
    if (unlikely(!ret)) {
        cat_update_last_error_with_previous("Fsnotify watch failed");
        return cat_false;
    }
    if (watch->event.filename != NULL) {
        event->filename = watch->event.filename;
        event->event = watch->event.event;
    }
    uv_fs_event_stop(&watch->handle);
    if (watch->coroutine != NULL) {
        cat_update_last_error(CAT_ECANCELED, "Fsnotify has been canceled");
        return cat_false;
    }

    uv_close((uv_handle_t *) &fsnotifier->watch.handle, cat_fsnotify_close_callback);

    CAT_LOG_DEBUG(FS_NOTIFIER, "cat_fsnotify_wait result(filename=%s, event=%d)", watch->event.filename, watch->event.event);

    return cat_true;
}
