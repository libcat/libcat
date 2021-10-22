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
  | Author: Twosee <twosee@php.net>                                          |
  +--------------------------------------------------------------------------+
 */

#include "cat_http.h"

CAT_API const char *cat_http_method_get_name(cat_http_method_t method)
{
  switch (method) {
#define CAT_HTTP_METHOD_NAME_GEN(_, name, string) case CAT_HTTP_METHOD_##name: return #string;
      CAT_HTTP_METHOD_MAP(CAT_HTTP_METHOD_NAME_GEN)
#undef CAT_HTTP_METHOD_NAME_GEN
  }
  return "UNKNOWN";
}

CAT_API const char *cat_http_status_get_reason(cat_http_status_code_t status)
{
    switch (status) {
#define CAT_HTTP_STATUS_GEN(_, code, reason) case code: return reason;
    CAT_HTTP_STATUS_MAP(CAT_HTTP_STATUS_GEN)
#undef CAT_HTTP_STATUS_GEN
    }
    return "UNKNOWN";
}

/* multipart parser things */

enum cat_multipart_state
{
    CAT_HTTP_MULTIPART_UNINIT = 0,
    CAT_HTTP_MULTIPART_IN_CONTENT_TYPE,
    CAT_HTTP_MULTIPART_TYPE_IS_MULTIPART,
    CAT_HTTP_MULTIPART_ALMOST_BOUNDARY,
    CAT_HTTP_MULTIPART_BOUNDARY,
    CAT_HTTP_MULTIPART_BOUNDARY_START,
    CAT_HTTP_MULTIPART_BOUNDARY_COMMON,
    CAT_HTTP_MULTIPART_BOUNDARY_QUOTED,
    CAT_HTTP_MULTIPART_BOUNDARY_END,
    CAT_HTTP_MULTIPART_OUT_CONTENT_TYPE,
    CAT_HTTP_MULTIPART_NOT_MULTIPART,
    CAT_HTTP_MULTIPART_BOUNDARY_OK,
    CAT_HTTP_MULTIPART_IN_BODY,
    CAT_HTTP_MULTIPART_BODY_END,
};

#define CAT_HTTP_MULTIPART_CB_FNAME(name) cat_http_multipart_parser_on_##name
#define CAT_HTTP_MULTIPART_ON_DATA(name, NAME) \
static long CAT_HTTP_MULTIPART_CB_FNAME(name)(multipart_parser *p, const char *at, size_t length){ \
    cat_http_parser_t* parser = cat_container_of(p, cat_http_parser_t, multipart); \
    parser->event = CAT_HTTP_PARSER_EVENT_##NAME; \
    parser->data = at; \
    parser->data_length = length; \
    CAT_LOG_DEBUG_V2(PROTOCOL, "http multipart parser data on_" # name ": [%zu]%.*s", length, (int)length, at); \
    if (((cat_http_parser_event_t) (parser->events & parser->event)) == parser->event) { \
        return MPPE_PAUSED; \
    } \
    return MPPE_OK; \
}

#define CAT_HTTP_MULTIPART_ON_EVENT(name, NAME) \
static long CAT_HTTP_MULTIPART_CB_FNAME(name)(multipart_parser *p){ \
    cat_http_parser_t* parser = cat_container_of(p, cat_http_parser_t, multipart); \
    parser->event = CAT_HTTP_PARSER_EVENT_##NAME; \
    CAT_LOG_DEBUG_V2(PROTOCOL, "http multipart parser notify on_" # name ); \
    if (((cat_http_parser_event_t) (parser->events & parser->event)) == parser->event) { \
        return MPPE_PAUSED; \
    } \
    return MPPE_OK; \
}

CAT_HTTP_MULTIPART_ON_DATA(header_field, MULTIPART_HEADER_FIELD)
CAT_HTTP_MULTIPART_ON_DATA(header_value, MULTIPART_HEADER_VALUE)
CAT_HTTP_MULTIPART_ON_DATA(part_data, MULTIPART_DATA)
CAT_HTTP_MULTIPART_ON_EVENT(part_data_begin, MULTIPART_DATA_BEGIN)
CAT_HTTP_MULTIPART_ON_EVENT(headers_complete, MULTIPART_HEADERS_COMPLETE)
CAT_HTTP_MULTIPART_ON_EVENT(part_data_end, MULTIPART_DATA_END)

static long CAT_HTTP_MULTIPART_CB_FNAME(body_end)(multipart_parser *p) {
    cat_http_parser_t* parser = cat_container_of(p, cat_http_parser_t, multipart);
    CAT_ASSERT(!(parser->events & CAT_HTTP_PARSER_EVENT_FLAG_MULTIPART) || parser->multipart_state == CAT_HTTP_MULTIPART_IN_BODY);
    // escape mp parser
    parser->event = _CAT_HTTP_PARSER_EVENT_MULTIPART_BODY_END;
    parser->multipart_state = CAT_HTTP_MULTIPART_NOT_MULTIPART;
    CAT_LOG_DEBUG_V2(PROTOCOL, "http multipart parser data on_body_end");
    return MPPE_OK;
}

const multipart_parser_settings cat_http_multipart_parser_settings = {
    /* .calloc = */ NULL,
    /* .free = */ NULL,
    /* .on_header_field = */ CAT_HTTP_MULTIPART_CB_FNAME(header_field),
    /* .on_header_value = */ CAT_HTTP_MULTIPART_CB_FNAME(header_value),
    /* .on_part_data = */ CAT_HTTP_MULTIPART_CB_FNAME(part_data),
    /* .on_part_data_begin = */ CAT_HTTP_MULTIPART_CB_FNAME(part_data_begin),
    /* .on_headers_complete = */ CAT_HTTP_MULTIPART_CB_FNAME(headers_complete),
    /* .on_part_data_end = */ CAT_HTTP_MULTIPART_CB_FNAME(part_data_end),
    /* .on_body_end = */ CAT_HTTP_MULTIPART_CB_FNAME(body_end),
};

// TODO: maybe export-able? for email protocols usage?
static cat_bool_t cat_http_multipart_parser_execute(cat_http_parser_t *parser, const char *data, size_t length)
{

    size_t len = 0;
    char err_buf[4096];
    size_t ret;

    parser->event = CAT_HTTP_PARSER_EVENT_NONE;
    if (MPPE_ERROR == (len = multipart_parser_execute((multipart_parser*)&parser->multipart, data, length))) {
        ret = multipart_parser_error_msg(&parser->multipart, err_buf, sizeof(err_buf));
        if (ret == 0 || ret > 4095) {
#ifdef CAT_DEBUG
            CAT_ASSERT(0 && "multipart_parser_error_msg returns bad result");
#endif
        }
        cat_update_last_error(CAT_HTTP_ERRNO_MULTIPART_BODY, "failed to parse multipart body: %.*s", (int)ret, err_buf);
        return cat_false;
    }
    CAT_LOG_DEBUG_V3(PROTOCOL, "multipart_parser_execute returns %zu, parsed \"%.*s\"", len, (int)len, data);

    // printf("dl:%zu l:%zu\n", parser->data_length, len);
    CAT_ASSERT(
        (!(parser->event & CAT_HTTP_PARSER_EVENT_FLAG_DATA)) ||
        parser->data_length <= len
    );

    CAT_ASSERT(
        parser->event & CAT_HTTP_PARSER_EVENT_FLAG_MULTIPART ||
        parser->event == CAT_HTTP_PARSER_EVENT_NONE
    );
    // add ptr
    parser->multipart_ptr = data + len;
    parser->parsed_length = len;
    return cat_true;
}

/* parser */

#define CALLBACK_ERROR(CODE, fmt, ...) do {\
    /* cat_update_last_error(CAT_HTTP_ERRNO_ ## CODE, "HTTP-Parser execute failed: " fmt, ##__VA_ARGS__); */ \
    parser->libcat_error = CAT_HTTP_ERRNO_ ## CODE; \
    cat_update_last_error(CAT_HTTP_ERRNO_ ## CODE, fmt, ##__VA_ARGS__); \
    return -1;\
} while(0)

static cat_always_inline cat_http_parser_t *cat_http_parser_get_from_handle(const llhttp_t *llhttp)
{
    return cat_container_of(llhttp, cat_http_parser_t, llhttp);
}

#define CAT_HTTP_PARSER_ON_EVENT_BEGIN(name, NAME) \
static int cat_http_parser_on_##name(llhttp_t *llhttp) \
{ \
    cat_http_parser_t *parser = cat_http_parser_get_from_handle(llhttp); \
    \
    parser->event = CAT_HTTP_PARSER_EVENT_##NAME; \
    \

#define CAT_HTTP_PARSER_ON_EVENT_END() \
    if (((cat_http_parser_event_t) (parser->events & parser->event)) == parser->event) { \
        return HPE_PAUSED; \
    } else { \
        return HPE_OK; \
    } \
}

#define CAT_HTTP_PARSER_ON_EVENT(name, NAME) \
CAT_HTTP_PARSER_ON_EVENT_BEGIN(name, NAME) \
CAT_HTTP_PARSER_ON_EVENT_END()

#define CAT_HTTP_PARSER_ON_DATA_BEGIN(name, NAME) \
static int cat_http_parser_on_##name(llhttp_t *llhttp, const char *at, size_t length) \
{ \
    cat_http_parser_t *parser = cat_http_parser_get_from_handle(llhttp); \
    \
    parser->event = CAT_HTTP_PARSER_EVENT_##NAME; \
    parser->data = at; \
    parser->data_length = length;

#define CAT_HTTP_PARSER_ON_DATA_END() CAT_HTTP_PARSER_ON_EVENT_END()

#define CAT_HTTP_PARSER_ON_DATA(name, NAME) \
CAT_HTTP_PARSER_ON_DATA_BEGIN(name, NAME) \
CAT_HTTP_PARSER_ON_DATA_END()

CAT_HTTP_PARSER_ON_EVENT(message_begin,          MESSAGE_BEGIN   )
CAT_HTTP_PARSER_ON_DATA (url,                    URL             )
CAT_HTTP_PARSER_ON_DATA (status,                 STATUS          )

#define ULONG_T unsigned long

#define strcasecmp_fast(name, needle_str, mask_str) \
static inline int strcasecmp_##name(const char *a){ \
    ULONG_T *cmp = (ULONG_T *)needle_str; \
    ULONG_T *mask = (ULONG_T *)mask_str; \
    int i, _i; \
    const int strsize = sizeof(needle_str) - 1; \
    const int u32i = strsize / sizeof(ULONG_T) * sizeof(ULONG_T) / 4; \
    for (i=0; i > strsize % 4; i++){ \
        _i = strsize - 1 - i; \
        if (needle_str[_i] != (a[_i] | mask_str[_i])) { \
            return 0; \
        } \
    } \
    if ( \
        u32i * 4 + 4 <= strsize && \
        ((uint32_t*)needle_str)[u32i] != (((uint32_t*)a)[u32i] | ((uint32_t*)mask_str)[u32i]) \
    ) { \
        return 0; \
    } \
    for (i=0; i < (int)((sizeof(needle_str) - 1) / sizeof(ULONG_T)); i++){ \
        if (cmp[i] != (((ULONG_T *)a)[i] | mask[i])) { \
            return 0; \
        } \
    } \
    return 1; \
}

#if 1/*enable mp*/
static int cat_http_parser_on_header_value_complete(llhttp_t *llhttp);
#endif
CAT_HTTP_PARSER_ON_DATA_BEGIN(header_field,           HEADER_FIELD    ) do {
#if 1/*enable mp*/

    int ret;
    if ((ret = cat_http_parser_on_header_value_complete(llhttp)) != HPE_OK) {
        parser->libcat_error = (uint8_t)ret;
        return -1;
    }

    if (parser->multipart_state != CAT_HTTP_MULTIPART_UNINIT) {
        CALLBACK_ERROR(DUPLICATE_CONTENT_TYPE, "duplicate content-type header");
    }

    // copy header into buffer
    if (length == 0) {
        break;
    }
    size_t index_new = (size_t)parser->multipart_header_index + length;
    if (index_new < 13) {
        memcpy(parser->multipart_header + parser->multipart_header_index, at, length);
        parser->multipart_header_index = (uint8_t)index_new;
    } else {
        parser->multipart_header_index = UINT8_MAX;
    }

#endif
} while(0); CAT_HTTP_PARSER_ON_EVENT_END()

strcasecmp_fast(boundary_eq, "boundary=", "        \0");
strcasecmp_fast(multipart_dash, "multipart/", "         \0");

#if 1/*enable mp*/
#define shrink_boundary(parser) for (; \
        parser->multipart.boundary_length > 2 && \
        (parser->multipart.multipart_boundary[parser->multipart.boundary_length - 1] == '\t' || \
        parser->multipart.multipart_boundary[parser->multipart.boundary_length - 1] == ' '); \
    parser->multipart.boundary_length--)

static int parse_content_type(cat_http_parser_t *parser, const char *at, size_t length)
{
    // global variables
    // not parsed inputs length
    size_t mp_length = length;
    // buf is [mp_at, mp_endp)
    const char *mp_endp = at + length;
#define mp_at (mp_endp - mp_length)
#define mp_state (parser->multipart_state)
    // local variables
    size_t mp_size;
    const char *mp_c;
    cat_bool_t mp_bool;
#define DEBUG_STATE(name) do { \
    CAT_LOG_DEBUG_V3(PROTOCOL, "mpct parser " #name " [%zu]%.*s", mp_length, (int) mp_length, mp_at); \
} while(0)
// consume specified length
#define CONSUME_BUF(len) do { \
            CAT_ASSERT(parser->multipart_header_index < len); \
            mp_size = mp_length + (size_t)parser->multipart_header_index; \
            if (mp_size < len) { \
                /* not enough, break */ \
                /* printf("not enough %d -> %zu\n", parser->multipart_header_index, mp_size); */ \
                memcpy(parser->multipart_header + parser->multipart_header_index, mp_at, mp_length); \
                parser->multipart_header_index = (uint8_t)mp_size; \
                return HPE_OK; \
            } \
            /* copy at to buffer */ \
            memcpy(parser->multipart_header + parser->multipart_header_index, mp_at, len - parser->multipart_header_index); \
            mp_length -= len - parser->multipart_header_index; \
            /* clean multipart_header_index */ \
            parser->multipart_header_index = 0; \
        } while(0)
// consume until condition
#define CONSUME_UNTIL(cond) do { \
            for (mp_c = mp_at; mp_c < mp_endp; mp_c++) { \
                if (cond) { \
                    break; \
                } \
            } \
            if (mp_c == mp_endp) { \
                return HPE_OK; \
            } \
            /* update mp_length */ \
            mp_length = mp_endp - mp_c; \
        } while(0)
    /*
    boundary syntax from RFC2046 section 5.1.1
    boundary := 0*69<bchars> bcharsnospace
    bchars := bcharsnospace / " "
    bcharsnospace := DIGIT / ALPHA / "'" / "(" / ")" /
        "+" / "_" / "," / "-" / "." /
        "/" / ":" / "=" / "?"
    */
#define is_legal_boundary_char(ch) (\
    /* '\'', '(' ')' */ \
    ('\'' <= (ch) && (ch) <= ')') || \
    /* '+', ',', '-', '.', '/', NUM, ':' */ \
    ('+' <= (ch) && (ch) <= ':') || \
    (ch) == '=' || \
    (ch) == '?' || \
    ('A' <= (ch) && (ch) <= 'Z') || \
    (ch) == '_' || \
    ('a' <= (ch) && (ch) <= 'z') || \
    (ch) == ' ' \
)

    switch (mp_state) {
        case CAT_HTTP_MULTIPART_IN_CONTENT_TYPE:
            // s_start "start"
            DEBUG_STATE(s_start);
            //printf("parser->multipart_header_index is %zu\n", parser->multipart_header_index);
            CONSUME_BUF(10);

            //printf("parser->multipart_header is %.*s\n", 10, parser->multipart_header);
            if (!strcasecmp_multipart_dash(parser->multipart_header)) {
                // not mp
                mp_state = CAT_HTTP_MULTIPART_NOT_MULTIPART;
                return HPE_OK;
            }
            // is mp
            mp_state = CAT_HTTP_MULTIPART_TYPE_IS_MULTIPART;
            /* fallthrough */
        case CAT_HTTP_MULTIPART_TYPE_IS_MULTIPART:
s_mime_type_end:
            // s_mime_type_end "mime type end"
            DEBUG_STATE(s_mime_type_end);
            // skip to next semicolon
            CONSUME_UNTIL(*mp_c == ';');

            // consume ';'
            mp_length--;
            mp_state = CAT_HTTP_MULTIPART_ALMOST_BOUNDARY;
            /* fallthrough */
        case CAT_HTTP_MULTIPART_ALMOST_BOUNDARY:
s_almost_boundary:
            // s_almost_boundary "almost 'boundary='"
            DEBUG_STATE(s_almost_boundary);
            // skip ows
            CONSUME_UNTIL(*mp_c != ' ' && *mp_c != '\t');

            mp_state = CAT_HTTP_MULTIPART_BOUNDARY;
            /* fallthrough */
        case CAT_HTTP_MULTIPART_BOUNDARY:
            // s_boundary "boundary="
            DEBUG_STATE(s_boundary);
            CONSUME_BUF(9);

            // if "multipart/dasd; foo; boundary=";
            //       header buf is ^ -----> ^
            // so skip to semicolon
            for (mp_c = parser->multipart_header + 9 - 1; mp_c >= parser->multipart_header; mp_c--) {
                if (*mp_c == ';') {
                    break;
                }
            }
            if (mp_c >= parser->multipart_header) {
                mp_c++; /* ';' */
                // sizeof new things
                mp_length += parser->multipart_header + 9 - mp_c;
                goto s_almost_boundary;
            }

            if (!strcasecmp_boundary_eq(parser->multipart_header)) {
                // not boundary=
                // roll back to state "mime type end"
                mp_state = CAT_HTTP_MULTIPART_TYPE_IS_MULTIPART;
                goto s_mime_type_end;
            }
            if (parser->multipart.boundary_length != 0) {
                CALLBACK_ERROR(MULTIPART_HEADER, "duplicate boundary=");
            }
            // is boundary=
            mp_state = CAT_HTTP_MULTIPART_BOUNDARY_START;
            /* fallthrough */
        case CAT_HTTP_MULTIPART_BOUNDARY_START:
            // s_boundary_data "boundary data" NOT_ACCEPTABLE
            DEBUG_STATE(s_boundary_data);
            if (mp_length == 0) {
                return HPE_OK;
            }
            // reset boundary buf
            parser->multipart.boundary_length = 2;
            parser->multipart.multipart_boundary[0] = '-';
            parser->multipart.multipart_boundary[1] = '-';

            // goto next state
            mp_state = CAT_HTTP_MULTIPART_BOUNDARY_COMMON;
            if (mp_at[0] == '"') {
                mp_length--;
                mp_state = CAT_HTTP_MULTIPART_BOUNDARY_QUOTED;
                goto s_boundary_quoted_data;
            }
            /* fallthrough */
        case CAT_HTTP_MULTIPART_BOUNDARY_COMMON:
            // s_boundary_common_data "boundary common data"
            DEBUG_STATE(s_boundary_common_data);

            // find parsed size
            for (mp_c = mp_at; mp_c < mp_endp; mp_c++) {
                if (!is_legal_boundary_char(*mp_c)) {
                    break;
                }
            }
            mp_size = mp_c - mp_at;

            // should we keep on the state
            mp_bool = cat_true;
            if (mp_size + parser->multipart.boundary_length > BOUNDARY_MAX_LEN + 2) {
                // out of range
                mp_size = BOUNDARY_MAX_LEN + 2 - parser->multipart.boundary_length;
            }
            if (mp_size != mp_length) {
                // break at middle
                mp_bool = cat_false;
            }
            memcpy(parser->multipart.multipart_boundary + parser->multipart.boundary_length, mp_at, mp_size);
            parser->multipart.boundary_length += (uint8_t)mp_size;
            mp_length -= mp_size;

            if (mp_bool) {
                return HPE_OK;
            }

            shrink_boundary(parser);
            if (parser->multipart.boundary_length == 0) {
                CALLBACK_ERROR(MULTIPART_HEADER, "empty boundary");
            }

            mp_state = CAT_HTTP_MULTIPART_BOUNDARY_END;
            /* fallthrough */
        case CAT_HTTP_MULTIPART_BOUNDARY_END:
s_boundary_end:
            // s_boundary_end "boundary end"
            DEBUG_STATE(s_boundary_end);

            // consume to semicolon
            CONSUME_UNTIL((*mp_c) != '\t' && (*mp_c) != ' ');

            if (*mp_c != ';') {
                CALLBACK_ERROR(MULTIPART_HEADER, "extra char at boundary end");
            }
            length--;
            mp_state = CAT_HTTP_MULTIPART_TYPE_IS_MULTIPART;
            goto s_mime_type_end;
        case CAT_HTTP_MULTIPART_BOUNDARY_QUOTED:
s_boundary_quoted_data:
            // s_boundary_quoted_data "boundary quoted data" NOT_ACCEPTABLE
            DEBUG_STATE(s_boundary_quoted_data);

            // find parsed size
            for (mp_c = mp_at; mp_c < mp_endp; mp_c++) {
                if (!is_legal_boundary_char(*mp_c)) {
                    break;
                }
            }
            mp_size = mp_c - mp_at;

            if (mp_size + parser->multipart.boundary_length > BOUNDARY_MAX_LEN + 2) {
                CALLBACK_ERROR(MULTIPART_HEADER, "boundary is too long");
            }
            // copy to buf
            memcpy(parser->multipart.multipart_boundary + parser->multipart.boundary_length, mp_at, mp_size);
            parser->multipart.boundary_length += (uint8_t)mp_size;

            if (mp_length == mp_size) {
                // not end
                return HPE_OK;
            }

            // ending
            mp_length -= mp_size + 1;
            if (*mp_c != '"') {
                CALLBACK_ERROR(MULTIPART_HEADER, "illegal char %c in boundary", *mp_c);
            }
            if (parser->multipart.multipart_boundary[parser->multipart.boundary_length - 1] == ' ') {
                CALLBACK_ERROR(MULTIPART_HEADER, "boundary ends with space");
            }
            mp_state = CAT_HTTP_MULTIPART_BOUNDARY_END;
            goto s_boundary_end;
        default:
            // never here
            CAT_ASSERT(0 && "unknow state");
    }
    // never here
    CAT_ASSERT(0 && "bad return");
    return HPE_OK;
#undef DEBUG_STATE
#undef CONSUME_BUF
#undef CONSUME_UNTIL
#undef mp_at
#undef mp_state
}
#endif

strcasecmp_fast(content_type, "content-type", "       \0    ");

CAT_HTTP_PARSER_ON_DATA_BEGIN(header_value,           HEADER_VALUE    ) {
#if 1/*enable mp*/
    int ret;
    if (parser->multipart_state == CAT_HTTP_MULTIPART_UNINIT) {
        if (parser->multipart_header_index == 12 && strcasecmp_content_type(parser->multipart_header)) {
            CAT_LOG_DEBUG_V3(PROTOCOL, "mpct parser on_header_field_complete found content-type");
            parser->multipart_state = CAT_HTTP_MULTIPART_IN_CONTENT_TYPE;
        }
        parser->multipart_header_index = 0;
    }
    if (
        length > 0 &&
        parser->multipart_state >= CAT_HTTP_MULTIPART_IN_CONTENT_TYPE &&
        parser->multipart_state < CAT_HTTP_MULTIPART_OUT_CONTENT_TYPE
    ) {
        CAT_LOG_DEBUG_V3(PROTOCOL, "mpct parser state %d", parser->multipart_state);
        ret = parse_content_type(parser, at, length);
        if (HPE_OK != ret) {
            return -1;
        }
    }
#endif
} CAT_HTTP_PARSER_ON_DATA_END()

#if 1/*enable mp*/
static int cat_http_parser_on_header_value_complete(llhttp_t *llhttp)
{
    cat_http_parser_t *parser = cat_http_parser_get_from_handle(llhttp);

    //printf("on hc complete %d\n", parser->multipart_state);
    if (
        (parser->multipart_state >= CAT_HTTP_MULTIPART_IN_CONTENT_TYPE && parser->multipart_state < CAT_HTTP_MULTIPART_OUT_CONTENT_TYPE)
    ) {
        if (
            parser->multipart_state == CAT_HTTP_MULTIPART_BOUNDARY_QUOTED ||
            parser->multipart_state == CAT_HTTP_MULTIPART_BOUNDARY_START) {
            CALLBACK_ERROR(MULTIPART_HEADER, "unexpected EOF on parsing content-type header");
        }
        shrink_boundary(parser);

        if (parser->multipart.boundary_length <= 2) {
            CALLBACK_ERROR(MULTIPART_HEADER, "empty boundary on EOL");
        }
        if (0 != multipart_parser_init(&parser->multipart, NULL, 0, &cat_http_multipart_parser_settings)) {
            CALLBACK_ERROR(MULTIPART_HEADER, "multipart_parser_init failed");
        }
        parser->multipart_state = CAT_HTTP_MULTIPART_BOUNDARY_OK;
    }
    return HPE_OK;
}
#endif

CAT_HTTP_PARSER_ON_EVENT_BEGIN(headers_complete, HEADERS_COMPLETE) {
    parser->keep_alive = !!llhttp_should_keep_alive(&parser->llhttp);
    parser->content_length = parser->llhttp.content_length;
#if 1/*enable mp*/
    int ret;
    if ((ret = cat_http_parser_on_header_value_complete(llhttp)) != HPE_OK) {
        return ret;
    }
#endif
} CAT_HTTP_PARSER_ON_EVENT_END()

CAT_HTTP_PARSER_ON_DATA_BEGIN(body, BODY) {
#if 1 /* enable mp */
    //printf("on body %d\n", parser->multipart_state);
    CAT_ASSERT(
        parser->multipart_state == CAT_HTTP_MULTIPART_NOT_MULTIPART ||
        parser->multipart_state == CAT_HTTP_MULTIPART_BOUNDARY_OK
    );
    if ((parser->events & CAT_HTTP_PARSER_EVENT_FLAG_MULTIPART) && parser->multipart_state == CAT_HTTP_MULTIPART_BOUNDARY_OK) {
        return HPE_PAUSED;
    }
#endif
} CAT_HTTP_PARSER_ON_EVENT_END()
CAT_HTTP_PARSER_ON_EVENT(chunk_header,           CHUNK_HEADER    )
CAT_HTTP_PARSER_ON_EVENT(chunk_complete,         CHUNK_COMPLETE  )
CAT_HTTP_PARSER_ON_EVENT(message_complete,       MESSAGE_COMPLETE)

const llhttp_settings_t cat_http_parser_settings = {
    cat_http_parser_on_message_begin,
    cat_http_parser_on_url,
    cat_http_parser_on_status,
    cat_http_parser_on_header_field,
    cat_http_parser_on_header_value,
    cat_http_parser_on_headers_complete,
    cat_http_parser_on_body,
    cat_http_parser_on_message_complete,
    cat_http_parser_on_chunk_header,
    cat_http_parser_on_chunk_complete,
    NULL, NULL, NULL, NULL
};

static cat_always_inline void cat_http_parser__init(cat_http_parser_t *parser)
{
    parser->llhttp.method = CAT_HTTP_METHOD_UNKNOWN;
    parser->event = CAT_HTTP_PARSER_EVENT_NONE;
    parser->data = NULL;
    parser->data_length = 0;
    parser->parsed_length = 0;
    parser->content_length = 0;
    parser->keep_alive = cat_false;
    parser->libcat_error = 0;
    parser->multipart_state = CAT_HTTP_MULTIPART_UNINIT;
    parser->multipart_header_index = 0;
    parser->multipart.boundary_length = 0;
}

CAT_API void cat_http_parser_init(cat_http_parser_t *parser)
{
    llhttp_init(&parser->llhttp, HTTP_BOTH, &cat_http_parser_settings);
    cat_http_parser__init(parser);
    parser->events = CAT_HTTP_PARSER_EVENTS_NONE;
}

CAT_API void cat_http_parser_reset(cat_http_parser_t *parser)
{
    llhttp_reset(&parser->llhttp);
    cat_http_parser__init(parser);
}

CAT_API cat_http_parser_t *cat_http_parser_create(cat_http_parser_t *parser)
{
    if (parser == NULL) {
        parser = (cat_http_parser_t *) cat_malloc(sizeof(*parser));
#if CAT_ALLOC_HANDLE_ERRORS
        if (unlikely(parser == NULL)) {
            cat_update_last_error_of_syscall("Malloc for HTTP parser failed");
            return NULL;
        }
#endif
    }
    cat_http_parser_init(parser);

    return parser;
}

CAT_API cat_http_parser_type_t cat_http_parser_get_type(const cat_http_parser_t *parser)
{
    return (cat_http_parser_type_t) parser->llhttp.type;
}

CAT_API cat_bool_t cat_http_parser_set_type(cat_http_parser_t *parser, cat_http_parser_type_t type)
{
    if (unlikely(parser->llhttp.finish != HTTP_FINISH_SAFE)) {
        cat_update_last_error(CAT_EMISUSE, "Change HTTP-Parser type during execution is not allowed");
        return cat_false;
    }
    parser->llhttp.type = type;

    return cat_true;
}

CAT_API cat_http_parser_events_t cat_http_parser_get_events(const cat_http_parser_t *parser)
{
    return parser->events;
}

CAT_API void cat_http_parser_set_events(cat_http_parser_t *parser, cat_http_parser_events_t events)
{
    parser->events = (events & CAT_HTTP_PARSER_EVENTS_ALL);
}

static cat_bool_t cat_http_llhttp_execute(cat_http_parser_t *parser, const char *data, size_t length)
{
    // TODO: convert llhttp errno to our own errno?
    llhttp_errno_t error;

    parser->event = CAT_HTTP_PARSER_EVENT_NONE;
    error = llhttp_execute(&parser->llhttp, data, length);
    if (error != HPE_OK) {
        parser->parsed_length = llhttp_get_error_pos(&parser->llhttp) - data;
        if (unlikely(error != HPE_PAUSED)) {
            if (unlikely(error != HPE_PAUSED_UPGRADE)) {
                if (parser->libcat_error != 0) {
                    cat_update_last_error(parser->libcat_error, "HTTP-Parser execute failed: %s", cat_get_last_error_message());
                } else {
                    cat_update_last_error(error, "HTTP-Parser execute failed: %s", llhttp_get_error_reason(&parser->llhttp));
                }
                return cat_false;
            } else {
                llhttp_resume_after_upgrade(&parser->llhttp);
            }
        } else {
            llhttp_resume(&parser->llhttp);
        }
    } else {
        parser->parsed_length = length;
    }

    return cat_true;
}

CAT_API cat_bool_t cat_http_parser_execute(cat_http_parser_t *parser, const char *data, size_t length)
{
    cat_bool_t ret;
    size_t llhttp_parsed_length, mp_length;
    const char *mp_body;

    //printf("to parse %.*s\n", (int)length, data);
    if (
        1/*enable mp*/ &&
        parser->multipart_state == CAT_HTTP_MULTIPART_IN_BODY
    ) {
        // if in multipart body, bypass llhttp
        ret = cat_http_multipart_parser_execute(parser, data, length);

        if (!ret) {
            cat_update_last_error(CAT_HTTP_ERRNO_MULTIPART_BODY, "HTTP-Parser execute failed: %s", cat_get_last_error_message());
        }
    } else {
        // if not in multipart body
        ret = cat_http_llhttp_execute(parser, data, length);

        //printf("ret %d ev %s state %d\n", ret, cat_http_parser_get_event_name(parser), parser->multipart_state);
        if (
            1/*enable mp*/ &&
            ret &&
            parser->event == CAT_HTTP_PARSER_EVENT_BODY &&
            parser->multipart_state == CAT_HTTP_MULTIPART_BOUNDARY_OK
        ) {
            // if body with multipart boundary is OK
            llhttp_parsed_length = parser->parsed_length - parser->data_length;
            mp_body = parser->data;
            mp_length = parser->data_length;

            // update llhttp status
            // a little hacky here: llhttp wont check the body it got
            CAT_ASSERT(parser->content_length >= parser->data_length);
            ret = cat_http_llhttp_execute(parser, NULL, parser->content_length - parser->data_length);
            CAT_ASSERT(ret);
            if (parser->event == CAT_HTTP_PARSER_EVENT_BODY) {
                ret = cat_http_llhttp_execute(parser, NULL, 0);
                CAT_ASSERT(ret);
            }
            CAT_ASSERT(parser->event == CAT_HTTP_PARSER_EVENT_MESSAGE_COMPLETE);

            // now parsed length is all body, execute multipart_parser
            ret = cat_http_multipart_parser_execute(parser, mp_body, mp_length);
            parser->parsed_length += llhttp_parsed_length;
            parser->multipart_state = CAT_HTTP_MULTIPART_IN_BODY;
        }
    }

    if (
        1/*enable mp*/ && (
            parser->event == _CAT_HTTP_PARSER_EVENT_MULTIPART_BODY_END ||
            parser->event == CAT_HTTP_PARSER_EVENT_MESSAGE_COMPLETE
        )
    ) {
        // if MULTIPART_BODY_END, return llhttp event
        parser->event = CAT_HTTP_PARSER_EVENT_MESSAGE_COMPLETE;
        // reset mpp
        parser->multipart_state = CAT_HTTP_MULTIPART_UNINIT;
        parser->multipart.boundary_length = 0;
    }

#ifdef CAT_DEBUG
    if (parser->event & CAT_HTTP_PARSER_EVENT_FLAG_DATA) {
        CAT_LOG_DEBUG_V2(PROTOCOL, "http_parser_execute parsed %zu return %s [%zu]%.*s", parser->parsed_length,
            cat_http_parser_get_event_name(parser),
            parser->data_length, (int)parser->data_length, parser->data);
    } else {
        CAT_LOG_DEBUG_V2(PROTOCOL, "http_parser_execute parsed %zu return %s",
            parser->parsed_length,
            cat_http_parser_get_event_name(parser));
    }
#endif
    return ret;
}

CAT_API const char *cat_http_parser_event_name(cat_http_parser_event_t event)
{
    switch (event) {
#define CAT_HTTP_PARSER_EVENT_NAME_GEN(name, unused1) case CAT_HTTP_PARSER_EVENT_##name: return #name;
    CAT_HTTP_PARSER_EVENT_MAP(CAT_HTTP_PARSER_EVENT_NAME_GEN)
#undef CAT_HTTP_PARSER_EVENT_NAME_GEN
        case _CAT_HTTP_PARSER_EVENT_MULTIPART_BODY_END:
            break;
        default:
            break;
    }
    abort();
}

CAT_API cat_http_parser_event_t cat_http_parser_get_event(const cat_http_parser_t *parser)
{
    return parser->event;
}

CAT_API const char* cat_http_parser_get_event_name(const cat_http_parser_t *parser)
{
    return cat_http_parser_event_name(parser->event);
}

CAT_API const char* cat_http_parser_get_data(const cat_http_parser_t *parser)
{
    return parser->data;
}

CAT_API size_t cat_http_parser_get_data_length(const cat_http_parser_t *parser)
{
    return parser->data_length;
}

CAT_API cat_bool_t cat_http_parser_should_keep_alive(const cat_http_parser_t *parser)
{
    return parser->keep_alive;
}

CAT_API cat_bool_t cat_http_parser_finish(cat_http_parser_t *parser)
{
    llhttp_errno_t error;

    error = llhttp_finish(&parser->llhttp);
    if (unlikely(error != 0)) {
        cat_update_last_error(error, "HTTP-Parser finish failed: %s", llhttp_errno_name(error));
        return cat_false;
    }

    return cat_true;
}

CAT_API llhttp_errno_t cat_http_parser_get_error_code(const cat_http_parser_t *parser)
{
    return llhttp_get_errno(&parser->llhttp);
}

CAT_API const char *cat_http_parser_get_error_message(const cat_http_parser_t *parser)
{
    return llhttp_get_error_reason(&parser->llhttp);
}

CAT_API cat_bool_t cat_http_parser_is_completed(const cat_http_parser_t *parser)
{
    return parser->event == CAT_HTTP_PARSER_EVENT_MESSAGE_COMPLETE;
}

CAT_API const char *cat_http_parser_get_current_pos(const cat_http_parser_t *parser)
{

    // bypass llhttp_get_error_pos if in multipart body
    if (
        1/*enable mp*/ &&
        parser->event & CAT_HTTP_PARSER_EVENT_FLAG_MULTIPART &&
        parser->multipart_state == CAT_HTTP_MULTIPART_IN_BODY
    ) {
        return parser->multipart_ptr;
    }
    return llhttp_get_error_pos(&parser->llhttp);
}

CAT_API size_t cat_http_parser_get_current_offset(const cat_http_parser_t *parser, const char *data)
{
    const char *p = cat_http_parser_get_current_pos(parser);

    if (p == NULL) {
        return 0;
    }

    return p - data;
}

CAT_API size_t cat_http_parser_get_parsed_length(const cat_http_parser_t *parser)
{
    return parser->parsed_length;
}

CAT_API cat_http_method_t cat_http_parser_get_method(const cat_http_parser_t *parser)
{
    return parser->llhttp.method;
}

CAT_API const char  *cat_http_parser_get_method_name(const cat_http_parser_t *parser)
{
    return cat_http_method_get_name(parser->llhttp.method);
}

CAT_API uint8_t cat_http_parser_get_major_version(const cat_http_parser_t *parser)
{
    return parser->llhttp.http_major;
}

CAT_API uint8_t cat_http_parser_get_minor_version(const cat_http_parser_t *parser)
{
    return parser->llhttp.http_minor;
}

CAT_API const char *cat_http_parser_get_protocol_version(const cat_http_parser_t *parser)
{
    if (parser->llhttp.http_major == 1) {
        if (parser->llhttp.http_minor == 1) {
            return "1.1";
        }
        if (parser->llhttp.http_minor == 0) {
            return "1.0";
        }
    }
    if (parser->llhttp.http_major == 2) {
        if (parser->llhttp.http_minor == 0) {
            return "2";
        }
    }
    return "UNKNOWN";
}

CAT_API cat_http_status_code_t cat_http_parser_get_status_code(const cat_http_parser_t *parser)
{
    return parser->llhttp.status_code;
}

CAT_API const char *cat_http_parser_get_reason_phrase(const cat_http_parser_t *parser)
{
    return cat_http_status_get_reason(parser->llhttp.status_code);
}

CAT_API uint64_t cat_http_parser_get_content_length(const cat_http_parser_t *parser)
{
    return parser->content_length;
}

CAT_API cat_bool_t cat_http_parser_is_upgrade(const cat_http_parser_t *parser)
{
    return !!parser->llhttp.upgrade;
}
