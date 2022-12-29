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

#include "cat_api.h"
#include "cat_http.h"

#define HTTP_REQUEST_MAX_LENGTH              (2 * 1024 * 1024)
#define HTTP_EMPTY_RESPONSE                  "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Length: 0\r\n\r\n"
#define HTTP_SERVICE_UNAVAILABLE_RESPONSE    "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n"

static cat_data_t *echo_server_handle_connection(cat_data_t *data)
{
    cat_socket_t *connection = (cat_socket_t *) data;
    cat_http_parser_t parser;

    cat_http_parser_init(&parser);
    cat_http_parser_set_type(&parser, CAT_HTTP_PARSER_TYPE_REQUEST);
    cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENT_BODY | CAT_HTTP_PARSER_EVENT_MESSAGE_COMPLETE);

    while (1) {
        /* signal thread way */
        static char read_buffer[HTTP_REQUEST_MAX_LENGTH];
        const char *body_begin = NULL;
        size_t body_length = 0;
        size_t total_bytes = 0;
        while (1) {
            ssize_t n = cat_socket_recv(
                connection,
                read_buffer + total_bytes,
                HTTP_REQUEST_MAX_LENGTH - total_bytes
            );
            if (unlikely(n <= 0)) {
                goto _close;
            }
            total_bytes += (size_t) n;
            if (unlikely(total_bytes == HTTP_REQUEST_MAX_LENGTH)) {
                goto _error;
            }
            while (1) {
                if (!cat_http_parser_execute(&parser, read_buffer, n)) {
                    goto _error;
                }
                if (cat_http_parser_get_event(&parser) == CAT_HTTP_PARSER_EVENT_BODY) {
                    if (body_begin == NULL) {
                        body_begin = cat_http_parser_get_data(&parser);
                    }
                    body_length += cat_http_parser_get_data_length(&parser);
                } else {
                    break;
                }
            }
            if (cat_http_parser_is_completed(&parser)) {
                if (body_length == 0) {
                    (void) cat_socket_send(connection, CAT_STRL(HTTP_EMPTY_RESPONSE));
                } else {
                    char *response = cat_sprintf(
                        "HTTP/1.1 200 OK\r\n"
                        "Connection: keep-alive\r\n"
                        "Content-Length: %zu\r\n\r\n"
                        "%.*s",
                        body_length,
                        (int) body_length,
                        body_begin ? body_begin : ""
                    );
                    if (unlikely(response == NULL)) {
                        goto _error;
                    }
                    (void) cat_socket_send(connection, response, strlen(response));
                }
                break;
            }
        }
    }
    if (0) {
        _error:
        (void) cat_socket_send(connection, CAT_STRL(HTTP_SERVICE_UNAVAILABLE_RESPONSE));
    }
    _close:
    cat_socket_close(connection);
    return NULL;
}

static void echo_server_run()
{
    cat_socket_t server;
    cat_bool_t ret;

    ret = cat_socket_create(&server, CAT_SOCKET_TYPE_TCP) != NULL ? cat_true : cat_false;
    if (!ret) {
        exit(1);
    }
    ret = cat_socket_bind_to(&server, CAT_STRL("0.0.0.0"), CAT_MAGIC_PORT);
    if (!ret) {
        exit(1);
    }
    ret = cat_socket_listen(&server, CAT_MAGIC_BACKLOG);
    if (!ret) {
        exit(1);
    }
    fprintf(stdout, "Server is running on 0.0.0.0:%d\n", CAT_MAGIC_PORT);
    while (1) {
        cat_socket_t *connection = cat_socket_create(NULL, cat_socket_get_simple_type(&server));
        if (connection == NULL) {
            exit(1);
        }
        if (!cat_socket_accept(&server, connection)) {
            exit(1);
        }
        cat_coroutine_run(NULL, echo_server_handle_connection, connection);
    }
}

int main(void)
{
    cat_init_all();
    cat_run(CAT_RUN_EASY);
    echo_server_run();
    return 0;
}
