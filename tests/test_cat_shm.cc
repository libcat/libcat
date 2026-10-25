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
  | Author: Yun Dou <dixyes@gmail.com>                                       |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

#include "cat_futex.h"
#include "cat_shm.h"
#include "cat_time.h"

#ifndef CAT_OS_WIN
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <string>

static std::string shm_test_name(const char *suffix)
{
    /* libcat adds the leading slash, and macOS accepts at most 31 characters in
     * total, so a generated name stays within 30 characters */
    std::string name = "libcat-shm-test-" + std::to_string((long) uv_os_getpid()) + "-" + suffix;
#ifdef __APPLE__
    EXPECT_LE(name.size(), 30u) << name;
#endif
    return name;
}

#ifndef CAT_OS_WIN

/* macOS maps a shared memory object by page, so an attach reports the requested
 * size rounded up to the page size. Other platforms keep the requested size. */
static size_t shm_attached_size(size_t requested)
{
#ifdef __APPLE__
    long page = sysconf(_SC_PAGESIZE);
    size_t page_size = page > 0 ? (size_t) page : (size_t) 4096;

    return (requested + page_size - 1) / page_size * page_size;
#else
    return requested;
#endif
}

#endif

/* A mapping of the process alone holds what the caller writes to it. */
TEST(cat_shm, an_anonymous_mapping_holds_data)
{
    cat_shm_t *shm = cat_shm_create_anonymous(128);
    ASSERT_NE(shm, nullptr);
    DEFER(cat_shm_detach(shm));

    ASSERT_EQ(cat_shm_get_size(shm), 128);
    ASSERT_EQ(cat_shm_get_name(shm), nullptr);
    ASSERT_EQ((uintptr_t) cat_shm_get_address(shm) % 4096, 0);

    int *value = (int *) cat_shm_get_address(shm);
    *value = 42;
    ASSERT_EQ(*value, 42);
}

TEST(cat_shm, a_create_refuses_a_size_of_zero)
{
    ASSERT_EQ(cat_shm_create_anonymous(0), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

namespace {

struct shm_waiter_s {
    uv_thread_t thread;
    cat_futex_t *word;
    std::atomic<int> entered;
    std::atomic<int> woke;
};

void shm_waiter_run(void *data)
{
    struct shm_waiter_s *self = (struct shm_waiter_s *) data;

    self->entered = 1;
    self->woke = cat_futex_wait(self->word, 0, TEST_IO_TIMEOUT) ? 1 : 0;
}

} // namespace

/*
 * A word in the mapping is one word for both threads, so a wake that one sends
 * reaches the other. This joins the mapping with the futex.
 */
TEST(cat_shm, two_threads_share_an_anonymous_mapping)
{
    cat_shm_t *shm = cat_shm_create_anonymous(sizeof(cat_futex_t));
    ASSERT_NE(shm, nullptr);
    DEFER(cat_shm_detach(shm));

    cat_futex_t *word = (cat_futex_t *) cat_shm_get_address(shm);
    cat_atomic_int32_init(word, 0);

    struct shm_waiter_s waiter = {};
    waiter.word = word;
    ASSERT_EQ(uv_thread_create(&waiter.thread, shm_waiter_run, &waiter), 0);

    for (int round = 0; round < 5000 && waiter.entered.load() == 0; round++) {
        uv_sleep(1);
    }
    ASSERT_EQ(waiter.entered.load(), 1);
    uv_sleep(20);

    cat_atomic_int32_store(word, 1);
    ASSERT_TRUE(cat_futex_wake(word, 1));

    ASSERT_EQ(uv_thread_join(&waiter.thread), 0);
    ASSERT_EQ(waiter.woke.load(), 1);
    ASSERT_EQ(cat_atomic_int32_load(word), 1);
}

#ifndef CAT_OS_WIN

/* A forked child keeps the pages, because the heap is what a fork copies. */
TEST(cat_shm, a_forked_child_shares_an_anonymous_mapping)
{
    cat_shm_t *shm = cat_shm_create_anonymous(sizeof(int));
    ASSERT_NE(shm, nullptr);
    DEFER(cat_shm_detach(shm));

    int *value = (int *) cat_shm_get_address(shm);
    *value = 0;

    pid_t pid = fork();
    ASSERT_NE(pid, -1);
    if (pid == 0) {
        *value = 42;
        _exit(0);
    }

    int status = 0;
    ASSERT_EQ(waitpid(pid, &status, 0), pid);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(*value, 42);
}

#endif

/* A name leads a second mapping to the same page, in this process too. */
TEST(cat_shm, a_named_mapping_attaches_by_name)
{
    std::string name = shm_test_name("attach");
    cat_shm_t *created = cat_shm_create_named(name.c_str(), sizeof(int));
    ASSERT_NE(created, nullptr);
    DEFER(cat_shm_detach(created));
    DEFER(cat_shm_unlink(name.c_str()));

    ASSERT_EQ(cat_shm_get_size(created), sizeof(int));
    ASSERT_STREQ(cat_shm_get_name(created), name.c_str());

    int *created_value = (int *) cat_shm_get_address(created);
    *created_value = 7;

    cat_shm_t *attached = cat_shm_attach_named(name.c_str());
    ASSERT_NE(attached, nullptr);
    DEFER(cat_shm_detach(attached));

#ifdef CAT_OS_WIN
    /* VirtualQuery reports the view rounded up to a page */
    ASSERT_GE(cat_shm_get_size(attached), sizeof(int));
#else
    ASSERT_EQ(cat_shm_get_size(attached), shm_attached_size(sizeof(int)));
#endif
    ASSERT_STREQ(cat_shm_get_name(attached), name.c_str());
    ASSERT_NE(cat_shm_get_address(attached), cat_shm_get_address(created));

    int *attached_value = (int *) cat_shm_get_address(attached);
    ASSERT_EQ(*attached_value, 7);

    *attached_value = 9;
    ASSERT_EQ(*created_value, 9);
}

/* A name that still exists is an error, so a leftover name cannot be shared by
 * accident. */
TEST(cat_shm, a_create_refuses_a_name_that_exists)
{
    std::string name = shm_test_name("exists");
    cat_shm_t *first = cat_shm_create_named(name.c_str(), sizeof(int));
    ASSERT_NE(first, nullptr);
    DEFER(cat_shm_detach(first));
    DEFER(cat_shm_unlink(name.c_str()));

    ASSERT_EQ(cat_shm_create_named(name.c_str(), sizeof(int)), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EEXIST);
}

#ifndef CAT_OS_WIN

/*
 * A POSIX name lives on after the mapping that created it goes. On Windows the
 * section goes away with its last mapping, so there is nothing to attach after
 * the detach.
 */
TEST(cat_shm, a_name_outlives_the_mapping)
{
    std::string name = shm_test_name("lifetime");
    cat_shm_t *created = cat_shm_create_named(name.c_str(), sizeof(int));
    ASSERT_NE(created, nullptr);
    DEFER(cat_shm_unlink(name.c_str()));

    *(int *) cat_shm_get_address(created) = 5;
    cat_shm_detach(created);

    cat_shm_t *attached = cat_shm_attach_named(name.c_str());
    ASSERT_NE(attached, nullptr);
    DEFER(cat_shm_detach(attached));
    ASSERT_EQ(*(int *) cat_shm_get_address(attached), 5);

    /* the mapping stays valid after the name goes, and no attach finds it */
    ASSERT_TRUE(cat_shm_unlink(name.c_str()));
    ASSERT_EQ(*(int *) cat_shm_get_address(attached), 5);
    ASSERT_EQ(cat_shm_attach_named(name.c_str()), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOENT);
}

#endif

TEST(cat_shm, an_attach_refuses_a_missing_name)
{
    std::string name = shm_test_name("missing");

    ASSERT_EQ(cat_shm_attach_named(name.c_str()), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOENT);
}

/* An empty name has no object behind it, on every entry point, and neither
 * does a missing name. */
TEST(cat_shm, an_empty_name_is_refused)
{
    ASSERT_EQ(cat_shm_create_named("", sizeof(int)), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);

    ASSERT_EQ(cat_shm_attach_named(""), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);

    ASSERT_FALSE(cat_shm_unlink(""));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

/* A missing name is no name at all. */
TEST(cat_shm, a_null_name_is_refused)
{
    ASSERT_EQ(cat_shm_create_named(nullptr, sizeof(int)), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);

    ASSERT_EQ(cat_shm_attach_named(nullptr), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);

    ASSERT_FALSE(cat_shm_unlink(nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

/* A size that no mapping can hold fails at the platform, now that no header
 * arithmetic bounds it. The error is the one that the platform reports. */
TEST(cat_shm, a_size_that_no_mapping_can_hold_is_refused)
{
    ASSERT_EQ(cat_shm_create_anonymous(SIZE_MAX), nullptr);

    cat_errno_t code = cat_get_last_error_code();
    ASSERT_TRUE(code == CAT_ENOMEM || code == CAT_EINVAL);
}

/* The platform bounds the length of a name, and libcat reports the bound at the
 * call instead of a platform error. Windows limits the name to MAX_PATH (260)
 * characters, macOS to 31 characters including the leading slash that the
 * backend adds, and a POSIX file name component to NAME_MAX (255). */
TEST(cat_shm, a_name_beyond_the_platform_limit_is_refused)
{
#ifdef CAT_OS_WIN
    const size_t limit = 260;
#elif defined(__APPLE__)
    const size_t limit = 30;
#else
    const size_t limit = 255;
#endif
    std::string longest(limit, 'a');
    std::string too_long(limit + 1, 'a');

#ifndef CAT_OS_WIN
    /* a name at the limit still creates an object */
    cat_shm_t *created = cat_shm_create_named(longest.c_str(), sizeof(int));
    ASSERT_NE(created, nullptr);
    DEFER(cat_shm_detach(created));
    DEFER(cat_shm_unlink(longest.c_str()));
#endif

    ASSERT_EQ(cat_shm_create_named(too_long.c_str(), sizeof(int)), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);

    ASSERT_EQ(cat_shm_attach_named(too_long.c_str()), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);

    ASSERT_FALSE(cat_shm_unlink(too_long.c_str()));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

#ifndef CAT_OS_WIN

/* The POSIX form carries a leading slash, and the name keeps it. */
TEST(cat_shm, a_posix_name_may_keep_its_leading_slash)
{
    std::string name = "/" + shm_test_name("slash");
    cat_shm_t *created = cat_shm_create_named(name.c_str(), sizeof(int));
    ASSERT_NE(created, nullptr);
    DEFER(cat_shm_detach(created));
    DEFER(cat_shm_unlink(name.c_str()));

    ASSERT_STREQ(cat_shm_get_name(created), name.c_str());
    *(int *) cat_shm_get_address(created) = 3;

    cat_shm_t *attached = cat_shm_attach_named(name.c_str());
    ASSERT_NE(attached, nullptr);
    DEFER(cat_shm_detach(attached));
    ASSERT_EQ(*(int *) cat_shm_get_address(attached), 3);
}

/* A create reports the size that the caller passed, and the object starts at
 * the address with no libcat metadata in front of the caller data. */
TEST(cat_shm, a_posix_create_maps_exactly_the_requested_size)
{
    const size_t size = 128;
    std::string name = shm_test_name("exact");
    std::string path = "/" + name;

    cat_shm_t *created = cat_shm_create_named(name.c_str(), size);
    ASSERT_NE(created, nullptr);
    DEFER(cat_shm_detach(created));
    DEFER(cat_shm_unlink(name.c_str()));

    int fd = shm_open(path.c_str(), O_RDWR, 0);
    ASSERT_NE(fd, -1);
    DEFER(close(fd));
    struct stat information;
    ASSERT_EQ(fstat(fd, &information), 0);
    ASSERT_EQ((size_t) information.st_size, shm_attached_size(size));

    ASSERT_EQ(cat_shm_get_size(created), size);

    /* the address is the start of the object, so both ends belong to it */
    char *data = (char *) cat_shm_get_address(created);
    data[0] = 'a';
    data[size - 1] = 'z';

    cat_shm_t *attached = cat_shm_attach_named(name.c_str());
    ASSERT_NE(attached, nullptr);
    DEFER(cat_shm_detach(attached));
    ASSERT_EQ(cat_shm_get_size(attached), shm_attached_size(size));
    ASSERT_EQ(((char *) cat_shm_get_address(attached))[0], 'a');
    ASSERT_EQ(((char *) cat_shm_get_address(attached))[size - 1], 'z');
}

/* A shared object that libcat did not create has no metadata to check, so an
 * attach takes it with the size that the object reports. */
TEST(cat_shm, an_attach_takes_a_foreign_mapping)
{
    std::string name = shm_test_name("foreign");
    std::string path = "/" + name;
    int fd = shm_open(path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    ASSERT_NE(fd, -1);
    DEFER(shm_unlink(path.c_str()));

    ASSERT_EQ(ftruncate(fd, 8192), 0);
    close(fd);

    cat_shm_t *attached = cat_shm_attach_named(name.c_str());
    ASSERT_NE(attached, nullptr);
    DEFER(cat_shm_detach(attached));
    ASSERT_EQ(cat_shm_get_size(attached), shm_attached_size(8192));
}

/* The name is visible before the create finished its truncate, so an empty
 * object is an early attach and attach refuses it. */
TEST(cat_shm, an_attach_refuses_an_empty_object)
{
    std::string name = shm_test_name("empty");
    std::string path = "/" + name;
    int fd = shm_open(path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    ASSERT_NE(fd, -1);
    DEFER(shm_unlink(path.c_str()));
    close(fd);

    ASSERT_EQ(cat_shm_attach_named(name.c_str()), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

/* An object that no call created has no name to remove. */
TEST(cat_shm, an_unlink_refuses_a_missing_name)
{
    std::string name = shm_test_name("unlinked");

#ifdef CAT_OS_WIN
    /* a named section ends when the last handle closes, and no call removes the
     * name earlier, so an unlink only reports success */
    ASSERT_TRUE(cat_shm_unlink(name.c_str()));
#else
    ASSERT_FALSE(cat_shm_unlink(name.c_str()));
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOENT);
#endif
}

#ifdef CAT_OS_LINUX

/* shm_open rejects a name with a separator inside. macOS accepts a separator
 * there, so this case is Linux only. */
TEST(cat_shm, a_create_refuses_a_name_that_shm_open_rejects)
{
    ASSERT_EQ(cat_shm_create_named("libcat/inner-slash", sizeof(int)), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

#endif /* CAT_OS_LINUX */

#if SIZE_MAX > 0xFFFFFFFFUL

/* A size that ftruncate refuses fails the create, and the name that the create
 * took before the truncate does not stay. */
TEST(cat_shm, a_create_that_cannot_truncate_removes_its_name)
{
    std::string name = shm_test_name("truncate");

    ASSERT_EQ(cat_shm_create_named(name.c_str(), SIZE_MAX), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_EQ(cat_shm_attach_named(name.c_str()), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOENT);
}

/* A size that the kernel accepts for the object but not for the mapping fails
 * the create, and the name that no mapping holds does not stay. */
TEST(cat_shm, a_create_that_cannot_map_removes_its_name)
{
    /* the suffix stays short, because macOS rejects a name over 30 characters */
    std::string name = shm_test_name("nomap");

    ASSERT_EQ(cat_shm_create_named(name.c_str(), (size_t) 1 << 50), nullptr);
    ASSERT_EQ(cat_shm_attach_named(name.c_str()), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOENT);
}

/* An object that exists but cannot be mapped fails the attach with the reason
 * of the map, and its name stays for the next attach. */
TEST(cat_shm, an_attach_reports_an_object_that_does_not_fit)
{
    std::string name = shm_test_name("huge");
    std::string path = "/" + name;
    int fd = shm_open(path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    ASSERT_NE(fd, -1);
    DEFER(shm_unlink(path.c_str()));
    ASSERT_EQ(ftruncate(fd, (off_t) 1 << 50), 0);
    close(fd);

    ASSERT_EQ(cat_shm_attach_named(name.c_str()), nullptr);
#ifdef CAT_OS_LINUX
    ASSERT_EQ(cat_get_last_error_code(), CAT_ENOMEM);
#endif
}

#endif /* SIZE_MAX > 0xFFFFFFFFUL */

#endif
