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
  +--------------------------------------------------------------------------+
 */

#include "cat_api.h"

#include "llhttp.h"

#define HTTP_REQUEST_MAX_LENGTH              (2 * 1024 * 1024)
#define HTTP_EMPTY_RESPONSE                  "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Length: 0\r\n\r\n"
#define HTTP_SERVICE_UNAVAILABLE_RESPONSE    "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n"

typedef struct {
    const char *body_begin;
    size_t body_length;
    cat_bool_t complete;
} echo_context_t;

int llhttp_on_body(llhttp_t *parser, const char *at, size_t length)
{
    echo_context_t *context = (echo_context_t *) parser->data;
    if (unlikely(context->body_begin == NULL)) {
        context->body_begin = at;
    }
    context->body_length += length;
    return 0;
}

static int llhttp_on_message_complete(llhttp_t *parser)
{
    echo_context_t *context = (echo_context_t *) parser->data;
    context->complete = 1;
    return 0;
}

static cat_data_t *echo_server_accept_callback(cat_data_t *data)
{
    cat_socket_t *client = (cat_socket_t *) data;
    llhttp_t parser;
    llhttp_settings_t settings;

    llhttp_settings_init(&settings);
    settings.on_body = llhttp_on_body;
    settings.on_message_complete = llhttp_on_message_complete;

    while (1) {
        /* signal thread way */
        static char read_buffer[HTTP_REQUEST_MAX_LENGTH];
        size_t total_bytes = 0;
        echo_context_t context = {0};
        llhttp_init(&parser, HTTP_REQUEST, &settings);
        parser.data = &context;
        while (1) {
            ssize_t n;
            enum llhttp_errno error;

            n = cat_socket_recv(
                client,
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

            error = llhttp_execute(&parser, read_buffer, n);
            if (context.complete) {
                if (context.body_length == 0) {
                    (void) cat_socket_send(client, CAT_STRL(HTTP_EMPTY_RESPONSE));
                } else {
                    char *response = cat_sprintf(
                        "HTTP/1.1 200 OK\r\n"
                        "Connection: keep-alive\r\n"
                        "Content-Length: %zu\r\n\r\n"
                        "%.*s",
                        context.body_length,
                        (int) context.body_length,
                        context.body_begin ? context.body_begin : ""
                    );
                    if (unlikely(response == NULL)) {
                        goto _error;
                    }
                    (void) cat_socket_send(client, response, strlen(response));
                }
                break;
            }
        }
    }
    if (0) {
        _error:
        (void) cat_socket_send(client, CAT_STRL(HTTP_SERVICE_UNAVAILABLE_RESPONSE));
    }
    _close:
    cat_socket_close(client);
    return CAT_COROUTINE_DATA_NULL;
}

static void echo_server_run()
{
    cat_socket_t server;
    cat_bool_t ret;

    ret = cat_socket_create(&server, CAT_SOCKET_TYPE_TCP) != NULL ? cat_true : cat_false;
    if (!ret) {
        exit(1);
    }
    ret = cat_socket_bind(&server, CAT_STRL("0.0.0.0"), CAT_MAGIC_PORT);
    if (!ret) {
        exit(1);
    }
    ret = cat_socket_listen(&server, CAT_MAGIC_BACKLOG);
    if (!ret) {
        exit(1);
    }
    while (1) {
        cat_socket_t *client = cat_socket_accept(&server, NULL);
        if (client == NULL) {
            exit(1);
        }
        cat_coroutine_run(NULL, echo_server_accept_callback, client);
    }
}

int main(void)
{
    cat_init_all();
    cat_run(CAT_RUN_EASY);
    echo_server_run();
    return 0;
}
