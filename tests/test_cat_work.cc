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

#include "test.h"

TEST(cat_work, base)
{
    SKIP_IF(is_valgrind());

    int buckets[10] = { };
    cat_msec_t s = cat_time_msec();

    for (int n = 0; n < 10; n++) {
        co([&, n] {
            EXPECT_TRUE(work([&] {
                usleep((n + 1) * 1000); /* 1ms ~ 10ms */
                buckets[n] = n;
            }, 50)); /* wait max 50ms */
        });
    }

    s = cat_time_msec() - s;
    EXPECT_LE(s, 5); /* <= 5ms := no-blocking */
    cat_time_msleep(50);
    for (int n = 0; n < 10; n++) {
        ASSERT_EQ(buckets[n], n); /* work done */
    }
}
