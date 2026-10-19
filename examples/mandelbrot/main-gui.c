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
 * The Mandelbrot set in a window, drawn by the threads of this library, zoomable
 * without limit.
 *
 * Wheel zooms under the pointer, drag moves the plane, R returns to the whole set
 * and Esc closes the window.
 *
 * A pool of worker threads draws a frame as a band of rows for each job. The pool
 * lives for the whole run, so a frame does not create threads. When the view
 * changes during a frame, the generation of that frame moves on and the workers
 * stop at their next row boundary.
 *
 * The window and the coroutine that hands out the work belong to the main thread.
 * The event loop polls and yields with cat_time_delay between polls, which lets
 * the wakeups of the workers arrive.
 *
 * Precision follows the depth. To about 1e12 the centre and the iteration run in
 * double. Past that the centre and the iteration switch to quad precision, which
 * reaches about 1e30 and costs about seventy times a double.
 *
 * Without a display, set SDL_VIDEODRIVER=dummy and pass --frames.
 *
 * Usage:
 *   mandelbrot-gui [--frames N] [--walk] [--save out.png] [--dump out.png]
 *                  [--size WxH] [--threads N] [--iterations N]
 *                  [--at RE,IM] [--depth N] [--walk-to DEPTH]
 *
 * --save writes the picture of the threads and --dump writes what the window
 * shows, text included. With --frames and no input, the run draws the same view
 * again and again. --walk closes in on the boundary by itself instead.
 */

#include "cat.h"
#include "cat_api.h"
#include "cat_thread.h"

#include "mandel.h"
#include "png.h"
#include "pool.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */
/* What the window writes over the picture                                    */
/* ------------------------------------------------------------------------- */

/*
 * Zoom, centre and the rest go in the top left corner, drawn with the font that
 * SDL carries, so the window needs no font file.
 */
static void gui_hud(SDL_Renderer *renderer, const struct mandel_view_s *view, int height,
                    int iterations, double fps)
{
    char center_re[96];
    char center_im[96];
    const char *precision = mandel_view_needs_quad(view, height) ? "quad" : "double";
    const float top = 10.0f;
    const float line_height = 12.0f;
    const SDL_FRect box = { 8.0f, 8.0f, 420.0f, 46.0f };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
    (void) SDL_RenderFillRect(renderer, &box);

    SDL_SetRenderDrawColor(renderer, 235, 235, 235, 255);
    (void) SDL_RenderDebugTextFormat(renderer, 14.0f, top,
        "zoom %.6g x    iterations %d    %.1f fps", mandel_view_depth(view, height), iterations,
        fps);
    mandel_format_center(view, center_re, sizeof(center_re), center_im, sizeof(center_im));
    (void) SDL_RenderDebugTextFormat(renderer, 14.0f, top + line_height,
        "center %s %s (%s)", center_re, center_im, precision);
    (void) SDL_RenderDebugTextFormat(renderer, 14.0f, top + line_height * 2.0f,
        "wheel zooms at the pointer, drag moves, R resets, Esc quits");
}

/* ------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    static cat_thread_t main_thread;
    int frames = -1;
    const char *save = NULL;
    const char *dump = NULL;
    int walk = 0;
    int width = 1280;
    int height = 720;
    int iterations = 1024;
    double start_re = 0.0;
    double start_im = 0.0;
    double start_depth = 0.0;
    double walk_to = 0.0;
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    int threads = cpus > 0 ? (int) cpus : 1;
    struct mandel_view_s view;
    uint32_t *image = NULL;
    struct mandel_pool_s pool;
    cat_atomic_uint64_t generation;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    uint64_t frame_generation = 0;
    /* one wheel notch closes the window in by this much */
    const double zoom_in = 1.25;
    int running = 1;
    int exit_code = 0;
    int drawn_frames = 0;
    int frame_iterations = iterations;
    Uint64 last_frame_ticks = 0;

    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--frames") == 0 && index + 1 < argc) {
            frames = atoi(argv[++index]);
        } else if (strcmp(argv[index], "--save") == 0 && index + 1 < argc) {
            save = argv[++index];
        } else if (strcmp(argv[index], "--dump") == 0 && index + 1 < argc) {
            dump = argv[++index];
        } else if (strcmp(argv[index], "--size") == 0 && index + 1 < argc) {
            if (sscanf(argv[++index], "%dx%d", &width, &height) != 2) {
                fprintf(stderr, "--size wants WIDTHxHEIGHT\n");
                return 2;
            }
        } else if (strcmp(argv[index], "--threads") == 0 && index + 1 < argc) {
            threads = atoi(argv[++index]);
        } else if (strcmp(argv[index], "--iterations") == 0 && index + 1 < argc) {
            iterations = atoi(argv[++index]);
        } else if (strcmp(argv[index], "--at") == 0 && index + 1 < argc) {
            /* a deep view needs a centre with more digits than a double holds,
             * so the text is read as a quad directly */
            const char *text = argv[++index];
            const char *comma = strchr(text, ',');

            if (comma == NULL) {
                fprintf(stderr, "--at wants RE,IM\n");
                return 2;
            }
            start_re = strtod(text, NULL);
            start_im = strtod(comma + 1, NULL);
        } else if (strcmp(argv[index], "--depth") == 0 && index + 1 < argc) {
            start_depth = atof(argv[++index]);
        } else if (strcmp(argv[index], "--walk") == 0) {
            walk = 1;
        } else if (strcmp(argv[index], "--walk-to") == 0 && index + 1 < argc) {
            walk_to = atof(argv[++index]);
        } else {
            fprintf(stderr,
                "usage: %s [--frames N] [--save out.png] [--size WxH] [--threads N] "
                "[--iterations N]\n", argv[0]);
            return 2;
        }
    }

    if (width <= 0 || height <= 0 || threads <= 0 || iterations <= 0) {
        fprintf(stderr, "the size, the threads and the iterations must be positive\n");
        return 2;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "sdl init failed: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("mandelbrot", width, height, SDL_WINDOW_RESIZABLE);
    if (window == NULL) {
        fprintf(stderr, "sdl window failed: %s\n", SDL_GetError());
        exit_code = 1;
        goto _exit;
    }

    renderer = SDL_CreateRenderer(window, NULL);
    if (renderer == NULL) {
        fprintf(stderr, "sdl renderer failed: %s\n", SDL_GetError());
        exit_code = 1;
        goto _exit;
    }
    (void) SDL_SetRenderVSync(renderer, 1);

    image = (uint32_t *) cat_sys_malloc((size_t) width * (size_t) height * sizeof(*image));
    if (image == NULL) {
        fprintf(stderr, "out of memory for %dx%d\n", width, height);
        exit_code = 1;
        goto _exit;
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
        width, height);
    if (texture == NULL) {
        fprintf(stderr, "sdl texture failed: %s\n", SDL_GetError());
        exit_code = 1;
        goto _exit;
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

    cat_atomic_uint64_init(&generation, 0);
    mandel_view_reset(&view, width, height);
    if (start_re != 0.0 || start_im != 0.0) {
        view.center_re = mandel_real(start_re);
        view.center_im = mandel_real(start_im);
    }
    if (start_depth > 0.0) {
        /* a view centred on the given point, closed in by that factor */
        view.scale *= start_depth;
    }
    if (walk_to > 0.0) {
        /*
         * Closing in without drawing the frames on the way. The centre keeps its
         * quad precision through every step, which is what makes a view this
         * deep meaningful at all.
         */
        int steps = 0;

        while (mandel_view_depth(&view, height) < walk_to && steps < 100000) {
            mandel_view_zoom_at(&view, width, height, (double) width * 0.5,
                (double) height * 0.5, 1.05);
            steps++;
        }
        printf("mandelbrot-gui: closed in %d steps to depth %.3e\n", steps,
            mandel_view_depth(&view, height));
    }

    /* the pool outlives every frame, which is the whole point of it */
    if (!mandel_pool_init(&pool, threads)) {
        fprintf(stderr, "pool init failed\n");
        exit_code = 1;
        goto _exit;
    }

    printf("mandelbrot-gui: %dx%d, pool of %d, video driver %s\n", width, height, threads,
        SDL_GetCurrentVideoDriver());
    fflush(stdout);

    while (running) {
        const int jobs_per_frame = threads * 4;
        int pushed = 0;
        int stale = 0;

        /* the frame about to be drawn belongs to this generation */
        frame_generation = cat_atomic_uint64_fetch_add(&generation, 1) + 1;
        frame_iterations = mandel_iterations_for(&view, height, iterations);
        mandel_pool_reset_finished(&pool);

        for (int index = 0; index < jobs_per_frame; index++) {
            struct mandel_pool_job_s job;
            int rows = height / jobs_per_frame;
            int extra = height % jobs_per_frame;

            memset(&job, 0, sizeof(job));
            job.image = image;
            job.width = width;
            job.height = height;
            job.iterations = frame_iterations;
            job.view = view;
            job.row_begin = index * rows + (index < extra ? index : extra);
            job.row_end = job.row_begin + rows + (index < extra ? 1 : 0);
            job.generation = &generation;
            job.my_generation = frame_generation;

            if (!mandel_pool_push(&pool, &job)) {
                fprintf(stderr, "pool is full\n");
                running = 0;
                break;
            }
            pushed++;
        }

        /*
         * The window stays alive while the frame is drawn. The yield lets the
         * wakeups of the workers arrive, and a view change in the middle marks
         * the jobs already handed out as out of date.
         */
        while (running && mandel_pool_finished(&pool) < pushed) {
            SDL_Event event;

            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    running = 0;
                } else if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_Q) {
                        running = 0;
                    } else if (event.key.key == SDLK_R) {
                        mandel_view_reset(&view, width, height);
                        (void) cat_atomic_uint64_fetch_add(&generation, 1);
                        stale = 1;
                    }
                } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    double factor = event.wheel.y > 0 ? zoom_in : 1.0 / zoom_in;

                    mandel_view_zoom_at(&view, width, height, event.wheel.mouse_x,
                        event.wheel.mouse_y, factor);
                    (void) cat_atomic_uint64_fetch_add(&generation, 1);
                    stale = 1;
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                           event.button.button == SDL_BUTTON_LEFT) {
                    SDL_CaptureMouse(true);
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                           event.button.button == SDL_BUTTON_LEFT) {
                    SDL_CaptureMouse(false);
                } else if (event.type == SDL_EVENT_MOUSE_MOTION &&
                           (event.motion.state & SDL_BUTTON_LMASK) != 0) {
                    view.center_re -= (double) event.motion.xrel / view.scale;
                    view.center_im -= (double) event.motion.yrel / view.scale;
                    (void) cat_atomic_uint64_fetch_add(&generation, 1);
                    stale = 1;
                }
            }

            (void) cat_time_delay(1);
        }

        /* Abandoned jobs bail at their next row, so this wait is short, but it
         * has to happen before the buffer is drawn into again. */
        if (!running || stale) {
            continue;
        }

        (void) SDL_UpdateTexture(texture, NULL, image, width * (int) sizeof(*image));
        (void) SDL_RenderClear(renderer);
        (void) SDL_RenderTexture(renderer, texture, NULL, NULL);

        {
            Uint64 now = SDL_GetTicks();
            double fps = 0.0;

            if (last_frame_ticks != 0 && now > last_frame_ticks) {
                fps = 1000.0 / (double) (now - last_frame_ticks);
            }
            last_frame_ticks = now;
            gui_hud(renderer, &view, height, frame_iterations, fps);
        }

        SDL_RenderPresent(renderer);

        if (dump != NULL && frames > 0 && drawn_frames + 1 >= frames) {
            SDL_Surface *shown = SDL_RenderReadPixels(renderer, NULL);
            SDL_Surface *plain = NULL;

            if (shown != NULL) {
                plain = SDL_ConvertSurface(shown, SDL_PIXELFORMAT_XRGB8888);
            }
            if (plain != NULL) {
                uint32_t *copy = (uint32_t *) cat_sys_malloc(
                    (size_t) width * (size_t) height * sizeof(*copy));

                if (copy != NULL) {
                    for (int row = 0; row < height; row++) {
                        memcpy(copy + (size_t) row * (size_t) width,
                            (const uint8_t *) plain->pixels + (size_t) row * (size_t) plain->pitch,
                            (size_t) width * sizeof(*copy));
                    }
                    printf("mandelbrot-gui: %s %s\n",
                        png_write(dump, copy, width, height) ? "dumped" : "failed to dump", dump);
                    cat_sys_free(copy);
                }
                SDL_DestroySurface(plain);
            }
            if (shown != NULL) {
                SDL_DestroySurface(shown);
            }
        }

        drawn_frames++;

        if (frames > 0 && drawn_frames >= frames) {
            break;
        }
        if (frames > 0 && walk) {
            /*
             * A headless run receives no input, so with --walk it walks into the
             * set by itself. The point it holds is the one where the main cardioid
             * meets the period two bulb, which is on the boundary at every
             * scale, because the middle of the reset view is inside the set and
             * stays black.
             */
            double re = -0.75;
            double im = 0.0;
            double x = (re - view.center_re) * view.scale + (double) width * 0.5;
            double y = (im - view.center_im) * view.scale + (double) height * 0.5;

            mandel_view_zoom_at(&view, width, height, x, y, 1.05);
        }
    }

    printf("mandelbrot-gui: drew %d frames, digest %016llx\n", drawn_frames,
        (unsigned long long) mandel_digest(image, (size_t) width * (size_t) height * sizeof(*image)));

    if (save != NULL) {
        printf("mandelbrot-gui: %s %s\n", png_write(save, image, width, height) ? "wrote" : "failed",
            save);
    }

_exit:
    if (pool.thread_count > 0) {
        mandel_pool_shutdown(&pool);
    }
    cat_sys_free(image);
    if (texture != NULL) {
        SDL_DestroyTexture(texture);
    }
    if (renderer != NULL) {
        SDL_DestroyRenderer(renderer);
    }
    if (window != NULL) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();

    if (cat_globals_available()) {
        (void) cat_stop();
        (void) cat_runtime_shutdown_all();
        (void) cat_runtime_close_all();
    }

    return exit_code;
}
