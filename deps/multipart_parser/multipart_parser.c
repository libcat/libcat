/* Based on node-formidable by Felix Geisendörfer
 * Igor Afonov - afonov@gmail.com - 2012
 * MIT License - http://www.opensource.org/licenses/mit-license.php
 */

#include "multipart_parser.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#ifdef DEBUG_MULTIPART
//#ifdef _DEBUG
#define multipart_log(format, ...) do {\
    fprintf(stderr, "[HTTP_MULTIPART_PARSER] %s:%d: " format "\n", __FILE__, __LINE__, __VA_ARGS__); \
} while(0)
#else
#define multipart_log(format, ...)
#endif

#define NOTIFY_CB(FOR, ni)                                             \
do {                                                                   \
  if (p->settings->on_##FOR) {                                         \
    if ((ret = p->settings->on_##FOR(p)) == MPPE_OK) {                 \
        /* do nothing */                                               \
    } else if (ret == MPPE_PAUSED) {                                   \
        if (mark == 0) {                                               \
            p->i = ni;                                                 \
            return i;                                                  \
        }else {                                                        \
            p->i = i-mark+ni;                                          \
            return mark;                                               \
        }                                                              \
    } else {                                                           \
        return SIZE_MAX;                                               \
    }                                                                  \
  }                                                                    \
} while (0)

#define EMIT_DATA_CB(FOR, ptr, len)                                    \
do {                                                                   \
  if (p->settings->on_##FOR) {                                         \
    if ((ret = p->settings->on_##FOR(p, ptr, len)) == MPPE_OK) {       \
        /* do nothing */                                               \
    } else if (ret == MPPE_PAUSED) {                                   \
        p->i = 1;                                                      \
        return i;                                                      \
    } else {                                                           \
        return SIZE_MAX;                                               \
    }                                                                  \
  }                                                                    \
} while (0)

#define LF 10
#define CR 13

enum state
{
    s_uninitialized = 1,
    s_start,
    s_start_boundary,
    s_header_field_start,
    s_header_field,
    s_headers_almost_done,
    s_header_value_start,
    s_header_value,
    s_header_value_almost_done,
    s_part_data_start,
    s_part_data,
    s_part_data_almost_boundary,
    s_part_data_boundary,
    s_part_data_almost_end,
    s_part_data_end,
    s_part_data_final_hyphen,
    s_end
};

int multipart_parser_init(multipart_parser *mp, const char *boundary, size_t boundary_length, const multipart_parser_settings* settings)
{
    if (boundary_length < 1 || boundary_length > BOUNDARY_MAX_LEN) {
        errno = EINVAL;
        return -1;
    }
    if (boundary != NULL && boundary_length != 0) {
        memcpy(mp->multipart_boundary, "--", 2);
        memcpy(mp->multipart_boundary + 2, boundary, boundary_length);
        mp->boundary_length = (unsigned char)(boundary_length + 2);
        // set \0
        memset(&mp->multipart_boundary[2 + boundary_length], 0, sizeof(mp->multipart_boundary) - 2 - boundary_length);
    }
    mp->lookbehind = (mp->multipart_boundary + mp->boundary_length + 1);
    mp->i = 0;
    mp->index = 0;
    mp->state = s_start;
    if (settings != NULL) {
        mp->settings = settings;
    }

    return 0;
}

multipart_parser* multipart_parser_alloc(const char *boundary, size_t boundary_length, const multipart_parser_settings* settings)
{
    multipart_parser* p;
    multipart_calloc_func calloc_func = settings->calloc;
    multipart_free_func free_func = settings->free;

    if (calloc_func == NULL) {
        calloc_func = calloc;
    }
    if (free_func == NULL) {
        free_func = free;
    }

    p = calloc_func(sizeof(multipart_parser) + boundary_length + boundary_length + 9 + 4, sizeof(char));
    if (NULL == p) {
        // ENOMEM
        return p;
    }

    if (multipart_parser_init(p, boundary, boundary_length, settings) != 0) {
        free_func(p);
        return NULL;
    }

    return p;
}


void multipart_parser_free(multipart_parser* p)
{
    multipart_free_func free_func = p->settings->free;

    if (free_func == NULL) {
        free_func = free;
    }
    free_func(p);
}

size_t multipart_parser_execute(multipart_parser* p, const char *buf, size_t len)
{
    size_t i = p->i;
    size_t mark = 0;
    char c, cl;
    int is_last = 0;
    int ret;

    while (i < len) {
        c = buf[i];
        is_last = (i == (len - 1));
        switch (p->state) {
            case s_start:
                multipart_log("s_start");
                p->index = 0;
                p->state = s_start_boundary;
                /* fallthrough */
            case s_start_boundary:
                multipart_log("s_start_boundary");
                if (p->index == p->boundary_length) {
                    if (c != CR) {
                        return i;
                    }
                    p->index++;
                    break;
                } else if (p->index == (p->boundary_length + 1)) {
                    if (c != LF) {
                        return i;
                    }
                    p->index = 0;
                    p->state = s_header_field_start;
                    NOTIFY_CB(part_data_begin, 1);
                    break;
                }
                if (c != p->multipart_boundary[p->index]) {
                    return i;
                }
                p->index++;
                break;
            case s_header_field_start:
                multipart_log("s_header_field_start");
                mark = i;
                p->state = s_header_field;
                /* fallthrough */
            case s_header_field:
                multipart_log("s_header_field");
                if (c == CR) {
                    p->state = s_headers_almost_done;
                    break;
                }
                if (c == '-') {
                    break;
                }
                if (c == ':') {
                    p->state = s_header_value_start;
                    EMIT_DATA_CB(header_field, buf + mark, i - mark);
                    break;
                }
                cl = c | 0x20;
                if (cl < 'a' || cl > 'z') {
                    multipart_log("invalid character in header name");
                    return i;
                }
                if (is_last)
                    EMIT_DATA_CB(header_field, buf + mark, (i - mark) + 1);
                break;
            case s_headers_almost_done:
                multipart_log("s_headers_almost_done");
                if (c != LF) {
                    return i;
                }
                p->state = s_part_data_start;
                break;
            case s_header_value_start:
                multipart_log("s_header_value_start");
                if (c == ' ') {
                    break;
                }
                mark = i;
                p->state = s_header_value;
                /* fallthrough */
            case s_header_value:
                multipart_log("s_header_value");
                if (c == CR) {
                    p->state = s_header_value_almost_done;
                    EMIT_DATA_CB(header_value, buf + mark, i - mark);
                }
                if (is_last)
                    EMIT_DATA_CB(header_value, buf + mark, (i - mark) + 1);
                break;
            case s_header_value_almost_done:
                multipart_log("s_header_value_almost_done");
                if (c != LF) {
                    return i;
                }
                p->state = s_header_field_start;
                break;
            case s_part_data_start:
                multipart_log("s_part_data_start");
                mark = i;
                p->state = s_part_data;
                NOTIFY_CB(headers_complete, 0);
                /* fallthrough */
            case s_part_data:
                multipart_log("s_part_data");
                if (c == CR) {
                    p->state = s_part_data_almost_boundary;
                    p->lookbehind[0] = CR;
                    EMIT_DATA_CB(part_data, buf + mark, i - mark);
                    mark = i;
                    break;
                }
                if (is_last)
                    EMIT_DATA_CB(part_data, buf + mark, (i - mark) + 1);
                break;
            case s_part_data_almost_boundary:
                multipart_log("s_part_data_almost_boundary");
                if (c == LF) {
                    p->state = s_part_data_boundary;
                    p->lookbehind[1] = LF;
                    p->index = 0;
                    break;
                }
                p->state = s_part_data;
                mark = i--;
                EMIT_DATA_CB(part_data, p->lookbehind, 1);
                break;
            case s_part_data_boundary:
                multipart_log("s_part_data_boundary");
                if (p->multipart_boundary[p->index] != c) {
                    p->state = s_part_data;
                    mark = i--;
                    EMIT_DATA_CB(part_data, p->lookbehind, 2 + p->index);
                    break;
                }
                p->lookbehind[2 + p->index] = c;
                if ((++p->index) == p->boundary_length) {
                    p->state = s_part_data_almost_end;
                    NOTIFY_CB(part_data_end, 1);
                }
                break;
            case s_part_data_almost_end:
                multipart_log("s_part_data_almost_end");
                if (c == '-') {
                    p->state = s_part_data_final_hyphen;
                    break;
                }
                if (c == CR) {
                    p->state = s_part_data_end;
                    break;
                }
                return i;
            case s_part_data_final_hyphen:
                multipart_log("s_part_data_final_hyphen");
                if (c == '-') {
                    NOTIFY_CB(body_end, 1);
                    p->state = s_end;
                    break;
                }
                return i;
            case s_part_data_end:
                multipart_log("s_part_data_end");
                if (c == LF) {
                    p->state = s_header_field_start;
                    NOTIFY_CB(part_data_begin, 1);
                    break;
                }
                return i;
            case s_end:
                multipart_log("s_end: %02X", (int) c);
                break;
            default:
                multipart_log("Multipart parser unrecoverable error");
                return SIZE_MAX;
        }
        ++i;
    }
    return i;
}
