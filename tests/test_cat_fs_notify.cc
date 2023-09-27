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
  | Author: Codinghuang <codinghuang@qq.com>                                 |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

static void create_file(const char* name)
{
    cat_file_t file = cat_fs_open(name,  O_WRONLY | O_CREAT, S_IWUSR | S_IRUSR);
    ASSERT_GT(file, 2);
    int r = cat_fs_close(file);
    ASSERT_EQ(r, 0);
    cat_time_msleep(100);
}

static void touch_file(const char* name) {
    cat_file_t file = cat_fs_open(name, O_RDWR | O_APPEND);
    ssize_t bytes = cat_fs_write(file, "foo", 3);
    ASSERT_EQ(bytes, 3);
    int r = cat_fs_close(file);
    ASSERT_EQ(r, 0);
    cat_time_msleep(100);
}

TEST(cat_fsnotify, watch_dir)
{
    std::string watch_dir =  "/tmp/cat_tests_dir";
    std::string file1(watch_dir + "/file1");
    std::string file2(watch_dir + "/file2");

    remove(file2.c_str());
    remove(file1.c_str());
    remove(watch_dir.c_str());

    cat_fs_mkdir(watch_dir.c_str(), 0755);
    create_file(file1.c_str());
    create_file(file2.c_str());

    int fs_event_cb_called = 0;
    int fs_ops = 0;

    cat_fs_notify_watch_context_t *watch_ctx = cat_fs_notify_watch_context_init(watch_dir.c_str());
    cat_fs_notify_start(watch_ctx);

    cat_sync_wait_group_t wg;
    ASSERT_NE(cat_sync_wait_group_create(&wg), nullptr);

    cat_coroutine_t *co1 = co([watch_dir, &fs_event_cb_called, watch_ctx, &wg] {
        ASSERT_TRUE(cat_sync_wait_group_add(&wg, 1));
        while (true) {
            cat_fs_notify_event_t *event = cat_fs_notify_wait_event(watch_ctx);
            if (event == nullptr) {
                ASSERT_STREQ(cat_get_last_error_message(), "Fsnotify has been canceled");
                break;
            }
            fs_event_cb_called++;

            // cat_time_msleep(0); // TODO

            std::string filename = std::string(event->filename);
            ASSERT_TRUE(filename.find("file1") != std::string::npos || filename.find("file2") != std::string::npos);
            ASSERT_TRUE(event->ops & UV_RENAME || event->ops & UV_CHANGE);
            cat_free(event);
        }
        ASSERT_TRUE(cat_sync_wait_group_done(&wg));
    });

    for (size_t i = 0; i < 2; i++) {
        touch_file(file1.c_str());
        fs_ops++;
        touch_file(file2.c_str());
        fs_ops++;
    }
    cat_coroutine_resume(co1, nullptr, nullptr);
    ASSERT_TRUE(cat_sync_wait_group_wait(&wg, TEST_IO_TIMEOUT));
    ASSERT_EQ(fs_ops, fs_event_cb_called);
    cat_fs_notify_stop(watch_ctx);
    cat_fs_notify_watch_context_cleanup(watch_ctx);
}

TEST(cat_fsnotify, watch_dir_concurrency)
{
    std::string watch_dir =  "/tmp/cat_tests_dir";
    std::string file1(watch_dir + "/file1");
    std::string file2(watch_dir + "/file2");

    remove(file2.c_str());
    remove(file1.c_str());
    remove(watch_dir.c_str());

    cat_fs_mkdir(watch_dir.c_str(), 0755);
    create_file(file1.c_str());
    create_file(file2.c_str());

    int fs_event_cb_called = 0;
    int fs_ops = 0;

    cat_fs_notify_watch_context_t *watch_ctx = cat_fs_notify_watch_context_init(watch_dir.c_str());
    cat_fs_notify_start(watch_ctx);

    cat_sync_wait_group_t wg;
    ASSERT_NE(cat_sync_wait_group_create(&wg), nullptr);

    cat_coroutine_t * co1 = co([watch_dir, &fs_event_cb_called, watch_ctx, &wg] {
        ASSERT_TRUE(cat_sync_wait_group_add(&wg, 1));
        while (true) {
            cat_fs_notify_event_t *event = cat_fs_notify_wait_event(watch_ctx);
            if (event == nullptr) {
                ASSERT_STREQ(cat_get_last_error_message(), "Fsnotify has been canceled");
                break;
            }
            fs_event_cb_called++;

            // cat_time_msleep(0); // TODO

            std::string filename = std::string(event->filename);
            ASSERT_TRUE(filename.find("file1") != std::string::npos || filename.find("file2") != std::string::npos);
            ASSERT_TRUE(event->ops & UV_RENAME || event->ops & UV_CHANGE);
            cat_free(event);
        }
        ASSERT_TRUE(cat_sync_wait_group_done(&wg));
    });

    co([watch_dir, watch_ctx, &wg] {
        ASSERT_TRUE(cat_sync_wait_group_add(&wg, 1));
        while (true) {
            cat_fs_notify_event_t *event = cat_fs_notify_wait_event(watch_ctx);
            if (event == nullptr) {
                ASSERT_STREQ(cat_get_last_error_message(), "Fsnotify is in waiting");
                break;
            }
        }
        ASSERT_TRUE(cat_sync_wait_group_done(&wg));
    });

    for (size_t i = 0; i < 2; i++) {
        touch_file(file1.c_str());
        fs_ops++;
        touch_file(file2.c_str());
        fs_ops++;
    }

    cat_coroutine_resume(co1, nullptr, nullptr);

    ASSERT_TRUE(cat_sync_wait_group_wait(&wg, TEST_IO_TIMEOUT));
    ASSERT_EQ(fs_ops, fs_event_cb_called);
    cat_fs_notify_stop(watch_ctx);
    cat_fs_notify_watch_context_cleanup(watch_ctx);
}

TEST(cat_fsnotify, watch_file)
{
    std::string watch_dir =  "/tmp/cat_tests_dir";
    std::string file1(watch_dir + "/file1");
    std::string file2(watch_dir + "/file2");

    remove(file2.c_str());
    remove(file1.c_str());
    remove(watch_dir.c_str());

    cat_fs_mkdir(watch_dir.c_str(), 0755);
    create_file(file1.c_str());
    create_file(file2.c_str());

    int fs_event_cb_called = 0;
    int fs_ops = 0;

    cat_fs_notify_watch_context_t *watch_ctx = cat_fs_notify_watch_context_init(file2.c_str());
    cat_fs_notify_start(watch_ctx);

    cat_sync_wait_group_t wg;
    ASSERT_NE(cat_sync_wait_group_create(&wg), nullptr);

    cat_coroutine_t *co1 = co([watch_dir, &fs_event_cb_called, watch_ctx, &wg] {
        ASSERT_TRUE(cat_sync_wait_group_add(&wg, 1));
        while (true) {
            cat_fs_notify_event_t *event = cat_fs_notify_wait_event(watch_ctx);
            if (event == nullptr) {
                ASSERT_STREQ(cat_get_last_error_message(), "Fsnotify has been canceled");
                break;
            }
            fs_event_cb_called++;

            // cat_time_msleep(0); // TODO

            std::string filename = std::string(event->filename);
            ASSERT_TRUE(filename.find("file1") == std::string::npos);
            ASSERT_TRUE(filename.find("file2") != std::string::npos);
            ASSERT_TRUE(event->ops & UV_RENAME || event->ops & UV_CHANGE);
            cat_free(event);
        }
        ASSERT_TRUE(cat_sync_wait_group_done(&wg));
    });

    for (size_t i = 0; i < 2; i++) {
        touch_file(file1.c_str());
        fs_ops++;
        touch_file(file2.c_str());
        fs_ops++;
    }
    cat_coroutine_resume(co1, nullptr, nullptr);
    ASSERT_TRUE(cat_sync_wait_group_wait(&wg, TEST_IO_TIMEOUT));
    ASSERT_EQ(fs_ops / 2, fs_event_cb_called);
    cat_fs_notify_stop(watch_ctx);
    cat_fs_notify_watch_context_cleanup(watch_ctx);
}

TEST(cat_fsnotify, watch_file_exact_path)
{
    /*
        This test watches a file named "file.jsx" and modifies a file named
        "file.js". The test verifies that no events occur for file.jsx.
    */

    std::string watch_dir =  "/tmp/cat_tests_dir";
    std::string file1(watch_dir + "/file.js");
    std::string file2(watch_dir + "/file.jsx");

    remove(file2.c_str());
    remove(file1.c_str());
    remove(watch_dir.c_str());
    cat_fs_mkdir(watch_dir.c_str(), 0755);
    create_file(file1.c_str());
    create_file(file2.c_str());

    int fs_event_cb_called = 0;
    int fs_ops = 0;

    cat_fs_notify_watch_context_t *watch_ctx = cat_fs_notify_watch_context_init(file2.c_str());
    cat_fs_notify_start(watch_ctx);

    cat_sync_wait_group_t wg;
    ASSERT_NE(cat_sync_wait_group_create(&wg), nullptr);

    cat_coroutine_t *co1 = co([watch_dir, &fs_event_cb_called, watch_ctx, &wg] {
        ASSERT_TRUE(cat_sync_wait_group_add(&wg, 1));
        while (true) {
            cat_fs_notify_event_t *event = cat_fs_notify_wait_event(watch_ctx);
            if (event == nullptr) {
                ASSERT_STREQ(cat_get_last_error_message(), "Fsnotify has been canceled");
                break;
            }
            fs_event_cb_called++;

            // cat_time_msleep(0); // TODO

            std::string filename = std::string(event->filename);
            ASSERT_TRUE(filename.find("file.js") == std::string::npos);
            ASSERT_TRUE(filename.find("file.jsx") == std::string::npos);
            ASSERT_TRUE(event->ops & UV_RENAME || event->ops & UV_CHANGE);
            cat_free(event);
        }
        ASSERT_TRUE(cat_sync_wait_group_done(&wg));
    });

    for (size_t i = 0; i < 2; i++) {
        touch_file(file1.c_str());
        fs_ops++;
    }
    cat_coroutine_resume(co1, nullptr, nullptr);
    ASSERT_TRUE(cat_sync_wait_group_wait(&wg, TEST_IO_TIMEOUT));
    ASSERT_EQ(0, fs_event_cb_called);
    cat_fs_notify_stop(watch_ctx);
    cat_fs_notify_watch_context_cleanup(watch_ctx);
}

TEST(cat_fsnotify, watch_file_twice)
{
    std::string watch_dir =  "/tmp/cat_tests_dir";
    std::string file1(watch_dir + "/empty_file");

    cat_fs_notify_watch_context_t *watch_ctx1 = cat_fs_notify_watch_context_init(file1.c_str());
    cat_fs_notify_start(watch_ctx1);
    cat_fs_notify_watch_context_t *watch_ctx2 = cat_fs_notify_watch_context_init(file1.c_str());
    cat_fs_notify_start(watch_ctx2);
    cat_time_msleep(100);
    cat_fs_notify_stop(watch_ctx1);
    cat_fs_notify_stop(watch_ctx2);
    cat_fs_notify_watch_context_cleanup(watch_ctx1);
    cat_fs_notify_watch_context_cleanup(watch_ctx2);
}

TEST(cat_fsnotify, cleanup_when_waiting)
{
    std::string watch_dir =  "/tmp/cat_tests_dir";
    std::string file1(watch_dir + "/file1");

    remove(file1.c_str());
    remove(watch_dir.c_str());

    cat_fs_mkdir(watch_dir.c_str(), 0755);
    create_file(file1.c_str());

    int fs_event_cb_called = 0;
    int fs_ops = 0;

    cat_fs_notify_watch_context_t *watch_ctx = cat_fs_notify_watch_context_init(file1.c_str());
    cat_fs_notify_start(watch_ctx);

    cat_sync_wait_group_t wg;
    ASSERT_NE(cat_sync_wait_group_create(&wg), nullptr);

    cat_coroutine_t *co1 = co([watch_dir, &fs_event_cb_called, watch_ctx, &wg] {
        ASSERT_TRUE(cat_sync_wait_group_add(&wg, 1));
        while (true) {
            cat_fs_notify_event_t *event = cat_fs_notify_wait_event(watch_ctx);
            if (event == nullptr) {
                ASSERT_STREQ(cat_get_last_error_message(), "Fsnotify has been canceled");
                break;
            }
            fs_event_cb_called++;

            // cat_time_msleep(0); // TODO

            std::string filename = std::string(event->filename);
            ASSERT_TRUE(filename.find("file1") != std::string::npos || filename.find("file2") != std::string::npos);
            ASSERT_TRUE(event->ops & UV_RENAME || event->ops & UV_CHANGE);
            cat_free(event);
        }
        ASSERT_TRUE(cat_sync_wait_group_done(&wg));
    });

    cat_bool_t ret = cat_fs_notify_watch_context_cleanup(watch_ctx);
    ASSERT_EQ(cat_false, ret);

    for (size_t i = 0; i < 2; i++) {
        touch_file(file1.c_str());
        fs_ops++;
    }

    cat_coroutine_resume(co1, nullptr, nullptr);
    ASSERT_TRUE(cat_sync_wait_group_wait(&wg, TEST_IO_TIMEOUT));
    ASSERT_EQ(fs_ops, fs_event_cb_called);
    cat_fs_notify_stop(watch_ctx);
    cat_fs_notify_watch_context_cleanup(watch_ctx);
}
