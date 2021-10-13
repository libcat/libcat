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
  |         codinghuang <2812240764@qq.com>                                  |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

#include "cat_http.h"

static const cat_const_string_t request_get = cat_const_string(
    "GET /get HTTP/1.1\r\n"
    "Host: www.foo.com\r\n"
    "User-Agent: curl/7.64.1\r\n"
    "Accept: */*\r\n"
    "Connection: keep-alive\r\n"
    "\r\n"
);

static const cat_const_string_t request_get_with_connection_close = cat_const_string(
    "GET /get HTTP/1.1\r\n"
    "Host: www.foo.com\r\n"
    "User-Agent: curl/7.64.1\r\n"
    "Accept: */*\r\n"
    "Connection: close\r\n"
    "\r\n"
);

static const cat_const_string_t request_get_http10 = cat_const_string(
    "GET /get HTTP/1.0\r\n"
    "Host: www.foo.com\r\n"
    "User-Agent: curl/7.64.1\r\n"
    "Accept: */*\r\n"
    "\r\n"
);

static const cat_const_string_t request_get_http10_with_keep_alive = cat_const_string(
    "GET /get HTTP/1.0\r\n"
    "Host: www.foo.com\r\n"
    "User-Agent: curl/7.64.1\r\n"
    "Accept: */*\r\n"
    "Connection: keep-alive\r\n"
    "\r\n"
);

static const cat_const_string_t request_post = cat_const_string(
    "POST /api/build/v1/foo HTTP/1.1\r\n"
    "Host: www.foo.com\r\n"
    "User-Agent: curl/7.64.1\r\n"
    "Accept: */*\r\n"
    "Content-Length: 7\r\n"
    "Content-Type: application/x-www-form-urlencoded\r\n"
    "\r\n"
    "foo=bar"
);

TEST(cat_http_parser, method_get_name)
{
#define CAT_HTTP_METHOD_VARS_GEN(id, name, string) char str_##name[] = #string;
      CAT_HTTP_METHOD_MAP(CAT_HTTP_METHOD_VARS_GEN)
#undef CAT_HTTP_METHOD_VARS_GEN
#define CAT_HTTP_METHOD_ASSERTIONS_GEN(id, name, string) ASSERT_STREQ(str_##name, cat_http_method_get_name(CAT_HTTP_METHOD_##name));
      CAT_HTTP_METHOD_MAP(CAT_HTTP_METHOD_ASSERTIONS_GEN)
#undef CAT_HTTP_METHOD_ASSERTIONS_GEN
    ASSERT_STREQ("UNKNOWN", cat_http_method_get_name(~0));
}

TEST(cat_http_parser, status_get_reason)
{
#define CAT_HTTP_STATUS_VARS_GEN(name, code, reason) char name[] = reason;
    CAT_HTTP_STATUS_MAP(CAT_HTTP_STATUS_VARS_GEN)
#undef CAT_HTTP_STATUS_VARS_GEN
#define CAT_HTTP_STATUS_ASSERTIONS_GEN(name, code, reason) ASSERT_STREQ(name, cat_http_status_get_reason(CAT_HTTP_STATUS_##name));
    CAT_HTTP_STATUS_MAP(CAT_HTTP_STATUS_ASSERTIONS_GEN)
#undef CAT_HTTP_STATUS_ASSERTIONS_GEN
    ASSERT_STREQ("UNKNOWN", cat_http_status_get_reason(~0));
}

TEST(cat_http_parser, create_null)
{
    cat_http_parser_t *parser;

    parser = cat_http_parser_create(nullptr);
    ASSERT_NE(nullptr, parser);

    cat_free(parser);
}

TEST(cat_http_parser, reset)
{
    cat_http_parser_t *parser, _parser;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    cat_http_parser_reset(parser);
    ASSERT_EQ(CAT_HTTP_METHOD_UNKNOWN, parser->llhttp.method);
    ASSERT_EQ(nullptr, parser->data);
    ASSERT_EQ(0, parser->data_length);
    ASSERT_EQ(cat_false, cat_http_parser_should_keep_alive(parser));
}

TEST(cat_http_parser, get_type)
{
    cat_http_parser_t *parser, _parser;
    cat_http_parser_type_t type;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    type = cat_http_parser_get_type(parser);
    ASSERT_EQ(parser->llhttp.type, type);
}

TEST(cat_http_parser, set_type)
{
    cat_http_parser_t *parser, _parser;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    ASSERT_TRUE(cat_http_parser_set_type(parser, CAT_HTTP_PARSER_TYPE_REQUEST));
    ASSERT_EQ(CAT_HTTP_PARSER_TYPE_REQUEST, parser->llhttp.type);
}

TEST(cat_http_parser, set_type_not_finish)
{
    cat_http_parser_t *parser, _parser;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    parser->llhttp.finish = HTTP_FINISH_UNSAFE;
    ASSERT_FALSE(cat_http_parser_set_type(parser, CAT_HTTP_PARSER_TYPE_REQUEST));
    ASSERT_EQ(CAT_EMISUSE, cat_get_last_error_code());
    ASSERT_STREQ("Change HTTP-Parser type during execution is not allowed", cat_get_last_error_message());
}

TEST(cat_http_parser, get_events)
{
    cat_http_parser_t *parser, _parser;
    cat_http_parser_events_t events;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    events = cat_http_parser_get_events(parser);
    ASSERT_EQ(parser->events, events);
}

TEST(cat_http_parser, get_event)
{
    cat_http_parser_t *parser, _parser;
    cat_http_parser_event_t event;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    event = cat_http_parser_get_event(parser);
    ASSERT_EQ(parser->event, event);
}

TEST(cat_http_parser, get_event_name)
{
    cat_http_parser_t *parser, _parser;
    const char *event_name;

    char NONE[] = "NONE";

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    event_name = cat_http_parser_get_event_name(parser);
    ASSERT_STREQ(NONE, event_name);
}

TEST(cat_http_parser, get_data)
{
    cat_http_parser_t *parser, _parser;
    const char *data;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    data = cat_http_parser_get_data(parser);
    ASSERT_STREQ(parser->data, data);
}

TEST(cat_http_parser, get_data_length)
{
    cat_http_parser_t *parser, _parser;
    size_t data_length;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    data_length = cat_http_parser_get_data_length(parser);
    ASSERT_EQ(parser->data_length, data_length);
}

TEST(cat_http_parser, should_keep_alive)
{
    cat_http_parser_t *parser, _parser;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    ASSERT_FALSE(cat_http_parser_should_keep_alive(parser));

    ASSERT_TRUE(cat_http_parser_execute(parser, request_get.data, request_get.length));
    ASSERT_TRUE(cat_http_parser_is_completed(parser));
    ASSERT_TRUE(cat_http_parser_should_keep_alive(parser));

    ASSERT_TRUE(cat_http_parser_execute(parser, request_get_with_connection_close.data, request_get_with_connection_close.length));
    ASSERT_TRUE(cat_http_parser_is_completed(parser));
    ASSERT_FALSE(cat_http_parser_should_keep_alive(parser));

    // always incomplete because previous request is "Connection: close"
    ASSERT_TRUE(cat_http_parser_execute(parser, request_get_http10.data, request_get_http10.length));
    ASSERT_FALSE(cat_http_parser_is_completed(parser));
    cat_http_parser_reset(parser);

    ASSERT_TRUE(cat_http_parser_execute(parser, request_get_http10.data, request_get_http10.length));
    ASSERT_TRUE(cat_http_parser_is_completed(parser));
    ASSERT_FALSE(cat_http_parser_should_keep_alive(parser));

    // always incomplete because previous request is HTTP/1.0 and "Connection: close" by default
    ASSERT_TRUE(cat_http_parser_execute(parser, request_get_http10_with_keep_alive.data, request_get_http10_with_keep_alive.length));
    ASSERT_FALSE(cat_http_parser_is_completed(parser));
    cat_http_parser_reset(parser);

    ASSERT_TRUE(cat_http_parser_execute(parser, request_get_http10_with_keep_alive.data, request_get_http10_with_keep_alive.length));
    ASSERT_TRUE(cat_http_parser_is_completed(parser));
    ASSERT_TRUE(cat_http_parser_should_keep_alive(parser));

    // without reset it also works because previous request is keep-alive
    ASSERT_TRUE(cat_http_parser_execute(parser, request_get_http10.data, request_get_http10.length));
    ASSERT_TRUE(cat_http_parser_is_completed(parser));
    ASSERT_FALSE(cat_http_parser_should_keep_alive(parser));
}

TEST(cat_http_parser, finish_safe)
{
    cat_http_parser_t *parser, _parser;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    parser->llhttp.finish = HTTP_FINISH_SAFE;
    ASSERT_TRUE(cat_http_parser_finish(parser));
}

TEST(cat_http_parser, finish_unsafe)
{
    cat_http_parser_t *parser, _parser;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    parser->llhttp.finish = HTTP_FINISH_UNSAFE;
    ASSERT_FALSE(cat_http_parser_finish(parser));
    ASSERT_STREQ("Invalid EOF state", parser->llhttp.reason);
    ASSERT_EQ(HPE_INVALID_EOF_STATE, cat_get_last_error_code());
    ASSERT_STREQ("HTTP-Parser finish failed: HPE_INVALID_EOF_STATE", cat_get_last_error_message());
}

TEST(cat_http_parser, get_error_code)
{
    cat_http_parser_t *parser, _parser;
    llhttp_errno_t llhttp_errno;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    llhttp_errno = cat_http_parser_get_error_code(parser);
    ASSERT_EQ(0, llhttp_errno);
}

TEST(cat_http_parser, get_error_message)
{
    cat_http_parser_t *parser, _parser;
    const char *error_message;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    error_message = cat_http_parser_get_error_message(parser);
    ASSERT_EQ(nullptr, error_message);
}

TEST(cat_http_parser, get_parsed_length)
{
    cat_http_parser_t *parser, _parser;
    size_t parsed_length = 0;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    cat_http_parser_set_events(parser, CAT_HTTP_PARSER_EVENTS_ALL);

    const char *p = request_get.data;
    const char *pe = request_get.data + request_get.length;

    while (true) {
        ASSERT_TRUE(cat_http_parser_execute(parser, p, pe - p));
        parsed_length += cat_http_parser_get_parsed_length(parser);

        if (cat_http_parser_is_completed(parser)) {
            ASSERT_EQ(request_get.length, parsed_length);
            break;
        }

        p = cat_http_parser_get_current_pos(parser);
    }
}

TEST(cat_http_parser, get_parsed_length2)
{
    cat_http_parser_t *parser, _parser;
    size_t parsed_length;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);

    const char *p = request_get.data;
    const char *pe = request_get.data + request_get.length;
    ASSERT_TRUE(cat_http_parser_execute(parser, p, pe - p));

    ASSERT_TRUE(cat_http_parser_is_completed(parser));

    parsed_length = cat_http_parser_get_parsed_length(parser);
    ASSERT_EQ(request_get.length, parsed_length);
}

TEST(cat_http_parser, get_protocol_version)
{
    cat_http_parser_t *parser, _parser;
    size_t parsed_length;

    const cat_const_string_t request_101 = cat_const_string(
        "GET /get HTTP/1.0\r\n"
        "\r\n"
    );

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    cat_http_parser_set_events(parser, CAT_HTTP_PARSER_EVENTS_ALL);

    const char *p = request_101.data;
    const char *pe = request_101.data + request_101.length;

    while (true) {
        ASSERT_TRUE(cat_http_parser_execute(parser, p, pe - p));

        if (cat_http_parser_is_completed(parser)) {
            parsed_length = cat_http_parser_get_current_offset(parser, request_101.data);
            ASSERT_EQ(request_101.length, parsed_length);
            break;
        }

        p = cat_http_parser_get_current_pos(parser);
    }

    char v100[] = "1.0";
    ASSERT_STREQ(v100, cat_http_parser_get_protocol_version(parser));
}

TEST(cat_http_parser, get_protocol_version_unknown)
{
    cat_http_parser_t *parser, _parser;
    size_t parsed_length;

    const cat_const_string_t request_unknown = cat_const_string(
        "GET /get HTTP/0.0\r\n"
        "\r\n"
    );

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    cat_http_parser_set_events(parser, CAT_HTTP_PARSER_EVENTS_ALL);

    const char *p = request_unknown.data;
    const char *pe = request_unknown.data + request_unknown.length;

    while (true) {
        ASSERT_TRUE(cat_http_parser_execute(parser, p, pe - p));

        if (cat_http_parser_is_completed(parser)) {
            parsed_length = cat_http_parser_get_current_offset(parser, request_unknown.data);
            ASSERT_EQ(request_unknown.length, parsed_length);
            break;
        }

        p = cat_http_parser_get_current_pos(parser);
    }

    char unknown[] = "UNKNOWN";
    ASSERT_STREQ(unknown, cat_http_parser_get_protocol_version(parser));
}

TEST(cat_http_parser, get_status_code)
{
    cat_http_parser_t *parser, _parser;
    cat_http_status_code_t status_code;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    status_code = cat_http_parser_get_status_code(parser);

    ASSERT_EQ(parser->llhttp.status_code, status_code);
}

TEST(cat_http_parser, is_upgrade)
{
    cat_http_parser_t *parser, _parser;
    cat_bool_t is_upgrade;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    is_upgrade = cat_http_parser_is_upgrade(parser);

    ASSERT_FALSE(is_upgrade);
    // TODO: true case
}

TEST(cat_http_parser, get_content_length)
{
    cat_http_parser_t *parser, _parser;
    cat_http_parser_event_t event;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    cat_http_parser_set_events(parser, CAT_HTTP_PARSER_EVENTS_ALL);

    const char *p = request_post.data;
    const char *pe = request_post.data + request_post.length;

    while (true) {
        ASSERT_TRUE(cat_http_parser_execute(parser, p, pe - p));
        event = cat_http_parser_get_event(parser);
        if (event == CAT_HTTP_PARSER_EVENT_HEADERS_COMPLETE) {
            ASSERT_EQ(7, cat_http_parser_get_content_length(parser));
        } else if (event == CAT_HTTP_PARSER_EVENT_MESSAGE_COMPLETE) {
            size_t parsed_length = cat_http_parser_get_current_offset(parser, request_post.data);
            ASSERT_EQ(request_post.length, parsed_length);
            break;
        }
        p = cat_http_parser_get_current_pos(parser);
    }
}

TEST(cat_http_parser, get_method_name)
{
    cat_http_parser_t *parser, _parser;

    ASSERT_EQ((parser = cat_http_parser_create(&_parser)), &_parser);
    cat_http_parser_set_events(parser, CAT_HTTP_PARSER_EVENTS_ALL);

    const char *p = request_get.data;
    const char *pe = request_get.data + request_get.length;

    while (true) {
        ASSERT_TRUE(cat_http_parser_execute(parser, p, pe - p));

        if (parser->event & CAT_HTTP_PARSER_EVENT_FLAG_DATA) {
            if (parser->event & CAT_HTTP_PARSER_EVENT_FLAG_LINE) {
                char GET[] = "GET";
                ASSERT_STREQ(GET, cat_http_parser_get_method_name(parser));
            }
            break;
        }

        if (cat_http_parser_is_completed(parser)) {
            break;
        }

        p = cat_http_parser_get_current_pos(parser);
    }
}

TEST(cat_http_parser, none)
{
    cat_http_parser_t parser;
    ASSERT_EQ(cat_http_parser_create(&parser), &parser);
    const cat_const_string_t *requests[] = { &request_get, &request_post };
    for (size_t i = 0; i < CAT_ARRAY_SIZE(requests); i++) {
        const cat_const_string_t *request = requests[i];
        for (int n = 2; n--;) {
            ASSERT_TRUE(cat_http_parser_execute(&parser, request->data, request->length));
            ASSERT_TRUE(cat_http_parser_is_completed(&parser));
            ASSERT_TRUE(cat_http_parser_should_keep_alive(&parser));
        }
        const char *p = request->data;
        while (true) {
            ASSERT_TRUE(cat_http_parser_execute(&parser, p++, 1));
            if (cat_http_parser_is_completed(&parser)) {
                break;
            }
        }
        ASSERT_EQ(p, request->data + request->length);
        ASSERT_EQ(p, parser.data + parser.data_length + (cat_http_parser_get_method(&parser) == CAT_HTTP_METHOD_GET ? CAT_STRLEN("\r\n\r\n") : 0));
    }
}

TEST(cat_http_parser, all)
{
    cat_http_parser_t parser;
    ASSERT_EQ(cat_http_parser_create(&parser), &parser);
    cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENTS_ALL);

    const char *p = request_get.data;
    const char *pe = request_get.data + request_get.length;
    while (true) {
        ASSERT_TRUE(cat_http_parser_execute(&parser, p, pe - p));
        if (parser.event & CAT_HTTP_PARSER_EVENT_FLAG_DATA) {
            if (parser.event & CAT_HTTP_PARSER_EVENT_FLAG_LINE) {
                ASSERT_EQ(cat_http_parser_get_method(&parser), CAT_HTTP_METHOD_GET);
            }
            CAT_LOG_DEBUG(TEST, "%s: %.*s", cat_http_parser_get_event_name(&parser), (int ) parser.data_length, parser.data);
        } else {
            CAT_LOG_DEBUG(TEST, "%s", cat_http_parser_get_event_name(&parser));
        }
        if (cat_http_parser_is_completed(&parser)) {
            CAT_LOG_DEBUG(TEST, "Completed, error=%s", cat_http_parser_get_error_message(&parser));
            ASSERT_EQ(cat_http_parser_get_major_version(&parser), 1);
            ASSERT_EQ(cat_http_parser_get_minor_version(&parser), 1);
            break;
        }
        p = cat_http_parser_get_current_pos(&parser);
    }
}

TEST(cat_http_parser, byte_by_byte)
{
    cat_http_parser_t parser;
    ASSERT_EQ(cat_http_parser_create(&parser), &parser);
    cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENTS_ALL);

    size_t step_length = 1;
    const char *p = request_post.data;
    const char *pe = p + step_length;
    const char *de = request_post.data + request_post.length;
    while (true) {
        ASSERT_TRUE(cat_http_parser_execute(&parser, p, pe - p));
        p += cat_http_parser_get_parsed_length(&parser);
        CAT_LOG_DEBUG(TEST, "parsed %zu bytes", cat_http_parser_get_parsed_length(&parser));
        if (p == pe || parser.event == CAT_HTTP_PARSER_EVENT_NONE) {
            pe += step_length; // recv more bytes
            if (pe > de) {
                pe = de;
            }
        }
        if (parser.event & CAT_HTTP_PARSER_EVENT_FLAG_DATA) {
            CAT_LOG_DEBUG(TEST, "%s: \"%.*s\"", cat_http_parser_get_event_name(&parser), (int ) parser.data_length, parser.data);
        } else {
            CAT_LOG_DEBUG(TEST, "%s", cat_http_parser_get_event_name(&parser));
        }
        if (cat_http_parser_is_completed(&parser)) {
            CAT_LOG_DEBUG(TEST, "Completed, error=%s", cat_http_parser_get_error_message(&parser));
            ASSERT_EQ(cat_http_parser_get_major_version(&parser), 1);
            ASSERT_EQ(cat_http_parser_get_minor_version(&parser), 1);
            ASSERT_EQ(cat_http_parser_get_method(&parser), CAT_HTTP_METHOD_POST);
            break;
        }
    }
}

static const cat_const_string_t multipart_req_heads[] = {
    // normal multipart form
    cat_const_string(
        "POST /upload HTTP/1.1\r\n"
        "Host: www.foo.com\r\n"
        "User-Agent: SomeBadBot/1\r\n"
        "Accept: */*\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: multipart/form; boundary=%s\r\n"
        "\r\n"
    ),
    // strange letter case
    cat_const_string(
        "POST /upload HTTP/1.1\r\n"
        "Host: www.foo.com\r\n"
        "User-Agent: SomeBadBot/2\r\n"
        "Accept: */*\r\n"
        "ConTenT-LengTh: %d\r\n"
        "conTent-tyPe: MultiPart/fORm; boundarY=%s\r\n"
        "\r\n"
    ),
    // with charset and strange ows
    cat_const_string(
        "POST /upload HTTP/1.1\r\n"
        "Host: www.foo.com\r\n"
        "User-Agent: SomeBadBot/3\r\n"
        "Accept: */*\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: MultiPart/fORm;\t charsEt=utF-8      ; boundarY=%s\r\n"
        "\r\n"
    ),
    // normal 206
    cat_const_string(
        "HTTP/1.1 206 Partial Content\r\n"
        "Date: Wed, 15 Nov 1995 06:25:24 GMT\r\n"
        "Last-Modified: Wed, 15 Nov 1995 04:58:08 GMT\r\n"
        "Server: SomeBadServer/4\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: multipart/byteranges;\t charsEt=utF-8      ; boundarY=%s\r\n"
        "\r\n"
    ),
};

static const cat_const_string_t multipart_req_heads_bad[] = {
    // no boundary
    cat_const_string(
        "POST /upload HTTP/1.1\r\n"
        "Host: www.foo.com\r\n"
        "User-Agent: SomeBadBot/1\r\n"
        "Accept: */*\r\n"
        "Content-Length: %d\r\n"
        "X-Not-boundary: %s\r\n"
        "Content-Type: MultiPart/fORm\r\n"
        "\r\n"
    ),
    // no boundary
    cat_const_string(
        "POST /upload HTTP/1.1\r\n"
        "Host: www.foo.com\r\n"
        "User-Agent: SomeBadBot/2\r\n"
        "Accept: */*\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: MultiPart/fORm;\t charsEt=utF-8;miao=%s  ;\r\n"
        "\r\n"
    ),
    // duplicate content-type
    cat_const_string(
        "POST /upload HTTP/1.1\r\n"
        "Host: www.foo.com\r\n"
        "User-Agent: SomeBadBot/3\r\n"
        "Accept: */*\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: MultiPart/fORm;\t charsEt=utF-8;boundary=%s  ;\r\n"
        "Content-Type: application/json  ;\r\n"
        "\r\n"
    ),
    // duplicate content-type
    cat_const_string(
        "HTTP/1.1 206 Partial Content\r\n"
        "Date: Wed, 15 Nov 1995 06:25:24 GMT\r\n"
        "Last-Modified: Wed, 15 Nov 1995 04:58:08 GMT\r\n"
        "Server: SomeBadServer/4\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: multipart/byteranges;\t boundary=%s\r\n"
        "Content-Type: application/json\r\n"
        "\r\n"
    ),
};

static struct{
    const char *literal;
    const char *real;
} boundaries[] = {
    {"cafebabe", "cafebabe"},
    {"\"cafebabe\"", "cafebabe"},
    {"\"cafebabe\" \t", "cafebabe"},
    {"cafebabe\t", "cafebabe"},
    {"cafebabe\t ", "cafebabe"},
    {"cafebabe \t", "cafebabe"},
    {"dix's bound-ary_1:OK", "dix's bound-ary_1:OK"},
    {"\"dix's bound-ary_1:OK\"", "dix's bound-ary_1:OK"},
    {"foo bar", "foo bar"},
    {"foo bar ", "foo bar"},
    {"\"foo bar\" ", "foo bar"},
    {" foo bar ", " foo bar"},
    {"\" foo bar\" ", " foo bar"},
    {
        "1234567890"
        "2234567890"
        "3234567890"
        "4234567890"
        "5234567890"
        "6234567890"
        "7234567890",
        "1234567890"
        "2234567890"
        "3234567890"
        "4234567890"
        "5234567890"
        "6234567890"
        "7234567890",
    },
    {
        "\"1234567890"
        "2234567890"
        "3234567890"
        "4234567890"
        "5234567890"
        "6234567890"
        "7234567890\"",
        "1234567890"
        "2234567890"
        "3234567890"
        "4234567890"
        "5234567890"
        "6234567890"
        "7234567890",
    },
};

static const char *boundaries_bad[] = {
    "e=mc^2", // bad char
    "{}cafebabe", // bad char
    "\"cafebabe", // unmatched quote
    "\"cafebabe\" ceshi", // extra part
    "1234567890"
    "2234567890"
    "3234567890"
    "4234567890"
    "5234567890"
    "6234567890"
    "7234567890"
    "ceshi", // too long
    "\"cafebabe \"", // space end
    "\" \"", // empty
    "\"\"", // empty
    "", // empty
};

#define CONTINUE_PARSE() do {\
    p = cat_http_parser_get_current_pos(&parser); \
    ASSERT_TRUE(cat_http_parser_execute(&parser, p, pe - p)); \
    if (parser.event & CAT_HTTP_PARSER_EVENT_FLAG_DATA) { \
        CAT_LOG_DEBUG_V3(TEST, "Paused, event: %s, data %.*s", cat_http_parser_event_name(parser.event), (int)parser.data_length, parser.data); \
    } else { \
        CAT_LOG_DEBUG_V3(TEST, "Paused, event: %s", cat_http_parser_event_name(parser.event)); \
    } \
} while(0)

#define ASSERT_DATA(NAME, expect) do { \
    ASSERT_EQ(parser.event, CAT_HTTP_PARSER_EVENT_##NAME); \
    ASSERT_EQ(parser.data_length, sizeof(expect) - 1); \
    ASSERT_EQ(std::string(parser.data, parser.data_length), std::string(expect)); \
    CONTINUE_PARSE(); \
} while(0)

#define ASSERT_EVENT(NAME) do { \
    ASSERT_EQ(parser.event, CAT_HTTP_PARSER_EVENT_##NAME); \
    CONTINUE_PARSE(); \
} while(0)

#define ASSERT_COMPLETE() do { \
    ASSERT_EQ(parser.event, CAT_HTTP_PARSER_EVENT_MESSAGE_COMPLETE); \
    ASSERT_TRUE(cat_http_parser_is_completed(&parser)); \
} while(0)


static const cat_const_string_t multipart_req_body = cat_const_string(
    "--%s\r\n"
    "Content-Disposition: form-data; name=\"description\"\r\n"
    "\r\n"
    "some text\r\n"
    "--%s\r\n"
    "Content-Disposition: form-data; name=\"myFile\"; filename=\"foo.txt\"\r\n"
    "Content-Type: text/plain\r\n"
    "\r\n"
    "content of the uploaded file foo.txt\r\n"
    "--%s--"
);

#define ASSERT_MULTIPART_FLOW() do { \
    ASSERT_EVENT(MULTIPART_DATA_BEGIN); \
    ASSERT_DATA(MULTIPART_HEADER_FIELD, "Content-Disposition"); \
    ASSERT_DATA(MULTIPART_HEADER_VALUE, "form-data; name=\"description\""); \
    ASSERT_EVENT(MULTIPART_HEADERS_COMPLETE); \
    ASSERT_DATA(MULTIPART_DATA, "some text"); \
    ASSERT_EVENT(MULTIPART_DATA_END); \
    ASSERT_EVENT(MULTIPART_DATA_BEGIN); \
    ASSERT_DATA(MULTIPART_HEADER_FIELD, "Content-Disposition"); \
    ASSERT_DATA(MULTIPART_HEADER_VALUE, "form-data; name=\"myFile\"; filename=\"foo.txt\""); \
    ASSERT_DATA(MULTIPART_HEADER_FIELD, "Content-Type"); \
    ASSERT_DATA(MULTIPART_HEADER_VALUE, "text/plain"); \
    ASSERT_EVENT(MULTIPART_HEADERS_COMPLETE); \
    ASSERT_DATA(MULTIPART_DATA, "content of the uploaded file foo.txt"); \
    ASSERT_EVENT(MULTIPART_DATA_END); \
    ASSERT_COMPLETE(); \
} while(0)

TEST(cat_http_parser, multipart)
{
    cat_http_parser_t parser;
    ASSERT_EQ(cat_http_parser_create(&parser), &parser);
    cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENTS_ALL);

    char head_buf[8192];
    char body_buf[8192];

    for(int i = 0; i < CAT_ARRAY_SIZE(multipart_req_heads); i++){
        for(int j = 0; j < CAT_ARRAY_SIZE(boundaries); j++){
            const char *boundary = boundaries[j].literal;
            const char *boundary_real = boundaries[j].real;
            int body_len = sprintf(body_buf, multipart_req_body.data, boundary_real, boundary_real, boundary_real);
            int head_len = sprintf(head_buf, multipart_req_heads[i].data, body_len, boundary);
            memcpy(&head_buf[head_len], body_buf, body_len);
            head_buf[head_len + body_len] = '\0';

            // common success test
            const char *p = head_buf;
            const char *pe = &head_buf[head_len + body_len];
            CAT_LOG_DEBUG_V3(TEST, "Parsing data:\n%.*s\n\n", head_len + body_len, head_buf);
            while (true) {
                ASSERT_TRUE(cat_http_parser_execute(&parser, p, pe - p));
                if (parser.event == CAT_HTTP_PARSER_EVENT_HEADERS_COMPLETE) {
                    // test only, this should be private!
                    ASSERT_EQ(std::string(boundaries[j].real), std::string(parser.multipart.multipart_boundary + 2));
                }
                if (parser.event & CAT_HTTP_PARSER_EVENT_FLAG_MULTIPART) {
                    ASSERT_MULTIPART_FLOW();
                }
                if (cat_http_parser_is_completed(&parser)) {
                    CAT_LOG_DEBUG(TEST, "Completed, error=%s", cat_http_parser_get_error_message(&parser));
                    break;
                }
                ASSERT_FALSE(cat_http_parser_is_completed(&parser));
                p = cat_http_parser_get_current_pos(&parser);
            }
            // headers only
            head_len = sprintf(head_buf, multipart_req_heads[i].data, 0, boundary);
            p = head_buf;
            pe = &head_buf[head_len];
            while (true) {
                ASSERT_TRUE(cat_http_parser_execute(&parser, p, pe - p));
                if (parser.event == CAT_HTTP_PARSER_EVENT_HEADERS_COMPLETE) {
                    // test only, this should be private!
                    ASSERT_EQ(std::string(boundaries[j].real), std::string(parser.multipart.multipart_boundary + 2));
                }
                if (parser.event & CAT_HTTP_PARSER_EVENT_FLAG_MULTIPART) {
                    ASSERT_MULTIPART_FLOW();
                }
                if (cat_http_parser_is_completed(&parser)) {
                    CAT_LOG_DEBUG(TEST, "Completed, error=%s", cat_http_parser_get_error_message(&parser));
                    break;
                }
                ASSERT_FALSE(cat_http_parser_is_completed(&parser));
                p = cat_http_parser_get_current_pos(&parser);
            }
        }
        // use create to avoid res/req change
        cat_http_parser_create(&parser);
        cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENTS_ALL);
    }
}

TEST(cat_http_parser, multipart_only_data_cb)
{
    cat_http_parser_t parser;
    ASSERT_EQ(cat_http_parser_create(&parser), &parser);
    cat_http_parser_set_events(&parser,
        CAT_HTTP_PARSER_EVENT_MULTIPART_DATA |
        CAT_HTTP_PARSER_EVENT_MULTIPART_HEADER_FIELD |
        CAT_HTTP_PARSER_EVENT_MULTIPART_HEADER_VALUE
    );

    char head_buf[8192];
    char body_buf[8192];

    const char *boundary = boundaries[0].literal;
    int body_len = sprintf(body_buf, multipart_req_body.data, boundary, boundary, boundary);
    int head_len = sprintf(head_buf, multipart_req_heads[0].data, body_len, boundary);
    memcpy(&head_buf[head_len], body_buf, body_len);
    head_buf[head_len + body_len] = '\0';

    // common success test
    const char *p = head_buf;
    const char *pe = &head_buf[head_len + body_len];
    CAT_LOG_DEBUG_V3(TEST, "Parsing data:\n%.*s\n\n", head_len + body_len, head_buf);
    ASSERT_TRUE(cat_http_parser_execute(&parser, p, pe - p));
    ASSERT_DATA(MULTIPART_HEADER_FIELD, "Content-Disposition");
    ASSERT_DATA(MULTIPART_HEADER_VALUE, "form-data; name=\"description\"");
    ASSERT_DATA(MULTIPART_DATA, "some text");
    ASSERT_DATA(MULTIPART_HEADER_FIELD, "Content-Disposition");
    ASSERT_DATA(MULTIPART_HEADER_VALUE, "form-data; name=\"myFile\"; filename=\"foo.txt\"");
    ASSERT_DATA(MULTIPART_HEADER_FIELD, "Content-Type");
    ASSERT_DATA(MULTIPART_HEADER_VALUE, "text/plain");
    ASSERT_DATA(MULTIPART_DATA, "content of the uploaded file foo.txt");
    ASSERT_COMPLETE();

}

// from firefox
static const cat_const_string_t multipart_req_body_empty_body = cat_const_string(
    "-----------------------------6169044094038990135731635364\r\n"
    "Content-Disposition: form-data; name=\"Text\"\r\n"
    "\r\n"
    "foo bar\r\n"
    "-----------------------------6169044094038990135731635364\r\n"
    "Content-Disposition: form-data; name=\"Ceshi\"; filename=\"\"\r\n"
    "Content-Type: application/octet-stream\r\n"
    "\r\n"
    "\r\n"
    "-----------------------------6169044094038990135731635364--\r\n"
);

TEST(cat_http_parser, multipart_empty_body)
{
    cat_http_parser_t parser;
    ASSERT_EQ(cat_http_parser_create(&parser), &parser);
    cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENTS_ALL);

    char head_buf[8192];

    const char *boundary = "---------------------------6169044094038990135731635364";
    int head_len = sprintf(head_buf, multipart_req_heads[0].data, multipart_req_body_empty_body.length, boundary);
    memcpy(&head_buf[head_len], multipart_req_body_empty_body.data, multipart_req_body_empty_body.length);
    head_buf[head_len + multipart_req_body_empty_body.length] = '\0';

    // firefox test
    const char *p = head_buf;
    const char *pe = &head_buf[head_len + multipart_req_body_empty_body.length];
    CAT_LOG_DEBUG_V3(TEST, "Parsing data:\n%.*s\n\n", head_len + multipart_req_body_empty_body.length, head_buf);
    while (true) {
        ASSERT_TRUE(cat_http_parser_execute(&parser, p, pe - p));
        if (parser.event & CAT_HTTP_PARSER_EVENT_FLAG_MULTIPART) {
            ASSERT_EVENT(MULTIPART_DATA_BEGIN);
            ASSERT_DATA(MULTIPART_HEADER_FIELD, "Content-Disposition");
            ASSERT_DATA(MULTIPART_HEADER_VALUE, "form-data; name=\"Text\"");
            ASSERT_EVENT(MULTIPART_HEADERS_COMPLETE);
            ASSERT_DATA(MULTIPART_DATA, "foo bar");
            ASSERT_EVENT(MULTIPART_DATA_END);
            ASSERT_EVENT(MULTIPART_DATA_BEGIN);
            ASSERT_DATA(MULTIPART_HEADER_FIELD, "Content-Disposition");
            ASSERT_DATA(MULTIPART_HEADER_VALUE, "form-data; name=\"Ceshi\"; filename=\"\"");
            ASSERT_DATA(MULTIPART_HEADER_FIELD, "Content-Type");
            ASSERT_DATA(MULTIPART_HEADER_VALUE, "application/octet-stream");
            ASSERT_EVENT(MULTIPART_HEADERS_COMPLETE);
            ASSERT_DATA(MULTIPART_DATA, "");
            ASSERT_EVENT(MULTIPART_DATA_END);
            ASSERT_COMPLETE();
        }
        if (cat_http_parser_is_completed(&parser)) {
            CAT_LOG_DEBUG(TEST, "Completed, error=%s", cat_http_parser_get_error_message(&parser));
            break;
        }
        ASSERT_FALSE(cat_http_parser_is_completed(&parser));
        p = cat_http_parser_get_current_pos(&parser);
    }
    return;
}


// from firefox
static const cat_const_string_t multipart_req_body_multiline = cat_const_string(
    "-----------------------------6169044094038990135731635364\r\n"
    "Content-Disposition: form-data; name=\"Text\"\r\n"
    "\r\n"
    "foo bar\r\n"
    "-----------------------------6169044094038990135731635364\r\n"
    "Content-Disposition: form-data; name=\"Ceshi\"; filename=\"\"\r\n"
    "Content-Type: application/octet-stream\r\n"
    "\r\n"
    "cesh\ni2\r\n"
    "ceshi\r1\r\n"
    "ces\r\n--hi3\r\n"
    "-----------------------------6169044094038990135731635364--\r\n"
);

TEST(cat_http_parser, multipart_multiline)
{
    cat_http_parser_t parser;
    ASSERT_EQ(cat_http_parser_create(&parser), &parser);
    cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENTS_ALL);

    char head_buf[8192];

    const char *boundary = "---------------------------6169044094038990135731635364";
    int head_len = sprintf(head_buf, multipart_req_heads[0].data, multipart_req_body_multiline.length, boundary);
    memcpy(&head_buf[head_len], multipart_req_body_multiline.data, multipart_req_body_multiline.length);
    head_buf[head_len + multipart_req_body_multiline.length] = '\0';

    // multi line multipart contents
    const char *p = head_buf;
    const char *pe = &head_buf[head_len + multipart_req_body_multiline.length];
    CAT_LOG_DEBUG_V3(TEST, "Parsing data:\n%.*s\n\n", head_len + multipart_req_body_multiline.length, head_buf);
    while (true) {
        ASSERT_TRUE(cat_http_parser_execute(&parser, p, pe - p));
        if (parser.event & CAT_HTTP_PARSER_EVENT_FLAG_MULTIPART) {
            ASSERT_EVENT(MULTIPART_DATA_BEGIN);
            ASSERT_DATA(MULTIPART_HEADER_FIELD, "Content-Disposition");
            ASSERT_DATA(MULTIPART_HEADER_VALUE, "form-data; name=\"Text\"");
            ASSERT_EVENT(MULTIPART_HEADERS_COMPLETE);
            ASSERT_DATA(MULTIPART_DATA, "foo bar");
            ASSERT_EVENT(MULTIPART_DATA_END);
            ASSERT_EVENT(MULTIPART_DATA_BEGIN);
            ASSERT_DATA(MULTIPART_HEADER_FIELD, "Content-Disposition");
            ASSERT_DATA(MULTIPART_HEADER_VALUE, "form-data; name=\"Ceshi\"; filename=\"\"");
            ASSERT_DATA(MULTIPART_HEADER_FIELD, "Content-Type");
            ASSERT_DATA(MULTIPART_HEADER_VALUE, "application/octet-stream");
            ASSERT_EVENT(MULTIPART_HEADERS_COMPLETE);
            ASSERT_DATA(MULTIPART_DATA, "cesh\ni2\r\n" "ceshi\r1\r\n" "ces\r\n--hi3");
            ASSERT_EVENT(MULTIPART_DATA_END);
            ASSERT_COMPLETE();
        }
        if (cat_http_parser_is_completed(&parser)) {
            CAT_LOG_DEBUG(TEST, "Completed, error=%s", cat_http_parser_get_error_message(&parser));
            break;
        }
        ASSERT_FALSE(cat_http_parser_is_completed(&parser));
        p = cat_http_parser_get_current_pos(&parser);
    }
    return;
}

#define SASSERT_DATA(ev, data) \
    if (__LINE__ >= state) { \
        ASSERT_DATA(ev, data); \
        state = __LINE__ + 1; \
        continue; \
    }
#define SASSERT_EVENT(ev) \
    if (__LINE__ >= state) { \
        ASSERT_EVENT(ev); \
        state = __LINE__ + 1; \
        continue; \
    }

TEST(cat_http_parser, multipart_stream)
{
    cat_http_parser_t parser;
    ASSERT_EQ(cat_http_parser_create(&parser), &parser);
    cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENTS_ALL);

    char http_buf[8192];
    char body_buf[8192];
    size_t body_i = 0;

    const char *boundary = "---------------------------6169044094038990135731635364";
    int head_len = sprintf(http_buf, multipart_req_heads[0].data, multipart_req_body_multiline.length, boundary);
    memcpy(&http_buf[head_len], multipart_req_body_multiline.data, multipart_req_body_multiline.length);
    http_buf[head_len + multipart_req_body_multiline.length] = '\0';

    // multi line multipart contents
    const char *p = http_buf;
    const char *pe = &http_buf[head_len + multipart_req_body_multiline.length];

    size_t slice_a_len = 128;
    size_t slice_b_len = pe - p - slice_a_len;
    int state = 0;

    pe = p + slice_a_len;

    CAT_LOG_DEBUG_V3(TEST, "Parsing data slice A:\n%.*s\n\n", slice_a_len, p);
    CAT_LOG_DEBUG_V3(TEST, "Parsing data slice B:\n%.*s\n\n", slice_b_len, p + slice_a_len);

    ASSERT_TRUE(cat_http_parser_execute(&parser, p, pe - p));
    CAT_LOG_DEBUG_V3(TEST, "Paused, event: %s", cat_http_parser_event_name(parser.event));
    while (true) {
        if (CAT_HTTP_PARSER_EVENT_NONE == parser.event) {
            // slice done
            break;
        }
        SASSERT_EVENT(MESSAGE_BEGIN);
        SASSERT_DATA(URL, "/upload");
        SASSERT_DATA(HEADER_FIELD, "Host");
        SASSERT_DATA(HEADER_VALUE, "www.foo.com");
        SASSERT_DATA(HEADER_FIELD, "User-Agent");
        SASSERT_DATA(HEADER_VALUE, "SomeBadBot/1");
        SASSERT_DATA(HEADER_FIELD, "Accept");
        SASSERT_DATA(HEADER_VALUE, "*/*");
        SASSERT_DATA(HEADER_FIELD, "Content-Length");
        SASSERT_DATA(HEADER_VALUE, "366");
        SASSERT_DATA(HEADER_FIELD, "Content-Type");
        CONTINUE_PARSE();
        CONTINUE_PARSE();
        CONTINUE_PARSE();
        CONTINUE_PARSE();
        CONTINUE_PARSE();
        CONTINUE_PARSE();
        SASSERT_DATA(HEADER_VALUE, "multipart/form; boundary=---------------------------6169044094038990135731635364");
        SASSERT_EVENT(MULTIPART_DATA_BEGIN);
        SASSERT_DATA(MULTIPART_HEADER_FIELD, "Content-Disposition");
        SASSERT_DATA(MULTIPART_HEADER_VALUE, "form-data; name=\"Text\"");
        SASSERT_EVENT(MULTIPART_HEADERS_COMPLETE);
        if (state <= __LINE__) {
            if (parser.event == CAT_HTTP_PARSER_EVENT_MULTIPART_DATA_END) {
                // check data
                ASSERT_EQ(std::string("foo bar"), std::string(body_buf, body_i));
                body_i = 0;
            } else if (parser.event == CAT_HTTP_PARSER_EVENT_MULTIPART_DATA) {
                // save read data
                memcpy(body_buf + body_i, parser.data, parser.data_length);
                body_i += parser.data_length;
                continue;
            } else {
                ASSERT_TRUE(
                    parser.event == CAT_HTTP_PARSER_EVENT_MULTIPART_DATA_END ||
                    parser.event == CAT_HTTP_PARSER_EVENT_MULTIPART_DATA);
            }
        }
        SASSERT_EVENT(MULTIPART_DATA_END);
        SASSERT_EVENT(MULTIPART_DATA_BEGIN);
        SASSERT_DATA(MULTIPART_HEADER_FIELD, "Content-Disposition");
        SASSERT_DATA(MULTIPART_HEADER_VALUE, "form-data; name=\"Ceshi\"; filename=\"\"");
        SASSERT_DATA(MULTIPART_HEADER_FIELD, "Content-Type");
        SASSERT_DATA(MULTIPART_HEADER_VALUE, "application/octet-stream");
        SASSERT_EVENT(MULTIPART_HEADERS_COMPLETE);
        if (state <= __LINE__) {
            if (parser.event == CAT_HTTP_PARSER_EVENT_MULTIPART_DATA_END) {
                // check data
                ASSERT_EQ(std::string("cesh\ni2\r\n" "ceshi\r1\r\n" "ces\r\n--hi3"), std::string(body_buf, body_i));
                body_i = 0;
            } else if (parser.event == CAT_HTTP_PARSER_EVENT_MULTIPART_DATA) {
                // save read data
                memcpy(body_buf + body_i, parser.data, parser.data_length);
                body_i += parser.data_length;
                continue;
            } else {
                ASSERT_TRUE(
                    parser.event == CAT_HTTP_PARSER_EVENT_MULTIPART_DATA_END ||
                    parser.event == CAT_HTTP_PARSER_EVENT_MULTIPART_DATA);
            }
        }
        SASSERT_EVENT(MULTIPART_DATA_END);
        ASSERT_EQ(parser.event, CAT_HTTP_PARSER_EVENT_MESSAGE_COMPLETE);
        ASSERT_TRUE(cat_http_parser_is_completed(&parser));
    }
    ASSERT_TRUE(cat_http_parser_is_completed(&parser));
}

TEST(cat_http_parser, multipart_bad_boundaries)
{
    cat_http_parser_t parser;
    ASSERT_EQ(cat_http_parser_create(&parser), &parser);
    cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENTS_ALL);

    char head_buf[8192];
    char body_buf[8192];
    // bad boundaries
    for(int i = 0; i < CAT_ARRAY_SIZE(multipart_req_heads); i++){
        for(int j = 0; j < CAT_ARRAY_SIZE(boundaries_bad); j++){
            const char *boundary = boundaries_bad[j];
            int body_len = sprintf(body_buf, multipart_req_body.data, boundary, boundary, boundary);
            int head_len = sprintf(head_buf, multipart_req_heads[i].data, body_len, boundary);
            memcpy(&head_buf[head_len], body_buf, body_len);
            head_buf[head_len + body_len] = '\0';
            CAT_LOG_DEBUG_V3(TEST, "Parsing data:\n%.*s\n\n", head_len + body_len, head_buf);
            const char *p = head_buf;
            const char *pe = &head_buf[head_len + body_len];
            while (true) {
                if(!cat_http_parser_execute(&parser, p, pe - p)){
                    CAT_LOG_DEBUG(TEST, "Parsing failed with: %d: %s", cat_get_last_error_code(), cat_get_last_error_message());
                    break;
                }
                ASSERT_FALSE(cat_http_parser_is_completed(&parser));
                p = cat_http_parser_get_current_pos(&parser);
            }
            // use create to avoid res/req change
            cat_http_parser_create(&parser);
            cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENTS_ALL);
        }
    }
}

TEST(cat_http_parser, multipart_bad_heads)
{
    cat_http_parser_t parser;
    ASSERT_EQ(cat_http_parser_create(&parser), &parser);
    cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENTS_ALL);

    char head_buf[8192];
    char body_buf[8192];
    // bad heads
    for(int i = 0; i < CAT_ARRAY_SIZE(multipart_req_heads_bad); i++){
        for(int j = 0; j < CAT_ARRAY_SIZE(boundaries); j++){
            const char *boundary = boundaries[j].literal;
            const char *boundary_real = boundaries[j].real;
            int body_len = sprintf(body_buf, multipart_req_body.data, boundary_real, boundary_real, boundary_real);
            int head_len = sprintf(head_buf, multipart_req_heads_bad[i].data, body_len, boundary);
            memcpy(&head_buf[head_len], body_buf, body_len);
            head_buf[head_len + body_len] = '\0';
            CAT_LOG_DEBUG_V3(TEST, "Parsing data:\n%.*s\n\n", head_len + body_len, head_buf);
            const char *p = head_buf;
            const char *pe = &head_buf[head_len + body_len];
            while (true) {
                if(!cat_http_parser_execute(&parser, p, pe - p)){
                    CAT_LOG_DEBUG(TEST, "Parsing failed with: %d: %s", cat_get_last_error_code(), cat_get_last_error_message());
                    break;
                }
                ASSERT_FALSE(cat_http_parser_is_completed(&parser));
                p = cat_http_parser_get_current_pos(&parser);
            }
            // use create to avoid res/req change
            cat_http_parser_create(&parser);
            cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENTS_ALL);
        }
    }
}
