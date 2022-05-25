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

#define CAT_GLOBALS_STRUCT(name)                        name##_globals_s
#define CAT_GLOBALS_TYPE(name)                          name##_globals_t
#define CAT_GLOBALS(name)                               name##_globals

#define CAT_GLOBALS_STRUCT_BEGIN(name)                  typedef struct CAT_GLOBALS_STRUCT(name)
#define CAT_GLOBALS_STRUCT_END(name)                    CAT_GLOBALS_TYPE(name)

#define CAT_GLOBALS_BZERO(name)                         memset(CAT_GLOBALS_BULK(name), 0, sizeof(CAT_GLOBALS_TYPE(name)))

#ifndef CAT_THREAD_SAFE

#ifdef CAT_USE_THREAD_LOCAL
#define CAT_THREAD_SAFE 1
#if defined(CAT_OS_WIN) && defined(_MSC_VER)
#define CAT_THREAD_LOCAL __declspec(thread)
#else
#define CAT_THREAD_LOCAL __thread
#endif
#endif /* CAT_USE_THREAD_LOCAL */

#ifdef CAT_THREAD_LOCAL
#define CAT_GLOBALS_DECLARE(name)                       CAT_THREAD_LOCAL CAT_GLOBALS_TYPE(name) CAT_GLOBALS(name)
#else
#define CAT_GLOBALS_DECLARE(name)                       CAT_GLOBALS_TYPE(name) CAT_GLOBALS(name)
#endif
#define CAT_GLOBALS_GET(name, value)                    CAT_GLOBALS(name).value
#define CAT_GLOBALS_BULK(name)                          &CAT_GLOBALS(name)

#define CAT_GLOBALS_REGISTER(name)
#define CAT_GLOBALS_UNREGISTER(name)

#endif /* CAT_THREAD_SAFE */
