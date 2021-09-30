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

#define mp_status multipart_boundary[0]
#define mp_boundary multipart_boundary

// used for fast strncasecmp
typedef union{
    uint8_t u8[4];
    uint32_t u32;
} string_int;

static const multipart_parser_settings cat_http_multipart_parser_settings;

static cat_bool_t cat_http_multipart_parser_execute(cat_http_parser_t *parser, const char *data, size_t length);

/* parser */

#define CALLBACK_ERROR(CODE, ...) do {\
    cat_update_last_error(CAT_HTTP_ERRNO_ ## CODE, "HTTP-Parser execute failed: %s", __VA_ARGS__); \
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

CAT_HTTP_PARSER_ON_DATA_BEGIN(header_field,           HEADER_FIELD    ) {
    static const string_int content_type[] = {
        {'c','o','n','t',},
        {'e','n','t','-',},
        {'t','y','p','e',},
    };
    static const string_int content_type_mask[] = {
        // 'A' | '\x20' (' ') => 'a'
        {' ',' ',' ',' ',},
        {' ',' ',' ','\0',},
        {' ',' ',' ',' ',},
    };
    if(
        1/*enable mp*/ &&
        length >= 12 &&
        content_type[2].u32 == (((uint32_t *)at)[2] | content_type_mask[2].u32) &&
        content_type[1].u32 == (((uint32_t *)at)[1] | content_type_mask[1].u32) &&
        content_type[0].u32 == (((uint32_t *)at)[0] | content_type_mask[0].u32)
    ){
        if(parser->multipart.mp_status != CAT_HTTP_MULTIPART_UNINIT){
            CALLBACK_ERROR(DUPLICATE_CONTENT_TYPE, "duplicate content-type header");
        }
        parser->multipart.mp_status = CAT_HTTP_MULTIPART_IN_CONTENT_TYPE;
    }
} CAT_HTTP_PARSER_ON_EVENT_END()


static inline cat_bool_t parse_boundary(const char** _at, size_t *_length)
{
    static const string_int boundary[] = {
        {'b','o','u','n',},
        {'d','a','r','y',},
    };
    const char *ret, *at = *_at;
    size_t length = *_length;
    cat_bool_t use_quote = cat_false;
    size_t i;
    char ch;

#define consume(n) do { \
    /* printf("consume \"%.*s\"\n", n, at); */ \
    CAT_ASSERT(length >= n); \
    length-=n; \
    at+=n; \
} while(0);
    while(length >= 10){
        // go to next semicolon
        do {
            consume(1);
        } while (*at != ';' && length >= 10);
        if (length < 10){
            break;
        }
        consume(1);
        // skip ows
        while ((*at == '\t' || *at == ' ') && length > 10) {
            consume(1);
        }
        // match "boundary="
        if(
            length < 10 ||
            boundary[0].u32 != (((uint32_t *)at)[0] | 0x20202020) ||
            boundary[1].u32 != (((uint32_t *)at)[1] | 0x20202020) ||
            at[8] != '='
        ){
            // not match
            // skip ows
            while ((*at == '\t' || *at == ' ') && length > 10) {
                consume(1);
            }
            continue;
        }
        consume(9);
        if(*at == '"'){
            use_quote = cat_true;
            ret = at + 1;
        }else{
            ret = at;
        }
        
        // peek to boundary end
        /*
        boundary syntax from RFC2046 section 5.1.1

        boundary := 0*69<bchars> bcharsnospace
        bchars := bcharsnospace / " "
        bcharsnospace := DIGIT / ALPHA / "'" / "(" / ")" /
            "+" / "_" / "," / "-" / "." /
            "/" / ":" / "=" / "?"
        */
        i = 0;
        do{
            ch = ret[i++];
        }while(
            i <= length && (
                ('\'' <= ch && ch <= ')') || // '\'', '(' ')'
                ('+' <= ch && ch <= ':') || // '+', ',', '-', '.', '/', NUM, ':'
                ch == '=' ||
                ch == '?' ||
                ('A' <= ch && ch <= 'Z') ||
                ch == '_' ||
                ('a' <= ch && ch <= 'z') ||
                ch == ' '
            )
        );
        i--;
        // check boundary end
        if (use_quote) {
            if(ret[i] != '"'){
                cat_update_last_error(0, "quote not end");
                return cat_false;
            }
            // check if ends with space
            if (ret[i-1] == ' ') {
                cat_update_last_error(0, "ends with space");
                return cat_false;
            }
        } else {
            // roll back if ends with space
            for (; i>0 && i<=length && ret[i-1] == ' '; i--);
        }
        if (i < 1){
            cat_update_last_error(0, "no legal boundary found");
            return cat_false;
        }
        CAT_LOG_DEBUG_V2(PROTOCOL, "http parser found multipart boundary: \"%.*s\"", i, ret);
        // verify next
        consume(i);
        if(use_quote){
            consume(2);
        }
        // skip ows
        while ((*at == '\t' || *at == ' ') && length > 0) {
            consume(1);
        }
        if(length > 0 && *at != ';'){
            cat_update_last_error(0, "extra char at end");
            return cat_false;
        }
        *_at = ret;
        *_length = i;
        return cat_true;
    }
    cat_update_last_error(0, "no legal boundary found");
    return cat_false;
}

CAT_HTTP_PARSER_ON_DATA_BEGIN(header_value,           HEADER_VALUE    ) {
    static const string_int multipart[] = {
        {'m','u','l','t',},
        {'i','p','a','r',},
    };

    if (1/*enable mp*/ && parser->multipart.mp_status == CAT_HTTP_MULTIPART_IN_CONTENT_TYPE) {
        CAT_LOG_DEBUG_V3(PROTOCOL, "http parser checking content-type header");
        size_t i = 0;
        // skip ows
        while ((at[i] == '\t' || at[i] == ' ') && i < length) {
            i++;
        }
        const char *mp_at = &at[i];
        if (length >= 10 &&
            '/' == mp_at[9] &&
            multipart[0].u32 == (((uint32_t *)mp_at)[0] | 0x20202020) &&
            multipart[1].u32 == (((uint32_t *)mp_at)[1] | 0x20202020) &&
            't' == (mp_at[8] | 0x20)
        ) {
            i = length - i;
            CAT_LOG_DEBUG_V3(PROTOCOL, "http parser header is multipart, parsing \"%.*s\"", (int)i, mp_at);
            if(!parse_boundary(&mp_at, &i)){
                CALLBACK_ERROR(BAD_BOUNDARY, cat_get_last_error_message());
            }
            CAT_ASSERT(mp_at[0] > CAT_HTTP_MULTIPART_STATUS_MAX);
            if(i > 70){
                CALLBACK_ERROR(BAD_BOUNDARY, "multipart boundary is too long");
            }
            if (0 != multipart_parser_init(&parser->multipart, mp_at, i, &cat_http_multipart_parser_settings)){
                CALLBACK_ERROR(MULTIPART, "failed initialize multipart parser: %d", errno);
            }
        } else {
            parser->multipart.mp_status = CAT_HTTP_MULTIPART_NOT_MULTIPART;
        }
    }
} CAT_HTTP_PARSER_ON_DATA_END()

CAT_HTTP_PARSER_ON_EVENT_BEGIN(headers_complete, HEADERS_COMPLETE) {
    parser->keep_alive = !!llhttp_should_keep_alive(&parser->llhttp);
    parser->content_length = parser->llhttp.content_length;
} CAT_HTTP_PARSER_ON_EVENT_END()

CAT_HTTP_PARSER_ON_DATA (body,                   BODY            )
CAT_HTTP_PARSER_ON_EVENT(chunk_header,           CHUNK_HEADER    )
CAT_HTTP_PARSER_ON_EVENT(chunk_complete,         CHUNK_COMPLETE  )

CAT_HTTP_PARSER_ON_EVENT_BEGIN(message_complete,       MESSAGE_COMPLETE) {
    parser->multipart.mp_status = CAT_HTTP_MULTIPART_UNINIT;
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
    NULL, NULL, NULL, NULL,
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
    parser->multipart.mp_status = CAT_HTTP_MULTIPART_UNINIT;
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

CAT_API cat_bool_t cat_http_parser_execute(cat_http_parser_t *parser, const char *data, size_t length)
{
    // TODO: convert llhttp errno to our own errno?
    llhttp_errno_t error;

    // bypass llhttp_execute if in multipart body
    if (
        1/*enable mp*/ &&
        parser->event & CAT_HTTP_PARSER_EVENT_FLAG_MULTIPART &&
        parser->multipart.mp_status == '-'
    ) {
        goto in_multipart;
    }

    parser->event = CAT_HTTP_PARSER_EVENT_NONE;
    error = llhttp_execute(&parser->llhttp, data, length);
    if (error != HPE_OK) {
        parser->parsed_length = llhttp_get_error_pos(&parser->llhttp) - data;
        if (unlikely(error != HPE_PAUSED)) {
            if (unlikely(error != HPE_PAUSED_UPGRADE)) {
                if (error > HPE_USER){
                    cat_update_last_error(error, "%s", cat_get_last_error_message());
                }else {
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

    if (
        1/*enable mp*/ &&
        parser->event == CAT_HTTP_PARSER_EVENT_BODY &&
        parser->multipart.mp_status == '-'
    ) {
        in_multipart:
        return cat_http_multipart_parser_execute(parser, data, length);
    }

    return cat_true;
}

CAT_API const char *cat_http_parser_event_name(cat_http_parser_event_t event)
{
    switch (event) {
#define CAT_HTTP_PARSER_EVENT_NAME_GEN(name, unused1) case CAT_HTTP_PARSER_EVENT_##name: return #name;
    CAT_HTTP_PARSER_EVENT_MAP(CAT_HTTP_PARSER_EVENT_NAME_GEN)
#undef CAT_HTTP_PARSER_EVENT_NAME_GEN
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
        parser->multipart.mp_status == '-'
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

/* multipart parser */

#define CAT_HTTP_MULTIPART_CB_FNAME(name) cat_http_multipart_parser_on_##name
#define CAT_HTTP_MULTIPART_ON_DATA(name, NAME) \
static int CAT_HTTP_MULTIPART_CB_FNAME(name)(multipart_parser *p, const char *at, size_t length){ \
    cat_http_parser_t* parser = cat_container_of(p, cat_http_parser_t, multipart); \
    parser->event = CAT_HTTP_PARSER_EVENT_##NAME; \
    CAT_LOG_DEBUG_V2(PROTOCOL, "http multipart parser data on_" # name ": [%d]%.*s", length, length, at); \
    if (((cat_http_parser_event_t) (parser->events & parser->event)) == parser->event) { \
        parser->data = at; \
        parser->data_length = length; \
        \
        return MPPE_PAUSED; \
    } \
    return MPPE_OK; \
}

#define CAT_HTTP_MULTIPART_ON_EVENT(name, NAME) \
static int CAT_HTTP_MULTIPART_CB_FNAME(name)(multipart_parser *p){ \
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

static int CAT_HTTP_MULTIPART_CB_FNAME(body_end)(multipart_parser *p){
    cat_http_parser_t* parser = cat_container_of(p, cat_http_parser_t, multipart);
    // escape mp parser
    parser->event = CAT_HTTP_PARSER_EVENT_MULTIPART_BODY_END;
    parser->multipart.mp_status = CAT_HTTP_MULTIPART_NOT_MULTIPART;
    if (((cat_http_parser_event_t) (parser->events & parser->event)) == parser->event) {
        return MPPE_PAUSED;
    }
    return MPPE_OK;
}

static const multipart_parser_settings cat_http_multipart_parser_settings = {
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

static cat_bool_t cat_http_multipart_parser_execute(cat_http_parser_t *parser, const char *data, size_t length){

    size_t len = 0;
    if(0 == (len = multipart_parser_execute((multipart_parser*)&parser->multipart, data, length))){
        cat_update_last_error(CAT_HTTP_ERRNO_MULTIPART, "HTTP-Parser execute failed: failed to parse multipart body");
        return cat_false;
    }
    // add ptr
    parser->multipart_ptr = data + len;
    parser->parsed_length = len;
    return cat_true;
}
