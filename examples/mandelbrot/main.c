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
 * The Mandelbrot set, rendered by the threads of this library and written to a
 * PNG file.
 *
 * This is the third shape of load in the examples: the work per pixel varies by
 * orders of magnitude, so a band of rows is not an even share and the threads
 * finish at different times. Unlike the matrix multiply and the hash, the cost
 * of a pixel is not known before it is computed.
 *
 * The threads share nothing but the image: each one owns a band of rows, and the
 * rows are the same no matter how many threads draw them. The demo checks that:
 * with --verify it renders the same image twice, once on one thread and once on
 * all of them, and compares the bytes.
 *
 * The PNG container is written here with zlib, so the only dependency is -lz.
 * The same picture in a window is main-gui.c.
 *
 * Usage:
 *   mandelbrot --verify [width] [height] [iterations]
 *   mandelbrot [width] [height] [iterations] [threads] [out.png]
 *
 * Defaults are 3840x2160, 4096 iterations, every cpu, mandelbrot.png.
 *
 * Measured on sixteen cores of a Ryzen 7 PRO 8840HS:
 *
 *   3840x2160, 4096 iterations    about 3 s
 *   6400x3600, 4096 iterations    about 9 s
 *   7680x4320, 4096 iterations    about 13 s
 */

#include "cat.h"
#include "cat_api.h"
#include "cat_shm.h"
#include "cat_thannel.h"
#include "cat_thread.h"

#include "mandel.h"
#include "png.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


/* ------------------------------------------------------------------------- */
/* The set                                                                    */
/* ------------------------------------------------------------------------- */

struct mandel_job_s {
    cat_thread_t thread;
    uint32_t *image;
    int width;
    int height;
    int iterations;
    int row_begin;
    int row_end;
    struct mandel_view_s view;
    double seconds;
    /* the queue mode uses these */
    cat_thannel_t *thannel;
    int batch;
    long tasks;
};

struct mandel_task_s {
    int row_begin;
    int row_end;
};

static void mandel_routine(cat_data_t *data)
{
    struct mandel_job_s *job = (struct mandel_job_s *) data;
    struct timespec started;
    struct timespec ended;

    (void) clock_gettime(CLOCK_MONOTONIC, &started);
    mandel_render_rows(job->image, job->width, job->height, &job->view, job->iterations,
        job->row_begin, job->row_end, NULL, 0);
    (void) clock_gettime(CLOCK_MONOTONIC, &ended);

    job->seconds = (double) (ended.tv_sec - started.tv_sec) +
                   (double) (ended.tv_nsec - started.tv_nsec) / 1e9;
}

/*
 * The queue mode: the rows go to the threads through a thannel, in batches, so
 * a thread that draws a cheap band picks up the next range instead of waiting
 * for the others to finish theirs. A row of this set costs anywhere from a
 * handful of iterations to the full limit, which is what the static bands in
 * the default mode cannot even out.
 */
static void mandel_queue_routine(cat_data_t *data)
{
    struct mandel_job_s *job = (struct mandel_job_s *) data;
    cat_thannel_item_t items[64];
    struct timespec started;
    struct timespec ended;

    (void) clock_gettime(CLOCK_MONOTONIC, &started);

    for (;;) {
        size_t count = 0;

        if (!cat_thannel_popv(job->thannel, items, (size_t) job->batch, &count,
            CAT_TIMEOUT_FOREVER)) {
            break;
        }

        for (size_t index = 0; index < count; index++) {
            const struct mandel_task_s *task = (const struct mandel_task_s *) items[index].data;

            mandel_render_rows(job->image, job->width, job->height, &job->view, job->iterations,
                task->row_begin, task->row_end, NULL, 0);
            cat_sys_free(items[index].data);
        }
        job->tasks += (long) count;
    }

    (void) clock_gettime(CLOCK_MONOTONIC, &ended);
    job->seconds = (double) (ended.tv_sec - started.tv_sec) +
                   (double) (ended.tv_nsec - started.tv_nsec) / 1e9;
}

/* Hands out ranges of chunk rows, in batches of batch. */
static int mandel_produce(cat_thannel_t *thannel, int height, int chunk, int batch)
{
    struct mandel_task_s tasks[64];
    cat_thannel_item_t items[64];
    int count = 0;

    for (int row = 0; row < height; row += chunk) {
        int row_end = row + chunk;

        if (row_end > height) {
            row_end = height;
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
    static cat_thread_t main_thread;
    int verify = 0;
    int queue_batch = 0;
    int arg = 1;
    int width;
    int height;
    int iterations;
    long cpus;
    int threads;
    const char *path;
    struct mandel_view_s view;
    size_t image_size;
    uint32_t *image = NULL;
    uint32_t *reference = NULL;
    struct mandel_job_s *jobs = NULL;
    cat_shm_t *shm = NULL;
    cat_thannel_t thannel;
    struct timespec started;
    struct timespec ended;
    double elapsed;
    int created = 0;
    int exit_code = 0;

    if (argc > arg && strcmp(argv[arg], "--verify") == 0) {
        verify = 1;
        arg++;
    }
    if (argc > arg && strcmp(argv[arg], "--queue") == 0) {
        queue_batch = argc > arg + 1 ? atoi(argv[arg + 1]) : 1;
        arg += 2;
        if (queue_batch < 1) {
            queue_batch = 1;
        }
        if (queue_batch > 64) {
            queue_batch = 64;
        }
    }

    cpus = sysconf(_SC_NPROCESSORS_ONLN);
    width = argc > arg ? atoi(argv[arg]) : 3840;
    height = argc > arg + 1 ? atoi(argv[arg + 1]) : 2160;
    iterations = argc > arg + 2 ? atoi(argv[arg + 2]) : 4096;
    threads = verify ? (cpus > 0 ? (int) cpus : 1)
                     : (argc > arg + 3 ? atoi(argv[arg + 3]) : (cpus > 0 ? (int) cpus : 1));
    path = argc > arg + 4 ? argv[arg + 4] : "mandelbrot.png";

    if (width <= 0 || height <= 0 || iterations <= 0 || threads < 0) {
        fprintf(stderr,
            "usage: %s [--verify] [--queue batch] [width] [height] [iterations] [threads] [out.png]\n",
            argv[0]);
        return 2;
    }

    image_size = (size_t) width * (size_t) height * sizeof(*image);
    if (queue_batch) {
        /* the threads draw into one mapping, which a process could share too */
        shm = cat_shm_create_anonymous(image_size);
        if (shm == NULL) {
            fprintf(stderr, "shared image failed: %s\n", cat_get_last_error_message());
            return 1;
        }
        image = (uint32_t *) cat_shm_get_address(shm);
    } else {
        image = (uint32_t *) cat_sys_malloc(image_size);
        if (image == NULL) {
            fprintf(stderr, "out of memory for a %dx%d image\n", width, height);
            return 1;
        }
    }

    if (verify) {
        threads = cpus > 0 ? (int) cpus : 1;
        reference = (uint32_t *) cat_sys_malloc(image_size);
        if (reference == NULL) {
            fprintf(stderr, "out of memory for the reference image\n");
            if (shm != NULL) {
                cat_shm_detach(shm);
            } else {
                cat_sys_free(image);
            }
            return 1;
        }
    }

    if (!cat_init_all() || !cat_thread_register_main(&main_thread)) {
        fprintf(stderr, "runtime init failed: %s\n", cat_get_last_error_message());
        exit_code = 1;
        goto _exit;
    }
    if (!cat_run(CAT_RUN_EASY)) {
        fprintf(stderr, "run failed: %s\n", cat_get_last_error_message());
        exit_code = 1;
        goto _exit;
    }

    jobs = (struct mandel_job_s *) cat_sys_malloc((size_t) threads * sizeof(*jobs));
    if (jobs == NULL) {
        fprintf(stderr, "out of memory for the threads\n");
        exit_code = 1;
        goto _exit;
    }
    memset(jobs, 0, (size_t) threads * sizeof(*jobs));

    mandel_view_reset(&view, width, height);

    printf("mandelbrot: %dx%d, at most %d iterations, %d threads\n", width, height, iterations,
        threads);
    printf("mandelbrot: mode=%s batch=%d\n", queue_batch ? "queue" : "static", queue_batch);
    fflush(stdout);

    if (queue_batch) {
        cat_thannel_size_t capacity = (cat_thannel_size_t) (threads * queue_batch * 4);

        if (capacity < 8) {
            capacity = 8;
        }
        if (cat_thannel_create(&thannel, capacity) == NULL) {
            fprintf(stderr, "thannel failed: %s\n", cat_get_last_error_message());
            exit_code = 1;
            goto _exit;
        }
    }

    (void) clock_gettime(CLOCK_MONOTONIC, &started);

    for (int index = 0; index < threads; index++) {
        struct mandel_job_s *job = &jobs[index];
        int rows = height / threads;
        int extra = height % threads;

        job->image = image;
        job->width = width;
        job->height = height;
        job->iterations = iterations;
        job->view = view;
        job->batch = queue_batch;
        job->thannel = queue_batch ? &thannel : NULL;

        if (queue_batch) {
            if (!cat_thread_create(&job->thread, mandel_queue_routine, job)) {
                fprintf(stderr, "thread %d failed: %s\n", index, cat_get_last_error_message());
                exit_code = 1;
                break;
            }
        } else {
            job->row_begin = index * rows + (index < extra ? index : extra);
            job->row_end = job->row_begin + rows + (index < extra ? 1 : 0);

            if (!cat_thread_create(&job->thread, mandel_routine, job)) {
                fprintf(stderr, "thread %d failed: %s\n", index, cat_get_last_error_message());
                exit_code = 1;
                break;
            }
        }
        created++;
    }

    if (queue_batch) {
        /* eight rows per range keeps the ranges small next to the cost of one */
        if (!mandel_produce(&thannel, height, 8, queue_batch)) {
            fprintf(stderr, "produce failed: %s\n", cat_get_last_error_message());
            exit_code = 1;
        }
        if (!cat_thannel_close(&thannel)) {
            fprintf(stderr, "close failed: %s\n", cat_get_last_error_message());
            exit_code = 1;
        }
    }

    for (int index = 0; index < created; index++) {
        if (!cat_thread_join(&jobs[index].thread, CAT_TIMEOUT_FOREVER)) {
            fprintf(stderr, "join %d failed: %s\n", index, cat_get_last_error_message());
            exit_code = 1;
        }
    }

    (void) clock_gettime(CLOCK_MONOTONIC, &ended);
    elapsed = (double) (ended.tv_sec - started.tv_sec) +
              (double) (ended.tv_nsec - started.tv_nsec) / 1e9;

    printf("mandelbrot: drawn in %.3f s, digest %016llx\n", elapsed,
        (unsigned long long) mandel_digest(image, image_size));
    for (int index = 0; index < created; index++) {
        if (queue_batch) {
            printf("mandelbrot: worker %2d took %.3f s, %ld ranges\n", index,
                jobs[index].seconds, jobs[index].tasks);
        } else {
            printf("mandelbrot: worker %2d rows [%5d, %5d) took %.3f s\n", index,
                jobs[index].row_begin, jobs[index].row_end, jobs[index].seconds);
        }
    }

    /*
     * The share of a thread is a band of rows, and a pixel depends on nothing
     * but its own coordinates, so the image must not change with the number of
     * threads. This renders the same picture on one thread and compares.
     */
    if (verify) {
        mandel_render_rows(reference, width, height, &view, iterations, 0, height, NULL, 0);

        if (memcmp(reference, image, image_size) != 0) {
            uint64_t want = mandel_digest(reference, image_size);
            uint64_t got = mandel_digest(image, image_size);
            size_t offset = 0;

            while (offset < image_size && ((uint8_t *) reference)[offset] == ((uint8_t *) image)[offset]) {
                offset++;
            }

            printf("mandelbrot: the threads drew a different image, first byte %zu "
                   "(single %016llx, threaded %016llx)\n",
                offset, (unsigned long long) want, (unsigned long long) got);
            exit_code = 1;
        } else {
            printf("mandelbrot: one thread and %d threads drew the same image (ok)\n", threads);
        }
    }

    if (!png_write(path, image, width, height)) {
        exit_code = 1;
    } else {
        printf("mandelbrot: wrote %s\n", path);
    }

_exit:
    if (queue_batch) {
        cat_thannel_cleanup(&thannel);
    }
    if (shm != NULL) {
        cat_shm_detach(shm);
    } else {
        cat_sys_free(image);
    }
    cat_sys_free(jobs);
    cat_sys_free(reference);

    if (cat_globals_available()) {
        (void) cat_stop();
        (void) cat_runtime_shutdown_all();
        (void) cat_runtime_close_all();
    }

    return exit_code;
}
