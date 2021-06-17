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
  | Author: dixyes <dixyes@gmail.com>                                        |
  |         Twosee <twosee@php.net>                                          |
  |         codinghuang <2812240764@qq.com>                                  |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

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
    if(
        a.rfind("\\\\?\\", 0) != 0 && // already \\?\-ed
        a.length() + 1/* '\\' */ + b.length() > MAX_PATH
    ){
        aa = std::string("\\\\?\\") + a;
    }
#endif
    if(0 == aa.compare(aa.length()-1, 1, std::string(TEST_PATH_SEP))){
        return aa+b;
    }
    return aa+std::string(TEST_PATH_SEP)+b;
}

bool no_tmp(){
    static int accessible = access(TEST_TMP_PATH, R_OK | W_OK);
    return 0 != accessible;
}

TEST(cat_fs, path_join)
{
    ASSERT_EQ(std::string("aaa"), path_join("", "aaa"));
    ASSERT_EQ(std::string("tmp" TEST_PATH_SEP "aaa"), path_join("tmp", "aaa"));
    ASSERT_EQ(std::string("tmp" TEST_PATH_SEP "aaa"), path_join("tmp" TEST_PATH_SEP, "aaa"));
#ifdef CAT_OS_WIN
    std::string longname = std::string(299, 'x');
    std::string shortleft = std::string("\\\\?\\C:\\");
    std::string longleft = shortleft + longname;
    std::string full = longleft + std::string("\\") + longname;
    ASSERT_EQ(path_join(longleft, longname), full);
    ASSERT_EQ(path_join(path_join(shortleft, longname), longname), full);
#endif
}

TEST(cat_fs, open_close_read_write)
{
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    char buf[CAT_BUFFER_DEFAULT_SIZE], red[CAT_BUFFER_DEFAULT_SIZE];
    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_ocrw");
    const char * fn = fnstr.c_str();
    int fd = -1;
    cat_fs_unlink(fn);
    SKIP_IF_(cat_fs_access(fn, F_OK) == 0, "Cannot remove test file");
    // error info tests
    //  open
    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(fd = cat_fs_open(fn, O_RDONLY), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);
    //  close
    errno = 0;
    cat_clear_last_error();
    // uv wraps close, this may not fail with -1
    // so we donot assert it
    cat_fs_close(-1);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);

    ASSERT_GE(fd = cat_fs_open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0600), 0);
    DEFER({
        cat_fs_unlink(fn);
    });

    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_read(fd, buf, 5), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);

    cat_srand(buf, CAT_BUFFER_DEFAULT_SIZE);

    for(int i = 0; i<128; i++){
        ASSERT_EQ(cat_fs_write(fd, buf, CAT_BUFFER_DEFAULT_SIZE), CAT_BUFFER_DEFAULT_SIZE);
    }

    ASSERT_EQ(0, cat_fs_close(fd));

    //printf("%s\n",cat_get_last_error_message());
    ASSERT_GE(fd = cat_fs_open(fn, O_RDONLY, 0600), 0);
    DEFER(cat_fs_close(fd));

    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_write(fd, "hello", 5), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);

    for(int i = 0; i<128; i++){
        ASSERT_EQ(cat_fs_read(fd, red, CAT_BUFFER_DEFAULT_SIZE), CAT_BUFFER_DEFAULT_SIZE);
        ASSERT_EQ(std::string(buf, CAT_BUFFER_DEFAULT_SIZE), std::string(red, CAT_BUFFER_DEFAULT_SIZE));
    }
    ASSERT_LT(cat_fs_read(fd, red, CAT_BUFFER_DEFAULT_SIZE), CAT_BUFFER_DEFAULT_SIZE);
    ASSERT_NE(cat_get_last_error_code(), 0);
}

TEST(cat_fs, fread_fwrite)
{
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_frw");
    const char * fn = fnstr.c_str();
    char buf[4096] = { 0 };
    int fd = -1;
    cat_fs_unlink(fn);
    SKIP_IF_(cat_fs_access(fn, F_OK) == 0, "Cannot remove test file");

    ASSERT_GE(fd = cat_fs_open(fn, O_CREAT | O_WRONLY), 0);
    DEFER({
        if(fd > 0){
            cat_fs_close(fd);
        }
    });
    FILE *stream = nullptr, *rstream = nullptr;
    DEFER({
        if(stream){
            fclose(stream);
            // this will also close backing fd
            fd = -1;
        }
        if(rstream){
            fclose(rstream);
        }
    });
    // fdopen is a POSIX function, but it also usable in Windows
    ASSERT_NE(stream = fdopen(fd, "w"), nullptr);

    // no error here
    errno = 0;
    cat_clear_last_error();
    ASSERT_EQ(cat_fs_fwrite(buf, 2, 2048, stream), 2048);
    ASSERT_EQ(errno, 0);
    ASSERT_EQ(ferror(stream), 0);
    ASSERT_EQ(cat_get_last_error_code(), 0);

    // not opened for read
    errno = 0;
    cat_clear_last_error();
    ASSERT_EQ(cat_fs_fread(buf, 1, 4096, stream), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(ferror(stream), 0);
    ASSERT_NE(cat_get_last_error_code(), 0);

    // create new read only stream
    ASSERT_EQ(fclose(stream), 0);
    stream = nullptr;
    fd = -1;
    ASSERT_EQ(cat_fs_unlink(fn), 0);
    ASSERT_EQ(cat_fs_close(cat_fs_open(fn, O_CREAT | O_WRONLY, 0666)), 0);
    ASSERT_NE(rstream = fopen(fn, "r"), nullptr);

    // no error here, feof == true
    errno = 0;
    cat_clear_last_error();
    ASSERT_EQ(cat_fs_fread(buf, 1, 4096, rstream), 0);
    ASSERT_EQ(errno, 0);
    ASSERT_EQ(ferror(rstream), 0);
    ASSERT_NE(feof(rstream), 0);
    ASSERT_EQ(cat_get_last_error_code(), 0);

    // not opened for write
    errno = 0;
    cat_clear_last_error();
    ASSERT_EQ(cat_fs_fwrite(buf, 1, 4096, rstream), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(ferror(rstream), 0);
    ASSERT_NE(cat_get_last_error_code(), 0);
}

#define PWRITE_PREAD_BUFSIZ 8192
TEST(cat_fs, pwrite_pread)
{
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    char zeros[PWRITE_PREAD_BUFSIZ], rands[PWRITE_PREAD_BUFSIZ], red[PWRITE_PREAD_BUFSIZ];
    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_ocrw2");
    const char * fn = fnstr.c_str();
    int fd = -1;
    cat_fs_unlink(fn);
    ASSERT_GE(fd = cat_fs_open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0600), 0);
    DEFER({
        cat_fs_unlink(fn);
    });

    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_read(fd, zeros, PWRITE_PREAD_BUFSIZ - 1024), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);

    memset(zeros, 0, sizeof(zeros));
    memset(red, 0, sizeof(red));
    cat_srand(rands, PWRITE_PREAD_BUFSIZ);

    ASSERT_EQ(cat_fs_write(fd, zeros, PWRITE_PREAD_BUFSIZ - 1024), PWRITE_PREAD_BUFSIZ - 1024);

    ASSERT_EQ(cat_fs_pwrite(fd, &rands[0], 128, 0), 128);
    ASSERT_EQ(cat_fs_pwrite(fd, &rands[1234], 512, 1234), 512);
    ASSERT_EQ(cat_fs_pwrite(fd, &rands[PWRITE_PREAD_BUFSIZ - 1024], 512, PWRITE_PREAD_BUFSIZ - 1024), 512);
    // after these:
    // 0:128=rands, 128:1234=zeros, 1234:1746=rands, 1746:(siz-1024)=zeros, (size-1024):(siz-512)=rands

    ASSERT_EQ(0, cat_fs_close(fd));
    ASSERT_GE(fd = cat_fs_open(fn, O_RDONLY, 0600), 0);
    DEFER({cat_fs_close(fd);});

    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_write(fd, zeros, PWRITE_PREAD_BUFSIZ - 1024), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);

    // 0:128 = rands
    ASSERT_EQ(cat_fs_pread(fd, &red[0], 128, 0), 128);
    ASSERT_EQ(std::string(&red[0], 128), std::string(&rands[0], 128));
    // reproducibility
    ASSERT_EQ(cat_fs_pread(fd, &red[0], 128, 0), 128);
    ASSERT_EQ(std::string(&red[0], 128), std::string(&rands[0], 128));
    // 128:1234 = zeros
    ASSERT_EQ(cat_fs_pread(fd, &red[128], 1234-128, 128), 1234-128);
    ASSERT_EQ(std::string(&red[128], 1234-128), std::string(&zeros[128], 1234-128));
    // 1234:1746 = rands
    ASSERT_EQ(cat_fs_pread(fd, &red[1234], 512, 1234), 512);
    ASSERT_EQ(std::string(&red[1234], 512), std::string(&rands[1234], 512));
    // 1746:(siz-1024) = zeros
    ASSERT_EQ(cat_fs_pread(fd, &red[1746], PWRITE_PREAD_BUFSIZ-1024-1746, 1746), PWRITE_PREAD_BUFSIZ-1024-1746);
    ASSERT_EQ(std::string(&red[1746], PWRITE_PREAD_BUFSIZ-1024-1746), std::string(&zeros[1746], PWRITE_PREAD_BUFSIZ-1024-1746));
    // (size-1024):(siz-512) = rands
    ASSERT_EQ(cat_fs_pread(fd, &red[PWRITE_PREAD_BUFSIZ-1024], 1024, PWRITE_PREAD_BUFSIZ-1024), 512);
    ASSERT_EQ(std::string(&red[PWRITE_PREAD_BUFSIZ-1024], 512), std::string(&rands[PWRITE_PREAD_BUFSIZ-1024], 512));

}

// fsync and fdatasync cannot be tested here

TEST(cat_fs, ftruncate)
{
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    char buf[16], red[16];
    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_ftruncate");
    const char * fn = fnstr.c_str();
    int fd = -1;
    ASSERT_GE(fd = cat_fs_open(fn, O_RDWR | O_CREAT | O_TRUNC, 0600), 0);
    DEFER({
        cat_fs_unlink(fn);
    });

    cat_srand(buf, 16);

    ASSERT_EQ(cat_fs_write(fd, buf, 16), 16);
    ASSERT_EQ(0, cat_fs_close(fd));

    ASSERT_GE(fd = cat_fs_open(fn, O_RDWR), 0);
    DEFER(cat_fs_close(fd));
    ASSERT_EQ(cat_fs_ftruncate(fd, 8), 0);

    ASSERT_EQ(cat_fs_read(fd, red, 16), 8);
    ASSERT_EQ(std::string(red, 8), std::string(buf, 8));

    cat_fs_close(fd);
    ASSERT_GE(fd = cat_fs_open(fn, O_RDONLY, 0600), 0);
    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_ftruncate(fd, 8), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);
}

#define LSEEK_BUFSIZ 32768
TEST(cat_fs, stat_lstat_fstat)
{
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    umask(0);

    cat_stat_t statbuf = {0};
    char buf[LSEEK_BUFSIZ];
    cat_srand(buf, LSEEK_BUFSIZ);

    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_stats");
    const char * fn = fnstr.c_str();
    int fd = -1;
    cat_fs_unlink(fn);
    ASSERT_GE(fd = cat_fs_open(fn, O_RDWR | O_CREAT | O_TRUNC, 0666), 0);
    ASSERT_EQ(cat_fs_write(fd, buf, LSEEK_BUFSIZ), LSEEK_BUFSIZ);
    ASSERT_EQ(cat_fs_fstat(fd, &statbuf), 0);
    ASSERT_EQ(statbuf.st_mode & 0777, 0666);
    ASSERT_EQ(statbuf.st_size, LSEEK_BUFSIZ);
    ASSERT_EQ(cat_fs_close(fd), 0);

    /*
    // getosfhandle have internal assertions, so we donot test this
    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_fstat(fd, &statbuf), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);
    DEFER(cat_fs_unlink(fn));
    */

    ASSERT_EQ(cat_fs_stat(fn, &statbuf), 0);
    ASSERT_EQ(statbuf.st_mode & 0777, 0666);
    ASSERT_EQ(statbuf.st_size, LSEEK_BUFSIZ);
    ASSERT_EQ(cat_fs_lstat(fn, &statbuf), 0);
    ASSERT_EQ(statbuf.st_mode & 0777, 0666);
    ASSERT_EQ(statbuf.st_size, LSEEK_BUFSIZ);

    cat_fs_unlink(fn);
    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_stat(fn, &statbuf), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);
    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_lstat(fn, &statbuf), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);
}

TEST(cat_fs, link)
{
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_target");
    const char * fn = fnstr.c_str();
    std::string fn2str = path_join(TEST_TMP_PATH, "cat_tests_link");
    const char * fn2 = fn2str.c_str();

    DEFER({
        cat_fs_unlink(fn);
        cat_fs_unlink(fn2);
    });

    cat_stat_t statbuf;

    cat_fs_unlink(fn);
    cat_fs_unlink(fn2);
    ASSERT_EQ(cat_fs_close(cat_fs_open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0600)), 0);

    errno = 0;
    cat_clear_last_error();
    if(cat_fs_link(fn, fn2) != 0){
        ASSERT_NE(errno, 0);
        ASSERT_NE(cat_get_last_error_code(), 0);
        GTEST_SKIP_("File system may not support hard link");
    }
    ASSERT_LT(cat_fs_link(fn, fn), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);

    ASSERT_EQ(cat_fs_stat(fn2, &statbuf), 0);
    ASSERT_GT(statbuf.st_nlink, 1);
    uint64_t inode = statbuf.st_ino;
    ASSERT_EQ(cat_fs_stat(fn, &statbuf), 0);
    ASSERT_GT(statbuf.st_nlink, 1);
    ASSERT_EQ(inode, statbuf.st_ino);
}

TEST(cat_fs, symlink){
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_target");
    const char * fn = fnstr.c_str();
    std::string fn2str = path_join(TEST_TMP_PATH, "cat_tests_link");
    const char * fn2 = fn2str.c_str();

    DEFER({
        cat_fs_unlink(fn);
        cat_fs_unlink(fn2);
    });

    cat_stat_t statbuf;

    cat_fs_unlink(fn);
    cat_fs_unlink(fn2);
    ASSERT_EQ(cat_fs_close(cat_fs_open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0600)), 0);
    errno = 0;
    cat_clear_last_error();
    if(cat_fs_symlink(fn, fn2, 0) != 0){
        ASSERT_NE(errno, 0);
        ASSERT_NE(cat_get_last_error_code(), 0);
        GTEST_SKIP_("File system may not support symbol link");
    }
    ASSERT_LT(cat_fs_symlink(fn, fn, 0), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);

    ASSERT_EQ(cat_fs_stat(fn2, &statbuf), 0);
    uint64_t inode = statbuf.st_ino;
    ASSERT_EQ(cat_fs_lstat(fn2, &statbuf), 0);
    ASSERT_NE(inode, statbuf.st_ino);
    ASSERT_EQ(statbuf.st_mode & S_IFLNK, S_IFLNK);
}

TEST(cat_fs, lseek)
{
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    char buf[LSEEK_BUFSIZ], red[LSEEK_BUFSIZ];
    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_seek");
    const char * fn = fnstr.c_str();
    // this will cause assert in windows fail
    //ASSERT_LT(cat_fs_lseek(-1, -1, -1), 0);
    //ASSERT_NE(cat_get_last_error_code(), 0);
    int fd = cat_fs_open(fn, O_RDWR | O_CREAT | O_TRUNC, 0600);
    ASSERT_GE(fd, 0);
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

TEST(cat_fs, fseek_ftell)
{
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    char buf[LSEEK_BUFSIZ], red[LSEEK_BUFSIZ];
    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_fseektell");
    const char * fn = fnstr.c_str();
    cat_fs_unlink(fn);
    SKIP_IF_(cat_fs_access(fn, F_OK) == 0, "Cannot remove test file");

    FILE *stream = fopen(fn, "w+");
    DEFER({
        if(stream){
            fclose(stream);
        }
    });

    cat_srand(buf, LSEEK_BUFSIZ);

#define FILE_SEEK(offset, whence) (cat_fs_fseek(stream, offset, whence), cat_fs_ftell(stream))
    off_t ret;
    ASSERT_EQ(cat_fs_fwrite(buf, 1, LSEEK_BUFSIZ, stream), LSEEK_BUFSIZ);
    ASSERT_EQ(FILE_SEEK(0, SEEK_SET), 0);
    ASSERT_EQ(FILE_SEEK(10, SEEK_SET), 10);
    ASSERT_GT(ret = FILE_SEEK(0, SEEK_END), 0);
    ASSERT_EQ(FILE_SEEK(-10, SEEK_END), ret - 10);
    ASSERT_EQ(FILE_SEEK(0, SEEK_CUR), ret - 10);
    ASSERT_EQ(FILE_SEEK(-10, SEEK_CUR), ret - 20);
    ASSERT_EQ(FILE_SEEK(0, SEEK_SET), 0);
    ASSERT_EQ(cat_fs_fread(red, 1, LSEEK_BUFSIZ, stream), LSEEK_BUFSIZ);
    ASSERT_EQ(std::string(buf, LSEEK_BUFSIZ), std::string(red, LSEEK_BUFSIZ));
#undef FILE_SEEK
}

TEST(cat_fs, fflush){
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_fflush");
    const char * fn = fnstr.c_str();
    cat_fs_unlink(fn);
    SKIP_IF_(cat_fs_access(fn, F_OK) == 0, "Cannot remove test file");

    int fd = -1;
    FILE *stream = nullptr;
    ASSERT_GE(fd = cat_fs_open(fn, O_CREAT | O_RDWR), 0);
    DEFER({
        if(fd > 0){
            cat_fs_close(fd);
        }
    });
    // fdopen is a POSIX function, but it also usable in Windows
    ASSERT_NE(stream = fdopen(fd, "w+"), nullptr);
    DEFER({
        if(stream){
            fclose(stream);
            // this will also close backing fd
            fd = -1;
        }
    });

    char buffer[4096], red[4096];
    ASSERT_EQ(setvbuf(stream, buffer, _IOFBF, 4096), 0);

#define TEST_STR "nihaoshijie!"
    ASSERT_EQ(cat_fs_fwrite(TEST_STR, 1, sizeof(TEST_STR), stream), sizeof(TEST_STR));
#undef TEST_STR
    cat_stat_t statbuf;
    ASSERT_EQ(cat_fs_fstat(fd, &statbuf), 0);
    ASSERT_EQ(statbuf.st_size, 0);
    ASSERT_EQ(cat_fs_read(fd, red, 4096), 0);
}

TEST(cat_fs, access)
{
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_access");
    const char * fn = fnstr.c_str();
    int fd = -1;
    DEFER({
        cat_fs_unlink(fn);
    });
    cat_fs_unlink(fn);
    ASSERT_GE(fd = cat_fs_open(fn, O_RDONLY | O_CREAT, 0400), 0);
    ASSERT_EQ(cat_fs_access(fn, R_OK), 0);
    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_access(fn, X_OK|W_OK), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);
    ASSERT_EQ(cat_fs_close(fd), 0);

    cat_fs_unlink(fn);
    ASSERT_GE(fd = cat_fs_open(fn, O_RDWR | O_CREAT, 0700), 0);
    ASSERT_EQ(cat_fs_access(fn, W_OK|X_OK|R_OK), 0);
    ASSERT_EQ(cat_fs_close(fd), 0);
}

TEST(cat_fs, mkdir_rmdir)
{
    SKIP_IF_(no_tmp(), "Temp dir not writable");

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
    errno = 0;
    ASSERT_LT(cat_fs_mkdir(fn2, 0777), 0);
    ASSERT_EQ(errno, ENOENT);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOENT);
    ASSERT_EQ(cat_fs_mkdir(fn, 0777), 0);
    errno = 0;
    ASSERT_LT(cat_fs_mkdir(fn, 0777), 0);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EEXIST);
    ASSERT_EQ(errno, EEXIST);
    ASSERT_EQ(cat_fs_mkdir(fn2, 0777), 0);
    errno = 0;
    ASSERT_LT(cat_fs_rmdir(fn), 0);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOTEMPTY);
    ASSERT_EQ(errno, ENOTEMPTY);
    ASSERT_EQ(cat_fs_rmdir(fn2), 0);
    ASSERT_EQ(cat_fs_rmdir(fn), 0);
    errno = 0;
    ASSERT_LT(cat_fs_rmdir(fn2), 0);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOENT);
    ASSERT_EQ(errno, ENOENT);
}

#ifdef CAT_OS_WIN
TEST(cat_fs, symlink_windows) {
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_linkdir");
    const char* fn = fnstr.c_str();
    std::string lnstr = path_join(TEST_TMP_PATH, "cat_tests_linkdir2");
    const char* ln = lnstr.c_str();
    DEFER({
        cat_fs_rmdir(fn);
        cat_fs_rmdir(ln);
        cat_fs_unlink(ln);
    });
    cat_fs_rmdir(fn);
    cat_fs_rmdir(ln);
    cat_fs_unlink(ln);

    // make dir target
    ASSERT_EQ(cat_fs_mkdir(fn, 0777), 0);

    // make symlink
    errno = 0;
    cat_clear_last_error();
    if(cat_fs_symlink(fn, ln, CAT_FS_SYMLINK_DIR) != 0){
        ASSERT_NE(errno, 0);
        ASSERT_NE(cat_get_last_error_code(), 0);
        GTEST_SKIP_("File system may not support symbol link or perm denied");
    }
    ASSERT_LT(cat_fs_symlink(fn, ln, CAT_FS_SYMLINK_DIR), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);

    cat_stat_t statbuf;
    __int64 ino = 0;
    ASSERT_EQ(cat_fs_lstat(fn, &statbuf), 0);
    ino = statbuf.st_ino;
    ASSERT_EQ(cat_fs_stat(ln, &statbuf), 0);
    ASSERT_EQ(ino, statbuf.st_ino);
    ASSERT_EQ(cat_fs_lstat(ln, &statbuf), 0);
    ASSERT_NE(ino, statbuf.st_ino);
    // remove it
    ASSERT_EQ(cat_fs_unlink(ln), 0);
    // make symlink
    ASSERT_EQ(cat_fs_symlink(fn, ln, CAT_FS_SYMLINK_DIR), 0);
    // remove it
    ASSERT_EQ(cat_fs_rmdir(ln), 0);
    // make symlink
    ASSERT_EQ(cat_fs_symlink(fn, ln, CAT_FS_SYMLINK_JUNCTION), 0);
    ASSERT_EQ(cat_fs_lstat(fn, &statbuf), 0);
    ino = statbuf.st_ino;
    ASSERT_EQ(cat_fs_stat(ln, &statbuf), 0);
    ASSERT_EQ(ino, statbuf.st_ino);
    ASSERT_EQ(cat_fs_lstat(ln, &statbuf), 0);
    ASSERT_NE(ino, statbuf.st_ino);
    // remove it
    ASSERT_EQ(cat_fs_unlink(ln), 0);
    // make symlink
    ASSERT_EQ(cat_fs_symlink(fn, ln, CAT_FS_SYMLINK_JUNCTION), 0);
    // remove it
    ASSERT_EQ(cat_fs_unlink(ln), 0);
}
#endif

TEST(cat_fs, opendir_readdir_rewinddir_closedir)
{
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_readdir");
    const char * fn = fnstr.c_str();
    std::string fndstr = path_join(fnstr, "cat_tests_readdir_dir1");
    const char * fnd = fndstr.c_str();
    std::string fnd2str = path_join(fnstr, "cat_tests_readdir_dir2");
    const char * fnd2 = fnd2str.c_str();
    std::string fnf3str = path_join(fnstr, "cat_tests_readdir_file3");
    const char * fnf3 = fnf3str.c_str();
    DEFER({
        cat_fs_rmdir(fnd);
        cat_fs_rmdir(fnd2);
        cat_fs_unlink(fnf3);
        cat_fs_rmdir(fn);
    });
    cat_fs_rmdir(fnd);
    cat_fs_rmdir(fnd2);
    cat_fs_unlink(fnf3);
    cat_fs_rmdir(fn);

    errno = 0;
    cat_clear_last_error();
    ASSERT_EQ(cat_fs_opendir(nullptr), nullptr);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);
#ifndef CAT_OS_WIN
    errno = 0;
#endif // CAT_OS_WIN
    cat_clear_last_error();
    ASSERT_EQ(cat_fs_opendir("\\/<notexistpath>"), nullptr);
#ifndef CAT_OS_WIN
    ASSERT_NE(errno, 0);
#endif // CAT_OS_WIN
    ASSERT_NE(cat_get_last_error_code(), 0);
    errno = 0;
    cat_clear_last_error();
    cat_fs_rewinddir(nullptr);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);
    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_closedir(nullptr), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);
    errno = 0;
    cat_clear_last_error();
    ASSERT_EQ(cat_fs_readdir(nullptr), nullptr);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);

    ASSERT_EQ(cat_fs_mkdir(fn, 0777), 0);
    ASSERT_EQ(cat_fs_mkdir(fnd, 0777), 0);
    ASSERT_EQ(cat_fs_mkdir(fnd2, 0777), 0);
    ASSERT_EQ(cat_fs_close(cat_fs_open(fnf3, O_RDONLY | O_CREAT | O_TRUNC, 0400)), 0);

    cat_dir_t * dir;
    ASSERT_NE(dir = cat_fs_opendir(fn), nullptr);

    cat_dirent_t* dirents[5] = {0};
    DEFER({
        for (int i = 0; i < 5; i++) {
            if (dirents[i]) {
                if (dirents[i]->name) {
                    free((void*)dirents[i]->name);
                }
                free(dirents[i]);
            }
        }
    });
    for (int repeat = 0; repeat < 3; repeat++) {
        //printf("repeat %d\n", repeat);
        for (int i = 0; i < 5; i++) {
            //ASSERT_NE(dirents[i] = cat_fs_readdir(dir), nullptr);
            if ((dirents[i] = cat_fs_readdir(dir)) == nullptr) {
                printf("cat_fs_readdir failed at repeat %d, index %d\n", repeat, i);
                printf("%d: %s\n",cat_get_last_error_code(), cat_get_last_error_message());
                GTEST_FATAL_FAILURE_("failed test");
            }
            //printf("found %s\n", dirents[i]->name);
            std::string name = std::string(dirents[i]->name);

            if(std::string(".") == name){
                ASSERT_EQ(dirents[i]->type, CAT_DIRENT_DIR);
            }else if(std::string("..") == name){
                ASSERT_EQ(dirents[i]->type, CAT_DIRENT_DIR);
            }else if(std::string("cat_tests_readdir_dir1") == name){
                ASSERT_EQ(dirents[i]->type, CAT_DIRENT_DIR);
            }else if(std::string("cat_tests_readdir_dir2") == name){
                ASSERT_EQ(dirents[i]->type, CAT_DIRENT_DIR);
            }else if(std::string("cat_tests_readdir_file3") == name){
                ASSERT_EQ(dirents[i]->type, CAT_DIRENT_FILE);
            }else{
                printf("Bad result: %s at repeat %d, index %d", dirents[i]->name, repeat, i);
                GTEST_FATAL_FAILURE_("failed test");
            }
        }
        for (int i = 0; i < 5; i++) {
            for (int j = 4; j > i; j--) {
                //printf("compare %d with %d\n", i, j);
                //printf("compare %s with %s\n", dirents[i]->name, dirents[j]->name);
                ASSERT_NE(std::string(dirents[i]->name), std::string(dirents[j]->name));
            }
        }
        for (int i = 0; i < 5; i++) {
            if (dirents[i]) {
                if (dirents[i]->name) {
                    free((void*)dirents[i]->name);
                }
                free(dirents[i]);
                dirents[i] = nullptr;
            }
        }
        ASSERT_EQ(dirents[0] = cat_fs_readdir(dir), nullptr);
        cat_fs_rewinddir(dir);
    }

    ASSERT_EQ(cat_fs_closedir(dir), 0);
}

static int cat_test_nofile_filter(const cat_dirent_t * x){
    return x->type != CAT_DIRENT_FILE;
}

static int cat_test_reverse_compare(const cat_dirent_t * a, const cat_dirent_t * b){
    //printf("a: %p %s\n", a->name, a->name);
    //printf("b: %p %s\n", b->name, b->name);
    return -strcoll(a->name, b->name);
}

TEST(cat_fs, scandir)
{
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_dir");
    const char * fn = fnstr.c_str();
    std::string fndstr = path_join(fnstr, "cat_tests_dir_dir1");
    const char * fnd = fndstr.c_str();
    std::string fnd2str = path_join(fnstr, "cat_tests_dir_dir2");
    const char * fnd2 = fnd2str.c_str();
    std::string fnf3str = path_join(fnstr, "cat_tests_dir_file3");
    const char * fnf3 = fnf3str.c_str();
    DEFER({
        cat_fs_rmdir(fnd);
        cat_fs_rmdir(fnd2);
        cat_fs_unlink(fnf3);
        cat_fs_rmdir(fn);
    });
    cat_fs_rmdir(fnd);
    cat_fs_rmdir(fnd2);
    cat_fs_unlink(fnf3);
    cat_fs_rmdir(fn);

    int ret = -1;
    cat_dirent_t * namelist;

    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_scandir(fn, &namelist, NULL, NULL), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);

    ASSERT_EQ(cat_fs_mkdir(fn, 0777), 0);
    ASSERT_EQ(cat_fs_mkdir(fnd, 0777), 0);
    ASSERT_EQ(cat_fs_mkdir(fnd2, 0777), 0);
    ASSERT_EQ(cat_fs_close(cat_fs_open(fnf3, O_RDONLY | O_CREAT | O_TRUNC, 0400)), 0);

    ASSERT_EQ(ret = cat_fs_scandir(fn, &namelist, cat_test_nofile_filter, cat_test_reverse_compare), 2);
    DEFER({
        for(int i = 0 ;i < ret; i++){
            free((void*)namelist[i].name);
        }
        free(namelist);
    });
    ASSERT_EQ(std::string("cat_tests_dir_dir2"), std::string(namelist[0].name));
    ASSERT_EQ(std::string("cat_tests_dir_dir1"), std::string(namelist[1].name));

}

TEST(cat_fs, rename)
{
    SKIP_IF_(no_tmp(), "Temp dir not writable");

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
    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_rename(fn, fn3), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOENT);
    ASSERT_EQ(cat_fs_rename(fn2, fn3), 0);
}

TEST(cat_fs, readlink){
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_target");
    const char * fn = fnstr.c_str();
    std::string fn2str = path_join(TEST_TMP_PATH, "cat_tests_link");
    const char * fn2 = fn2str.c_str();

    DEFER({
        cat_fs_unlink(fn);
        cat_fs_unlink(fn2);
    });

    char buf[4096] = {0};

    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_readlink(fn2, buf, 4096), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);

    cat_fs_unlink(fn);
    cat_fs_unlink(fn2);
    ASSERT_EQ(cat_fs_close(cat_fs_open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0600)), 0);
    if(cat_fs_symlink(fn, fn2, 0) != 0){
        GTEST_SKIP_("File system may not support symbol link");
    }
    ASSERT_EQ(cat_fs_readlink(fn2, buf, 1), 1);
    ASSERT_NE(buf[0], 0);
    ASSERT_EQ(buf[1], 0);

    ASSERT_GT(cat_fs_readlink(fn2, buf, 4096), 0);
    ASSERT_EQ(std::string(buf), fn);
}

TEST(cat_fs, realpath){
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    std::string dstr = path_join(TEST_TMP_PATH, "cat_tests_dir");
    const char * d = dstr.c_str();
    std::string fnstr = path_join(dstr, "cat_tests_target");
    const char * fn = fnstr.c_str();
    std::string fn2str = path_join(dstr, "cat_tests_link");
    const char * fn2 = fn2str.c_str();

    std::string pathstr = path_join(TEST_TMP_PATH, "cat_tests_dir" TEST_PATH_SEP ".." TEST_PATH_SEP "cat_tests_dir" TEST_PATH_SEP "cat_tests_link");
    const char * testpath = pathstr.c_str();

    DEFER({
        cat_fs_unlink(fn);
        cat_fs_unlink(fn2);
        cat_fs_rmdir(d);
    });

    cat_fs_unlink(fn);
    cat_fs_unlink(fn2);
    cat_fs_rmdir(d);

    char buf[4096] = {0};
    errno = 0;
    cat_clear_last_error();
    ASSERT_EQ(cat_fs_realpath(testpath, buf), nullptr);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);

    ASSERT_EQ(cat_fs_mkdir(d, 0777), 0);
    ASSERT_EQ(cat_fs_close(cat_fs_open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0600)), 0);
    if(cat_fs_symlink(fn, fn2, 0) != 0){
        GTEST_SKIP_("File system may not support symbol link");
    }
    char * ret = nullptr;
    ASSERT_NE(ret = cat_fs_realpath(testpath, nullptr), nullptr);
    free(ret);
    ASSERT_EQ(ret = cat_fs_realpath(testpath, buf), buf);
    std::string teststr = std::string(buf);
    ASSERT_EQ(ret = cat_fs_realpath(fn, buf), buf);
    std::string origstr = std::string(buf);
    ASSERT_EQ(teststr, origstr);
}

TEST(cat_fs, chmod_fchmod){
    SKIP_IF_(no_tmp(), "Temp dir not writable");
    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_chmod");
    const char * fn = fnstr.c_str();
    cat_fs_unlink(fn);
    ASSERT_EQ(cat_fs_close(cat_fs_open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0200)), 0);
    DEFER({cat_fs_unlink(fn);});

    ASSERT_EQ(cat_fs_chmod(fn, 0600), 0);
    ASSERT_EQ(cat_fs_chmod(fn, 0400), 0);
    int fd = -1;
    ASSERT_GE(fd = cat_fs_open(fn, O_RDONLY), 0);
    ASSERT_EQ(cat_fs_fchmod(fd, 0600), 0);
    ASSERT_EQ(cat_fs_fchmod(fd, 0200), 0);
}

TEST(cat_fs, chown_fchown_lchown){
    SKIP_IF_(no_tmp(), "Temp dir not writable");
    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_chown");
    const char * fn = fnstr.c_str();
    std::string fn2str = path_join(TEST_TMP_PATH, "cat_tests_chownlink");
    const char * fn2 = fn2str.c_str();
    cat_fs_unlink(fn);
    cat_fs_unlink(fn2);
    ASSERT_EQ(cat_fs_close(cat_fs_open(fn, O_RDONLY | O_CREAT | O_TRUNC, 0400)), 0);
    ASSERT_EQ(cat_fs_link(fn, fn2), 0);
    DEFER({
        cat_fs_unlink(fn);
        cat_fs_unlink(fn2);
    });

    ASSERT_EQ(cat_fs_chown(fn, -1, -1), 0);
    ASSERT_EQ(cat_fs_chown(fn2, -1, -1), 0);
    ASSERT_EQ(cat_fs_lchown(fn2, -1, -1), 0);
    int fd = -1;
    ASSERT_GE(fd = cat_fs_open(fn, O_RDONLY), 0);
    ASSERT_EQ(cat_fs_fchown(fd, -1, -1), 0);
#ifndef CAT_OS_WIN
    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_chown("/", 0, 0), 0);
    ASSERT_EQ(errno, EPERM);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EPERM);
    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_lchown("/", 0, 0), 0);
    ASSERT_EQ(errno, EPERM);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EPERM);
#endif
}

TEST(cat_fs, copyfile)
{
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    char buf[16], red[16];
    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_copyfile1");
    const char * fn = fnstr.c_str();
    std::string fn2str = path_join(TEST_TMP_PATH, "cat_tests_copyfile2");
    const char * fn2 = fn2str.c_str();
    int fd = -1;
    cat_fs_unlink(fn);
    cat_fs_unlink(fn2);
    DEFER({
        cat_fs_unlink(fn);
        cat_fs_unlink(fn2);
    });

    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_copyfile(fn, fn2, 0), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);

    ASSERT_GE(fd = cat_fs_open(fn, O_RDWR | O_CREAT | O_TRUNC, 0600), 0);
    cat_srand(buf, 16);
    ASSERT_EQ(cat_fs_write(fd, buf, 16), 16);
    ASSERT_EQ(0, cat_fs_close(fd));

    ASSERT_EQ(cat_fs_copyfile(fn, fn2, 0), 0);
    ASSERT_GE(fd = cat_fs_open(fn, O_RDONLY), 0);
    ASSERT_EQ(cat_fs_read(fd, red, 16), 16);
    ASSERT_EQ(std::string(red, 16), std::string(buf, 16));
}

// sendfile should be tested in sockets test

static int cat_test_mkxtemps(const cat_dirent_t * ent){
    const size_t tpl_len = sizeof("cat_tests_mktemp.") - 1;
    return strlen(ent->name) == tpl_len + 6 &&
        std::string(ent->name, tpl_len) ==
            std::string("cat_tests_mktemp.", tpl_len);
}

TEST(cat_fs, mkdtemp_mkstemp)
{
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    const char * pstr = nullptr;
    int fd = -1;
    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_mktemp.XXXXXX");
    char * fn = strdup(fnstr.c_str());
    char * fn2 = strdup(fnstr.c_str());
    DEFER({
        if(fn){
            free(fn);
        }
        if(fn2){
            free(fn2);
        }
        cat_dirent_t * namelist;
        int ret = cat_fs_scandir(TEST_TMP_PATH, &namelist, cat_test_mkxtemps, nullptr);
        for(int i = 0; i < ret; i++){
            std::string deletefnstr = path_join(TEST_TMP_PATH, namelist[i].name);
            const char* deletefn = deletefnstr.c_str();
            cat_fs_unlink(deletefn);
            cat_fs_rmdir(deletefn);
            free((void*)namelist[i].name);
        }
        free(namelist);
    });

    ASSERT_NE(pstr = cat_fs_mkdtemp(fn), nullptr);
    ASSERT_EQ(cat_fs_access(pstr, R_OK | W_OK), 0);

    ASSERT_GE(fd = cat_fs_mkstemp(fn2), 0);
    cat_stat_t statbuf;
    ASSERT_EQ(cat_fs_fstat(fd, &statbuf), 0);
    ASSERT_EQ(statbuf.st_mode & 0600, 0600);
    ASSERT_EQ(cat_fs_close(fd), 0);
}

TEST(cat_fs, utime_lutime_futime){
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_utime");
    const char * fn = fnstr.c_str();
    std::string fn2str = path_join(TEST_TMP_PATH, "cat_tests_lutime");
    const char * fn2 = fn2str.c_str();

    double changed_time1 = 865353600.0;
    double changed_time2 = 936201600.0;
    cat_stat_t statbuf;

    cat_fs_unlink(fn2);
    cat_fs_unlink(fn);
    DEFER({
        cat_fs_unlink(fn2);
        cat_fs_unlink(fn);
    });

    // getosfhandle have internel assertion
    // so we donot test futime
    //ASSERT_GT(cat_fs_futime(-1, -1, -1), 0);
    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_utime(fn, -1, -1), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);
    errno = 0;
    cat_clear_last_error();
    ASSERT_LT(cat_fs_lutime(fn, -1, -1), 0);
    ASSERT_NE(errno, 0);
    ASSERT_NE(cat_get_last_error_code(), 0);

    int fd = -1;
    ASSERT_GE(fd = cat_fs_open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0200), 0);
    ASSERT_EQ(cat_fs_futime(fd, changed_time2, changed_time2), 0);
    ASSERT_EQ(cat_fs_lstat(fn, &statbuf), 0);
    ASSERT_EQ(statbuf.st_atim.tv_sec, 936201600);
    ASSERT_EQ(statbuf.st_mtim.tv_sec, 936201600);
    ASSERT_EQ(cat_fs_close(fd), 0);
    int symlink_usable = (cat_fs_symlink(fn, fn2, 0) == 0);
    ASSERT_EQ(cat_fs_stat(fn, &statbuf), 0);
    ASSERT_GT(statbuf.st_atim.tv_sec, changed_time1);
    ASSERT_GT(statbuf.st_mtim.tv_sec, changed_time1);
    if(symlink_usable){
        ASSERT_EQ(cat_fs_lstat(fn2, &statbuf), 0);
        ASSERT_GT(statbuf.st_atim.tv_sec, changed_time2);
        ASSERT_GT(statbuf.st_mtim.tv_sec, changed_time2);
    }

    ASSERT_EQ(cat_fs_utime(fn, changed_time1, changed_time1), 0);
    ASSERT_EQ(cat_fs_stat(fn, &statbuf), 0);
    ASSERT_EQ(statbuf.st_atim.tv_sec, 865353600);
    ASSERT_EQ(statbuf.st_mtim.tv_sec, 865353600);
    if(symlink_usable){
        ASSERT_EQ(cat_fs_stat(fn2, &statbuf), 0);
        ASSERT_EQ(statbuf.st_atim.tv_sec, 865353600);
        ASSERT_EQ(statbuf.st_mtim.tv_sec, 865353600);
        ASSERT_EQ(cat_fs_lutime(fn2, changed_time2, changed_time2), 0);
        ASSERT_EQ(cat_fs_lstat(fn2, &statbuf), 0);
        ASSERT_EQ(statbuf.st_atim.tv_sec, 936201600);
        ASSERT_EQ(statbuf.st_mtim.tv_sec, 936201600);
    }
}

TEST(cat_fs, stat_2038)
{
    SKIP_IF_(true, "Not yet fix in upstream");
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    umask(0);

    char buf[LSEEK_BUFSIZ];
    cat_srand(buf, LSEEK_BUFSIZ);

    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_stat2038");
    const char * fn = fnstr.c_str();
    int fd = -1;
    cat_fs_unlink(fn);
    ASSERT_GE(fd = cat_fs_open(fn, O_RDWR | O_CREAT | O_TRUNC, 0666), 0);
    ASSERT_EQ(cat_fs_write(fd, buf, LSEEK_BUFSIZ), LSEEK_BUFSIZ);
    ASSERT_EQ(cat_fs_close(fd), 0);
    DEFER({
        ASSERT_EQ(cat_fs_unlink(fn), 0);
    });

    cat_stat_t statbuf = {0};

    ASSERT_EQ(cat_fs_utime(fn, (double)((uint64_t)INT32_MAX + 86400ULL), (double)((uint64_t)INT32_MAX + 86400ULL)), 0);
    ASSERT_EQ(cat_fs_stat(fn, &statbuf), 0);
    ASSERT_GT(statbuf.st_atim.tv_sec, 0);
    ASSERT_GT(statbuf.st_atim.tv_nsec, 0);
    ASSERT_GT(statbuf.st_mtim.tv_sec, 0);
    ASSERT_GT(statbuf.st_mtim.tv_nsec, 0);
}

TEST(cat_fs, statfs){
    cat_statfs_t statfsbuf;
#ifdef CAT_OS_WIN
    ASSERT_EQ(cat_fs_statfs("C:\\", &statfsbuf), 0);
#else
    ASSERT_EQ(cat_fs_statfs("/", &statfsbuf), 0);
#endif
}

#ifdef CAT_OS_WIN

inline int maxpath_260_clean(const char *dir){
    cat_dirent_t * entry = NULL;
    cat_dir_t * rmd = cat_fs_opendir(dir);
    std::string fullfnstr;
    const char * fullfn;
    int report_error = 1;
    int ret;
    DEFER({
        if(report_error){
            printf("ret = %d, error %d, msg %s\n", ret, cat_get_last_error_code(), cat_get_last_error_message());
        }
    });
    if(NULL != rmd){
        while(nullptr != (entry = cat_fs_readdir(rmd))){
            const char *fn = entry->name;
            if (
                ('.' == fn[0] && 0 == fn[1]) ||
                ('.' == fn[0] && '.' == fn[1] && 0 == fn[2])
            ){
                free((void*)entry->name);
                free((void*)entry);
                continue;
            }
            fullfnstr = path_join(dir, fn);
            free((void*)entry->name);
            free((void*)entry);
            fullfn = fullfnstr.c_str();
            cat_stat_t statbuf;
            if((ret = cat_fs_lstat(fullfn, &statbuf)) != 0){
                return -1;
            }
            if((statbuf.st_mode & S_IFMT) == S_IFDIR){
                if(maxpath_260_clean(fullfn) != 0){
                    return -1;
                }
            }else{
                if((ret = cat_fs_unlink(fullfn)) != 0){
                    return -1;
                }
            }
        }
        //printf("rm %s\n", dir);
        if((ret = cat_fs_rmdir(dir)) != 0){
            return -1;
        }
        cat_fs_closedir(rmd);
    }
    report_error = 0;
    return 0;
}

TEST(cat_fs, maxpath_260){
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    // prepare target names
    std::string dirstr = path_join(TEST_TMP_PATH, "cat_tests_maxpath_260");
    const size_t dirlen = dirstr.length();
    const char * dir = dirstr.c_str();
    SKIP_IF_(dirlen > 239 /* 247(see below) - "\\.XXXXXX" */, "Temp dir path is too long");

    // prepare temp dir
    ASSERT_EQ(maxpath_260_clean(dir), 0);
    ASSERT_EQ(cat_fs_mkdir(dir, 0777), 0);
    DEFER({ASSERT_EQ(maxpath_260_clean(dir), 0);});

    // defer show errors
    int report_error = 1;
    int ret = -1;
    size_t fnlen;
    int rlen;
    cat_dir_t * d = nullptr;
    cat_clear_last_error();
    DEFER({
        if(report_error){
            printf("fn length=%zu, recursive mkdir path len=%d,\nret = %d, error %d, msg %s\n", fnlen, rlen, ret, cat_get_last_error_code(), cat_get_last_error_message());\
        }
    });

    // start from 247
    // 247: nothing special
    // [248, 259]: MAXPATH-sizeof(8.3) > fnlen > MAXPATH
    // 260: MAXPATH
    // 261: fnlen > MAXPATH
    // end at 261
    for(fnlen = 247; fnlen < 262; fnlen++){
        // prepare "source" file name
        std::stringstream sstream = std::stringstream();
        sstream << dirstr << "\\" << std::setfill('s') << std::setw(fnlen - dirlen - 1/*\*/) << "";
        std::string srcstr = sstream.str();
        const char* src = srcstr.c_str();
        //printf("s: %s\n", src);
        // prepare "destnation" file name
        std::stringstream dstream = std::stringstream();
        dstream << dirstr << "\\" << std::setfill('d') << std::setw(fnlen - dirlen - 1/*\*/) << "";
        std::string dststr = dstream.str();
        const char* dst = dststr.c_str();
        //printf("d: %s\n", dst);
        // prepare mk{d,s}temp template
        std::stringstream tstream = std::stringstream();
        tstream << dirstr << "\\" << std::setfill('t') << std::setw(fnlen - dirlen - 1/*\*/) << ".XXXXXX";
        std::string tmpstr = tstream.str();
        const char* tmp = tmpstr.c_str();
        //printf("t: %s\n", tmp);

        ASSERT_GT(ret = cat_fs_open(src, CAT_FS_O_WRONLY | CAT_FS_O_CREAT, 0666), 0);
        ASSERT_EQ(ret = cat_fs_close(ret), 0);

        ASSERT_EQ(ret = cat_fs_mkdir(dst, 0777), 0);
        ASSERT_NE(d = cat_fs_opendir(dst), nullptr);
        ASSERT_EQ(ret = cat_fs_closedir(d), 0);
        ASSERT_EQ(ret = cat_fs_rmdir(dst), 0);

        ASSERT_EQ(ret = cat_fs_rename(src, dst), 0);
        ASSERT_EQ(ret = cat_fs_unlink(dst), 0);

        ASSERT_EQ(ret = cat_fs_close(cat_fs_open(src, CAT_FS_O_WRONLY | CAT_FS_O_CREAT, 0666)), 0);
        ASSERT_EQ(ret = cat_fs_access(src, 0), 0);

        cat_stat_t statbuf = {0};
        ASSERT_EQ(ret = cat_fs_stat(src, &statbuf), 0);
        ASSERT_EQ(ret = cat_fs_lstat(src, &statbuf), 0);

        ASSERT_EQ(ret = cat_fs_utime(src, 0, 0), 0);
        ASSERT_EQ(ret = cat_fs_lutime(src, 0, 0), 0);

        // link and sym link may fail because of permission
        ret = cat_fs_link(src, dst);
        void* resolved = NULL;
        if(0 == ret){
            ASSERT_NE(resolved = cat_fs_realpath(dst, nullptr), nullptr);
            free(resolved);
            ASSERT_EQ(ret = cat_fs_unlink(dst), 0);
        }
        ret = cat_fs_symlink(src, dst, 0);
        if(0 == ret){
            ASSERT_EQ(ret = cat_fs_readlink(dst, nullptr, 0), 0);
            ASSERT_NE(resolved = cat_fs_realpath(dst, nullptr), nullptr);
            free(resolved);
            ASSERT_EQ(ret = cat_fs_unlink(dst), 0);
        }
        ret = cat_fs_symlink(src, dst, 1);
        if(0 == ret){
            ASSERT_EQ(ret = cat_fs_readlink(dst, nullptr, 0), 0);
            ASSERT_EQ(ret = cat_fs_unlink(dst), 0);
        }


        ASSERT_EQ(ret = cat_fs_chmod(src, 0666), 0);
        ASSERT_EQ(ret = cat_fs_chown(src, -1 , -1), 0);
        ASSERT_EQ(ret = cat_fs_lchown(src, -1, -1), 0);

        ASSERT_EQ(ret = cat_fs_copyfile(src, dst, 0), 0);

        const char * tmpname = nullptr;
        ASSERT_NE(tmpname = cat_fs_mkdtemp(tmp), nullptr);
        ASSERT_EQ(ret = cat_fs_rmdir(tmpname), 0);
        ASSERT_GT(ret = cat_fs_mkstemp(tmp), 0);
        ASSERT_EQ(cat_fs_close(ret), 0);
        // should rm that temp here?

        cat_statfs_t statfsbuf;
        ASSERT_EQ(ret = cat_fs_statfs(src, &statfsbuf), 0);
    }

    // deep mkdir/rmdir
    std::stringstream rstream = std::stringstream();
    rstream << dirstr ;
    for(rlen = 255; rlen > 0; rlen--){
        rstream << "\\" << std::setfill('r') << std::setw(rlen) << "";
        std::string rstr = rstream.str();
        //printf("%d\n", rstr.length());
        // hard limit for directory path is 32740, not documented
        // donot know why, see https://github.com/dotnet/runtime/issues/23807
        // maybe it's 32767 - sizeof(L"\\88888888.333") - sizeof('\0') ?
        if(rstr.length() > 32739){
            break;
        }
        const char* r = rstr.c_str();
        //printf("r: %s\n", r);
        ASSERT_EQ(ret = cat_fs_mkdir(r, 0777), 0);
        ASSERT_NE(d = cat_fs_opendir(r), nullptr);
        ASSERT_EQ(ret = cat_fs_closedir(d), 0);
        ASSERT_EQ(ret = cat_fs_rmdir(r), 0);
        ASSERT_EQ(ret = cat_fs_mkdir(r, 0777), 0);
    }

    report_error = 0;
}
#endif

/*
* flock behavior cannot be tested here without multi process support
* here we only test return value
* see mp_tests/flock_<platform>.c for detail
*/
TEST(cat_fs, flock_retval){
    SKIP_IF_(no_tmp(), "Temp dir not writable");
    // prepare fn
    char rand[sizeof("cat_tests_flock.") + 6] = "cat_tests_flock.";
    cat_srand(rand+sizeof("cat_tests_flock.")-1, 6);
    rand[sizeof(rand) - 1] = 0;
    std::string fnstr = path_join(TEST_TMP_PATH, rand);
    const char * fn = fnstr.c_str();
    // remove fn
    cat_fs_unlink(fn);
    DEFER({cat_fs_unlink(fn);});
    // create file
    int fd;
    ASSERT_GE(fd = cat_fs_open(fn, CAT_FS_O_RDWR | CAT_FS_O_CREAT), 0);

    ASSERT_EQ(cat_fs_flock(fd, CAT_LOCK_SH), 0);
    ASSERT_EQ(cat_fs_flock(fd, CAT_LOCK_EX), 0);
    ASSERT_EQ(cat_fs_flock(fd, CAT_LOCK_SH | CAT_LOCK_NB), 0);
    ASSERT_EQ(cat_fs_flock(fd, CAT_LOCK_EX | CAT_LOCK_NB), 0);
    ASSERT_EQ(cat_fs_flock(fd, CAT_LOCK_UN | CAT_LOCK_NB), 0);
    ASSERT_EQ(cat_fs_flock(fd, CAT_LOCK_EX | CAT_LOCK_NB), 0);
    ASSERT_EQ(cat_fs_flock(fd, CAT_LOCK_UN | CAT_LOCK_NB), 0);
}

TEST(cat_fs, cancel){
    SKIP_IF_(no_tmp(), "Temp dir not writable");

    std::string fnstr = path_join(TEST_TMP_PATH, "cat_tests_cancel");
    const char * fn = fnstr.c_str();
    cat_fs_unlink(fn);
    DEFER({cat_fs_unlink(fn);});
    std::string dnstr = path_join(TEST_TMP_PATH, "cat_tests_cancel_dir");
    const char * dn = dnstr.c_str();
    cat_fs_rmdir(dn);
    DEFER({cat_fs_rmdir(dn);});
    char buf[4096];
    int fd;
    FILE *stream = nullptr;
    cat_dir_t *dir;

    cat_coroutine_t *c;
#define CANCEL_TEST(expr) do{ \
    c = co([&]{ \
        expr; \
        ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED); \
    }); \
    ASSERT_NE(c, nullptr); \
    cat_coroutine_resume(c, nullptr, nullptr); \
}while(0)
    // fd
    CANCEL_TEST(ASSERT_LT(cat_fs_open(fn, CAT_FS_O_RDWR, 0666), 0));
    ASSERT_GE(fd = cat_fs_open(fn, CAT_FS_O_RDWR | CAT_FS_O_CREAT, 0666), 0);
    CANCEL_TEST(ASSERT_LT(cat_fs_close(fd), 0));
    ASSERT_GE(fd = cat_fs_open(fn, CAT_FS_O_RDONLY, 0666), 0);
    ASSERT_NE(stream = fdopen(fd, "r"), nullptr);
    CANCEL_TEST(ASSERT_LT(cat_fs_read(fd, buf, 4096), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_write(fd, buf, 4096), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_pread(fd, buf, 4096, 0), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_pwrite(fd, buf, 4096, 0), 0));
    CANCEL_TEST(ASSERT_EQ(cat_fs_fread(buf, 1, 4096, stream), 0));
    CANCEL_TEST(ASSERT_EQ(cat_fs_fwrite(buf, 1, 4096, stream), 0));
    CANCEL_TEST(ASSERT_NE(cat_fs_fseek(stream, 0, SEEK_SET), 0));
    CANCEL_TEST(ASSERT_NE(cat_fs_ftell(stream), 0));
    CANCEL_TEST(ASSERT_NE(cat_fs_fflush(stream), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_lseek(fd, 0, SEEK_SET), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_fsync(fd), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_fdatasync(fd), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_ftruncate(fd, 0), 0));
    // sendfile should be tested in socket
    // CANCEL_TEST(ASSERT_LT(cat_fs_sendfile(fd, 0, 0, 0), 0));
    CANCEL_TEST({
        cat_stat_t dummy;
        ASSERT_LT(cat_fs_fstat(fd, &dummy), 0);
    });
    CANCEL_TEST(ASSERT_LT(cat_fs_futime(fd, 0.0, 0.0), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_fchmod(fd, 0666), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_fchown(fd, -1, -1), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_flock(fd, CAT_LOCK_UN), 0));
    ASSERT_EQ(fclose(stream), 0);
    // dir
    ASSERT_EQ(cat_fs_mkdir(dn, 0777), 0);
    CANCEL_TEST(ASSERT_EQ(cat_fs_opendir(dn), nullptr));
    ASSERT_NE(dir = cat_fs_opendir(dn), nullptr);
    CANCEL_TEST(ASSERT_EQ(cat_fs_readdir(dir), nullptr));
    ASSERT_EQ(cat_fs_closedir(dir), 0);
    // windows version rewinddir is not blocking(it's not in thread pool)
#ifndef CAT_OS_WIN
    ASSERT_NE(dir = cat_fs_opendir(dn), nullptr);
    CANCEL_TEST(cat_fs_rewinddir(dir));
    ASSERT_EQ(cat_fs_closedir(dir), 0);
#endif // CAT_OS_WIN
    ASSERT_NE(dir = cat_fs_opendir(dn), nullptr);
    CANCEL_TEST(ASSERT_LT(cat_fs_closedir(dir), 0));
    // path
    CANCEL_TEST(ASSERT_LT(cat_fs_scandir(dn, nullptr, nullptr, nullptr), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_access(fn, F_OK), 0));
    CANCEL_TEST({
        cat_stat_t dummy;
        ASSERT_LT(cat_fs_stat(fn, &dummy), 0);
    });
    CANCEL_TEST({
        cat_stat_t dummy;
        ASSERT_LT(cat_fs_lstat(fn, &dummy), 0);
    });
    CANCEL_TEST(ASSERT_LT(cat_fs_utime(fn, 0.0, 0.0), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_lutime(fn, 0.0, 0.0), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_unlink(fn), 0));
    cat_fs_unlink(fn);
    CANCEL_TEST(ASSERT_LT(cat_fs_rename(fn, dn), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_link(fn, dn), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_symlink(fn, dn, 0), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_readlink(fn, buf, 4096), 0));
    CANCEL_TEST(ASSERT_EQ(cat_fs_realpath(fn, buf), nullptr));
    CANCEL_TEST(ASSERT_LT(cat_fs_chmod(fn, 0666), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_chown(fn, -1, -1), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_lchown(fn, -1, -1), 0));
    CANCEL_TEST(ASSERT_LT(cat_fs_copyfile(fn, dn, 0), 0));
#undef CANCEL_TEST

}
