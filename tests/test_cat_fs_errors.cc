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
 */

#include "test.h"

TEST(cat_fs, content_errors)
{
    std::string missing = get_random_path();
    ASSERT_FALSE(file_exists(missing.c_str()));
    size_t length = 17;
    ASSERT_EQ(cat_fs_get_contents(missing.c_str(), &length), nullptr);
    ASSERT_EQ(length, 0);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOENT);
    std::string child = missing + TEST_PATH_SEP "content";
    ASSERT_EQ(cat_fs_put_contents(child.c_str(), CAT_STRL("data")), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOENT);
}

TEST(cat_fs, invalid_file_descriptor)
{
    char buffer[8];
    cat_stat_t stat;
    ASSERT_EQ(cat_fs_pread(CAT_OS_INVALID_FD, buffer, sizeof(buffer), 0), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_EQ(cat_fs_pwrite(CAT_OS_INVALID_FD, CAT_STRL("data"), 0), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_EQ(cat_fs_fstat(CAT_OS_INVALID_FD, &stat), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_EQ(cat_fs_ftruncate(CAT_OS_INVALID_FD, 0), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_EQ(cat_fs_fsync(CAT_OS_INVALID_FD), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
}

TEST(cat_fs, flock_invalid_operation)
{
    ASSERT_EQ(cat_fs_flock(CAT_OS_INVALID_FD, 0), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_EQ(cat_fs_flock(CAT_OS_INVALID_FD,
        CAT_FS_FLOCK_FLAG_SHARED | CAT_FS_FLOCK_FLAG_EXCLUSIVE), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_EQ(cat_fs_flock(CAT_OS_INVALID_FD,
        CAT_FS_FLOCK_FLAG_EXCLUSIVE | CAT_FS_FLOCK_FLAG_NONBLOCK), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
}

TEST(cat_fs, flock_competing_handles)
{
    std::string path = get_random_path();
    cat_file_t first = cat_fs_open(path.c_str(), CAT_FS_OPEN_FLAG_CREAT | CAT_FS_OPEN_FLAG_RDWR, 0600);
    ASSERT_NE(first, CAT_OS_INVALID_FD);
    DEFER(cat_fs_unlink(path.c_str()));
    DEFER(cat_fs_close(first));
    cat_file_t second = cat_fs_open(path.c_str(), CAT_FS_OPEN_FLAG_RDWR);
    ASSERT_NE(second, CAT_OS_INVALID_FD);
    DEFER(cat_fs_close(second));
    ASSERT_EQ(cat_fs_flock(first, CAT_FS_FLOCK_FLAG_EXCLUSIVE), 0);
    ASSERT_EQ(cat_fs_flock(second, CAT_FS_FLOCK_FLAG_EXCLUSIVE | CAT_FS_FLOCK_FLAG_NONBLOCK), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EAGAIN);
    ASSERT_EQ(cat_fs_flock(first, CAT_FS_FLOCK_FLAG_UNLOCK), 0);
    ASSERT_EQ(cat_fs_flock(second, CAT_FS_FLOCK_FLAG_EXCLUSIVE), 0);
    ASSERT_EQ(cat_fs_flock(second, CAT_FS_FLOCK_FLAG_UNLOCK), 0);
}

TEST(cat_fs, stdio_seek_and_write_state)
{
    std::string path = get_random_path();
    ASSERT_EQ(cat_fs_put_contents(path.c_str(), CAT_STRL("data")), 4);
    DEFER(cat_fs_unlink(path.c_str()));
    cat_file_t fd = cat_fs_open(path.c_str(), CAT_FS_OPEN_FLAG_RDONLY);
    ASSERT_NE(fd, CAT_OS_INVALID_FD);
    FILE *stream = fdopen(fd, "r");
    if (stream == nullptr) {
        cat_fs_close(fd);
        FAIL();
    }
    DEFER(cat_fs_fclose(stream));
    ASSERT_EQ(setvbuf(stream, nullptr, _IONBF, 0), 0);
    ASSERT_EQ(cat_fs_fseek(stream, -1, SEEK_SET), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_EQ(cat_fs_ftell(stream), 0);
    cat_clear_last_error();
    ASSERT_EQ(cat_fs_fwrite("x", 1, 1, stream), 0);
    ASSERT_NE(ferror(stream), 0);
    ASSERT_NE(cat_get_last_error_code(), 0);
    clearerr(stream);
    cat_clear_last_error();
    char buffer[8];
    ASSERT_EQ(cat_fs_fread(buffer, 1, sizeof(buffer), stream), 4);
    ASSERT_EQ(std::string(buffer, 4), "data");
    ASSERT_NE(feof(stream), 0);
    ASSERT_EQ(ferror(stream), 0);
    ASSERT_EQ(cat_get_last_error_code(), 0);
    ASSERT_EQ(cat_fs_ftell(stream), 4);
}

TEST(cat_fs, stdio_read_error_then_write_reuse)
{
    std::string path = get_random_path();
    FILE *stream = fopen(path.c_str(), "w");
    ASSERT_NE(stream, nullptr);
    DEFER(cat_fs_unlink(path.c_str()));
    DEFER(cat_fs_fclose(stream));
    char buffer[8];
    cat_clear_last_error();
    ASSERT_EQ(cat_fs_fread(buffer, 1, sizeof(buffer), stream), 0);
    ASSERT_NE(ferror(stream), 0);
    ASSERT_NE(cat_get_last_error_code(), 0);
    cat_clear_last_error();
    // A zero-sized operation does not report a previously latched stream error.
    ASSERT_EQ(cat_fs_fread(buffer, 0, sizeof(buffer), stream), 0);
    ASSERT_EQ(cat_get_last_error_code(), 0);
    clearerr(stream);
    ASSERT_EQ(cat_fs_fwrite("data", 1, 4, stream), 4);
    ASSERT_EQ(cat_fs_fflush(stream), 0);
    ASSERT_EQ(ferror(stream), 0);
    ASSERT_EQ(cat_get_last_error_code(), 0);
}

TEST(cat_fs, stdio_closed_descriptor_errors)
{
    std::string path = get_random_path();
    cat_file_t fd = cat_fs_open(path.c_str(), CAT_FS_OPEN_FLAG_CREAT | CAT_FS_OPEN_FLAG_RDWR, 0600);
    ASSERT_NE(fd, CAT_OS_INVALID_FD);
    DEFER(cat_fs_unlink(path.c_str()));
    FILE *stream = fdopen(fd, "w+");
    if (stream == nullptr) {
        cat_fs_close(fd);
        FAIL();
    }
    bool open = true;
    DEFER(if (open) { cat_fs_fclose(stream); });
    ASSERT_EQ(cat_fs_close(fd), 0);
    // FILE 对象仍有效，但其系统 fd 已关闭；各包装函数必须报告真实的 EBADF。
    ASSERT_EQ(cat_fs_fwrite("data", 1, 4, stream), 4);
    ASSERT_EQ(cat_fs_fflush(stream), EOF);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_EQ(cat_fs_ftell(stream), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    clearerr(stream);
    char buffer[8];
    ASSERT_EQ(cat_fs_fread(buffer, 1, sizeof(buffer), stream), 0);
    ASSERT_NE(ferror(stream), 0);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    int result = cat_fs_fclose(stream);
    open = false;
    ASSERT_EQ(result, EOF);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
}
