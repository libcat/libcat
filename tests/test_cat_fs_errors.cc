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

#ifdef CAT_OS_LINUX
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

static bool has_blocked_io(long syscall, int fd)
{
    DIR *directory = opendir("/proc/self/task");
    if (directory == nullptr) {
        return false;
    }
    DEFER(closedir(directory));
    while (struct dirent *entry = readdir(directory)) {
        const std::string path = std::string("/proc/self/task/") + entry->d_name + "/syscall";
        int state = open(path.c_str(), O_RDONLY);
        if (state < 0) {
            continue;
        }
        char buffer[256];
        ssize_t length = read(state, buffer, sizeof(buffer) - 1);
        close(state);
        if (length <= 0) {
            continue;
        }
        buffer[length] = '\0';
        long operation;
        unsigned long long argument;
        if (sscanf(buffer, "%ld %llx", &operation, &argument) == 2 &&
            operation == syscall && argument == static_cast<unsigned long long>(fd)) {
            return true;
        }
    }
    return false;
}

static void test_canceled_read_buffer(bool stdio)
{
    int descriptors[2];
    ASSERT_EQ(pipe(descriptors), 0);
    FILE *stream = nullptr;
    DEFER(if (stream != nullptr) { fclose(stream); } else { close(descriptors[0]); });
    DEFER(if (descriptors[1] >= 0) { close(descriptors[1]); });
    if (stdio) {
        stream = fdopen(descriptors[0], "r");
        ASSERT_NE(stream, nullptr);
        ASSERT_EQ(setvbuf(stream, nullptr, _IONBF, 0), 0);
    }
    char buffer = 'a';
    wait_group wg;
    bool waiting = true;
    ssize_t received = 0;
    cat_errno_t error = 0;
    cat_coroutine_t *reader = co([&] {
        wg++;
        DEFER(wg--);
        received = stdio ? static_cast<ssize_t>(cat_fs_fread(&buffer, 1, sizeof(buffer), stream)) :
            cat_fs_read(descriptors[0], &buffer, sizeof(buffer));
        error = cat_get_last_error_code();
        waiting = false;
    });
    DEFER(if (waiting) { cat_coroutine_resume(reader, nullptr, nullptr); }
        close(descriptors[1]); descriptors[1] = -1; wg(TEST_IO_TIMEOUT); cat_coroutine_wait_all());
    // Observe the actual worker syscall instead of assuming that a delay starts the read.
    cat_msec_t deadline = cat_time_msec2() + TEST_IO_TIMEOUT;
    auto blocked = [&] {
        return has_blocked_io(SYS_read, descriptors[0]) || (stdio && has_blocked_io(SYS_readv, descriptors[0]));
    };
    while (!blocked() && cat_time_msec2() < deadline) {
        cat_sys_usleep(1);
    }
    ASSERT_TRUE(blocked());
    ASSERT_TRUE(cat_coroutine_resume(reader, nullptr, nullptr));
    ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
    ASSERT_EQ(received, stdio ? 0 : -1);
    ASSERT_EQ(error, CAT_ECANCELED);
    buffer = 'b';
    ASSERT_EQ(write(descriptors[1], "x", 1), 1);
    ASSERT_TRUE(cat_coroutine_wait_all());
    ASSERT_EQ(buffer, 'b');
    if (stdio) {
        ASSERT_EQ(cat_fs_fread(&buffer, SIZE_MAX, 1, stream), 0);
    } else {
        ASSERT_EQ(cat_fs_read(descriptors[0], &buffer, SIZE_MAX), -1);
    }
    ASSERT_EQ(cat_get_last_error_code(), CAT_EOVERFLOW);
    ASSERT_EQ(errno, EOVERFLOW);
}

TEST(cat_fs, canceled_read_does_not_modify_caller_buffer)
{
    ASSERT_NO_FATAL_FAILURE(test_canceled_read_buffer(false));
}

TEST(cat_fs, canceled_fread_does_not_modify_caller_buffer)
{
    ASSERT_NO_FATAL_FAILURE(test_canceled_read_buffer(true));
}

static void test_canceled_write_buffer(bool stdio)
{
    int descriptors[2];
    ASSERT_EQ(pipe(descriptors), 0);
    DEFER(close(descriptors[0]));
    FILE *stream = nullptr;
    DEFER(if (stream != nullptr) { fclose(stream); } else { close(descriptors[1]); });
    if (stdio) {
        stream = fdopen(descriptors[1], "w");
        ASSERT_NE(stream, nullptr);
        ASSERT_EQ(setvbuf(stream, nullptr, _IONBF, 0), 0);
    }
    long atomic_size = fpathconf(descriptors[1], _PC_PIPE_BUF);
    ASSERT_GT(atomic_size, 0);
    std::string payload(static_cast<size_t>(atomic_size), 'x');
    std::string buffer(payload.size(), '\0');
    size_t filled = 0;
    wait_group wg;
    bool waiting = true;
    ssize_t written = 0;
    cat_errno_t error = 0;
    cat_coroutine_t *writer = nullptr;
    DEFER(if (writer != nullptr && waiting) { cat_coroutine_resume(writer, nullptr, nullptr); }
        while (filled > 0) {
            ssize_t n = read(descriptors[0], buffer.data(), CAT_MIN(buffer.size(), filled));
            if (n <= 0) { break; }
            filled -= static_cast<size_t>(n);
        } wg(TEST_IO_TIMEOUT); cat_coroutine_wait_all());
    int flags = fcntl(descriptors[1], F_GETFL);
    ASSERT_GE(flags, 0);
    ASSERT_EQ(fcntl(descriptors[1], F_SETFL, flags | O_NONBLOCK), 0);
    ssize_t n;
    while ((n = write(descriptors[1], payload.data(), payload.size())) > 0) {
        filled += static_cast<size_t>(n);
    }
    ASSERT_EQ(errno, EAGAIN);
    ASSERT_GT(filled, 0);
    ASSERT_EQ(fcntl(descriptors[1], F_SETFL, flags), 0);
    writer = co([&] {
        wg++;
        DEFER(wg--);
        written = stdio ? static_cast<ssize_t>(cat_fs_fwrite(payload.data(), 1, payload.size(), stream)) :
            cat_fs_write(descriptors[1], payload.data(), payload.size());
        error = cat_get_last_error_code();
        waiting = false;
    });
    cat_msec_t deadline = cat_time_msec2() + TEST_IO_TIMEOUT;
    auto blocked = [&] {
        return has_blocked_io(SYS_write, descriptors[1]) || (stdio && has_blocked_io(SYS_writev, descriptors[1]));
    };
    while (!blocked() && cat_time_msec2() < deadline) {
        cat_sys_usleep(1);
    }
    ASSERT_TRUE(blocked());
    ASSERT_TRUE(cat_coroutine_resume(writer, nullptr, nullptr));
    ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
    ASSERT_EQ(written, stdio ? 0 : -1);
    ASSERT_EQ(error, CAT_ECANCELED);
    payload.assign(payload.size(), 'y');
    while (filled > 0) {
        n = read(descriptors[0], buffer.data(), CAT_MIN(buffer.size(), filled));
        ASSERT_GT(n, 0);
        filled -= static_cast<size_t>(n);
    }
    ASSERT_EQ(read(descriptors[0], buffer.data(), buffer.size()), static_cast<ssize_t>(buffer.size()));
    ASSERT_TRUE(cat_coroutine_wait_all());
    ASSERT_EQ(buffer, std::string(buffer.size(), 'x'));
    if (stdio) {
        ASSERT_EQ(cat_fs_fwrite(payload.data(), SIZE_MAX, 1, stream), 0);
    } else {
        ASSERT_EQ(cat_fs_write(descriptors[1], payload.data(), SIZE_MAX), -1);
    }
    ASSERT_EQ(cat_get_last_error_code(), CAT_EOVERFLOW);
    ASSERT_EQ(errno, EOVERFLOW);
}

TEST(cat_fs, canceled_write_preserves_submitted_bytes)
{
    ASSERT_NO_FATAL_FAILURE(test_canceled_write_buffer(false));
}

TEST(cat_fs, canceled_fwrite_preserves_submitted_bytes)
{
    ASSERT_NO_FATAL_FAILURE(test_canceled_write_buffer(true));
}

static size_t count_open_descriptors()
{
    DIR *directory = opendir("/proc/self/fd");
    EXPECT_NE(directory, nullptr);
    if (directory == nullptr) {
        return SIZE_MAX;
    }
    DEFER(closedir(directory));
    size_t count = 0;
    while (struct dirent *entry = readdir(directory)) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            count++;
        }
    }
    return count;
}

TEST(cat_fs, content_helpers_close_owned_descriptors)
{
    const std::string path = get_random_path();
    DEFER(cat_fs_unlink(path.c_str()));
    cat_file_t file = cat_fs_open(path.c_str(), CAT_FS_OPEN_FLAG_CREAT | CAT_FS_OPEN_FLAG_WRONLY, 0600);
    ASSERT_GE(file, 0);
    ASSERT_EQ(cat_fs_close(file), 0);
    size_t before = count_open_descriptors();
    for (unsigned int repeat = 0; repeat < 16; repeat++) {
        ASSERT_EQ(cat_fs_put_contents(path.c_str(), CAT_STRL("file helper data")), CAT_STRLEN("file helper data"));
        size_t length = 0;
        char *content = cat_fs_get_contents(path.c_str(), &length);
        ASSERT_NE(content, nullptr);
        DEFER(cat_free(content));
        ASSERT_EQ(std::string(content, length), "file helper data");
    }
    ASSERT_EQ(count_open_descriptors(), before);

    const std::string fifo = get_random_path();
    ASSERT_EQ(mkfifo(fifo.c_str(), S_IRUSR | S_IWUSR), 0);
    DEFER(unlink(fifo.c_str()));
    int peer = open(fifo.c_str(), O_RDWR);
    ASSERT_GE(peer, 0);
    DEFER(close(peer));
    before = count_open_descriptors();
    size_t length = 1;
    ASSERT_EQ(cat_fs_get_contents(fifo.c_str(), &length), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ESPIPE);
    ASSERT_EQ(length, 0);
    ASSERT_EQ(count_open_descriptors(), before);
}

TEST(cat_fs, canceled_open_closes_unclaimed_descriptor)
{
    const std::string path = get_random_path();
    DEFER(cat_fs_unlink(path.c_str()));
    cat_file_t file = cat_fs_open(path.c_str(), CAT_FS_OPEN_FLAG_CREAT | CAT_FS_OPEN_FLAG_WRONLY, 0600);
    ASSERT_GE(file, 0);
    ASSERT_EQ(cat_fs_close(file), 0);
    size_t before = count_open_descriptors();
    wait_group wg;
    bool waiting = true;
    cat_file_t opened = CAT_OS_INVALID_FD;
    cat_errno_t error = 0;
    cat_coroutine_t *coroutine = co([&] {
        wg++;
        DEFER(wg--);
        opened = cat_fs_open(path.c_str(), CAT_FS_OPEN_FLAG_RDONLY);
        error = cat_get_last_error_code();
        waiting = false;
    });
    ASSERT_NE(coroutine, nullptr);
    DEFER(if (waiting) { cat_coroutine_resume(coroutine, nullptr, nullptr); }
        wg(TEST_IO_TIMEOUT); cat_coroutine_wait_all());
    // Observe the worker's real open before allowing its completion callback to run.
    cat_msec_t deadline = cat_time_msec2() + TEST_IO_TIMEOUT;
    while (count_open_descriptors() == before && cat_time_msec2() < deadline) {
        cat_sys_usleep(1);
    }
    ASSERT_EQ(count_open_descriptors(), before + 1);
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
    ASSERT_TRUE(wg(TEST_IO_TIMEOUT));
    ASSERT_EQ(opened, CAT_OS_INVALID_FD);
    ASSERT_EQ(error, CAT_ECANCELED);
    ASSERT_TRUE(cat_coroutine_wait_all());
    ASSERT_EQ(count_open_descriptors(), before);
}

#if GTEST_HAS_DEATH_TEST
TEST(cat_fs, shutdown_with_blocked_flock_uses_existing_fail_stop)
{
    ASSERT_GT(TEST_IO_TIMEOUT, 0);
    const std::string path = get_random_path() + "-" + std::to_string(getpid());
    cat_file_t first = cat_fs_open(path.c_str(), CAT_FS_OPEN_FLAG_CREAT | CAT_FS_OPEN_FLAG_RDWR, 0600);
    ASSERT_GE(first, 0);
    DEFER(cat_fs_close(first));
    DEFER(if (file_exists(path.c_str())) { cat_fs_unlink(path.c_str()); });
    cat_file_t second = cat_fs_open(path.c_str(), CAT_FS_OPEN_FLAG_RDWR);
    ASSERT_GE(second, 0);
    DEFER(cat_fs_close(second));
    ASSERT_EQ(cat_fs_unlink(path.c_str()), 0);
    ASSERT_EXIT({
        alarm(static_cast<unsigned int>((TEST_IO_TIMEOUT + 999) / 1000));
        if (cat_fs_flock(first, CAT_FS_FLOCK_FLAG_EXCLUSIVE) != 0) {
            std::exit(1);
        }
        int locked = 0;
        cat_errno_t error = 0;
        cat_coroutine_t *waiter = co([&] {
            locked = cat_fs_flock(second, CAT_FS_FLOCK_FLAG_EXCLUSIVE);
            error = cat_get_last_error_code();
        });
        const cat_nsec_t start = cat_time_nsec();
        const cat_nsec_t timeout = static_cast<cat_nsec_t>(TEST_IO_TIMEOUT) * 1000 * 1000;
        while (!has_blocked_io(SYS_flock, second)) {
            if (cat_time_nsec() - start >= timeout || cat_time_delay(1) != CAT_RET_OK) {
                std::exit(2);
            }
        }
        if (!cat_coroutine_resume(waiter, nullptr, nullptr) ||
            locked != -1 || error != CAT_ECANCELED) {
            std::exit(3);
        }
        // Unix's existing blocked-thread shutdown ends this death-test child.
        cat_runtime_shutdown_all();
        std::exit(4);
    }, testing::KilledBySignal(SIGKILL), "");
}

TEST(cat_fs, canceled_queued_flock_allows_shutdown)
{
    ASSERT_GT(TEST_IO_TIMEOUT, 0);
    const std::string path = get_random_path() + "-" + std::to_string(getpid());
    cat_file_t file = cat_fs_open(path.c_str(), CAT_FS_OPEN_FLAG_CREAT | CAT_FS_OPEN_FLAG_RDWR, 0600);
    ASSERT_GE(file, 0);
    DEFER(cat_fs_close(file));
    ASSERT_EQ(cat_fs_unlink(path.c_str()), 0);
    char *original_pool_size = cat_env_get_silent("CAT_TPS", nullptr);
    DEFER({
        if (original_pool_size == nullptr) {
            EXPECT_EQ(uv_os_unsetenv("CAT_TPS"), 0);
        } else {
            EXPECT_TRUE(cat_env_set("CAT_TPS", original_pool_size));
        }
        cat_free(original_pool_size);
    });
    // Libcat's pool-size setting takes precedence over UV_THREADPOOL_SIZE.
    ASSERT_TRUE(cat_env_set("CAT_TPS", "1"));
    for (bool reject_new_thread : {false, true}) {
        if (reject_new_thread && getuid() == 0) {
            continue;
        }
        SCOPED_TRACE(reject_new_thread);
        ASSERT_EXIT({
            alarm(static_cast<unsigned int>((TEST_IO_TIMEOUT + 999) / 1000));
            uv_sem_t ready;
            uv_sem_t release;
            if (uv_sem_init(&ready, 0) != 0 || uv_sem_init(&release, 0) != 0) {
                std::exit(1);
            }
            bool worker_done = false;
            co([&] {
                worker_done = work(CAT_WORK_KIND_FAST_IO, [&] {
                    uv_sem_post(&ready);
                    uv_sem_wait(&release);
                }, TEST_IO_TIMEOUT);
            });
            uv_sem_wait(&ready);
            struct rlimit original_limit;
            if (reject_new_thread) {
                if (getrlimit(RLIMIT_NPROC, &original_limit) != 0) {
                    std::exit(4);
                }
                struct rlimit restricted = original_limit;
                restricted.rlim_cur = 0;
                if (setrlimit(RLIMIT_NPROC, &restricted) != 0) {
                    std::exit(5);
                }
            }
            int locked = 0;
            cat_errno_t error = 0;
            cat_coroutine_t *waiter = co([&] {
                locked = cat_fs_flock(file, CAT_FS_FLOCK_FLAG_EXCLUSIVE);
                error = cat_get_last_error_code();
            });
            bool resumed = cat_coroutine_resume(waiter, nullptr, nullptr);
            uv_sem_post(&release);
            bool joined = cat_coroutine_wait_all();
            uv_sem_destroy(&release);
            uv_sem_destroy(&ready);
            if (!resumed || !joined || !worker_done || locked != -1 || error != CAT_ECANCELED) {
                std::exit(2);
            }
            if ((!reject_new_thread && cat_fs_close(file) != 0) || !cat_stop() || !cat_shutdown_all()) {
                std::exit(3);
            }
            if (reject_new_thread && setrlimit(RLIMIT_NPROC, &original_limit) != 0) {
                std::exit(6);
            }
            alarm(0);
            std::exit(0);
        }, testing::ExitedWithCode(0), "");
    }
}

TEST(cat_fs, flock_thread_limit_failure_and_recovery)
{
    SKIP_IF_(getuid() == 0, "RLIMIT_NPROC does not restrict root");
    const std::string path = get_random_path();
    cat_file_t file = cat_fs_open(path.c_str(), CAT_FS_OPEN_FLAG_CREAT | CAT_FS_OPEN_FLAG_RDWR, 0600);
    ASSERT_GE(file, 0);
    DEFER(cat_fs_close(file); cat_fs_unlink(path.c_str()));
    ASSERT_TRUE(work(CAT_WORK_KIND_FAST_IO, [] {}, TEST_IO_TIMEOUT));
    ASSERT_EXIT({
        struct rlimit original;
        if (getrlimit(RLIMIT_NPROC, &original) != 0) {
            std::exit(1);
        }
        struct rlimit restricted = original;
        restricted.rlim_cur = 0;
        if (setrlimit(RLIMIT_NPROC, &restricted) != 0) {
            std::exit(2);
        }
        int locked = cat_fs_flock(file, CAT_FS_FLOCK_FLAG_EXCLUSIVE);
        cat_errno_t error = cat_get_last_error_code();
        if (setrlimit(RLIMIT_NPROC, &original) != 0) {
            std::exit(3);
        }
        if (locked != -1 || error != CAT_EAGAIN || !cat_coroutine_wait_all()) {
            std::exit(4);
        }
        if (cat_fs_flock(file, CAT_FS_FLOCK_FLAG_EXCLUSIVE) != 0 ||
            cat_fs_flock(file, CAT_FS_FLOCK_FLAG_UNLOCK) != 0) {
            std::exit(5);
        }
        std::exit(0);
    }, testing::ExitedWithCode(0), "");
}
#endif
#endif

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

    ASSERT_EQ(cat_fs_mkdir(missing.c_str(), 0700), 0);
    DEFER(cat_fs_rmdir(missing.c_str()));
#ifndef CAT_OS_WIN
    // Opening a directory succeeds on Unix, but reading it as file content fails.
    length = 17;
    ASSERT_EQ(cat_fs_get_contents(missing.c_str(), &length), nullptr);
    ASSERT_EQ(length, 0);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EISDIR);
    ASSERT_EQ(cat_fs_put_contents(missing.c_str(), CAT_STRL("data")), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EISDIR);
#endif
    const std::string data("a\0b", 3);
    ASSERT_EQ(cat_fs_put_contents(child.c_str(), data.data(), data.size()), static_cast<ssize_t>(data.size()));
    DEFER(cat_fs_unlink(child.c_str()));
    char *content = cat_fs_get_contents(child.c_str(), &length);
    ASSERT_NE(content, nullptr);
    DEFER(cat_free(content));
    ASSERT_EQ(length, data.size());
    ASSERT_EQ(std::string(content, length), data);
    ASSERT_EQ(content[length], '\0');
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

TEST(cat_fs, copyfile_rejects_unsupported_flags)
{
    const std::string path = get_random_path();
    const std::string destination = get_random_path();
    ASSERT_EQ(cat_fs_copyfile(path.c_str(), destination.c_str(), -1), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_EQ(errno, EINVAL);
    ASSERT_FALSE(file_exists(path.c_str()));
    ASSERT_FALSE(file_exists(destination.c_str()));
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

TEST(cat_fs, stdio_partial_items_preserve_received_bytes)
{
    const std::string path = get_random_path();
    ASSERT_EQ(cat_fs_put_contents(path.c_str(), CAT_STRL("abc")), 3);
    DEFER(cat_fs_unlink(path.c_str()));
    FILE *stream = fopen(path.c_str(), "r");
    ASSERT_NE(stream, nullptr);
    DEFER(cat_fs_fclose(stream));
    for (size_t item_size : {size_t(2), size_t(4)}) {
        SCOPED_TRACE(item_size);
        ASSERT_EQ(cat_fs_fseek(stream, 0, SEEK_SET), 0);
        char buffer[4] = {'x', 'x', 'x', 'x'};
        cat_clear_last_error();
        ASSERT_EQ(cat_fs_fread(buffer, item_size, sizeof(buffer) / item_size, stream), 3 / item_size);
        ASSERT_EQ(std::string(buffer, sizeof(buffer)), "abcx");
        ASSERT_NE(feof(stream), 0);
        ASSERT_EQ(ferror(stream), 0);
        ASSERT_EQ(cat_get_last_error_code(), 0);
    }
    cat_clear_last_error();
    ASSERT_EQ(cat_fs_fwrite("", 0, 1, stream), 0);
    ASSERT_EQ(cat_fs_fwrite("", 1, 0, stream), 0);
    ASSERT_EQ(cat_get_last_error_code(), 0);
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
    // The FILE object remains valid after its fd closes; wrappers must report EBADF.
    ASSERT_EQ(cat_fs_fwrite("data", 1, 4, stream), 4);
    ASSERT_EQ(cat_fs_fflush(stream), EOF);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    ASSERT_EQ(cat_fs_ftell(stream), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
    clearerr(stream);
    char buffer[8];
    // Some C libraries report only ferror(), which Libcat translates to EIO.
    errno = 0;
    ASSERT_EQ(fread(buffer, 1, sizeof(buffer), stream), 0);
    const int read_error = errno != 0 ? errno : EIO;
    ASSERT_NE(ferror(stream), 0);
    clearerr(stream);
    ASSERT_EQ(cat_fs_fread(buffer, 1, sizeof(buffer), stream), 0);
    ASSERT_NE(ferror(stream), 0);
    ASSERT_EQ(cat_orig_errno(cat_get_last_error_code()), read_error);
    int result = cat_fs_fclose(stream);
    open = false;
    ASSERT_EQ(result, EOF);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EBADF);
}
