/* Based on node-formidable by Felix Geisendörfer
 * Igor Afonov - afonov@gmail.com - 2012
 * MIT License - http://www.opensource.org/licenses/mit-license.php
 */
#ifndef _multipart_parser_h
#define _multipart_parser_h

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdlib.h>
#include <ctype.h>

typedef struct multipart_parser multipart_parser;
typedef struct multipart_parser_settings multipart_parser_settings;
typedef struct multipart_parser_state multipart_parser_state;

enum multipart_cb_ret
{
    MPPE_OK = 0,
    MPPE_PAUSED,
};

/*
* calloc function used in multipart_parser_alloc
*/
typedef void* (*multipart_calloc_func)(size_t size, size_t count);
/*
* free function used in multipart_parser_free
*/
typedef void (*multipart_free_func)(void *ptr);

/*
* data callback called at events, return MPE_OK to continue parser,
* MPE_PAUSED for making parser paused
*/
typedef long (*multipart_data_cb) (multipart_parser*, const char *at, size_t length);
/*
* notification callback called at events, return MPE_OK to continue parser,
* MPE_PAUSED for making parser paused
*/
typedef long (*multipart_notify_cb) (multipart_parser*);

// from RFC2046
#define BOUNDARY_MAX_LEN 70

struct multipart_parser {
    const multipart_parser_settings* settings;

    size_t i;
    size_t index;
    unsigned char boundary_length;

    unsigned char state;

    char* lookbehind;
    char multipart_boundary[(2 + BOUNDARY_MAX_LEN)*2 + 9];
};

struct multipart_parser_settings {
    multipart_calloc_func calloc;
    multipart_free_func free;

    multipart_data_cb on_header_field;
    multipart_data_cb on_header_value;
    multipart_data_cb on_part_data;

    multipart_notify_cb on_part_data_begin;
    multipart_notify_cb on_headers_complete;
    multipart_notify_cb on_part_data_end;
    multipart_notify_cb on_body_end;
};

/*
*   initialize multipart parser
*   @param mp pointer to mp struct
*   @param boundary boundary string
*   @param boundary_length boundary length in bytes, max length is 70
*   @param settings settings structure used, if it setted to NULL, keep current
*   @return 0 for success, -1 for failed, errno is setted
*/
int multipart_parser_init(
    multipart_parser *mp,
    const char *boundary,
    size_t boundary_length,
    const multipart_parser_settings* settings);
/*
*   allocate multipart parser
*   @param boundary boundary string
*   @param boundary_length boundary length in bytes, max length is 70
*   @param settings settings structure used
*   @return pointer to allocated memory for success, NULL for failed, errno is setted
*/
multipart_parser* multipart_parser_alloc(
    const char *boundary,
    size_t boundary_length,
    const multipart_parser_settings* settings);

/*
*   free multipart parser allocated by multipart_parser_alloc
*   @param p multipart parser to be free'd
*/
void multipart_parser_free(multipart_parser* p);

/*
*   execute data buf with size len
*   @param p multipart parser to be used
*   @param buf buffer for consuming
*   @param len buffer size
*   @return parsed length in bytes, SIZE_MAX for error
*/
size_t multipart_parser_execute(multipart_parser* p, const char *buf, size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
