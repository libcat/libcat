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

static cat_data_t *closer(cat_data_t *data)
{
    cat_socket_t *socket = (cat_socket_t *) data;

    cat_time_sleep(0);
    cat_info(TEST, "close the socket");
    cat_socket_close(socket);

    return NULL;
}

static void socket_connector(void)
{
    cat_socket_t *socket;
    cat_bool_t connected;

    socket = cat_socket_create(NULL, CAT_SOCKET_TYPE_TCP4);
    cat_coroutine_run(NULL, closer, socket);
    connected = cat_socket_connect(socket, "www.microsoft.com", 80, -1);
    cat_info(TEST, "connected = %u, error: %s", connected, cat_get_last_error_message());
}

int main(void)
{
    cat_init_all();
    cat_run(CAT_RUN_EASY);
    socket_connector();
    return 0;
}
