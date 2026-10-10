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
 */

#include "test.h"

TEST(cat_http_parser, upgrade_request)
{
    cat_http_parser_t parser;
    ASSERT_EQ(cat_http_parser_create(&parser), &parser);
    const char request[] =
        "GET /chat HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: Upgrade\r\n"
        "Upgrade: websocket\r\n\r\n";
    ASSERT_TRUE(cat_http_parser_execute(&parser, CAT_STRL(request)));
    ASSERT_TRUE(cat_http_parser_is_upgrade(&parser));
    ASSERT_EQ(cat_http_parser_get_method(&parser), CAT_HTTP_METHOD_GET);
    ASSERT_EQ(cat_http_parser_get_parsed_length(&parser), CAT_STRLEN(request));
}

TEST(cat_http_parser, malformed_request_and_reset)
{
    const char *requests[] = {
        "INVALID / HTTP/1.1\r\n\r\n",
        "GET / HTTP/x.y\r\n\r\n",
        "POST / HTTP/1.1\r\nContent-Length: invalid\r\n\r\n",
        "POST / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n",
    };
    cat_http_parser_t parser;
    ASSERT_EQ(cat_http_parser_create(&parser), &parser);
    for (const char *request : requests) {
        SCOPED_TRACE(request);
        ASSERT_FALSE(cat_http_parser_execute(&parser, request, strlen(request)));
        ASSERT_NE(cat_get_last_error_code(), 0);
        ASSERT_NE(cat_get_last_error_message(), nullptr);
        cat_http_parser_reset(&parser);
        ASSERT_TRUE(cat_http_parser_execute(&parser, CAT_STRL("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")));
        ASSERT_TRUE(cat_http_parser_is_completed(&parser));
        cat_http_parser_reset(&parser);
    }
}

TEST(cat_http_parser, chunked_body_events)
{
    cat_http_parser_t parser;
    ASSERT_EQ(cat_http_parser_create(&parser), &parser);
    cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENTS_ALL);
    const char request[] =
        "POST / HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
        "3\r\nabc\r\n2\r\nde\r\n0\r\n\r\n";
    const char *cursor = request;
    std::string body;
    std::vector<uint64_t> chunks;
    cat_http_parser_event_t previous = CAT_HTTP_PARSER_EVENT_NONE;
    while (!cat_http_parser_is_completed(&parser)) {
        ASSERT_TRUE(cat_http_parser_execute(&parser, cursor, request + CAT_STRLEN(request) - cursor));
        cat_http_parser_event_t event = cat_http_parser_get_event(&parser);
        if (event == CAT_HTTP_PARSER_EVENT_CHUNK_HEADER) {
            chunks.push_back(cat_http_parser_get_current_chunk_length(&parser));
        } else if (event == CAT_HTTP_PARSER_EVENT_BODY) {
            body.append(cat_http_parser_get_data(&parser), cat_http_parser_get_data_length(&parser));
        }
        const char *next = cat_http_parser_get_current_pos(&parser);
        ASSERT_GE(next, cursor);
        ASSERT_LE(next, request + CAT_STRLEN(request));
        // 同一位置可先报告 chunk 结束，再报告 message 结束；事件变化也代表进展。
        ASSERT_TRUE(next > cursor || event != previous || cat_http_parser_is_completed(&parser));
        previous = event;
        cursor = next;
    }
    ASSERT_TRUE(cat_http_parser_is_chunked(&parser));
    ASSERT_EQ(body, "abcde");
    ASSERT_EQ(chunks, (std::vector<uint64_t>{3, 2, 0}));
}

TEST(cat_http_parser, multipart_content_type_parameters)
{
    const char *types[] = {
        "multipart/form-data; charset=utf-8; boundary=test",
        "multipart/form-data; b=short; boundary=test",
        "multipart/form-data; bound=value; boundary=test",
        "multipart/form-data; boundary-extra=value; boundary=test",
        "multipart/form-data; binary=value; boundary=test",
        "multipart/form-data; boundary=test; charset=utf-8",
        "multipart/form-data; charset=\"utf-8\"; boundary=test",
        "multipart/form-data; note=\"a\\\"b\"; boundary=test",
        "multipart/form-data; boundary=\"test\"",
        "multipart/form-data; boundary=\"te\\st\"",
        "multipart/form-data; boundary=test; note=\"a\\\\b\"",
        "multipart/form-data; note=\"\x80\xff\"; boundary=test",
        "multipart/form-data \t; charset=utf-8; boundary=test",
        "multipart/form-data; 1=value; boundary=test",
        "multipart/form-data; b1=value; boundary=test",
        "multipart/form-data; boundary1=value; boundary=test",
        "multipart/form-data; note=\"escaped\\\tvalue\"; boundary=test",
    };
    const std::string content = "--test\r\nContent-Disposition: form-data; name=\"field\"\r\n\r\nabc\r\n--test--\r\n";
    for (const char *type : types) {
        SCOPED_TRACE(type);
        const std::string request = "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Type: " +
            std::string(type) + "\r\nContent-Length: " + std::to_string(content.length()) + "\r\n\r\n" + content;
        for (size_t input_size : {size_t(1), size_t(7), request.length()}) {
            SCOPED_TRACE(input_size);
            cat_http_parser_t parser;
            ASSERT_EQ(cat_http_parser_create(&parser), &parser);
            cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENTS_ALL);
            size_t offset = 0, iterations = 0;
            size_t available = std::min(input_size, request.length());
            std::string body;
            while (!cat_http_parser_is_completed(&parser)) {
                ASSERT_LT(iterations++, request.length() * 10);
                size_t size = available - offset;
                // Separate allocations prevent reading the next fragment before it arrives.
                std::vector<char> input(request.begin() + offset, request.begin() + available);
                ASSERT_TRUE(cat_http_parser_execute(&parser, input.empty() ? "" : input.data(), size));
                if (cat_http_parser_get_event(&parser) == CAT_HTTP_PARSER_EVENT_MULTIPART_BODY) {
                    ASSERT_TRUE(cat_http_parser_is_multipart(&parser));
                    ASSERT_EQ(std::string(parser.multipart.multipart_boundary, parser.multipart.boundary_length), "--test");
                    body.append(cat_http_parser_get_data(&parser), cat_http_parser_get_data_length(&parser));
                }
                size_t parsed = cat_http_parser_get_parsed_length(&parser);
                ASSERT_LE(parsed, size);
                if (cat_http_parser_get_event(&parser) == CAT_HTTP_PARSER_EVENT_MULTIPART_BODY) {
                    ASSERT_EQ(cat_http_parser_get_current_pos(&parser), input.data() + parsed);
                    ASSERT_EQ(cat_http_parser_get_current_offset(&parser, input.data()), parsed);
                }
                offset += parsed;
                available = std::min(request.length(), std::max(available, offset) + input_size);
            }
            // multipart 完成事件可能先于末尾 CRLF；未消费部分只能是终止换行。
            std::string remaining = request.substr(offset);
            ASSERT_TRUE(remaining.empty() || remaining == "\r\n" || remaining == "\n");
            ASSERT_EQ(body, "abc");
        }
    }
}

TEST(cat_http_parser, metadata_and_incomplete_message)
{
    cat_http_parser_t parser;
    ASSERT_EQ(cat_http_parser_create(&parser), &parser);
    ASSERT_TRUE(cat_http_parser_execute(&parser, "", 0));
    ASSERT_EQ(cat_http_parser_get_previous_event(&parser), CAT_HTTP_PARSER_EVENT_NONE);
    ASSERT_STREQ(cat_http_parser_get_previous_event_name(&parser), "NONE");
    ASSERT_STREQ(cat_http_parser_get_protocol_version(&parser), "UNKNOWN");
    ASSERT_EQ(cat_http_parser_get_current_offset(&parser, ""), 0);
    for (const auto &[version, expected] : std::array<std::pair<const char *, const char *>, 3>{
        {{"1.0", "1.0"}, {"1.1", "1.1"}, {"2.0", "2"}}}) {
        SCOPED_TRACE(version);
        cat_http_parser_reset(&parser);
        const std::string request = "GET / HTTP/" + std::string(version) + "\r\n\r\n";
        ASSERT_TRUE(cat_http_parser_execute(&parser, request.data(), request.size()));
        ASSERT_STREQ(cat_http_parser_get_protocol_version(&parser), expected);
    }
    cat_http_parser_reset(&parser);
    ASSERT_TRUE(cat_http_parser_execute(&parser, CAT_STRL("GET / HTTP/1.0\r\n\r\n")));
    ASSERT_EQ(cat_http_parser_get_major_version(&parser), 1);
    ASSERT_EQ(cat_http_parser_get_minor_version(&parser), 0);
    ASSERT_STREQ(cat_http_parser_get_protocol_version(&parser), "1.0");
    ASSERT_FALSE(cat_http_parser_should_keep_alive(&parser));
    ASSERT_EQ(cat_http_parser_create(&parser), &parser);
    ASSERT_TRUE(cat_http_parser_execute(&parser, CAT_STRL("HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n")));
    ASSERT_EQ(cat_http_parser_get_status_code(&parser), 201);
    ASSERT_STREQ(cat_http_parser_get_reason_phrase(&parser), "Created");
    ASSERT_TRUE(cat_http_parser_is_completed(&parser));
    ASSERT_EQ(cat_http_parser_create(&parser), &parser);
    ASSERT_TRUE(cat_http_parser_execute(&parser, CAT_STRL("POST / HTTP/1.1\r\nContent-Length: 4\r\n\r\na")));
    ASSERT_FALSE(cat_http_parser_is_completed(&parser));
    ASSERT_FALSE(cat_http_parser_finish(&parser));
    ASSERT_NE(cat_get_last_error_code(), 0);
}

TEST(cat_http_parser, invalid_multipart_metadata_preserves_http_body)
{
    const std::vector<std::string> types = {
        "text/plain; boundary=test",
        "multipart/form-data",
        "multipart/form-data; boundary=",
        "multipart/form-data; boundary=\"unterminated",
        "multipart/form-data; boundary=\"test \"",
        "multipart/form-data; boundary=test@",
        "multipart/form-data; note=\"unterminated; boundary=test",
        "multipart/form-data; note=\"escape\\",
        "multipart/form-data; boundary=" + std::string(71, 'a'),
        "multipart/@; boundary=test",
        "multipart/form/data; boundary=test",
        "multipart/form-data; @=value; boundary=test",
        "multipart/form-data; name@=value; boundary=test",
        "multipart/form-data; name=@; boundary=test",
        "multipart/form-data; note=\"valid\"@; boundary=test",
        "multipart/form-data; boundary@=test",
        "multipart/form-data; boundary=\"" + std::string(71, 'a') + "\"",
        "multipart/form-data; boundary=value; boundary=test",
    };
    for (const std::string &type : types) {
        SCOPED_TRACE(type);
        const std::string request = "POST / HTTP/1.1\r\nContent-Type: " + type +
            "\r\nContent-Length: 3\r\n\r\nabc";
        for (size_t input_size : {size_t(1), size_t(7), request.size()}) {
            SCOPED_TRACE(input_size);
            cat_http_parser_t parser;
            ASSERT_EQ(cat_http_parser_create(&parser), &parser);
            cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENTS_ALL);
            size_t offset = 0, iterations = 0;
            size_t available = std::min(input_size, request.size());
            std::string body;
            while (!cat_http_parser_is_completed(&parser)) {
                ASSERT_LT(iterations++, request.size() * 10);
                std::vector<char> input(request.begin() + offset, request.begin() + available);
                ASSERT_TRUE(cat_http_parser_execute(&parser, input.empty() ? "" : input.data(), input.size()));
                if (cat_http_parser_get_event(&parser) == CAT_HTTP_PARSER_EVENT_BODY) {
                    body.append(cat_http_parser_get_data(&parser), cat_http_parser_get_data_length(&parser));
                }
                size_t parsed = cat_http_parser_get_parsed_length(&parser);
                ASSERT_LE(parsed, input.size());
                offset += parsed;
                available = std::min(request.size(), std::max(available, offset) + input_size);
            }
            // An invalid media type disables multipart decoding, not HTTP message parsing.
            ASSERT_FALSE(cat_http_parser_is_multipart(&parser));
            ASSERT_EQ(body, "abc");
            ASSERT_EQ(offset, request.size());
        }
    }
}

TEST(cat_http_parser, malformed_multipart_body_and_reset)
{
    const char *bodies[] = {
        "--wrong\r\n",
        "--testX\n",
        "--test\rX",
        "--test\r\nInvalid@: value\r\n\r\nabc\r\n--test--\r\n",
        "--test\r\nHeader: value\rX\r\nabc\r\n--test--\r\n",
        "--test\r\n\rXabc\r\n--test--\r\n",
        "--test\r\n\r\nabc\r\n--test-X",
    };
    for (const char *body : bodies) {
        SCOPED_TRACE(body);
        const std::string request = "POST / HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=test\r\n"
            "Content-Length: " + std::to_string(strlen(body)) + "\r\n\r\n" + body;
        for (size_t input_size : {size_t(1), size_t(7), request.size()}) {
            SCOPED_TRACE(input_size);
            cat_http_parser_t parser;
            ASSERT_EQ(cat_http_parser_create(&parser), &parser);
            cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENTS_ALL);
            size_t offset = 0, iterations = 0;
            size_t available = std::min(input_size, request.size());
            bool rejected = false;
            while (!cat_http_parser_is_completed(&parser)) {
                ASSERT_LT(iterations++, request.size() * 10);
                std::vector<char> input(request.begin() + offset, request.begin() + available);
                if (!cat_http_parser_execute(&parser, input.empty() ? "" : input.data(), input.size())) {
                    rejected = true;
                    break;
                }
                size_t parsed = cat_http_parser_get_parsed_length(&parser);
                ASSERT_LE(parsed, input.size());
                offset += parsed;
                available = std::min(request.size(), std::max(available, offset) + input_size);
            }
            ASSERT_TRUE(rejected);
            ASSERT_EQ(cat_get_last_error_code(), CAT_EHP_MULTIPART);
            ASSERT_NE(std::string(cat_get_last_error_message()).find("Failed to parse multipart body"), std::string::npos);
            // Reset must discard the failed multipart state before parsing the next HTTP message.
            cat_http_parser_reset(&parser);
            cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENT_NONE);
            ASSERT_TRUE(cat_http_parser_execute(&parser, CAT_STRL("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")));
            ASSERT_TRUE(cat_http_parser_is_completed(&parser));
            ASSERT_FALSE(cat_http_parser_is_multipart(&parser));
        }
    }
}

TEST(cat_http_parser, multipart_response_without_content_length_and_reset)
{
    cat_http_parser_t parser;
    ASSERT_EQ(cat_http_parser_create(&parser), &parser);
    cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENT_MULTIPART_BODY);
    const char response[] = "HTTP/1.1 206 Partial Content\r\n"
        "Content-Type: multipart/byteranges; boundary=test\r\n\r\n"
        "--test\r\nContent-Range: bytes 0-2/3\r\n\r\nabc\r\n--test--\r\n";
    ASSERT_FALSE(cat_http_parser_execute(&parser, CAT_STRL(response)));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOTSUP);
    ASSERT_NE(std::string(cat_get_last_error_message()).find("multipart/byterange"), std::string::npos);
    cat_http_parser_reset(&parser);
    cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENT_NONE);
    ASSERT_TRUE(cat_http_parser_execute(&parser, CAT_STRL("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc")));
    ASSERT_TRUE(cat_http_parser_is_completed(&parser));
    ASSERT_EQ(cat_http_parser_get_status_code(&parser), 200);
}
