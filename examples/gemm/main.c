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
#include "cat_thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef float real_t;

struct gemm_job_s {
    cat_thread_t thread;
    const real_t *left;
    const real_t *right;
    real_t *result;
    int size;
    int row_begin;
    int row_end;
    double seconds;
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

int main(int argc, char **argv)
{
    int size = argc > 1 ? atoi(argv[1]) : 4096;
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    int threads = argc > 2 ? atoi(argv[2]) : (cpus > 0 ? (int) cpus : 1);
    static cat_thread_t main_thread;
    size_t count;
    real_t *left = NULL;
    real_t *right = NULL;
    real_t *result = NULL;
    struct gemm_job_s *jobs = NULL;
    double started;
    double elapsed;
    double error;
    int created = 0;
    int ok = 0;

    if (size <= 0 || threads <= 0 || threads > size) {
        fprintf(stderr, "usage: %s [size] [threads]\n", argv[0]);
        return 2;
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
    fflush(stdout);

    started = gemm_now();

    for (int index = 0; index < threads; index++) {
        struct gemm_job_s *job = &jobs[index];
        int rows = size / threads;
        int extra = size % threads;

        job->left = left;
        job->right = right;
        job->result = result;
        job->size = size;
        job->row_begin = index * rows + (index < extra ? index : extra);
        job->row_end = job->row_begin + rows + (index < extra ? 1 : 0);

        if (!cat_thread_create(&job->thread, gemm_routine, job)) {
            fprintf(stderr, "thread %d failed: %s\n", index, cat_get_last_error_message());
            goto _join;
        }
        created++;
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
        printf("gemm: worker %2d rows [%5d, %5d) took %.3f s\n",
            index, jobs[index].row_begin, jobs[index].row_end, jobs[index].seconds);
    }
    printf("gemm: sampled check, worst relative error %.3e (%s)\n", error,
        error < 1e-3 ? "ok" : "MISMATCH");

    if (error >= 1e-3) {
        ok = 0;
    }

_exit:
    cat_sys_free(jobs);
    cat_sys_free(result);
    cat_sys_free(right);
    cat_sys_free(left);

    (void) cat_stop();
    (void) cat_runtime_shutdown_all();
    (void) cat_runtime_close_all();

    return ok ? 0 : 1;
}
