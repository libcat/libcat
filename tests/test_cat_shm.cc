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
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <string>

static std::string shm_test_name(const char *suffix)
{
    return "libcat-shm-test-" + std::to_string((long) uv_os_getpid()) + "-" + suffix;
}

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

    ASSERT_EQ(cat_shm_get_size(attached), sizeof(int));
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

/* An empty name has no object behind it, on every entry point. */
TEST(cat_shm, an_empty_name_is_refused)
{
    ASSERT_EQ(cat_shm_create_named("", sizeof(int)), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);

    ASSERT_EQ(cat_shm_attach_named(""), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);

    ASSERT_FALSE(cat_shm_unlink(""));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

/* A size that leaves no room for the header is refused before any allocation. */
TEST(cat_shm, a_size_that_does_not_fit_is_refused)
{
    ASSERT_EQ(cat_shm_create_anonymous(SIZE_MAX), nullptr);
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

/* A shared object that libcat did not create has no header, so attach refuses
 * it instead of handing out an address with the wrong size. */
TEST(cat_shm, an_attach_refuses_a_foreign_mapping)
{
    std::string name = shm_test_name("foreign");
    std::string path = "/" + name;
    int fd = shm_open(path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    ASSERT_NE(fd, -1);
    DEFER(shm_unlink(path.c_str()));

    ASSERT_EQ(ftruncate(fd, 8192), 0);
    close(fd);

    ASSERT_EQ(cat_shm_attach_named(name.c_str()), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

/* An object that cannot hold the header is refused at the size, not at the
 * magic. */
TEST(cat_shm, an_attach_refuses_an_object_without_room_for_a_header)
{
    std::string name = shm_test_name("tiny");
    std::string path = "/" + name;
    int fd = shm_open(path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    ASSERT_NE(fd, -1);
    DEFER(shm_unlink(path.c_str()));

    ASSERT_EQ(ftruncate(fd, 128), 0);
    close(fd);

    ASSERT_EQ(cat_shm_attach_named(name.c_str()), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

#endif
