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

int main(void)
{
    cat_init_all();
    cat_run(CAT_RUN_EASY);

    struct addrinfo hints;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = 0;

    struct addrinfo *response = cat_dns_getaddrinfo("www.github.com", NULL, &hints);
    if (response == NULL) {
        fprintf(stderr, "Error: %s", cat_get_last_error_message());
        return EXIT_FAILURE;
    }

    struct addrinfo *response_head = response;
    char addr[CAT_SOCKET_IPV4_BUFFER_SIZE];
    do {
        uv_ip4_name((struct sockaddr_in *) response->ai_addr, addr, response->ai_addrlen);
        printf("Resolved[%u]: %s\n", response->ai_addrlen, addr);
    } while ((response = response->ai_next));
    cat_dns_freeaddrinfo(response_head);

    return EXIT_SUCCESS;
}
