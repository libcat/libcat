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

#include "cat_fs.h"
#include "cat_coroutine.h"
#include "cat_event.h"
#include "cat_time.h"

#include <fcntl.h>

typedef union
{
    cat_coroutine_t *coroutine;
    uv_req_t req;
    uv_fs_t fs;
} cat_fs_context_t;

#define CAT_FS_CREATE_CONTEXT(on_fail, context) \
    cat_fs_context_t *context = (cat_fs_context_t *) cat_malloc(sizeof(*context)); do { \
    if (unlikely(context == NULL)) { \
        cat_update_last_error_of_syscall("Malloc for file-system context failed"); \
        {on_fail} \
    } \
} while (0)

#define CAT_FS_CALL(on_fail, operation, ...) \
    CAT_FS_CREATE_CONTEXT(on_fail, context); \
    cat_bool_t done; \
    cat_bool_t ret; \
    int error = uv_fs_##operation(cat_event_loop, &context->fs, ##__VA_ARGS__, cat_fs_callback);

#define CAT_FS_HANDLE_ERROR(on_fail, operation) do { \
    if (error != 0) { \
        cat_update_last_error_with_reason(error, "File-System " #operation " init failed"); \
        cat_free(context); \
        {on_fail} \
    } \
    context->coroutine = CAT_COROUTINE_G(current); \
    ret = cat_time_wait(-1); \
    done = context->coroutine == NULL; \
    context->coroutine = NULL; \
    if (unlikely(!ret)) { \
        cat_update_last_error_with_previous("File-System " #operation " wait failed"); \
        (void) uv_cancel(&context->req); \
        {on_fail} \
    } \
    if (unlikely(!done)) { \
        cat_update_last_error(CAT_ECANCELED, "File-System " #operation " has been canceled"); \
        (void) uv_cancel(&context->req); \
        {on_fail} \
    } \
} while (0)

#define CAT_FS_HANDLE_RESULT(on_fail, on_done, operation) \
    CAT_FS_HANDLE_ERROR(on_fail, operation); \
    if (unlikely(context->fs.result < 0)) { \
        cat_update_last_error_with_reason((cat_errno_t) context->fs.result, "File-System " #operation " failed"); \
        {on_fail} \
    } \
    {on_done}

#define CAT_FS_DO_RESULT_EX(on_fail, on_done, operation, ...) do { \
        CAT_FS_CALL({on_fail}, operation, ##__VA_ARGS__) \
        CAT_FS_HANDLE_RESULT(on_fail, on_done, operation) \
} while (0)

#define CAT_FS_DO_RESULT(return_type, operation, ...) CAT_FS_DO_RESULT_EX({return -1;}, {return (return_type)context->fs.result;}, operation, __VA_ARGS__)

static void cat_fs_callback(uv_fs_t *fs)
{
    cat_fs_context_t *context = cat_container_of(fs, cat_fs_context_t, fs);

    if (context->coroutine != NULL) {
        cat_coroutine_t *coroutine = context->coroutine;
        context->coroutine = NULL;
        if (unlikely(!cat_coroutine_resume(coroutine, NULL, NULL))) {
            cat_core_error_with_last(FS, "File-System schedule failed");
        }
    }

    uv_fs_req_cleanup(&context->fs);
    cat_free(context);
}

CAT_API int cat_fs_open(const char *path, int flags, ...)
{
    va_list args;
    int mode = 0666;

    if (flags & O_CREAT) {
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }

    CAT_FS_DO_RESULT(int, open, path, flags, mode);
}

CAT_API off_t cat_fs_lseek(int fd, off_t offset, int whence)
{
    return lseek(fd, offset, whence);
}

CAT_API ssize_t cat_fs_read(int fd, void *buffer, size_t size)
{
    uv_buf_t buf = uv_buf_init((char *) buffer, (unsigned int) size);

    CAT_FS_DO_RESULT(ssize_t, read, fd, &buf, 1, 0);
}

CAT_API ssize_t cat_fs_write(int fd, const void *buffer, size_t length)
{
    uv_buf_t buf = uv_buf_init((char *) buffer, (unsigned int) length);

    CAT_FS_DO_RESULT(ssize_t, write, fd, &buf, 1, 0);
}

CAT_API int cat_fs_close(int fd)
{
    CAT_FS_DO_RESULT(int, close, fd);
}

CAT_API int cat_fs_access(const char *path, int mode)
{
    CAT_FS_DO_RESULT(int, access, path, mode);
}

CAT_API int cat_fs_mkdir(const char *path, int mode)
{
    CAT_FS_DO_RESULT(int, mkdir, path, mode);
}

CAT_API int cat_fs_rmdir(const char *path)
{
    CAT_FS_DO_RESULT(int, rmdir, path);
}

CAT_API int cat_fs_rename(const char *path, const char *new_path)
{
    CAT_FS_DO_RESULT(int, rename, path, new_path);
}

CAT_API int cat_fs_unlink(const char *path)
{
    CAT_FS_DO_RESULT(int, unlink, path);
}

#define CAT_FS_DO_STAT(name, target) \
    CAT_FS_DO_RESULT_EX({return -1;}, {memcpy(buf, &context->fs.statbuf, sizeof(uv_stat_t)); return 0;}, name, target)

CAT_API int cat_fs_stat(const char * path, cat_stat_t * buf){
    CAT_FS_DO_STAT(stat, path);
}

CAT_API int cat_fs_lstat(const char * path, cat_stat_t * buf){
    CAT_FS_DO_STAT(lstat, path);
}

CAT_API int cat_fs_fstat(int fd, cat_stat_t * buf){
    CAT_FS_DO_STAT(fstat, fd);
}

CAT_API int cat_fs_link(const char * path, const char * new_path){
    CAT_FS_DO_RESULT(int, link, path, new_path);
}

CAT_API int cat_fs_symlink(const char * path, const char * new_path, int flags){
    CAT_FS_DO_RESULT(int, symlink, path, new_path, flags);
}

#ifdef CAT_OS_WIN
#   define PATH_MAX 32768
#endif
CAT_API ssize_t cat_fs_readlink(const char * pathname, char * buf, size_t len){
    CAT_FS_DO_RESULT_EX({return -1;}, {
        int ret = strnlen(context->fs.ptr, PATH_MAX);
        if(ret > len){
            // will truncate
            ret = len;
        }
        strncpy(buf, context->fs.ptr, len);
        return ret;
    }, readlink, pathname);
}

CAT_API char * cat_fs_realpath(const char *pathname, char* buf){
    CAT_FS_DO_RESULT_EX({return NULL;}, {
        if(NULL == buf){
            return strdup(context->fs.ptr);
        }
        strcpy(buf, context->fs.ptr);
        return buf;
    }, realpath, pathname);
}
#ifdef CAT_OS_WIN
#   undef PATH_MAX
#endif