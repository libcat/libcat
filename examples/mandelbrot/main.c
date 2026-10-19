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

int main(int argc, char **argv)
{
    static cat_thread_t main_thread;
    int verify = argc > 1 && strcmp(argv[1], "--verify") == 0;
    int base = verify ? 2 : 1;
    int width = argc > base ? atoi(argv[base]) : 3840;
    int height = argc > base + 1 ? atoi(argv[base + 1]) : 2160;
    int iterations = argc > base + 2 ? atoi(argv[base + 2]) : 4096;
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    int threads = verify ? 0 : (argc > base + 3 ? atoi(argv[base + 3])
                                                : (cpus > 0 ? (int) cpus : 1));
    const char *path = argc > base + 4 ? argv[base + 4] : "mandelbrot.png";
    struct mandel_view_s view;
    size_t image_size;
    uint32_t *image = NULL;
    uint32_t *reference = NULL;
    struct mandel_job_s *jobs = NULL;
    struct timespec started;
    struct timespec ended;
    double elapsed;
    int created = 0;
    int exit_code = 0;

    if (width <= 0 || height <= 0 || iterations <= 0 || threads < 0) {
        fprintf(stderr, "usage: %s [--verify] [width] [height] [iterations] [threads] [out.png]\n",
            argv[0]);
        return 2;
    }

    image_size = (size_t) width * (size_t) height * sizeof(*image);
    image = (uint32_t *) cat_sys_malloc(image_size);
    if (image == NULL) {
        fprintf(stderr, "out of memory for a %dx%d image\n", width, height);
        return 1;
    }

    if (verify) {
        threads = cpus > 0 ? (int) cpus : 1;
        reference = (uint32_t *) cat_sys_malloc(image_size);
        if (reference == NULL) {
            fprintf(stderr, "out of memory for the reference image\n");
            cat_sys_free(image);
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
    fflush(stdout);

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
        job->row_begin = index * rows + (index < extra ? index : extra);
        job->row_end = job->row_begin + rows + (index < extra ? 1 : 0);

        if (!cat_thread_create(&job->thread, mandel_routine, job)) {
            fprintf(stderr, "thread %d failed: %s\n", index, cat_get_last_error_message());
            exit_code = 1;
            break;
        }
        created++;
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
        printf("mandelbrot: worker %2d rows [%5d, %5d) took %.3f s\n", index,
            jobs[index].row_begin, jobs[index].row_end, jobs[index].seconds);
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
    cat_sys_free(jobs);
    cat_sys_free(reference);
    cat_sys_free(image);

    if (cat_globals_available()) {
        (void) cat_stop();
        (void) cat_runtime_shutdown_all();
        (void) cat_runtime_close_all();
    }

    return exit_code;
}
