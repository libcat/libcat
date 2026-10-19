/*
  +--------------------------------------------------------------------------+
  | libcat                                                                   |
  +--------------------------------------------------------------------------+
  | Licensed under the Apache License, Version 2.0 (the "License");          |
  | you may not use this file except in compliance with the License.         |
  | You may obtain a copy of the License at                                  |
  | http://www.apache.org/licenses/LICENSE-2.0                               |
  +--------------------------------------------------------------------------+
  | Author: Yun Dou <dixyes@gmail.com>                                       |
  +--------------------------------------------------------------------------+
 */

/*
 * A small thread pool, built here and not in the library.
 *
 * The number of threads and the way work goes to them is a policy of the
 * application, so the pool lives here. The library gives what a pool needs. A
 * worker waits in cat_sem_acquire, which suspends the coroutine of the worker and
 * leaves its thread to the event loop. A worker that waits for work therefore
 * costs almost nothing.
 *
 * The queue is a fixed ring of jobs. The mutex guards the ring, and the semaphore
 * counts the jobs in it. This pair is correct while each post follows its push,
 * which the mutex guarantees.
 */

#ifndef MANDEL_POOL_H_
#define MANDEL_POOL_H_

#include "cat.h"
#include "cat_api.h"
#include "cat_thread.h"
#include "cat_thread_sync.h"

#include "mandel.h"

#include <string.h>

#define MANDEL_POOL_MAX_THREADS 64
#define MANDEL_POOL_MAX_JOBS 512

struct mandel_pool_job_s {
    uint32_t *image;
    int width;
    int height;
    int iterations;
    struct mandel_view_s view;
    int row_begin;
    int row_end;
    /* a job whose frame is no longer wanted stops at its next row boundary */
    cat_atomic_uint64_t *generation;
    uint64_t my_generation;
};

struct mandel_pool_s {
    cat_thread_t threads[MANDEL_POOL_MAX_THREADS];
    int thread_count;
    cat_mutex_t lock;
    cat_sem_t jobs;
    struct mandel_pool_job_s ring[MANDEL_POOL_MAX_JOBS];
    int head;
    int tail;
    int count;
    cat_atomic_bool_t quit;
    /* jobs finished since the caller last reset it */
    cat_atomic_int64_t finished;
};

static inline void mandel_pool_shutdown(struct mandel_pool_s *pool);

static inline void mandel_pool_worker(cat_data_t *data)
{
    struct mandel_pool_s *pool = (struct mandel_pool_s *) data;

    for (;;) {
        struct mandel_pool_job_s job;

        /* the coroutine sleeps here, and the thread of this worker stays with
         * the event loop until work arrives */
        if (!cat_sem_acquire(&pool->jobs, CAT_TIMEOUT_FOREVER)) {
            break;
        }

        if (cat_atomic_bool_load(&pool->quit)) {
            break;
        }

        if (!cat_mutex_lock(&pool->lock, CAT_TIMEOUT_FOREVER)) {
            break;
        }
        if (pool->count == 0) {
            /* the post and the push are separated by the unlock below, so this
             * is possible and simply means the work went to another worker */
            cat_mutex_unlock(&pool->lock);
            continue;
        }
        job = pool->ring[pool->head];
        pool->head = (pool->head + 1) % MANDEL_POOL_MAX_JOBS;
        pool->count--;
        cat_mutex_unlock(&pool->lock);

        mandel_render_rows(job.image, job.width, job.height, &job.view, job.iterations,
            job.row_begin, job.row_end, job.generation, job.my_generation);

        (void) cat_atomic_int64_fetch_add(&pool->finished, 1);
    }
}

static inline int mandel_pool_init(struct mandel_pool_s *pool, int threads)
{
    if (threads <= 0 || threads > MANDEL_POOL_MAX_THREADS) {
        return 0;
    }

    memset(pool, 0, sizeof(*pool));
    if (!cat_mutex_init(&pool->lock) || !cat_sem_init(&pool->jobs, 0)) {
        return 0;
    }
    cat_atomic_bool_init(&pool->quit, cat_false);
    cat_atomic_int64_init(&pool->finished, 0);

    for (int index = 0; index < threads; index++) {
        if (!cat_thread_create(&pool->threads[index], mandel_pool_worker, pool)) {
            fprintf(stderr, "pool: thread %d failed: %s\n", index, cat_get_last_error_message());
            pool->thread_count = index;
            mandel_pool_shutdown(pool);
            return 0;
        }
        pool->thread_count = index + 1;
    }

    return 1;
}

/* Hands one job over. The caller has to have reset finished before a batch. */
static inline int mandel_pool_push(struct mandel_pool_s *pool, const struct mandel_pool_job_s *job)
{
    if (!cat_mutex_lock(&pool->lock, CAT_TIMEOUT_FOREVER)) {
        return 0;
    }
    if (pool->count == MANDEL_POOL_MAX_JOBS) {
        cat_mutex_unlock(&pool->lock);
        return 0;
    }
    pool->ring[pool->tail] = *job;
    pool->tail = (pool->tail + 1) % MANDEL_POOL_MAX_JOBS;
    pool->count++;
    cat_mutex_unlock(&pool->lock);

    /* only now does a worker that was asleep learn there is work */
    cat_sem_post(&pool->jobs);

    return 1;
}

static inline void mandel_pool_reset_finished(struct mandel_pool_s *pool)
{
    cat_atomic_int64_store(&pool->finished, 0);
}

static inline int mandel_pool_finished(const struct mandel_pool_s *pool)
{
    return (int) cat_atomic_int64_load(&((struct mandel_pool_s *) pool)->finished);
}

/* Wakes every worker, tells them to stop and collects the threads. */
static inline void mandel_pool_shutdown(struct mandel_pool_s *pool)
{
    cat_atomic_bool_store(&pool->quit, cat_true);

    for (int index = 0; index < pool->thread_count; index++) {
        cat_sem_post(&pool->jobs);
    }

    for (int index = 0; index < pool->thread_count; index++) {
        if (!cat_thread_join(&pool->threads[index], CAT_TIMEOUT_FOREVER)) {
            fprintf(stderr, "pool: join %d failed: %s\n", index, cat_get_last_error_message());
        }
    }
    pool->thread_count = 0;

    cat_sem_destroy(&pool->jobs);
    cat_mutex_destroy(&pool->lock);
}

#endif /* MANDEL_POOL_H_ */
