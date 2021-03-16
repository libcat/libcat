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
  |         codinghuang <2812240764@qq.com>                                  |
  |         dixyes <dixyes@gmail.com>                                        |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

bool no_tmp(){
    static int accessible = access(TEST_TMP_PATH, R_OK | W_OK);
    return 0 != accessible;
}

std::string path_join(std::string a, std::string b){
    if("" == a){
        return b;
    }
    if("" == b){
        return a;
    }
    std::string aa = a;
#ifdef CAT_OS_WIN
    // see https://docs.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation
    if(4 /* "\\\\?\\" */ + a.length() + 1/* '\\' */ + b.length() > 32767){
        // too long
        return "";
    }
    if(a.length() + 1/* '\\' */ + b.length() > MAX_PATH){
        aa = std::string("\\\\?\\") + a;
    }
#endif
    if(0 == aa.compare(aa.length()-1, 1, std::string(TEST_PATH_SEP))){
        return aa+b;
    }
    return aa+std::string(TEST_PATH_SEP)+b;
}

TEST(cat_fs, path_join)
{
    ASSERT_EQ(std::string("aaa"), path_join("", "aaa")); 
    ASSERT_EQ(std::string("tmp" TEST_PATH_SEP "aaa"), path_join("tmp", "aaa")); 
    ASSERT_EQ(std::string("tmp" TEST_PATH_SEP "aaa"), path_join("tmp" TEST_PATH_SEP, "aaa")); 
}

TEST(cat_fs, open_close_read_write)
{
    SKIP_IF(no_tmp());

    char buf[CAT_BUFFER_DEFAULT_SIZE], red[CAT_BUFFER_DEFAULT_SIZE];
    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_ocrw");
    const char * fn = fnstr.c_str();
    int fd = -1;
    ASSERT_GT(fd = cat_fs_open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0600), 0);
    DEFER({
        cat_fs_unlink(fn);
    });

    ASSERT_LT(cat_fs_read(fd, buf, 5), 0);
    cat_srand(buf, CAT_BUFFER_DEFAULT_SIZE);

    for(int i = 0; i<1; i++){
        ASSERT_EQ(cat_fs_write(fd, buf, CAT_BUFFER_DEFAULT_SIZE), CAT_BUFFER_DEFAULT_SIZE);
    }

    ASSERT_EQ(0, cat_fs_close(fd));

    //printf("%s\n",cat_get_last_error_message());
    ASSERT_GT(fd = cat_fs_open(fn, O_RDONLY, 0600), 0);
    DEFER(cat_fs_close(fd));
    ASSERT_LT(cat_fs_write(fd, "hello", 5), 0);
    
    for(int i = 0; i<128; i++){
        ASSERT_EQ(cat_fs_read(fd, red, CAT_BUFFER_DEFAULT_SIZE), CAT_BUFFER_DEFAULT_SIZE);
        ASSERT_EQ(std::string(buf, CAT_BUFFER_DEFAULT_SIZE), std::string(red, CAT_BUFFER_DEFAULT_SIZE));
    }
}

#define LSEEK_BUFSIZ 32768
TEST(cat_fs, lseek)
{
    SKIP_IF(no_tmp());

    char buf[LSEEK_BUFSIZ], red[LSEEK_BUFSIZ];
    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_seek");
    const char * fn = fnstr.c_str();
    int fd = cat_fs_open(fn, O_RDWR | O_CREAT | O_TRUNC, 0600);
    ASSERT_GT(fd, 0);
    DEFER({
        ASSERT_EQ(cat_fs_close(fd), 0);
        ASSERT_EQ(cat_fs_unlink(fn), 0);
    });
    
    cat_srand(buf, LSEEK_BUFSIZ);

    int ret;
    ASSERT_EQ(cat_fs_write(fd, buf, LSEEK_BUFSIZ), LSEEK_BUFSIZ);
    ASSERT_EQ(cat_fs_lseek(fd, 0, SEEK_SET), 0);
    ASSERT_EQ(cat_fs_lseek(fd, 10, SEEK_SET), 10);
    ASSERT_GT(ret = cat_fs_lseek(fd, 0, SEEK_END), 0);
    ASSERT_EQ(cat_fs_lseek(fd, -10, SEEK_END), ret - 10);
    ASSERT_EQ(cat_fs_lseek(fd, 0, SEEK_CUR), ret - 10);
    ASSERT_EQ(cat_fs_lseek(fd, -10, SEEK_CUR), ret - 20);
    ASSERT_EQ(cat_fs_lseek(fd, 0, SEEK_SET), 0);
    ASSERT_EQ(cat_fs_read(fd, red, LSEEK_BUFSIZ), LSEEK_BUFSIZ);
    ASSERT_EQ(std::string(buf, LSEEK_BUFSIZ), std::string(red, LSEEK_BUFSIZ));
}

TEST(cat_fs, access)
{
    SKIP_IF(no_tmp());

    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_access");
    const char * fn = fnstr.c_str();
    int fd = -1;
    DEFER({
        cat_fs_unlink(fn);
    });
    cat_fs_unlink(fn);
    ASSERT_GT(fd = cat_fs_open(fn, O_RDONLY | O_CREAT, 0400), 0);
    ASSERT_EQ(cat_fs_access(fn, R_OK), 0);
    ASSERT_LT(cat_fs_access(fn, X_OK|W_OK), 0);
    ASSERT_EQ(cat_fs_close(fd), 0);

    cat_fs_unlink(fn);
    ASSERT_GT(fd = cat_fs_open(fn, O_RDWR | O_CREAT, 0700), 0);
    ASSERT_EQ(cat_fs_access(fn, W_OK|X_OK|R_OK), 0);
    ASSERT_EQ(cat_fs_close(fd), 0);
}

TEST(cat_fs, mkdir_rmdir)
{
    SKIP_IF(no_tmp());

    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_dir");
    const char * fn = fnstr.c_str();
    std::string fn2str = path_join(fnstr, "cat_tests_dir2");
    const char * fn2 = fn2str.c_str();
    DEFER({
        cat_fs_rmdir(fn2);
        cat_fs_rmdir(fn);
    });
    cat_fs_rmdir(fn2);
    cat_fs_rmdir(fn);
    ASSERT_LT(cat_fs_mkdir(fn2, 0777), 0);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOENT);
    ASSERT_EQ(cat_fs_mkdir(fn, 0777), 0);
    ASSERT_LT(cat_fs_mkdir(fn, 0777), 0);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EEXIST);
    ASSERT_EQ(cat_fs_mkdir(fn2, 0777), 0);
    ASSERT_LT(cat_fs_rmdir(fn), 0);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOTEMPTY);
    ASSERT_EQ(cat_fs_rmdir(fn2), 0);
    ASSERT_EQ(cat_fs_rmdir(fn), 0);
    ASSERT_LT(cat_fs_rmdir(fn2), 0);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOENT);
}

TEST(cat_fs, rename)
{
    SKIP_IF(no_tmp());

    std::string dstr = path_join(TEST_TMP_PATH, "cat_tests_dir");
    const char * d = dstr.c_str();
    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_rename1");
    const char * fn = fnstr.c_str();
    std::string fn2str = path_join(TEST_TMP_PATH, "cat_tests_rename2");
    const char * fn2 = fn2str.c_str();
    std::string fn3str = path_join(dstr, "cat_tests_rename3");
    const char * fn3 = fn3str.c_str();
    DEFER({
        cat_fs_unlink(fn3);
        cat_fs_unlink(fn2);
        cat_fs_unlink(fn);
        cat_fs_rmdir(d);
    });
    cat_fs_unlink(fn3);
    cat_fs_unlink(fn2);
    cat_fs_unlink(fn);
    cat_fs_rmdir(d);

    ASSERT_EQ(cat_fs_close(cat_fs_open(fn, O_WRONLY | O_CREAT, 0321)), 0);
    ASSERT_EQ(cat_fs_mkdir(d, 0777), 0);
    ASSERT_EQ(cat_fs_rename(fn, fn2), 0);
    ASSERT_EQ(cat_fs_close(cat_fs_open(fn, O_WRONLY | O_CREAT, 0321)), 0);
    ASSERT_EQ(cat_fs_rename(fn, fn2), 0);
    ASSERT_LT(cat_fs_rename(fn, fn3), 0);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOENT);
    ASSERT_EQ(cat_fs_rename(fn2, fn3), 0);
}