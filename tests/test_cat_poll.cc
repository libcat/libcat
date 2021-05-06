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

#include "test.h"

TEST(cat_poll, init_failed)
{
    ASSERT_EQ(cat_poll_one(CAT_OS_INVALID_FD, POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_ERROR);
#ifdef CAT_OS_UNIX_LIKE
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
#else
    // TODO: ?
#endif
}

#ifdef CAT_OS_UNIX_LIKE
TEST(cat_poll, start_failed)
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    ASSERT_GT(fd, 0);
    DEFER(close(fd));
    co([fd] {
        ASSERT_TRUE(cat_time_delay(0));
        ASSERT_EQ(cat_poll_one(fd, POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_ERROR);
        ASSERT_EQ(cat_get_last_error_code(), CAT_EEXIST);
    });
    ASSERT_EQ(cat_poll_one(fd, POLLERR, nullptr, 1), CAT_RET_NONE);
}
#endif
