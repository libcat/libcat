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
 * A PNG file for an XRGB image, written with zlib only.
 *
 * The container is a signature, an IHDR, one IDAT and an IEND, and the padding
 * byte of every pixel is dropped so the file holds plain RGB.
 */

#ifndef MANDEL_PNG_H_
#define MANDEL_PNG_H_

#include "cat.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zlib.h>

/* ------------------------------------------------------------------------- */
/* PNG output                                                                 */
/* ------------------------------------------------------------------------- */

static inline void png_put32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t) (value >> 24);
    out[1] = (uint8_t) (value >> 16);
    out[2] = (uint8_t) (value >> 8);
    out[3] = (uint8_t) value;
}

static inline int png_chunk(FILE *file, const char *type, const uint8_t *data, size_t size)
{
    uint8_t header[8];
    uint8_t trailer[4];
    uLong crc;

    png_put32(header, (uint32_t) size);
    memcpy(header + 4, type, 4);

    crc = crc32(0, header + 4, 4);
    if (size > 0) {
        crc = crc32(crc, data, (uInt) size);
    }
    png_put32(trailer, (uint32_t) crc);

    if (fwrite(header, 1, sizeof(header), file) != sizeof(header)) {
        return 0;
    }
    if (size > 0 && fwrite(data, 1, size, file) != size) {
        return 0;
    }

    return fwrite(trailer, 1, sizeof(trailer), file) == sizeof(trailer);
}

/* A PNG that holds 8 bit RGB pixels: signature, IHDR, one IDAT and IEND. The
 * image is XRGB, so the padding byte of every pixel is dropped here. */
static inline int png_write(const char *path, const uint32_t *image, int width, int height)
{
    const size_t stride = (size_t) width * 3;
    const size_t raw_size = (stride + 1) * (size_t) height;
    uint8_t *raw = NULL;
    uint8_t *packed = NULL;
    uLongf packed_size;
    FILE *file = NULL;
    int ok = 0;

    raw = (uint8_t *) cat_sys_malloc(raw_size);
    if (raw == NULL) {
        fprintf(stderr, "png: out of memory for the pixel filter\n");
        return 0;
    }

    /* every scanline carries a filter byte, and zero means no filter */
    for (int row = 0; row < height; row++) {
        uint8_t *line = raw + (stride + 1) * (size_t) row;
        const uint32_t *pixels = image + (size_t) row * (size_t) width;

        line[0] = 0;
        for (int column = 0; column < width; column++) {
            line[1 + (size_t) column * 3 + 0] = (uint8_t) (pixels[column] >> 16);
            line[1 + (size_t) column * 3 + 1] = (uint8_t) (pixels[column] >> 8);
            line[1 + (size_t) column * 3 + 2] = (uint8_t) pixels[column];
        }
    }

    packed_size = compressBound((uLong) raw_size);
    packed = (uint8_t *) cat_sys_malloc(packed_size);
    if (packed == NULL) {
        fprintf(stderr, "png: out of memory for the compressed image\n");
        goto _done;
    }
    if (compress2(packed, &packed_size, raw, (uLong) raw_size, 6) != Z_OK) {
        fprintf(stderr, "png: zlib refused the image\n");
        goto _done;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "png: could not open %s\n", path);
        goto _done;
    }

    if (fwrite("\x89PNG\r\n\x1a\n", 1, 8, file) != 8) {
        goto _done;
    }

    {
        uint8_t ihdr[13];

        png_put32(ihdr, (uint32_t) width);
        png_put32(ihdr + 4, (uint32_t) height);
        ihdr[8] = 8;   /* bits per channel */
        ihdr[9] = 2;   /* colour type 2: RGB */
        ihdr[10] = 0;  /* deflate */
        ihdr[11] = 0;  /* adaptive filtering */
        ihdr[12] = 0;  /* no interlace */

        if (!png_chunk(file, "IHDR", ihdr, sizeof(ihdr)) ||
            !png_chunk(file, "IDAT", packed, (size_t) packed_size) ||
            !png_chunk(file, "IEND", NULL, 0)) {
            fprintf(stderr, "png: could not write the chunks\n");
            goto _done;
        }
    }

    ok = 1;

_done:
    if (file != NULL && fclose(file) != 0) {
        ok = 0;
    }
    cat_sys_free(packed);
    cat_sys_free(raw);

    return ok;
}


#endif /* MANDEL_PNG_H_ */
