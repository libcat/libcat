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
 * The part the two front ends share: where the complex plane sits, what a pixel
 * looks like, and the loop that turns a band of rows into pixels.
 *
 * Pixels are 32 bit XRGB so a frame can go straight into a streaming texture,
 * and the command line front end unpacks them for its PNG.
 */

#ifndef MANDEL_H_
#define MANDEL_H_

#include "cat_atomic.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#if defined(__SIZEOF_FLOAT128__) && __SIZEOF_FLOAT128__ == 16
#include <quadmath.h>
#endif

/*
 * A double carries about sixteen digits, so a window narrower than 1e-13 of the
 * plane stops moving from one pixel to the next and the picture turns flat. The
 * iteration itself also loses its meaning at that depth, because the rounding of
 * z grows without bound near the boundary.
 *
 * The centre and the iterations are therefore carried in quad precision when the
 * view is deep, and in double when it is not. The pixel colour only needs the
 * escape value, which stays near four, so it is computed in double on both
 * paths. Quad arithmetic needs no library. Only the formatting of a quad number
 * does.
 */
#if defined(__SIZEOF_FLOAT128__) && __SIZEOF_FLOAT128__ == 16
#define MANDEL_HAVE_QUAD 1
typedef __float128 mandel_real_t;
#else
#define MANDEL_HAVE_QUAD 0
typedef double mandel_real_t;
#endif

/* Past this depth the centre no longer fits in a double, so the quad path takes
 * over. It is measured rather than guessed: a double view still shows structure
 * at 3e12 and is flat at 3e13. */
#define MANDEL_QUAD_DEPTH 1.0e12

struct mandel_view_s {
    /* the point in the middle of the window */
    mandel_real_t center_re;
    mandel_real_t center_im;
    /* how many pixels one unit of the plane covers */
    double scale;
};

/* The whole set with a little room around it, which is also the view the reset
 * key returns to. */
#define MANDEL_RESET_CENTER_RE (-0.7)
#define MANDEL_RESET_CENTER_IM 0.0
#define MANDEL_RESET_SCALE 300.0

static inline mandel_real_t mandel_real(double value)
{
#if MANDEL_HAVE_QUAD
    return (mandel_real_t) value;
#else
    return value;
#endif
}

static inline void mandel_view_reset(struct mandel_view_s *view, int width, int height)
{
    (void) width;

    view->center_re = mandel_real(MANDEL_RESET_CENTER_RE);
    view->center_im = mandel_real(MANDEL_RESET_CENTER_IM);
    /* the smaller side decides, so the set is not stretched by a wide window */
    view->scale = MANDEL_RESET_SCALE * ((double) height / 1080.0);
}

/* Moves the point under (x, y) to stay under it while the scale changes by
 * factor, which is what zooming under the pointer means. */
static inline void mandel_view_zoom_at(struct mandel_view_s *view, int width, int height, double x, double y,
                                double factor)
{
    mandel_real_t offset_x = ((mandel_real_t) x - (mandel_real_t) width * mandel_real(0.5)) /
                             (mandel_real_t) view->scale;
    mandel_real_t offset_y = ((mandel_real_t) y - (mandel_real_t) height * mandel_real(0.5)) /
                             (mandel_real_t) view->scale;
    mandel_real_t re = view->center_re + offset_x;
    mandel_real_t im = view->center_im + offset_y;

    view->scale *= factor;
    view->center_re = re - ((mandel_real_t) x - (mandel_real_t) width * mandel_real(0.5)) /
                            (mandel_real_t) view->scale;
    view->center_im = im - ((mandel_real_t) y - (mandel_real_t) height * mandel_real(0.5)) /
                            (mandel_real_t) view->scale;
}

/* How much closer the view is than the reset one, which is 1.0 at the start and
 * grows as the window closes in. */
static inline double mandel_view_depth(const struct mandel_view_s *view, int height)
{
    /*
     * scale is pixels per unit, so a larger scale means the window covers less of
     * the plane. The depth is how many times closer than the reset view.
     */
    return view->scale / (MANDEL_RESET_SCALE * ((double) height / 1080.0));
}

/* cat_true when the view is deep enough to need more than a double. */
static inline int mandel_view_needs_quad(const struct mandel_view_s *view, int height)
{
    return MANDEL_HAVE_QUAD && mandel_view_depth(view, height) > MANDEL_QUAD_DEPTH;
}

/* A deep view needs more steps to keep the boundary from turning into a blob, so
 * the limit follows the depth. */
static inline int mandel_iterations_for(const struct mandel_view_s *view, int height, int base)
{
    double depth = mandel_view_depth(view, height);

    if (depth <= 1.0) {
        return base;
    }

    return base + (int) (64.0 * (log(depth) / log(2.0)));
}

static inline uint32_t mandel_color(int iteration, int limit, double smooth)
{
    double phase;
    double red;
    double green;
    double blue;

    if (iteration >= limit || smooth < 0.0) {
        return 0;
    }

    /*
     * The escape time is smoothed by where the orbit left the disc, then turned
     * into a colour that cycles with it. Most of the plane escapes in a handful
     * of steps, so the square root spreads the small counts over the palette
     * instead of leaving one flat colour around the set.
     */
    phase = sqrt(smooth) * 0.05;
    red = 255.0 * (0.5 + 0.5 * cos(6.283185307179586 * (phase + 0.00)));
    green = 255.0 * (0.5 + 0.5 * cos(6.283185307179586 * (phase + 0.33)));
    blue = 255.0 * (0.5 + 0.5 * cos(6.283185307179586 * (phase + 0.67)));

    return ((uint32_t) red << 16) | ((uint32_t) green << 8) | (uint32_t) blue;
}

/*
 * Draws rows [row_begin, row_end) of the image in double.
 *
 * The generation lets a caller that changed its mind stop the band early: when
 * the value it reads no longer matches, nothing else of the band is drawn.
 */
static inline void mandel_render_rows_double(uint32_t *image, int width, int height,
                                      const struct mandel_view_s *view, int limit, int row_begin,
                                      int row_end, const cat_atomic_uint64_t *generation,
                                      uint64_t my_generation)
{
    double left = (double) view->center_re - ((double) width * 0.5) / view->scale;
    double top = (double) view->center_im - ((double) height * 0.5) / view->scale;
    double step = 1.0 / view->scale;

    for (int row = row_begin; row < row_end; row++) {
        double ci = top + (double) row * step;
        uint32_t *out = image + (size_t) row * (size_t) width;

        if ((row & 15) == 0 && generation != NULL &&
            cat_atomic_uint64_load((cat_atomic_uint64_t *) generation) != my_generation) {
            return;
        }

        for (int column = 0; column < width; column++) {
            double cr = left + (double) column * step;
            double zr = 0.0;
            double zi = 0.0;
            double zr2 = 0.0;
            double zi2 = 0.0;
            int iteration = 0;

            while (iteration < limit && zr2 + zi2 <= 4.0) {
                zi = 2.0 * zr * zi + ci;
                zr = zr2 - zi2 + cr;
                zr2 = zr * zr;
                zi2 = zi * zi;
                iteration++;
            }

            if (iteration >= limit) {
                out[column] = mandel_color(iteration, limit, -1.0);
            } else {
                double smooth = (double) iteration + 1.0 -
                                log(log(sqrt(zr2 + zi2))) / log(2.0);

                out[column] = mandel_color(iteration, limit, smooth);
            }
        }
    }
}

/* The same walk in quad precision, for a view whose centre no longer fits in a
 * double. The colour still comes from a double, because the escape value the
 * smooth part needs stays near four. */
#if MANDEL_HAVE_QUAD
static inline void mandel_render_rows_quad(uint32_t *image, int width, int height,
                                    const struct mandel_view_s *view, int limit, int row_begin,
                                    int row_end, const cat_atomic_uint64_t *generation,
                                    uint64_t my_generation)
{
    mandel_real_t half_width = (mandel_real_t) width * mandel_real(0.5);
    mandel_real_t half_height = (mandel_real_t) height * mandel_real(0.5);
    mandel_real_t scale = (mandel_real_t) view->scale;
    mandel_real_t left = view->center_re - half_width / scale;
    mandel_real_t top = view->center_im - half_height / scale;
    mandel_real_t step = mandel_real(1.0) / scale;
    const mandel_real_t four = mandel_real(4.0);

    for (int row = row_begin; row < row_end; row++) {
        mandel_real_t ci = top + (mandel_real_t) row * step;
        uint32_t *out = image + (size_t) row * (size_t) width;

        if ((row & 15) == 0 && generation != NULL &&
            cat_atomic_uint64_load((cat_atomic_uint64_t *) generation) != my_generation) {
            return;
        }

        for (int column = 0; column < width; column++) {
            mandel_real_t cr = left + (mandel_real_t) column * step;
            mandel_real_t zr = mandel_real(0.0);
            mandel_real_t zi = mandel_real(0.0);
            mandel_real_t zr2 = mandel_real(0.0);
            mandel_real_t zi2 = mandel_real(0.0);
            int iteration = 0;

            while (iteration < limit && zr2 + zi2 <= four) {
                zi = mandel_real(2.0) * zr * zi + ci;
                zr = zr2 - zi2 + cr;
                zr2 = zr * zr;
                zi2 = zi * zi;
                iteration++;
            }

            if (iteration >= limit) {
                out[column] = mandel_color(iteration, limit, -1.0);
            } else {
                /* the radius is near two here, so a double keeps every digit the
                 * colour can use */
                double radius = (double) (zr2 + zi2);
                double smooth = (double) iteration + 1.0 -
                                log(log(sqrt(radius))) / log(2.0);

                out[column] = mandel_color(iteration, limit, smooth);
            }
        }
    }
}
#endif

/* Picks the walk that the depth of this view needs. */
static inline void mandel_render_rows(uint32_t *image, int width, int height,
                               const struct mandel_view_s *view, int limit, int row_begin,
                               int row_end, const cat_atomic_uint64_t *generation,
                               uint64_t my_generation)
{
#if MANDEL_HAVE_QUAD
    if (mandel_view_needs_quad(view, height)) {
        mandel_render_rows_quad(image, width, height, view, limit, row_begin, row_end, generation,
            my_generation);
        return;
    }
#endif

    mandel_render_rows_double(image, width, height, view, limit, row_begin, row_end, generation,
        my_generation);
}

/* Writes the centre as a string, with enough digits for the depth it was zoomed
 * to. Only the formatting of a quad number needs the quadmath library. */
static inline void mandel_format_center(const struct mandel_view_s *view, char *left, size_t left_size,
                                 char *right, size_t right_size)
{
#if MANDEL_HAVE_QUAD
    quadmath_snprintf(left, left_size, "%.28Qe", view->center_re);
    quadmath_snprintf(right, right_size, "%.28Qe", view->center_im);
#else
    snprintf(left, left_size, "%.17e", (double) view->center_re);
    snprintf(right, right_size, "%.17e", (double) view->center_im);
#endif
}

/* A cheap digest of an image, so two runs can be compared by one number. */
static inline uint64_t mandel_digest(const void *image, size_t size)
{
    const uint8_t *bytes = (const uint8_t *) image;
    uint64_t digest = 1469598103934665603ull;

    for (size_t index = 0; index < size; index++) {
        digest = (digest ^ bytes[index]) * 1099511628211ull;
    }

    return digest;
}

#endif /* MANDEL_H_ */
