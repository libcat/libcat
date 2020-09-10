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

enum _cat_bool_t
{
    cat_false = 0,
    cat_true = 1
};

#ifdef CAT_IDE_HELPER
typedef enum _cat_bool_t cat_bool_t;
#else
#define cat_bool_t uint8_t
#endif

typedef void cat_data_t;
typedef void (*cat_data_callback_t)(cat_data_t *data);
typedef void (*cat_data_dtor_t)(cat_data_t *data);

typedef union {
    char      c;
    int       i;
    long      l;
    long long ll;
    unsigned char      uchar;
    unsigned int       uint;
    unsigned long      ulong;
    unsigned long long ullong;
    float    fval;
    double   dval;
    int8_t   int8;
    uint8_t  uint8;
    int16_t  int16;
    uint16_t uint16;
    int32_t  int32;
    uint32_t uint32;
    int64_t  int64;
    uint64_t uint64;
    intmax_t intmax;
    uintmax_t uintmax;
    size_t   size;
    ssize_t  ssize;
    off_t    offset;
    void     *ptr;
    char     *string;
    char     buffer[1];
    /* custom */
    cat_bool_t bval;
} cat_any_t;

#if defined(__x86_64__) || defined(__LP64__) || defined(_LP64) || defined(_WIN64)
#define CAT_L64 1
#endif
#ifndef PRId64
#define PRId64 "lld"
#endif
#ifndef PRIu64
#define PRIu64 "llu"
#endif
#ifndef PRIx64
#define PRIx64 "llx"
#endif
