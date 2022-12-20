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

#ifndef CAT_FSNOTIFY_H
#define CAT_FSNOTIFY_H
#ifdef __cplusplus
extern "C" {
#endif

#include "cat.h"
#include "cat_coroutine.h"
#include "cat_event.h"

typedef enum cat_fs_notify_event_kind_e {
    CAT_FS_NOTIFY_EVENT_KIND_UNKNOWN,
    CAT_FS_NOTIFY_EVENT_KIND_RENAME = UV_RENAME,
    CAT_FS_NOTIFY_EVENT_KIND_CHANGE = UV_CHANGE,
} cat_fs_notify_event_kind_t;

typedef struct {
    const char *filename;
    cat_fs_notify_event_kind_t event;
} cat_fs_notify_event_t;

typedef struct cat_fs_notify_watch_context_s {
    uv_fs_event_t handle;
    cat_coroutine_t *coroutine;
    const char *path;
    cat_fs_notify_event_t event;
} cat_fs_notify_watch_context_t;

typedef struct {
    cat_fs_notify_watch_context_t watch;
    int error;
} cat_fs_notify_t;

CAT_API cat_bool_t cat_fs_notify_wait(const char *path, cat_fs_notify_event_t *event);

#ifdef __cplusplus
}
#endif
#endif /* CAT_FSNOTIFY_H */
