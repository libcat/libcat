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

#include "cat.h"

#ifdef CAT_IDE_HELPER
#include "cat_log.h"
#endif

#include "cat_coroutine.h" /* for coroutine id (TODO: need to decouple it?) */

CAT_API cat_log_t cat_log_function;

static cat_always_inline void cat_log_timespec_sub(struct timespec *tv, const struct timespec *a, const struct timespec *b)
{
    tv->tv_sec = a->tv_sec - b->tv_sec;
    tv->tv_nsec = a->tv_nsec - b->tv_nsec;
    if (tv->tv_nsec < 0) {
        tv->tv_sec--;
        tv->tv_nsec += 1000000000;
    }
}

CAT_API void cat_log_standard(CAT_LOG_PARAMATERS)
{
    const char *type_string;
    char *message;
    FILE *output;

    switch (type) {
        case CAT_LOG_TYPE_DEBUG : {
#ifndef CAT_ENABLE_DEBUG_LOG
            return;
#else
            type_string = "Debug";
            output = stdout;
            break;
#endif
        }
        case CAT_LOG_TYPE_INFO : {
            type_string = "Info";
            output = stdout;
            break;
        }
        case CAT_LOG_TYPE_NOTICE : {
            type_string = "Notice";
            output = CAT_LOG_G(error_output);
            break;
        }
        case CAT_LOG_TYPE_WARNING : {
            type_string = "Warning";
            output = CAT_LOG_G(error_output);
            break;
        }
        case CAT_LOG_TYPE_ERROR : {
            type_string = "Error";
            output = CAT_LOG_G(error_output);
            break;
        }
        case CAT_LOG_TYPE_CORE_ERROR : {
            type_string = "Core Error";
            output = CAT_LOG_G(error_output);
            break;
        }
        default:
            CAT_NEVER_HERE("Unknown log type");
    }

    do {
        va_list args;
        va_start(args, format);
        message = cat_vsprintf(format, args);
        if (unlikely(message == NULL)) {
            fprintf(CAT_LOG_G(error_output), "Sprintf log message failed" CAT_EOL);
            return;
        }
        va_end(args);
    } while (0);

    do {
        unsigned int timestamps_level = CAT_LOG_G(show_timestamps);
        if (timestamps_level == 0) {
            break;
        }
        struct {
            const char *name;
            unsigned int width;
            unsigned int scale;
        } scale_options[] = {
            { "s" , 0, 1000000000 }, // placeholder
            { "ms", 3, 1000000 },
            { "us", 6, 1000 },
            { "ns", 9, 1 },
        };
        char buffer[32];
        if (!CAT_LOG_G(show_timestamps_as_relative)) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            time_t local = ts.tv_sec;
            struct tm *tm = localtime(&local);
            size_t length;
            length = strftime(CAT_STRS(buffer), CAT_LOG_G(timestamps_format), tm);
            if (length == 0) {
                break;
            }
            timestamps_level = MIN(timestamps_level, CAT_ARRAY_SIZE(scale_options));
            if (timestamps_level > 1) {
                // TODO: now just use "us" as default, supports more in the future
                int f_length = snprintf(
                    buffer + length, sizeof(buffer) - length,
                    ".%0*ld",
                    scale_options[timestamps_level - 1].width,
                    (long) (ts.tv_nsec / scale_options[timestamps_level - 1].scale)
                );
                if (f_length >= 0) {
                    length += f_length;
                }
            }
            buffer[length] = '\0';
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            static struct timespec ots;
            if (ots.tv_sec == 0) {
                ots = ts;
            }
            struct timespec dts;
            cat_log_timespec_sub(&dts, &ts, &ots);
            ots = ts;
            int length = snprintf(CAT_STRS(buffer), "%6ld", (long) dts.tv_sec);
            // starts with msec, not sec
            timestamps_level = MIN(timestamps_level, CAT_ARRAY_SIZE(scale_options) - 1);
            int f_length = snprintf(
                buffer + length, sizeof(buffer) - length,
                ".%0*ld",
                scale_options[timestamps_level].width,
                (long) dts.tv_nsec / scale_options[timestamps_level].scale
            );
            if (f_length >= 0) {
                length += f_length;
            }
            buffer[length] = '\0';
        }
        fprintf(output, "[%s] ", buffer);
    } while (0);

    do {
        const char *name = cat_coroutine_get_current_role_name();
        if (name != NULL) {
            fprintf(
                output,
                "%s: <%s> %s in %s" CAT_EOL,
                type_string, module_name, message, name
            );
        } else {
            fprintf(
                output,
                "%s: <%s> %s in R" CAT_COROUTINE_ID_FMT CAT_EOL,
                type_string, module_name, message, cat_coroutine_get_current_id()
            );
        }
    } while (0);
#ifdef CAT_SOURCE_POSITION
    if (CAT_LOG_G(show_source_postion)) {
        fprintf(
            output,
            "SP: " CAT_SOURCE_POSITION_FMT CAT_EOL,
            CAT_SOURCE_POSITION_RELAY_C
        );
    }
#endif

    if (type & CAT_LOG_TYPES_ABNORMAL) {
        cat_set_last_error(code, message);
    } else {
        cat_free(message);
    }

    if (type & (CAT_LOG_TYPE_ERROR | CAT_LOG_TYPE_CORE_ERROR)) {
        cat_abort();
    }
}

CAT_API const char *cat_log_str_quote(const char *str, size_t n, char **tmp_str)
{
    return cat_log_str_quote_unlimited(str, CAT_MIN(n, CAT_LOG_G(str_size)), tmp_str);
}

CAT_API const char *cat_log_str_quote_unlimited(const char *str, size_t n, char **tmp_str)
{
    char *quoted_data;
    cat_str_quote(str, n, &quoted_data, NULL);
    *tmp_str = quoted_data;
    return quoted_data;
}
