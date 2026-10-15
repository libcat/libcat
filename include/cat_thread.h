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

#ifndef CAT_THREAD_H
#define CAT_THREAD_H
#ifdef __cplusplus
extern "C" {
#endif

#include "cat.h"
#include "cat_atomic.h"

/*
 * The thread model needs a per-thread place to keep the object of the running
 * thread, so a build without a thread local store has none of these symbols.
 */
#ifdef CAT_THREAD_SAFE

typedef uv_thread_t cat_thread_id_t;

#define CAT_THREAD_NAME_MAX 15

typedef struct cat_thread_s cat_thread_t;

/*
 * The registry alone needs the identity, the parent link, the state and the
 * name. Creation adds the routine, its data and the return value, and join adds
 * the rest.
 *
 * Fields are grouped by size, so the compiler adds little padding.
 */
struct cat_thread_s {
    /* 8 byte */
    cat_thread_id_t id;
    cat_data_t *return_value;
    cat_thread_t *parent;
    /* 4 byte */
    uint32_t state;
    /* at most CAT_THREAD_NAME_MAX bytes plus the terminator, kept last */
    char name[CAT_THREAD_NAME_MAX + 1];
};

#define CAT_THREAD_STATE_JOINABLE    (1 << 0)
#define CAT_THREAD_STATE_JOINED      (1 << 1)
#define CAT_THREAD_STATE_EXITED      (1 << 2)
/* the object of a thread that libcat did not create */
#define CAT_THREAD_STATE_EXTERNAL    (1 << 3)

CAT_API cat_bool_t cat_thread_module_init(void);
CAT_API cat_bool_t cat_thread_module_shutdown(void);
CAT_API cat_bool_t cat_thread_runtime_init(void);
CAT_API cat_bool_t cat_thread_runtime_shutdown(void);

CAT_API cat_bool_t cat_thread_register_main(cat_thread_t *thread);
CAT_API cat_bool_t cat_thread_register_external(cat_thread_t *thread);
CAT_API cat_bool_t cat_thread_register(cat_thread_t *thread); CAT_INTERNAL

CAT_API cat_thread_t   *cat_thread_get_current(void);
CAT_API cat_bool_t      cat_thread_equal(const cat_thread_t *a, const cat_thread_t *b);
CAT_API cat_bool_t      cat_thread_is_main(const cat_thread_t *thread);
CAT_API cat_bool_t      cat_thread_is_running(const cat_thread_t *thread);
CAT_API cat_thread_t   *cat_thread_get_parent(const cat_thread_t *thread);
CAT_API cat_thread_id_t cat_thread_get_id(const cat_thread_t *thread);
CAT_API cat_thread_id_t cat_thread_get_current_id(void);
CAT_API uint32_t        cat_thread_get_count(void);
CAT_API uint32_t        cat_thread_get_external_count(void);
CAT_API void           *cat_thread_get_return_value(const cat_thread_t *thread);
CAT_API cat_bool_t      cat_thread_set_name(const char *name);

/*
 * Only the object of the running thread is thread local. The root and the two
 * counters belong to the process, because every thread reads them.
 */
CAT_GLOBALS_STRUCT_BEGIN(cat_thread) {
    cat_thread_t *current;
} CAT_GLOBALS_STRUCT_END(cat_thread);

extern CAT_API CAT_GLOBALS_DECLARE(cat_thread);

#define CAT_THREAD_G(x) CAT_GLOBALS_GET(cat_thread, x)

/*
 * Every read and write of the block goes through these, because a thread without
 * a runtime has no block and G(x) would walk away from a null base.
 */
static cat_always_inline cat_thread_t *cat_thread_get_current_safe(void)
{
    return cat_globals_available() ? CAT_THREAD_G(current) : NULL;
}

static cat_always_inline cat_bool_t cat_thread_set_current(cat_thread_t *thread)
{
    if (!cat_globals_available()) {
        return cat_false;
    }
    CAT_THREAD_G(current) = thread;

    return cat_true;
}

#endif /* CAT_THREAD_SAFE */

#ifdef __cplusplus
}
#endif
#endif /* CAT_THREAD_H */
