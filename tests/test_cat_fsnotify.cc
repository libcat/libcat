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

TEST(cat_fsnotify, cat_fsnotify_wait)
{

    std::string watch_dir =  TEST_TMP_PATH + std::string("/cat_tests_dir");
    std::string file1(watch_dir + "/file1");

    remove(file1.c_str());
    remove(watch_dir.c_str());

    cat_fs_mkdir(watch_dir.c_str(), 0755);

    cat_sync_wait_group_t *wg, _wg;
    wg = cat_sync_wait_group_create(&_wg);
    ASSERT_NE(wg, nullptr);
    ASSERT_TRUE(cat_sync_wait_group_add(wg, 2));

    int fs_event_cb_called = 0;
    int fs_event_created = 0;

    cat_coroutine_t *co1 = co([watch_dir, wg, &fs_event_cb_called] {
        while (true) {
            cat_fs_notify_event_t event;
            cat_bool_t ret = cat_fsnotify_wait(watch_dir.c_str(), &event);
            if (ret == cat_false) {
                ASSERT_STREQ(cat_get_last_error_message(), "Fsnotify has been canceled");
                break;
            }
            fs_event_cb_called++;

            ASSERT_TRUE(event.event == CAT_FS_NOTIFY_EVENT_KIND_RENAME || event.event == CAT_FS_NOTIFY_EVENT_KIND_CHANGE);
            ASSERT_STREQ(std::string(watch_dir + "/file1").c_str(), std::string(watch_dir + "/" + event.filename).c_str());
        }

        ASSERT_TRUE(cat_sync_wait_group_done(wg));
    });

    co([watch_dir, wg, co1, &fs_event_created] {
        int r;
        std::string filename(watch_dir + "/file1");
        cat_file_t file = cat_fs_open(filename.c_str(),  O_WRONLY | O_CREAT);
        ASSERT_GT(file, 2);
        r = cat_fs_close(file);
        ASSERT_EQ(r, 0);
        fs_event_created++;

        ASSERT_TRUE(cat_sync_wait_group_done(wg));
        cat_time_msleep(100);
        cat_coroutine_resume(co1, nullptr, nullptr);
    });

    ASSERT_TRUE(cat_sync_wait_group_wait(wg, TEST_IO_TIMEOUT));

    ASSERT_EQ(fs_event_created, fs_event_cb_called);
}
