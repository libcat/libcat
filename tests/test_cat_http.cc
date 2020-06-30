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
  | Author: Twosee <twose@qq.com>                                            |
  | Author: codinghuang <2812240764@qq.com>                                  |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

#include "cat_http.h"

static cat_http_parser_t parser;

static const cat_const_string_t request_get = cat_const_string(
    "GET /get HTTP/1.1\r\n"
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
#define CAT_HTTP_METHOD_VARS_GEN(id, name, string) char name[] = #string;
      CAT_HTTP_METHOD_MAP(CAT_HTTP_METHOD_VARS_GEN)
#undef CAT_HTTP_METHOD_VARS_GEN
#define CAT_HTTP_METHOD_ASSERTIONS_GEN(id, name, string) ASSERT_STREQ(name, cat_http_method_get_name(CAT_HTTP_METHOD_##name));
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

    parser = cat_http_parser_create(&_parser);
    cat_http_parser_reset(parser);
    ASSERT_EQ(CAT_HTTP_METHOD_UNKNOWN, parser->llhttp.method);
    ASSERT_EQ(nullptr, parser->data);
    ASSERT_EQ(0, parser->data_length);
    ASSERT_EQ(cat_false, parser->keep_alive);
}

TEST(cat_http_parser, get_type)
{
    cat_http_parser_t *parser, _parser;
    cat_http_parser_type_t type;

    parser = cat_http_parser_create(&_parser);
    type = cat_http_parser_get_type(parser);
    ASSERT_EQ(parser->llhttp.type, type);
}

TEST(cat_http_parser, set_type)
{
    cat_http_parser_t *parser, _parser;

    parser = cat_http_parser_create(&_parser);
    ASSERT_TRUE(cat_http_parser_set_type(parser, CAT_HTTP_PARSER_TYPE_REQUEST));
    ASSERT_EQ(CAT_HTTP_PARSER_TYPE_REQUEST, parser->llhttp.type);
}

TEST(cat_http_parser, set_type_not_finish)
{
    cat_http_parser_t *parser, _parser;

    parser = cat_http_parser_create(&_parser);
    parser->llhttp.finish = HTTP_FINISH_UNSAFE;
    ASSERT_FALSE(cat_http_parser_set_type(parser, CAT_HTTP_PARSER_TYPE_REQUEST));
    ASSERT_EQ(CAT_EMISUSE, cat_get_last_error_code());
    ASSERT_STREQ("Change HTTP-Parser type during execution is not allowed", cat_get_last_error_message());
}

TEST(cat_http_parser, get_events)
{
    cat_http_parser_t *parser, _parser;
    cat_http_parser_events_t events;

    parser = cat_http_parser_create(&_parser);
    events = cat_http_parser_get_events(parser);
    ASSERT_EQ(parser->events, events);
}

TEST(cat_http_parser, get_event)
{
    cat_http_parser_t *parser, _parser;
    cat_http_parser_event_t event;

    parser = cat_http_parser_create(&_parser);
    event = cat_http_parser_get_event(parser);
    ASSERT_EQ(parser->event, event);
}

TEST(cat_http_parser, get_event_name)
{
    cat_http_parser_t *parser, _parser;
    const char *event_name;

    char NONE[] = "NONE";

    parser = cat_http_parser_create(&_parser);
    event_name = cat_http_parser_get_event_name(parser);
    ASSERT_STREQ(NONE, event_name);
}

TEST(cat_http_parser, get_data)
{
    cat_http_parser_t *parser, _parser;
    const char *data;

    parser = cat_http_parser_create(&_parser);
    data = cat_http_parser_get_data(parser);
    ASSERT_STREQ(parser->data, data);
}

TEST(cat_http_parser, get_data_length)
{
    cat_http_parser_t *parser, _parser;
    size_t data_length;

    parser = cat_http_parser_create(&_parser);
    data_length = cat_http_parser_get_data_length(parser);
    ASSERT_EQ(parser->data_length, data_length);
}

TEST(cat_http_parser, should_keep_alive)
{
    cat_http_parser_t *parser, _parser;
    cat_bool_t keep_alive;

    parser = cat_http_parser_create(&_parser);
    keep_alive = cat_http_parser_should_keep_alive(parser);
    ASSERT_EQ(parser->keep_alive, keep_alive);
}

TEST(cat_http_parser, finish_safe)
{
    cat_http_parser_t *parser, _parser;

    parser = cat_http_parser_create(&_parser);
    parser->llhttp.finish = HTTP_FINISH_SAFE;
    ASSERT_TRUE(cat_http_parser_finish(parser));
}

TEST(cat_http_parser, finish_unsafe)
{
    cat_http_parser_t *parser, _parser;

    parser = cat_http_parser_create(&_parser);
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

    parser = cat_http_parser_create(&_parser);
    llhttp_errno = cat_http_parser_get_error_code(parser);
    ASSERT_EQ(0, llhttp_errno);
}

TEST(cat_http_parser, get_error_message)
{
    cat_http_parser_t *parser, _parser;
    const char *error_message;

    parser = cat_http_parser_create(&_parser);
    error_message = cat_http_parser_get_error_message(parser);
    ASSERT_EQ(nullptr, error_message);
}

TEST(cat_http_parser, get_parsed_length)
{
    cat_http_parser_t *parser, _parser;
    size_t parsed_length;

    parser = cat_http_parser_create(&_parser);
    cat_http_parser_set_events(parser, CAT_HTTP_PARSER_EVENTS_ALL);

    const char *p = request_get.data;
    const char *pe = request_get.data + request_get.length;

    while (true) {
        ASSERT_TRUE(cat_http_parser_execute(parser, p, pe - p));

        if (cat_http_parser_is_completed(parser)) {
            parsed_length = cat_http_parser_get_parsed_length(parser, request_get.data);
            ASSERT_EQ(request_get.length, parsed_length);
            break;
        }

        p = cat_http_parser_get_current_pos(parser);
    }
}

TEST(cat_http_parser, get_parsed_length_zero)
{
    cat_http_parser_t *parser, _parser;
    size_t parsed_length;

    parser = cat_http_parser_create(&_parser);

    const char *p = request_get.data;
    const char *pe = request_get.data + request_get.length;
    ASSERT_TRUE(cat_http_parser_execute(parser, p, pe - p));

    ASSERT_TRUE(cat_http_parser_is_completed(parser));

    parsed_length = cat_http_parser_get_parsed_length(parser, request_get.data);
    ASSERT_EQ(0, parsed_length);
}

TEST(cat_http_parser, get_protocol_version)
{
    cat_http_parser_t *parser, _parser;
    size_t parsed_length;

    const cat_const_string_t request_101 = cat_const_string(
        "GET /get HTTP/1.0\r\n"
        "\r\n"
    );

    parser = cat_http_parser_create(&_parser);
    cat_http_parser_set_events(parser, CAT_HTTP_PARSER_EVENTS_ALL);

    const char *p = request_101.data;
    const char *pe = request_101.data + request_101.length;

    while (true) {
        ASSERT_TRUE(cat_http_parser_execute(parser, p, pe - p));

        if (cat_http_parser_is_completed(parser)) {
            parsed_length = cat_http_parser_get_parsed_length(parser, request_101.data);
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

    parser = cat_http_parser_create(&_parser);
    cat_http_parser_set_events(parser, CAT_HTTP_PARSER_EVENTS_ALL);

    const char *p = request_unknown.data;
    const char *pe = request_unknown.data + request_unknown.length;

    while (true) {
        ASSERT_TRUE(cat_http_parser_execute(parser, p, pe - p));

        if (cat_http_parser_is_completed(parser)) {
            parsed_length = cat_http_parser_get_parsed_length(parser, request_unknown.data);
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

    parser = cat_http_parser_create(&_parser);
    status_code = cat_http_parser_get_status_code(parser);

    ASSERT_EQ(parser->llhttp.status_code, status_code);
}

TEST(cat_http_parser, is_upgrade)
{
    cat_http_parser_t *parser, _parser;
    cat_bool_t is_upgrade;

    parser = cat_http_parser_create(&_parser);
    is_upgrade = cat_http_parser_is_upgrade(parser);

    ASSERT_FALSE(is_upgrade);
}

TEST(cat_http_parser, get_content_length)
{
    cat_http_parser_t *parser, _parser;
    cat_http_parser_event_t event;

    parser = cat_http_parser_create(&_parser);
    cat_http_parser_set_events(parser, CAT_HTTP_PARSER_EVENTS_ALL);

    const char *p = request_post.data;
    const char *pe = request_post.data + request_post.length;

    while (true) {
        ASSERT_TRUE(cat_http_parser_execute(parser, p, pe - p));
        event = cat_http_parser_get_event(parser);
        if (event == CAT_HTTP_PARSER_EVENT_HEADERS_COMPLETE) {
            ASSERT_EQ(7, cat_http_parser_get_content_length(parser));
        } else if (event == CAT_HTTP_PARSER_EVENT_MESSAGE_COMPLETE) {
            size_t parsed_length = cat_http_parser_get_parsed_length(parser, request_post.data);
            ASSERT_EQ(request_post.length, parsed_length);
            break;
        }
        p = cat_http_parser_get_current_pos(parser);
    }
}

TEST(cat_http_parser, get_method_name)
{
    cat_http_parser_t *parser, _parser;

    parser = cat_http_parser_create(&_parser);
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
    ASSERT_NE(cat_http_parser_create(&parser), nullptr);
    const cat_const_string_t *requests[] = { &request_get, &request_post };
    for (size_t i = 0; i < CAT_ARRAY_SIZE(requests); i++) {
        const cat_const_string_t *request = requests[i];
        for (int n = 2; n--;) {
            ASSERT_TRUE(cat_http_parser_execute(&parser, request->data, request->length));
            ASSERT_TRUE(cat_http_parser_is_completed(&parser));
            ASSERT_TRUE(parser.keep_alive);
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
    ASSERT_NE(cat_http_parser_create(&parser), nullptr);
    cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENTS_ALL);

    const char *p = request_get.data;
    const char *pe = request_get.data + request_get.length;
    while (true) {
        ASSERT_TRUE(cat_http_parser_execute(&parser, p, pe - p));
        if (parser.event & CAT_HTTP_PARSER_EVENT_FLAG_DATA) {
            if (parser.event & CAT_HTTP_PARSER_EVENT_FLAG_LINE) {
                ASSERT_EQ(cat_http_parser_get_method(&parser), CAT_HTTP_METHOD_GET);
            }
            cat_debug(HTTP, "%s: %.*s", cat_http_parser_get_event_name(&parser), (int ) parser.data_length, parser.data);
        } else {
            cat_debug(HTTP, "%s", cat_http_parser_get_event_name(&parser));
        }
        if (cat_http_parser_is_completed(&parser)) {
            cat_debug(HTTP, "COMPELETED, error=%s", cat_http_parser_get_error_message(&parser));
            ASSERT_EQ(cat_http_parser_get_major_version(&parser), 1);
            ASSERT_EQ(cat_http_parser_get_minor_version(&parser), 1);
            break;
        }
        p = cat_http_parser_get_current_pos(&parser);
    }
}

