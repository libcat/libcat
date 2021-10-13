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

#define CAT_HTTP_MULTIPART_CB_FNAME(name) cat_http_multipart_parser_on_##name
#define CAT_HTTP_MULTIPART_ON_DATA(name, NAME) \
static long CAT_HTTP_MULTIPART_CB_FNAME(name)(multipart_parser *p, const char *at, size_t length){ \
    cat_http_parser_t* parser = cat_container_of(p, cat_http_parser_t, multipart); \
    parser->event = CAT_HTTP_PARSER_EVENT_##NAME; \
    CAT_LOG_DEBUG_V2(PROTOCOL, "http multipart parser data on_" # name ": [%zu]%.*s", length, (int)length, at); \
    if (((cat_http_parser_event_t) (parser->events & parser->event)) == parser->event) { \
        parser->data = at; \
        parser->data_length = length; \
        \
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

static long CAT_HTTP_MULTIPART_CB_FNAME(body_end)(multipart_parser *p){
    cat_http_parser_t* parser = cat_container_of(p, cat_http_parser_t, multipart);
    CAT_ASSERT(parser->multipart_status == CAT_HTTP_MULTIPART_IN_BODY);
    // escape mp parser
    parser->event = _CAT_HTTP_PARSER_EVENT_MULTIPART_BODY_END;
    parser->multipart_status = CAT_HTTP_MULTIPART_NOT_MULTIPART;
    CAT_LOG_DEBUG_V2(PROTOCOL, "http multipart parser data on_body_end");
    if (((cat_http_parser_event_t) (parser->events & parser->event)) == parser->event) {
        return MPPE_PAUSED;
    }
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
static cat_bool_t cat_http_multipart_parser_execute(cat_http_parser_t *parser, const char *data, size_t length){

    size_t len = 0;

    parser->event = CAT_HTTP_PARSER_EVENT_NONE;
    if(SIZE_MAX == (len = multipart_parser_execute((multipart_parser*)&parser->multipart, data, length))){
        cat_update_last_error(CAT_HTTP_ERRNO_MULTIPART, "HTTP-Parser execute failed: failed to parse multipart body");
        return cat_false;
    }

    CAT_LOG_DEBUG_V3(PROTOCOL, "multipart_parser_execute returns %d, parsed \"%.*s\"", len, (int)len, data);
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
    cat_update_last_error(CAT_HTTP_ERRNO_ ## CODE, "HTTP-Parser execute failed: " fmt, ##__VA_ARGS__); \
    return CAT_HTTP_ERRNO_ ## CODE;\
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

strcasecmp_fast(content_type, "content-type", "       \0    ");

CAT_HTTP_PARSER_ON_DATA_BEGIN(header_field,           HEADER_FIELD    ) do {
#if 1/*enable mp*/
    // copy header into buffer
    if (length == 0) {
        break;
    }
    size_t index_new = (size_t)parser->multipart_header_index + length;
    if (index_new < 13) {
        memcpy(parser->multipart_header + parser->multipart_header_index, at, length);
        parser->multipart_header_index = index_new;
    } else {
        parser->multipart_header_index = UINT8_MAX;
    }
    
#endif
} while(0); CAT_HTTP_PARSER_ON_EVENT_END()

static int cat_http_parser_on_header_field_complete(llhttp_t *llhttp)
{
#if 1/*enable mp*/
    cat_http_parser_t *parser = cat_http_parser_get_from_handle(llhttp);
    parser->multipart_state = CAT_HTTP_MULTIPART_NOT_MULTIPART;
    if (
        parser->multipart_header_index == 12 &&
        strcasecmp_content_type(parser->multipart_header)
    ) {
        CAT_ASSERT(
            parser->multipart_state == CAT_HTTP_MULTIPART_BOUNDARY_OK || 
            parser->multipart_state == CAT_HTTP_MULTIPART_NOT_MULTIPART ||
            parser->multipart_state == CAT_HTTP_MULTIPART_UNINIT
        );
        if (parser->multipart_state != CAT_HTTP_MULTIPART_UNINIT) {
            CALLBACK_ERROR(DUPLICATE_CONTENT_TYPE, "duplicate content-type header");
        }
        parser->multipart_state = CAT_HTTP_MULTIPART_IN_CONTENT_TYPE;
    }
    parser->multipart_header_index = 0;
#endif
    return HPE_OK;
}

strcasecmp_fast(boundary_eq, "boundary=", "        \0");
strcasecmp_fast(multipart_dash, "multipart/", "         \0");
 
CAT_HTTP_PARSER_ON_DATA_BEGIN(header_value,           HEADER_VALUE    ) do {
#if 1/*enable mp*/
    size_t mp_length = length;
    size_t mp_length_new;
    char *mp_endp = at + length;
    char *mp_c;
    cat_bool_t mp_bool;
    if (length == 0) {
        break;
    }
    if (
        parser->multipart_state < CAT_HTTP_MULTIPART_IN_CONTENT_TYPE || 
        parser->multipart_state > CAT_HTTP_MULTIPART_OUT_CONTENT_TYPE
    ) {
        // if in TYPE_IS_MULTIPART, BOUNDARY_START, BOUNDARY_END status continue, else break
        break;
    }
// consume specified length
#define CONSUME_BUF(len) \
            CAT_ASSERT(parser->multipart_header_index < len); \
            mp_length_new = mp_length + (size_t)parser->multipart_header_index; \
            if (mp_length_new < len) { \
                /* not enough, break */ \
                memcpy(parser->multipart_header + parser->multipart_header_index, at, mp_length_new); \
                parser->multipart_header_index = mp_length_new; \
                continue; /* breaks outter do while(0) */ \
            } \
            /* copy at to buffer */ \
            memcpy(parser->multipart_header + parser->multipart_header_index, at, len - parser->multipart_header_index); \
            mp_length -= len - parser->multipart_header_index; \
            parser->multipart_header_index = 0;

    switch (parser->multipart_state) {
        case CAT_HTTP_MULTIPART_IN_CONTENT_TYPE:
            // state "start"
            CONSUME_BUF(10)

            if(!strcasecmp_multipart_dash(parser->multipart_header)){
                // not mp
                parser->multipart_state = CAT_HTTP_MULTIPART_NOT_MULTIPART;
                continue; // breaks outter do while(0)
            }
            // is mp
            parser->multipart_state = CAT_HTTP_MULTIPART_TYPE_IS_MULTIPART;
            /* fallthrough */
        case CAT_HTTP_MULTIPART_TYPE_IS_MULTIPART:
s_mime_type_end:
            // state "mime type end"
            // skip to next semicolon
            mp_bool = cat_false;
            for (mp_c = mp_endp - length; mp_c <= mp_endp; mp_c++) {
                if (*mp_c == ';') {
                    mp_bool = cat_true;
                    break;
                }
            }
            if (!mp_bool) {
                continue; // breaks outter do while(0)
            }

            mp_length = mp_endp - mp_c;
            parser->multipart_state = CAT_HTTP_MULTIPART_ALMOST_BOUNDARY;
            /* fallthrough */
         case CAT_HTTP_MULTIPART_ALMOST_BOUNDARY:
            // state "almost 'boundary='"
            CONSUME_BUF(9)

            if(!strcasecmp_boundary_eq(parser->multipart_header)){
                // not boundary=
                // roll back to state "mime type end"
                parser->multipart_state = CAT_HTTP_MULTIPART_TYPE_IS_MULTIPART;
                goto s_mime_type_end;
            }
            // is boundary=
            parser->multipart_state = CAT_HTTP_MULTIPART_BOUNDARY_START;
            /* fallthrough */
         case CAT_HTTP_MULTIPART_BOUNDARY_QUOTED_START:
            
         CAT_HTTP_MULTIPART_BOUNDARY_START

    }


#endif
} while(0); CAT_HTTP_PARSER_ON_DATA_END()

static int cat_http_parser_on_header_field_complete(llhttp_t *llhttp)
{
    cat_http_parser_t *parser = cat_http_parser_get_from_handle(llhttp);

    if (1/*enable mp*/ && parser->multipart_status == CAT_HTTP_MULTIPART_IN_CONTENT_TYPE) {
        CAT_LOG_DEBUG_V3(PROTOCOL, "http parser checking content-type header");
        size_t i = 0;
        // skip ows
        while ((at[i] == '\t' || at[i] == ' ') && i < length) {
            i++;
        }
        const char *mp_at = &at[i];
        if (length >= 10 &&
            strcasecmp_multipart(mp_at)
        ) {
            i = length - i;
            CAT_LOG_DEBUG_V3(PROTOCOL, "http parser header is multipart, parsing \"%.*s\"", (int)i, mp_at);
            if(!parse_boundary(&mp_at, &i)){
                CALLBACK_ERROR(BAD_BOUNDARY, "%s", cat_get_last_error_message());
            }
            if(i > 70){
                CALLBACK_ERROR(BAD_BOUNDARY, "multipart boundary is too long");
            }
            if (0 != multipart_parser_init(&parser->multipart, mp_at, i, &cat_http_multipart_parser_settings)){
                CALLBACK_ERROR(MULTIPART, "failed initialize multipart parser: %d", errno);
            }
            parser->multipart_status = CAT_HTTP_MULTIPART_BOUNDARY_OK;
        } else {
            parser->multipart_status = CAT_HTTP_MULTIPART_NOT_MULTIPART;
        }
    }

static int cat_http_parser_on_header_value_complete(llhttp_t *llhttp)
{
    cat_http_parser_t *parser = cat_http_parser_get_from_handle(llhttp);

}

CAT_HTTP_PARSER_ON_EVENT_BEGIN(headers_complete, HEADERS_COMPLETE) {
    parser->keep_alive = !!llhttp_should_keep_alive(&parser->llhttp);
    parser->content_length = parser->llhttp.content_length;
} CAT_HTTP_PARSER_ON_EVENT_END()

CAT_HTTP_PARSER_ON_DATA_BEGIN(body, BODY){
    CAT_ASSERT(
        parser->multipart_status == CAT_HTTP_MULTIPART_BOUNDARY_OK ||
        parser->multipart_status == CAT_HTTP_MULTIPART_NOT_MULTIPART ||
        parser->multipart_status == CAT_HTTP_MULTIPART_UNINIT
    );
    if (
        (parser->events & CAT_HTTP_PARSER_EVENT_FLAG_MULTIPART) &&
        parser->multipart_status == CAT_HTTP_MULTIPART_BOUNDARY_OK
    ){
        return HPE_PAUSED;
    }
} CAT_HTTP_PARSER_ON_EVENT_END()
CAT_HTTP_PARSER_ON_EVENT(chunk_header,           CHUNK_HEADER    )
CAT_HTTP_PARSER_ON_EVENT(chunk_complete,         CHUNK_COMPLETE  )

CAT_HTTP_PARSER_ON_EVENT_BEGIN(message_complete,       MESSAGE_COMPLETE) {
    parser->multipart_status = CAT_HTTP_MULTIPART_UNINIT;
} CAT_HTTP_PARSER_ON_EVENT_END()

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
    NULL, NULL, NULL,
    cat_http_parser_on_header_value_complete,
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
    parser->multipart_status = CAT_HTTP_MULTIPART_UNINIT;
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
                if (error > HPE_USER){
                    cat_update_last_error(error, "%s", cat_get_last_error_message());
                } else {
                    cat_update_last_error(error, "HTTP-Parser execute failed: %s", llhttp_errno_name(error));
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

    if (
        1/*enable mp*/ &&
        parser->multipart_status == CAT_HTTP_MULTIPART_IN_BODY
    ) {
        // if in multipart body, bypass llhttp
        ret = cat_http_multipart_parser_execute(parser, data, length);
    } else {
        // if not in multipart body
        ret = cat_http_llhttp_execute(parser, data, length);

        if (
            1/*enable mp*/ &&
            ret &&
            parser->event == CAT_HTTP_PARSER_EVENT_BODY &&
            parser->multipart_status == CAT_HTTP_MULTIPART_BOUNDARY_OK
        ) {
            // if entering body with multipart boundary is OK, execute multipart_parser
            ret = cat_http_multipart_parser_execute(parser, parser->data, parser->data_length);
            parser->parsed_length += parser->data - data;
            parser->multipart_status = CAT_HTTP_MULTIPART_IN_BODY;
        }
    }

    if (
        1/*enable mp*/ &&
        parser->event == _CAT_HTTP_PARSER_EVENT_MULTIPART_BODY_END
    ) {
        // if MULTIPART_BODY_END, skip it by redo call cat_http_llhttp_execute

        ret = cat_http_llhttp_execute(parser, data + parser->parsed_length, length - parser->parsed_length);
    }

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
        parser->multipart_status == CAT_HTTP_MULTIPART_IN_BODY
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
