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

#include "cat_channel.h"
#include "cat_coroutine.h"

#define CAT_CHANNEL_IS_CLOSING ((cat_data_dtor_t) -1)

#define CAT_CHANNEL_CHECKER(channel, failure) do { \
    if (unlikely(channel->dtor == CAT_CHANNEL_IS_CLOSING)) { \
        cat_update_last_error(CAT_EINVAL, "Channel is closing"); \
        failure; \
    } \
} while (0)

CAT_API cat_channel_t *cat_channel_create(cat_channel_t *channel, cat_channel_size_t capacity, size_t size, cat_data_dtor_t dtor)
{
    channel->capacity = capacity;
    channel->length = 0;
    channel->data_size = size;
    channel->dtor = dtor;
    cat_queue_init(&channel->storage);
    cat_queue_init(&channel->producers);
    cat_queue_init(&channel->consumers);

    return channel;
}

CAT_API cat_bool_t cat_channel_push(cat_channel_t *channel, const cat_data_t *data)
{
    return cat_channel_push_ex(channel, data, -1);
}

CAT_API cat_bool_t cat_channel_push_ex(cat_channel_t *channel, const cat_data_t *data, cat_timeout_t timeout)
{
    CAT_CHANNEL_CHECKER(channel, return cat_false);
    cat_bool_t unbuffered = channel->capacity == 0;

    /* if it is full, just wait */
    if (unbuffered ? cat_queue_empty(&channel->consumers) : (channel->length == channel->capacity)) {
        cat_bool_t ret;
        cat_queue_t *waiter = &CAT_COROUTINE_G(current)->waiter.node;
        cat_queue_push_back(&channel->producers, waiter);
        ret = cat_time_wait(timeout);
        cat_queue_remove(waiter);
        if (unlikely(!ret)) {
            /* sleep failed or timedout */
            cat_update_last_error_with_previous("Wait for channel producer failed");
            return cat_false;
        }
        if (unlikely(channel->length == channel->capacity)) {
            /* still full, must be canceled */
            cat_update_last_error(CAT_ECANCELED, "Channel push has been canceled");
            return cat_false;
        }
        CAT_ASSERT(!unbuffered || (channel->length == ((cat_channel_size_t ) -1)));
    } else {
        CAT_ASSERT(cat_queue_empty(&channel->producers));
    }
    /* push the bucket to the storage */
    do {
        cat_channel_bucket_t *bucket = (cat_channel_bucket_t *) cat_malloc(offsetof(cat_channel_bucket_t, data) + channel->data_size);
        if (unlikely(bucket == NULL)) {
            cat_update_last_error_of_syscall("Malloc for channel bucket failed");
            return cat_false;
        }
        memcpy(bucket->data, data, channel->data_size);
        cat_queue_push_back(&channel->storage, &bucket->node);
        channel->length++;
    } while (0);
    /* notify a possible consumer to consume */
    if (channel->length > 0) {
        size_t length;
        do {
            cat_coroutine_t *consumer = cat_queue_front_data(&channel->consumers, cat_coroutine_t, waiter.node);
            if (consumer == NULL) {
                break;
            }
            length = channel->length;
            if (unlikely(!cat_coroutine_resume_ez(consumer))) {
                cat_core_error_with_last(CHANNEL, "Notify consumer failed");
            }
            /* continue if consumer did not consume */
        } while (unlikely(channel->length == length));
    }

    return cat_true;
}

CAT_API cat_bool_t cat_channel_pop(cat_channel_t *channel, cat_data_t *data)
{
    return cat_channel_pop_ex(channel, data, -1);
}

CAT_API cat_bool_t cat_channel_pop_ex(cat_channel_t *channel, cat_data_t *data, cat_timeout_t timeout)
{
    CAT_CHANNEL_CHECKER(channel, return cat_false);
    cat_bool_t unbuffered = channel->capacity == 0;
    cat_coroutine_t *producer = NULL;

    /* if it is empty, just wait */
    if (unbuffered ? cat_queue_empty(&channel->producers) : (channel->length == 0)) {
        cat_bool_t ret = cat_true;
        cat_queue_t *waiter = &CAT_COROUTINE_G(current)->waiter.node;
        cat_queue_push_back(&channel->consumers, waiter);
        ret = cat_time_wait(timeout);
        cat_queue_remove(waiter);
        if (unlikely(!ret)) {
            /* sleep failed or timedout */
            cat_update_last_error_with_previous("Wait for channel consumer failed");
            return cat_false;
        }
        if (unlikely(channel->length == 0)) {
            /* still empty, must be canceled */
            cat_update_last_error(CAT_ECANCELED, "Channel pop has been canceled");
            return cat_false;
        }
    } else {
        CAT_ASSERT(cat_queue_empty(&channel->consumers));
    }
    /* pop the bucket from the storage */
    do {
        cat_channel_bucket_t *bucket = cat_queue_front_data(&channel->storage, cat_channel_bucket_t, node);
        channel->length--;
        if (bucket == NULL) {
            producer = cat_queue_front_data(&channel->producers, cat_coroutine_t, waiter.node);
            CAT_ASSERT(producer != NULL);
            if (unlikely(!cat_coroutine_resume_ez(producer))) {
                cat_core_error_with_last(CHANNEL, "Notify producer failed");
            }
            bucket = cat_queue_front_data(&channel->storage, cat_channel_bucket_t, node);
            CAT_ASSERT(bucket != NULL);
        }
        cat_queue_remove(&bucket->node);
        memcpy(data, bucket->data, channel->data_size);
        cat_free(bucket);
    } while (0);
    /* notify a possible producer to continue to produce */
    if (producer == NULL) {
        size_t length;
        do {
            producer = cat_queue_front_data(&channel->producers, cat_coroutine_t, waiter.node);
            if (producer == NULL) {
                break;
            }
            length = channel->length;
            if (unlikely(!cat_coroutine_resume_ez(producer))) {
                cat_core_error_with_last(CHANNEL, "Notify producer failed");
            }
            /* continue if producer did not produce */
        } while (unlikely(channel->length == length));
    }

    return cat_true;
}

CAT_API void cat_channel_close(cat_channel_t *channel)
{
    CAT_CHANNEL_CHECKER(channel, return);
    /* notify all waiters */
    cat_coroutine_t *waiter;
    while ((waiter = cat_queue_front_data(&channel->producers, cat_coroutine_t, waiter.node))) {
        if (unlikely(!cat_coroutine_resume_ez(waiter))) {
            cat_core_error_with_last(CHANNEL, "Close producer failed");
        }
    }
    while ((waiter = cat_queue_front_data(&channel->consumers, cat_coroutine_t, waiter.node))) {
        if (unlikely(!cat_coroutine_resume_ez(waiter))) {
            cat_core_error_with_last(CHANNEL, "Close consumer failed");
        }
    }
    CAT_ASSERT(cat_queue_empty(&channel->producers));
    CAT_ASSERT(cat_queue_empty(&channel->consumers));
    /* clean up the data bucket (no more consumers) */
    do {
        cat_queue_t *storage = &channel->storage;
        cat_data_dtor_t dtor = channel->dtor;
        cat_channel_bucket_t *bucket;
        /* prevent from channel operations in dtor function */
        channel->dtor = CAT_CHANNEL_IS_CLOSING;
        while ((bucket = cat_queue_front_data(storage, cat_channel_bucket_t, node))) {
            cat_queue_remove(&bucket->node);
            if (dtor != NULL) {
                dtor(bucket->data);
            }
            cat_free(bucket);
            channel->length--;
        }
        /* revert and we can reuse this channel (if necessary...) */
        channel->dtor = dtor;
    } while (0);
    /* everything will be reset after close */
    CAT_ASSERT(channel->length == 0);
    CAT_ASSERT(cat_queue_empty(&channel->storage));
}

CAT_API cat_channel_size_t cat_channel_get_capacity(const cat_channel_t * channel)
{
    return channel->capacity;
}

CAT_API cat_channel_size_t cat_channel_get_length(const cat_channel_t * channel)
{
    return channel->length;
}

CAT_API cat_bool_t cat_channel_is_empty(const cat_channel_t * channel)
{
    return channel->length == 0;
}

CAT_API cat_bool_t cat_channel_is_full(const cat_channel_t * channel)
{
    return channel->length == channel->capacity;
}

CAT_API cat_bool_t cat_channel_has_producers(const cat_channel_t * channel)
{
    return cat_queue_empty(&channel->producers);
}

CAT_API cat_bool_t cat_channel_has_consumers(const cat_channel_t * channel)
{
    return cat_queue_empty(&channel->consumers);
}

CAT_API cat_bool_t cat_channel_is_readable(const cat_channel_t * channel)
{
    return channel->length > 0 || !cat_queue_empty(&channel->producers);
}

CAT_API cat_bool_t cat_channel_is_writable(const cat_channel_t * channel)
{
    return channel->length < channel->capacity || !cat_queue_empty(&channel->consumers);
}

CAT_API cat_data_dtor_t cat_channel_get_dtor(const cat_channel_t * channel)
{
    return channel->dtor;
}

CAT_API cat_data_dtor_t cat_channel_set_dtor(cat_channel_t * channel, cat_data_dtor_t dtor)
{
    cat_data_dtor_t old_dtor = channel->dtor;

    channel->dtor = dtor;

    return old_dtor;
}
