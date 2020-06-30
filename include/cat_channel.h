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

#ifndef CAT_CHANNEL_H
#define CAT_CHANNEL_H
#ifdef __cplusplus
extern "C" {
#endif

#include "cat.h"
#include "cat_queue.h"
#include "cat_time.h"

typedef uint32_t cat_channel_size_t;
#define CAT_CHANNEL_SIZE_FMT "%u"

typedef struct
{
    cat_queue_t node;
    char data[1];
} cat_channel_bucket_t;

typedef struct
{
    /* dynamic */
    cat_queue_t storage;
    cat_queue_t producers;
    cat_queue_t consumers;
    cat_channel_size_t length;
    /* static */
    cat_channel_size_t capacity;
    size_t data_size;
    cat_data_dtor_t dtor;
} cat_channel_t;

CAT_API cat_channel_t *cat_channel_create(cat_channel_t *channel, cat_channel_size_t capacity, size_t data_size, cat_data_dtor_t dtor);

CAT_API cat_bool_t cat_channel_push(cat_channel_t *channel, const cat_data_t *data);
CAT_API cat_bool_t cat_channel_push_ex(cat_channel_t *channel, const cat_data_t *data, cat_timeout_t timeout);

CAT_API cat_bool_t cat_channel_pop(cat_channel_t *channel, cat_data_t *data);
CAT_API cat_bool_t cat_channel_pop_ex(cat_channel_t *channel, cat_data_t *data, cat_timeout_t timeout);

/* Notice: close will never break the channel so we can reuse the channel after close done (if necessary) */
CAT_API void cat_channel_close(cat_channel_t *channel);

/* status */

CAT_API cat_channel_size_t cat_channel_get_capacity(const cat_channel_t * channel);
CAT_API cat_channel_size_t cat_channel_get_length(const cat_channel_t * channel);
CAT_API cat_bool_t cat_channel_is_empty(const cat_channel_t * channel);
CAT_API cat_bool_t cat_channel_is_full(const cat_channel_t * channel);
CAT_API cat_bool_t cat_channel_has_producers(const cat_channel_t * channel);
CAT_API cat_bool_t cat_channel_has_consumers(const cat_channel_t * channel);
CAT_API cat_data_dtor_t cat_channel_get_dtor(const cat_channel_t * channel);
CAT_API cat_data_dtor_t cat_channel_set_dtor(cat_channel_t * channel, cat_data_dtor_t dtor);

#ifdef __cplusplus
}
#endif
#endif  /* CAT_CHANNEL_H */
