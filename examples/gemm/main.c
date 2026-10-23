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
  | Author: Yun Dou <dixyes@gmail.com>                                       |
  +--------------------------------------------------------------------------+
 */

/*
 * A CPU bound job spread over threads: dense matrix multiply, one thread per
 * row block, every thread started and joined through this library.
 *
 * Usage: gemm [size] [threads]
 *
 * With no arguments it multiplies 4096 by 4096 on every cpu. The loops are bound
 * by memory bandwidth, so the size is what decides the duration:
 *
 *   size 3072   about 1.1 s
 *   size 4096   about 5 s
 *   size 5200   about 10 s
 *
 * Build it with -O3 against a release build of the library, because the time is
 * spent in the loops below.
 */

#include "cat.h"
#include "cat_api.h"
#include "cat_futex.h"
#include "cat_shm.h"
#include "cat_thannel.h"
#include "cat_thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef float real_t;

/* How the rows reach the threads:
 *
 *   static   one band of rows per thread, joined at the end
 *   chan     a thannel of row ranges, one range per push and pop
 *   batch    the same thannel, ranges enter and leave in batches
 *   futex    a ring of ranges in shared memory, head and tail are atomics and
 *            a waiting thread sleeps in cat_futex_wait, so no event loop is
 *            involved in a hand over
 */
enum gemm_mode_e {
    GEMM_MODE_STATIC = 0,
    GEMM_MODE_CHAN,
    GEMM_MODE_BATCH,
    GEMM_MODE_FUTEX,
};

struct gemm_task_s {
    int row_begin;
    int row_end;
};

/*
 * The futex queue lives in one shared mapping, so threads share it now and
 * processes could share it the same way. head is the next range a worker may
 * claim, and tail is the next range the producer fills.
 */
struct gemm_futex_queue_s {
    cat_futex_t head;
    cat_futex_t tail;
    cat_futex_t closed;
    /* workers that sleep in the wait below, so a push wakes one only when one
     * could be asleep */
    cat_atomic_int32_t sleeping;
    cat_atomic_int32_t capacity;
    struct gemm_task_s slots[];
};

struct gemm_job_s {
    cat_thread_t thread;
    const real_t *left;
    const real_t *right;
    real_t *result;
    int size;
    int row_begin;
    int row_end;
    double seconds;
    /* the queue modes use these */
    cat_thannel_t *thannel;
    struct gemm_futex_queue_s *queue;
    int batch;
    long tasks;
};

static double gemm_now(void)
{
    struct timespec now;

    (void) clock_gettime(CLOCK_MONOTONIC, &now);

    return (double) now.tv_sec + (double) now.tv_nsec / 1e9;
}

/* result = left * right, for the rows this job owns. The loop order keeps the
 * row of right in cache while the row of left stays in a register. */
static void gemm_block(const real_t *left, const real_t *right, real_t *result,
                       int size, int row_begin, int row_end)
{
    for (int row = row_begin; row < row_end; row++) {
        real_t *restrict out = result + (size_t) row * (size_t) size;
        const real_t *restrict in = left + (size_t) row * (size_t) size;

        memset(out, 0, (size_t) size * sizeof(*out));

        for (int k = 0; k < size; k++) {
            real_t factor = in[k];
            const real_t *restrict line = right + (size_t) k * (size_t) size;

            for (int column = 0; column < size; column++) {
                out[column] += factor * line[column];
            }
        }
    }
}

static void gemm_routine(cat_data_t *data)
{
    struct gemm_job_s *job = (struct gemm_job_s *) data;
    double started = gemm_now();

    gemm_block(job->left, job->right, job->result, job->size, job->row_begin, job->row_end);

    job->seconds = gemm_now() - started;
}

static void gemm_fill(real_t *matrix, size_t count, unsigned int seed)
{
    for (size_t index = 0; index < count; index++) {
        seed = seed * 1103515245u + 12345u;
        matrix[index] = (real_t) ((seed >> 16) & 0x3ff) / 1024.0f;
    }
}

/* One element done again in double, to check the row block that produced it. */
static double gemm_element(const real_t *left, const real_t *right, int size, int row, int column)
{
    double total = 0.0;

    for (int k = 0; k < size; k++) {
        total += (double) left[(size_t) row * (size_t) size + (size_t) k] *
                 (double) right[(size_t) k * (size_t) size + (size_t) column];
    }

    return total;
}

/* The sampled elements sit on the borders of the row blocks, so a wrong block
 * boundary shows up here. */
static double gemm_check(const real_t *left, const real_t *right, const real_t *result, int size)
{
    static const int fractions[][2] = {
        { 0, 0 },
        { 0, 1 },
        { 1, 0 },
        { 1, 1 },
        { 2, 3 },
        { 3, 2 },
    };
    double worst = 0.0;

    for (size_t index = 0; index < sizeof(fractions) / sizeof(fractions[0]); index++) {
        int row = fractions[index][0] == 0 ? 0
            : fractions[index][0] == 1 ? size - 1
            : size / (fractions[index][0] + 1);
        int column = fractions[index][1] == 0 ? 0
            : fractions[index][1] == 1 ? size - 1
            : size / (fractions[index][1] + 1);
        double expected = gemm_element(left, right, size, row, column);
        double actual = (double) result[(size_t) row * (size_t) size + (size_t) column];
        double scale = expected != 0.0 ? expected : 1.0;
        double error = (actual - expected) / scale;

        if (error < 0) {
            error = -error;
        }
        if (error > worst) {
            worst = error;
        }
    }

    return worst;
}

/* ------------------------------------------------------------------------- */
/* The queues                                                                 */
/* ------------------------------------------------------------------------- */

#define GEMM_MAX_BATCH 64

/* One shared mapping holds the ring and its slots. */
static struct gemm_futex_queue_s *gemm_queue_create(cat_shm_t **shm, int capacity)
{
    struct gemm_futex_queue_s *queue;
    size_t size = sizeof(*queue) + (size_t) capacity * sizeof(queue->slots[0]);

    *shm = cat_shm_create_anonymous(size);
    if (*shm == NULL) {
        return NULL;
    }

    queue = (struct gemm_futex_queue_s *) cat_shm_get_address(*shm);
    cat_atomic_int32_init(&queue->head, 0);
    cat_atomic_int32_init(&queue->tail, 0);
    cat_atomic_int32_init(&queue->closed, 0);
    cat_atomic_int32_init(&queue->sleeping, 0);
    cat_atomic_int32_init(&queue->capacity, capacity);

    return queue;
}

/* Blocks while the ring is full. Only a thread that runs one coroutine may call
 * it, which is the case for the producer and for every worker here. */
static void gemm_queue_push(struct gemm_futex_queue_s *queue, int row_begin, int row_end)
{
    for (;;) {
        int head = cat_atomic_int32_load(&queue->head);
        int tail = cat_atomic_int32_load(&queue->tail);
        int capacity = cat_atomic_int32_load(&queue->capacity);
        int slot;

        if (tail - head < capacity) {
            slot = tail % capacity;
            queue->slots[slot].row_begin = row_begin;
            queue->slots[slot].row_end = row_end;

            /* publish the slot, then wake a worker that could be asleep */
            cat_atomic_int32_store_explicit(&queue->tail, tail + 1, CAT_ATOMIC_MEMORY_ORDER_RELEASE);
            if (cat_atomic_int32_load(&queue->sleeping) > 0) {
                (void) cat_futex_wake(&queue->tail, 1);
            }
            return;
        }

        (void) cat_futex_wait(&queue->head, head, CAT_TIMEOUT_FOREVER);
    }
}

/* Claims up to batch ranges. Returns 0 once the producer closed the ring. */
static int gemm_queue_pop(struct gemm_futex_queue_s *queue, struct gemm_task_s *tasks, int batch)
{
    for (;;) {
        int head = cat_atomic_int32_load(&queue->head);
        int tail = cat_atomic_int32_load_explicit(&queue->tail, CAT_ATOMIC_MEMORY_ORDER_ACQUIRE);
        int capacity = cat_atomic_int32_load(&queue->capacity);
        int count;
        int expected;

        if (head == tail) {
            if (cat_atomic_int32_load(&queue->closed)) {
                return 0;
            }

            /* Sleep only when the ring is still empty after this worker says it
             * is about to sleep, so a push cannot miss it. */
            cat_atomic_int32_fetch_add(&queue->sleeping, 1);
            if (cat_atomic_int32_load_explicit(&queue->tail, CAT_ATOMIC_MEMORY_ORDER_ACQUIRE) == tail) {
                (void) cat_futex_wait(&queue->tail, tail, CAT_TIMEOUT_FOREVER);
            }
            cat_atomic_int32_fetch_sub(&queue->sleeping, 1);
            continue;
        }

        count = tail - head;
        if (count > batch) {
            count = batch;
        }

        /* Copy before the claim: a slot stays untouched by the producer until
         * head passes it, and a worker that loses the claim simply copies again
         * from the new head. */
        for (int index = 0; index < count; index++) {
            tasks[index] = queue->slots[(head + index) % capacity];
        }

        expected = head;
        if (!cat_atomic_int32_compare_exchange_strong(&queue->head, &expected, head + count)) {
            continue;
        }

        /* the ring has room again, so the producer may fill it */
        (void) cat_futex_wake(&queue->head, count);

        return count;
    }
}

static void gemm_queue_close(struct gemm_futex_queue_s *queue)
{
    cat_atomic_int32_store(&queue->closed, 1);
    (void) cat_futex_wake(&queue->tail, -1);
}

/* A worker of the futex mode: no event loop takes part in a hand over. */
static void gemm_futex_routine(cat_data_t *data)
{
    struct gemm_job_s *job = (struct gemm_job_s *) data;
    struct gemm_task_s tasks[GEMM_MAX_BATCH];
    double started = gemm_now();

    for (;;) {
        int count = gemm_queue_pop(job->queue, tasks, job->batch);

        if (count == 0) {
            break;
        }
        for (int index = 0; index < count; index++) {
            gemm_block(job->left, job->right, job->result, job->size,
                tasks[index].row_begin, tasks[index].row_end);
        }
        job->tasks += count;
    }

    job->seconds = gemm_now() - started;
}

/* A worker of the thannel modes: the pop parks the coroutine, and the wake of
 * the producer goes through the event loop of this thread. */
static void gemm_thannel_routine(cat_data_t *data)
{
    struct gemm_job_s *job = (struct gemm_job_s *) data;
    cat_thannel_item_t items[GEMM_MAX_BATCH];
    double started = gemm_now();

    for (;;) {
        size_t count = 0;

        if (!cat_thannel_popv(job->thannel, items, (size_t) job->batch, &count,
            CAT_TIMEOUT_FOREVER)) {
            break;
        }

        for (size_t index = 0; index < count; index++) {
            const struct gemm_task_s *task = (const struct gemm_task_s *) items[index].data;

            gemm_block(job->left, job->right, job->result, job->size, task->row_begin,
                task->row_end);
            cat_sys_free(items[index].data);
        }
        job->tasks += (long) count;
    }

    job->seconds = gemm_now() - started;
}

/* Hands every range over in batches of at most GEMM_MAX_BATCH. */
static int gemm_produce(cat_thannel_t *thannel, int size, int chunk, int batch)
{
    struct gemm_task_s tasks[GEMM_MAX_BATCH];
    cat_thannel_item_t items[GEMM_MAX_BATCH];
    int count = 0;
    int row;

    for (row = 0; row < size; row += chunk) {
        int row_end = row + chunk;

        if (row_end > size) {
            row_end = size;
        }

        tasks[count].row_begin = row;
        tasks[count].row_end = row_end;
        items[count].data = &tasks[count];
        items[count].size = sizeof(tasks[count]);
        count++;

        if (count == batch) {
            if (!cat_thannel_pushv(thannel, items, (size_t) count, CAT_TIMEOUT_FOREVER)) {
                return 0;
            }
            count = 0;
        }
    }

    if (count > 0 && !cat_thannel_pushv(thannel, items, (size_t) count, CAT_TIMEOUT_FOREVER)) {
        return 0;
    }

    return 1;
}

int main(int argc, char **argv)
{
    int size = argc > 1 ? atoi(argv[1]) : 4096;
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    int threads = argc > 2 ? atoi(argv[2]) : (cpus > 0 ? (int) cpus : 1);
    enum gemm_mode_e mode = GEMM_MODE_STATIC;
    int chunk = 0;
    int batch = argc > 5 ? atoi(argv[5]) : 8;
    static cat_thread_t main_thread;
    size_t count;
    real_t *left = NULL;
    real_t *right = NULL;
    real_t *result = NULL;
    struct gemm_job_s *jobs = NULL;
    cat_thannel_t thannel;
    cat_shm_t *shm = NULL;
    struct gemm_futex_queue_s *queue = NULL;
    double started;
    double elapsed;
    double error;
    int created = 0;
    int ok = 0;

    if (size <= 0 || threads <= 0 || threads > size) {
        fprintf(stderr, "usage: %s [size] [threads] [static|chan|batch|futex] [chunk] [batch]\n",
            argv[0]);
        return 2;
    }

    if (argc > 3) {
        if (strcmp(argv[3], "static") == 0) {
            mode = GEMM_MODE_STATIC;
        } else if (strcmp(argv[3], "chan") == 0) {
            mode = GEMM_MODE_CHAN;
        } else if (strcmp(argv[3], "batch") == 0) {
            mode = GEMM_MODE_BATCH;
        } else if (strcmp(argv[3], "futex") == 0) {
            mode = GEMM_MODE_FUTEX;
        } else {
            fprintf(stderr, "unknown mode %s\n", argv[3]);
            return 2;
        }
    }

    /* Small enough ranges give every thread a turn at the queue, large enough
     * ones keep the hand over out of the way. */
    chunk = argc > 4 ? atoi(argv[4]) : size / (threads * 16);
    if (chunk < 1) {
        chunk = 1;
    }
    if (batch < 1) {
        batch = 1;
    }
    if (batch > GEMM_MAX_BATCH) {
        batch = GEMM_MAX_BATCH;
    }
    if (mode == GEMM_MODE_CHAN) {
        batch = 1;
    }

    if (!cat_init_all() || !cat_thread_register_main(&main_thread)) {
        fprintf(stderr, "runtime init failed: %s\n", cat_get_last_error_message());
        return 1;
    }
    /* The loop has to run, because a join gives the thread back to it. */
    if (!cat_run(CAT_RUN_EASY)) {
        fprintf(stderr, "run failed: %s\n", cat_get_last_error_message());
        return 1;
    }

    count = (size_t) size * (size_t) size;
    left = (real_t *) cat_sys_malloc(count * sizeof(*left));
    right = (real_t *) cat_sys_malloc(count * sizeof(*right));
    result = (real_t *) cat_sys_malloc(count * sizeof(*result));
    jobs = (struct gemm_job_s *) cat_sys_malloc((size_t) threads * sizeof(*jobs));

    if (left == NULL || right == NULL || result == NULL || jobs == NULL) {
        fprintf(stderr, "out of memory for size %d\n", size);
        goto _exit;
    }

    gemm_fill(left, count, 2024u);
    gemm_fill(right, count, 7u);
    memset(jobs, 0, (size_t) threads * sizeof(*jobs));

    printf("gemm: size=%d threads=%d (of %ld cpus)\n", size, threads, cpus);
    printf("gemm: mode=%s chunk=%d batch=%d\n",
        mode == GEMM_MODE_STATIC ? "static" : mode == GEMM_MODE_CHAN ? "chan"
            : mode == GEMM_MODE_BATCH ? "batch" : "futex",
        mode == GEMM_MODE_STATIC ? 0 : chunk, mode == GEMM_MODE_STATIC ? 0 : batch);
    fflush(stdout);

    started = gemm_now();

    if (mode != GEMM_MODE_STATIC && mode != GEMM_MODE_FUTEX) {
        cat_thannel_size_t capacity = (cat_thannel_size_t) (threads * batch * 4);

        if (capacity < 8) {
            capacity = 8;
        }
        if (cat_thannel_create(&thannel, capacity) == NULL) {
            fprintf(stderr, "thannel failed: %s\n", cat_get_last_error_message());
            goto _exit;
        }
    }

    if (mode == GEMM_MODE_FUTEX) {
        int capacity = threads * batch * 4;

        if (capacity < 8) {
            capacity = 8;
        }
        queue = gemm_queue_create(&shm, capacity);
        if (queue == NULL) {
            fprintf(stderr, "shared queue failed: %s\n", cat_get_last_error_message());
            goto _exit;
        }
    }

    for (int index = 0; index < threads; index++) {
        struct gemm_job_s *job = &jobs[index];
        int rows = size / threads;
        int extra = size % threads;
        void (*routine)(cat_data_t *);

        job->left = left;
        job->right = right;
        job->result = result;
        job->size = size;
        job->batch = batch;
        job->thannel = mode == GEMM_MODE_FUTEX || mode == GEMM_MODE_STATIC ? NULL : &thannel;
        job->queue = queue;

        if (mode == GEMM_MODE_STATIC) {
            job->row_begin = index * rows + (index < extra ? index : extra);
            job->row_end = job->row_begin + rows + (index < extra ? 1 : 0);
            routine = gemm_routine;
        } else if (mode == GEMM_MODE_FUTEX) {
            routine = gemm_futex_routine;
        } else {
            routine = gemm_thannel_routine;
        }

        if (!cat_thread_create(&job->thread, routine, job)) {
            fprintf(stderr, "thread %d failed: %s\n", index, cat_get_last_error_message());
            goto _join;
        }
        created++;
    }

    if (mode == GEMM_MODE_FUTEX) {
        for (int row = 0; row < size; row += chunk) {
            int row_end = row + chunk;

            if (row_end > size) {
                row_end = size;
            }
            gemm_queue_push(queue, row, row_end);
        }
        gemm_queue_close(queue);
    } else if (mode != GEMM_MODE_STATIC) {
        if (!gemm_produce(&thannel, size, chunk, batch)) {
            fprintf(stderr, "produce failed: %s\n", cat_get_last_error_message());
        }
        if (!cat_thannel_close(&thannel)) {
            fprintf(stderr, "close failed: %s\n", cat_get_last_error_message());
        }
    }

_join:
    for (int index = 0; index < created; index++) {
        if (!cat_thread_join(&jobs[index].thread, CAT_TIMEOUT_FOREVER)) {
            fprintf(stderr, "join %d failed: %s\n", index, cat_get_last_error_message());
            goto _exit;
        }
    }

    elapsed = gemm_now() - started;
    ok = 1;

    error = gemm_check(left, right, result, size);

    printf("gemm: %dx%d in %.3f s, %.2f GFLOP/s\n",
        size, size, elapsed, 2.0 * (double) size * (double) size * (double) size / elapsed / 1e9);
    for (int index = 0; index < created; index++) {
        if (mode == GEMM_MODE_STATIC) {
            printf("gemm: worker %2d rows [%5d, %5d) took %.3f s\n",
                index, jobs[index].row_begin, jobs[index].row_end, jobs[index].seconds);
        } else {
            printf("gemm: worker %2d took %.3f s, %ld ranges\n",
                index, jobs[index].seconds, jobs[index].tasks);
        }
    }
    printf("gemm: sampled check, worst relative error %.3e (%s)\n", error,
        error < 1e-3 ? "ok" : "MISMATCH");

    if (error >= 1e-3) {
        ok = 0;
    }

_exit:
    if (mode != GEMM_MODE_STATIC && mode != GEMM_MODE_FUTEX) {
        cat_thannel_cleanup(&thannel);
    }
    if (shm != NULL) {
        cat_shm_detach(shm);
    }
    cat_sys_free(jobs);
    cat_sys_free(result);
    cat_sys_free(right);
    cat_sys_free(left);

    (void) cat_stop();
    (void) cat_runtime_shutdown_all();
    (void) cat_runtime_close_all();

    return ok ? 0 : 1;
}
