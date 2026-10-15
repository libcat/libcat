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

#include "cat.h"

#include "cat_thread.h"

#ifdef CAT_THREAD_SAFE

CAT_GLOBALS_DECLARE(cat_thread);

/*
 * The root and the two counters belong to the process, because every thread
 * reads them. Only the object of the running thread is thread local, and it
 * lives in the globals block above.
 *
 * The root is written once and read afterwards, so it needs no lock. The
 * counters take writes from the registering threads, so they are atomic.
 */
static cat_thread_t *cat_thread_main;
static cat_atomic_uint32_t cat_thread_count;
static cat_atomic_uint32_t cat_thread_external_count;

CAT_API cat_bool_t cat_thread_module_init(void)
{
    CAT_GLOBALS_REGISTER(cat_thread);

    return cat_true;
}

CAT_API cat_bool_t cat_thread_module_shutdown(void)
{
    CAT_GLOBALS_UNREGISTER(cat_thread);

    return cat_true;
}

CAT_API cat_bool_t cat_thread_runtime_init(void)
{
    /* nothing of this thread is known until it registers an object, so a caller
     * that reads the current thread before that gets a null */
    return cat_true;
}

CAT_API cat_bool_t cat_thread_runtime_shutdown(void)
{
    cat_thread_t *thread = cat_thread_get_current_safe();

    if (thread != NULL) {
        /* an external thread stands in both counts, because the root is one of
         * them and it takes the same path */
        if (thread->state & CAT_THREAD_STATE_EXTERNAL) {
            cat_atomic_uint32_fetch_sub(&cat_thread_external_count, 1);
            cat_atomic_uint32_fetch_sub(&cat_thread_count, 1);
        } else {
            cat_atomic_uint32_fetch_sub(&cat_thread_count, 1);
        }
        (void) cat_thread_set_current(NULL);
    }

    return cat_true;
}

/* the checks both public entries share, so a rejected call leaves the object of
 * the caller untouched */
static cat_bool_t cat_thread_register_check(const cat_thread_t *thread)
{
    if (thread == NULL) {
        cat_update_last_error(CAT_EINVAL, "Thread object is null");
        return cat_false;
    }

    if (!cat_globals_available()) {
        /* this thread never took a runtime, so it has nowhere to keep the
         * object of the running thread */
        cat_update_last_error(CAT_EMISUSE, "This thread has no runtime");
        return cat_false;
    }

    if (CAT_THREAD_G(current) != NULL && CAT_THREAD_G(current) != thread) {
        /* one thread holds one object, and a second one would leave the first
         * one counted and marked as running */
        cat_update_last_error(CAT_EMISUSE, "This thread has already registered an object");
        return cat_false;
    }

    return cat_true;
}

/* the one write path of the thread state, which both public entries use */
CAT_API cat_bool_t cat_thread_register(cat_thread_t *thread)
{
    if (!cat_thread_register_check(thread)) {
        return cat_false;
    }

    if (CAT_THREAD_G(current) == thread) {
        return cat_true;
    }

    thread->id = uv_thread_self();
    thread->state |= CAT_THREAD_STATE_JOINABLE;

    return cat_thread_set_current(thread);
}

CAT_API cat_bool_t cat_thread_register_main(cat_thread_t *thread)
{
    if (thread == NULL) {
        cat_update_last_error(CAT_EINVAL, "Thread object is null");
        return cat_false;
    }

    if (cat_thread_main != NULL) {
        /* the root is settled, so this call either repeats it or breaks it; a
         * thread id tells the two apart, because another thread may hold this
         * pointer */
        cat_thread_id_t current_id = uv_thread_self();
        if (cat_thread_main != thread || !uv_thread_equal(&thread->id, &current_id)) {
            cat_update_last_error(CAT_EMISUSE, "The main thread is already registered");
            return cat_false;
        }
        return cat_true;
    }

    if (!cat_thread_register_check(thread)) {
        return cat_false;
    }

    memset(thread, 0, sizeof(*thread));
    thread->state = CAT_THREAD_STATE_EXTERNAL | CAT_THREAD_STATE_JOINABLE;
    thread->parent = NULL;
    thread->id = uv_thread_self();
    if (!cat_thread_set_current(thread)) {
        return cat_false;
    }
    cat_thread_main = thread;
    cat_atomic_uint32_fetch_add(&cat_thread_count, 1);
    cat_atomic_uint32_fetch_add(&cat_thread_external_count, 1);

    return cat_true;
}

CAT_API cat_bool_t cat_thread_register_external(cat_thread_t *thread)
{
    if (thread == NULL) {
        cat_update_last_error(CAT_EINVAL, "Thread object is null");
        return cat_false;
    }

    if (cat_thread_main == NULL) {
        /* without the root there is nothing to attach an outside thread to */
        cat_update_last_error(CAT_EMISUSE, "The main thread is not registered yet");
        return cat_false;
    }

    if (cat_thread_get_current_safe() == thread) {
        return cat_true;
    }

    if (!cat_thread_register_check(thread)) {
        return cat_false;
    }

    memset(thread, 0, sizeof(*thread));
    thread->state = CAT_THREAD_STATE_EXTERNAL | CAT_THREAD_STATE_JOINABLE;
    thread->parent = cat_thread_main;
    thread->id = uv_thread_self();
    if (!cat_thread_set_current(thread)) {
        return cat_false;
    }
    /* an external thread stands in both counts, because the root is one of them
     * and it takes the same path */
    cat_atomic_uint32_fetch_add(&cat_thread_count, 1);
    cat_atomic_uint32_fetch_add(&cat_thread_external_count, 1);

    return cat_true;
}

CAT_API cat_thread_t *cat_thread_get_current(void)
{
    cat_thread_t *thread = cat_thread_get_current_safe();

    if (unlikely(thread == NULL)) {
        cat_update_last_error(CAT_EMISUSE, "Thread is not registered");
    }

    return thread;
}

CAT_API cat_bool_t cat_thread_equal(const cat_thread_t *a, const cat_thread_t *b)
{
    if (a == NULL || b == NULL) {
        return cat_false;
    }

    return !!uv_thread_equal(&a->id, &b->id);
}

CAT_API cat_bool_t cat_thread_is_main(const cat_thread_t *thread)
{
    return thread != NULL && thread == cat_thread_main;
}

CAT_API cat_bool_t cat_thread_is_running(const cat_thread_t *thread)
{
    return thread != NULL && !(thread->state & CAT_THREAD_STATE_EXITED);
}

CAT_API cat_thread_t *cat_thread_get_parent(const cat_thread_t *thread)
{
    return thread != NULL ? thread->parent : NULL;
}

CAT_API cat_thread_id_t cat_thread_get_id(const cat_thread_t *thread)
{
    if (thread == NULL) {
        cat_update_last_error(CAT_EINVAL, "Thread object is null");
        return (cat_thread_id_t) 0;
    }

    return thread->id;
}

CAT_API cat_thread_id_t cat_thread_get_current_id(void)
{
    return uv_thread_self();
}

CAT_API uint32_t cat_thread_get_count(void)
{
    return cat_atomic_uint32_load(&cat_thread_count);
}

CAT_API uint32_t cat_thread_get_external_count(void)
{
    return cat_atomic_uint32_load(&cat_thread_external_count);
}

CAT_API void *cat_thread_get_return_value(const cat_thread_t *thread)
{
    return thread != NULL ? thread->return_value : NULL;
}

CAT_API cat_bool_t cat_thread_set_name(const char *name)
{
    cat_thread_t *thread = cat_thread_get_current();
    if (thread == NULL) {
        return cat_false;
    }

    /* the platform sets the name of the running thread only, and uv keeps the
     * per-platform spelling of the length limit */
    const char *wanted = name != NULL ? name : "";
    size_t length = strlen(wanted);
    if (length > CAT_THREAD_NAME_MAX) {
        cat_update_last_error(CAT_EINVAL, "Thread name is longer than %d bytes", CAT_THREAD_NAME_MAX);
        return cat_false;
    }

    /* an empty name drops the name of this thread: the platform may refuse to
     * take it back, and the object drops it either way */
    if (length == 0) {
        thread->name[0] = '\0';
        (void) uv_thread_setname("");

        return cat_true;
    }

    /* the platform is asked first, so a name it refuses never reaches the
     * object and the two stay in step */
    int error = uv_thread_setname(wanted);
    if (error != 0) {
        cat_update_last_error(CAT_EINVAL, "Thread name is not accepted by the platform, reason: %s",
            uv_strerror(error));
        return cat_false;
    }

    memcpy(thread->name, wanted, length + 1);

    return cat_true;
}

#endif /* CAT_THREAD_SAFE */
