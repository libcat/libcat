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
  | Author: Twosee <twose@qq.com>                                            |
  +--------------------------------------------------------------------------+
 */

#ifndef CAT_QUEUE_H_
#define CAT_QUEUE_H_

/* copy from libuv queue.h */

#include <stddef.h>

typedef void *cat_queue_t[2];

/* private */

#define cat_queue_next(queue)       (*(cat_queue_t **) &((*(queue))[0]))
#define cat_queue_prev(queue)       (*(cat_queue_t **) &((*(queue))[1]))
#define cat_queue_prev_next(queue)  (cat_queue_next(cat_queue_prev(queue)))
#define cat_queue_next_prev(queue)  (cat_queue_prev(cat_queue_next(queue)))

/* public */

#define cat_queue_init(queue) do { \
    cat_queue_next(queue) = (queue); \
    cat_queue_prev(queue) = (queue); \
} while(0)

#define cat_queue_data(ptr, type, field) \
  ((type *) ((char *) (ptr) - offsetof(type, field)))

#define cat_queue_empty(queue) \
  ((const cat_queue_t *) (queue) == (const cat_queue_t *) cat_queue_next(queue))

#define cat_queue_is_in(queue) (!cat_queue_empty(queue))

#define cat_queue_front(head) \
    ((!cat_queue_empty(head)) ? cat_queue_next(head) : NULL)

#define cat_queue_back(head) \
    ((!cat_queue_empty(head)) ? cat_queue_prev(head) : NULL)

#define cat_queue_front_data(head, type, queue) \
    ((!cat_queue_empty(head)) ? cat_queue_data(cat_queue_next(head), type, queue) : NULL)

#define cat_queue_back_data(head, type, queue) \
    ((!cat_queue_empty(head)) ? cat_queue_data(cat_queue_prev(head), type, queue) : NULL)

#define cat_queue_push_front(head, queue) do { \
    cat_queue_next(queue) = cat_queue_next(head); \
    cat_queue_prev(queue) = (head); \
    cat_queue_next_prev(queue) = (queue); \
    cat_queue_next(head) = (queue); \
} while(0)

#define cat_queue_push_back(head, queue) do { \
    cat_queue_next(queue) = (head); \
    cat_queue_prev(queue) = cat_queue_prev(head); \
    cat_queue_prev_next(queue) = (queue); \
    cat_queue_prev(head) = (queue); \
} while (0)

#define cat_queue_pop_front(head) \
        cat_queue_remove(cat_queue_next(head))

#define cat_queue_pop_back(head) \
        cat_queue_remove(cat_queue_prev(head))

#define cat_queue_remove(queue) do { \
    cat_queue_prev_next(queue) = cat_queue_next(queue); \
    cat_queue_next_prev(queue) = cat_queue_prev(queue); \
} while(0)

#define CAT_QUEUE_FOREACH(queue, head) \
  for ((queue) = cat_queue_next(head); (queue) != (head); (queue) = cat_queue_next(queue))

#define CAT_QUEUE_FOREACH_DATA_START(head, type, queue, name) do { \
    cat_queue_t *queue; \
    type *name; \
    CAT_QUEUE_FOREACH(queue, head) { \
        name = cat_queue_data(queue, type, queue); \

#define CAT_QUEUE_FOREACH_DATA_END() \
    } \
} while (0)

#endif /* CAT_QUEUE_H_ */
