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

  http_parser.c - http parser read from stdin and print events trigged while parsing
  this code is also for fuzzers
*/

#include "cat.h"
#include "cat_api.h"
#include "cat_http.h"

int main(int argc, const char **argv) {
    char buf[8192];
    ssize_t red;
    const char *p, *pe;
    cat_init_all();

    // a parser can be simply on stack
    cat_http_parser_t parser;

    // use cat_http_parser_init, cat_http_parser_reset or cat_http_parser_create to prepare the parser
    cat_http_parser_init(&parser);

    // by default parser is working in both req and res modes
    // you can also specify mode by
    cat_http_parser_set_type(&parser, CAT_HTTP_PARSER_TYPE_BOTH);

    // subscribe all events
    // you can also subscribe only specific events by bitwise or them
    cat_http_parser_set_events(&parser, CAT_HTTP_PARSER_EVENTS_ALL);

    //int fd = open("req_multi.data", O_RDONLY);
    red = read(0, buf, 8192);
    if (red < 0) {
        fprintf(stderr, "failed to read stdin\n");
        return 1;
    }
  
    p = buf;
    pe = buf + red;

    while (1) {
        // return true if successfully parsed
        if (!cat_http_parser_execute(&parser, p, pe - p)) {
            fprintf(stderr, "failed to parse\n");
            return -1;
        }
        // parsed length is parser read size, may be smaller than full requested size
        p += cat_http_parser_get_parsed_length(&parser);

        printf("[EVENT] %s\n", cat_http_parser_get_event_name(&parser));
        if (parser.event & CAT_HTTP_PARSER_EVENT_FLAG_DATA) {
            printf("[DATA] [%zu]%.*s\n", parser.data_length, (int)parser.data_length, parser.data);
        }
        
        if (parser.event == CAT_HTTP_PARSER_EVENT_MESSAGE_COMPLETE) {
            // successfully parsed, exit
            return 0;
        }

        if (pe - p == 0) {
            // body not end
            fprintf(stderr, "body not end\n");
            return 1;
        }

        if (parser.event == CAT_HTTP_PARSER_EVENT_NONE) {
            // body not end
            fprintf(stderr, "body not end\n");
            return 1;
        }
    }
}