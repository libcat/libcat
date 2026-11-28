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

/* cat_api.h does not aggregate the interrupt header yet */
#include "cat_interrupt.h"

#ifdef CAT_OS_WIN

/* A Windows process has no signal to forward, and uv_poll only accepts
 * sockets, so there is no descriptor to use as an interrupt source */
TEST(cat_interrupt, source_is_not_available)
{
    SKIP_IF_(true, "Interrupt sources are not available on Windows");
}

#else

#include <unistd.h>

namespace {

struct interrupt_probe_s {
    cat_interrupt_waiter_t waiter;
    int wake_count;
    cat_bool_t self_unregister;
    cat_bool_t registered;
};

void interrupt_probe_wake(cat_interrupt_waiter_t *waiter)
{
    interrupt_probe_s *probe = cat_container_of(waiter, interrupt_probe_s, waiter);

    probe->wake_count++;

    if (probe->self_unregister) {
        /* The dispatch walk must survive a waiter that leaves the queue from
         * its own wake callback */
        cat_interrupt_waiter_unregister(waiter);
        probe->registered = cat_false;
    }
}

void interrupt_probe_init(interrupt_probe_s *probe, cat_bool_t self_unregister = cat_false)
{
    probe->waiter.wake = interrupt_probe_wake;
    probe->wake_count = 0;
    probe->self_unregister = self_unregister;
    probe->registered = cat_false;
}

void interrupt_probe_register(interrupt_probe_s *probe)
{
    cat_interrupt_waiter_register(&probe->waiter);
    probe->registered = cat_true;
}

void interrupt_probe_unregister(interrupt_probe_s *probe)
{
    if (probe->registered) {
        cat_interrupt_waiter_unregister(&probe->waiter);
        probe->registered = cat_false;
    }
}

struct interrupt_pipe_s {
    int fds[2];

    interrupt_pipe_s()
    {
        fds[0] = -1;
        fds[1] = -1;
    }

    ~interrupt_pipe_s()
    {
        if (fds[0] >= 0) {
            close(fds[0]);
        }
        if (fds[1] >= 0) {
            close(fds[1]);
        }
    }

    bool open()
    {
        return pipe(fds) == 0;
    }

    bool write_byte()
    {
        return write(fds[1], "!", 1) == 1;
    }
};

} // namespace

TEST(cat_interrupt, an_invalid_descriptor_is_refused)
{
    ASSERT_EQ(cat_interrupt_source_set(CAT_OS_INVALID_SOCKET), cat_false);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

TEST(cat_interrupt, a_descriptor_that_cannot_be_polled_is_refused)
{
    std::string path = get_random_path();
    ASSERT_EQ(cat_fs_put_contents(path.c_str(), CAT_STRL("data")), CAT_STRLEN("data"));
    DEFER(cat_fs_unlink(path.c_str()));
    cat_file_t file = cat_fs_open(path.c_str(), CAT_FS_OPEN_FLAG_RDONLY);
    ASSERT_GE(file, 0);
    DEFER(cat_fs_close(file));

    /* A regular file has no readiness to poll, so the watcher cannot start */
    ASSERT_EQ(cat_interrupt_source_set((cat_os_socket_t) file), cat_false);
    ASSERT_NE(cat_get_last_error_code(), 0);
    /* The refused descriptor must not be left behind as the source */
    cat_interrupt_source_clear();
}

TEST(cat_interrupt, setting_the_same_descriptor_twice_keeps_one_watcher)
{
    interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));
    /* Setting the descriptor that is already armed changes nothing, so it must
     * neither replace the watcher nor fail */
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));

    interrupt_probe_s probe;
    interrupt_probe_init(&probe);
    interrupt_probe_register(&probe);
    DEFER(interrupt_probe_unregister(&probe));
    DEFER(cat_interrupt_source_clear());

    ASSERT_TRUE(pipe.write_byte());
    ASSERT_EQ(cat_time_delay(10), CAT_RET_OK);

    /* One byte, one callback, one wake: a second watcher would add a wake */
    ASSERT_EQ(probe.wake_count, 1);
}

TEST(cat_interrupt, clearing_without_a_source_changes_nothing)
{
    interrupt_probe_s probe;
    interrupt_probe_init(&probe);
    interrupt_probe_register(&probe);
    DEFER(interrupt_probe_unregister(&probe));
    DEFER(cat_interrupt_source_clear());

    /* There is no source, so no wait can have been ended: a wake here would
     * report an interrupt that never happened */
    cat_interrupt_source_clear();
    cat_interrupt_poll_close();

    ASSERT_EQ(probe.wake_count, 0);
    ASSERT_EQ(cat_interrupt_waiter_is_interrupted(&probe.waiter), cat_false);
}

TEST(cat_interrupt, a_cleared_source_stops_waking_waiters)
{
    interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));

    interrupt_probe_s probe;
    interrupt_probe_init(&probe);
    interrupt_probe_register(&probe);
    DEFER(interrupt_probe_unregister(&probe));
    DEFER(cat_interrupt_source_clear());

    /* The clear reports the interrupt once, because the source the waiter
     * depended on is gone */
    cat_interrupt_source_clear();
    ASSERT_EQ(probe.wake_count, 1);

    /* The handle survives the clear, so a second clear must not report another
     * interrupt that no source produced */
    cat_interrupt_source_clear();
    ASSERT_EQ(probe.wake_count, 1);
}

TEST(cat_interrupt, a_closed_writer_stops_the_source)
{
    interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));

    /* The read end reports end of file and stays readable, so the source has to
     * stop instead of waking the waiters in every loop round */
    ASSERT_EQ(close(pipe.fds[1]), 0);
    pipe.fds[1] = -1;
    ASSERT_EQ(cat_time_delay(10), CAT_RET_OK);

    interrupt_probe_s probe;
    interrupt_probe_init(&probe);
    interrupt_probe_register(&probe);
    DEFER(interrupt_probe_unregister(&probe));
    DEFER(cat_interrupt_source_clear());

    /* No waiter is left to wake, and the stopped source no longer fires */
    ASSERT_EQ(cat_time_delay(10), CAT_RET_OK);
    ASSERT_EQ(probe.wake_count, 0);

    /* A source can be armed again after the old one closed */
    interrupt_pipe_s next;
    ASSERT_TRUE(next.open());
    ASSERT_TRUE(cat_interrupt_source_set(next.fds[0]));
    DEFER(cat_interrupt_source_clear());
    ASSERT_TRUE(next.write_byte());
    ASSERT_EQ(cat_time_delay(10), CAT_RET_OK);
    ASSERT_EQ(probe.wake_count, 1);
}

TEST(cat_interrupt, closing_a_runtime_without_a_source_is_harmless)
{
    /* Shutdown closes the watcher unconditionally, so it has to be safe on a
     * runtime that never set a source */
    cat_interrupt_poll_close();
    cat_interrupt_poll_close();

    interrupt_probe_s probe;
    interrupt_probe_init(&probe);
    interrupt_probe_register(&probe);
    DEFER(interrupt_probe_unregister(&probe));
    DEFER(cat_interrupt_source_clear());

    /* Closing the watcher is not an interrupt: no source produced one */
    ASSERT_EQ(probe.wake_count, 0);
}

/* The walk must survive a callback that removes another waiter whose owner then
 * frees it: a wake callback resumes a coroutine, and that coroutine can do
 * anything, including a cancel that frees the other waiter */
TEST(cat_interrupt, a_wake_can_remove_another_waiter)
{
    struct row_s {
        cat_interrupt_waiter_t waiter;
        int wake_count;
        row_s *remove;
    };

    interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));

    row_s *first = new row_s();
    row_s *second = new row_s();
    DEFER(delete first);
    first->wake_count = 0;
    second->wake_count = 0;
    first->remove = second;
    first->waiter.wake = [](cat_interrupt_waiter_t *waiter) {
        row_s *row = cat_container_of(waiter, row_s, waiter);
        row->wake_count++;
        if (row->remove != nullptr) {
            cat_interrupt_waiter_unregister(&row->remove->waiter);
        }
    };
    second->waiter.wake = first->waiter.wake;

    cat_interrupt_waiter_register(&first->waiter);
    cat_interrupt_waiter_register(&second->waiter);
    ASSERT_TRUE(pipe.write_byte());
    ASSERT_EQ(cat_time_delay(10), CAT_RET_OK);

    /* the removed waiter is gone, so the walk stopped before it */
    ASSERT_EQ(first->wake_count, 1);
    ASSERT_EQ(second->wake_count, 0);
    delete second;
    cat_interrupt_source_clear();
}

/* A wake callback that installs a fresh source is the natural reaction after the
 * writer closed. The callback that retires the old source must not then stop a
 * handle that the re-arm has already closed */
TEST(cat_interrupt, a_wake_can_set_a_new_source)
{
    struct row_s {
        cat_interrupt_waiter_t waiter;
        int new_fd;
        cat_bool_t rearms;
        int wake_count;
    };

    interrupt_pipe_s closed_source;
    ASSERT_TRUE(closed_source.open());
    ASSERT_TRUE(cat_interrupt_source_set(closed_source.fds[0]));
    interrupt_pipe_s fresh;
    ASSERT_TRUE(fresh.open());

    row_s *row = new row_s();
    DEFER(delete row);
    row->new_fd = fresh.fds[0];
    row->rearms = cat_true;
    row->wake_count = 0;
    row->waiter.wake = [](cat_interrupt_waiter_t *waiter) {
        row_s *self = cat_container_of(waiter, row_s, waiter);
        self->wake_count++;
        if (self->rearms) {
            self->rearms = cat_false;
            ASSERT_TRUE(cat_interrupt_source_set(self->new_fd));
        }
    };
    cat_interrupt_waiter_register(&row->waiter);

    /* closing the writer makes the read end report end of file */
    ASSERT_EQ(close(closed_source.fds[1]), 0);
    closed_source.fds[1] = -1;
    ASSERT_EQ(cat_time_delay(10), CAT_RET_OK);
    ASSERT_EQ(row->wake_count, 1);

    /* the new source works; the dispatch removed the waiter, so register again
     * for the next wait */
    cat_interrupt_waiter_register(&row->waiter);
    ASSERT_TRUE(fresh.write_byte());
    ASSERT_EQ(cat_time_delay(10), CAT_RET_OK);
    ASSERT_EQ(row->wake_count, 2);
    cat_interrupt_waiter_unregister(&row->waiter);
    cat_interrupt_source_clear();
}

/* A replacement that cannot start leaves the working source in place */
TEST(cat_interrupt, a_refused_replacement_keeps_the_working_source)
{
    struct row_s {
        cat_interrupt_waiter_t waiter;
        int wake_count;
    };

    interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));

    row_s *row = new row_s();
    DEFER(delete row);
    row->wake_count = 0;
    row->waiter.wake = [](cat_interrupt_waiter_t *waiter) {
        row_s *self = cat_container_of(waiter, row_s, waiter);
        self->wake_count++;
    };
    cat_interrupt_waiter_register(&row->waiter);

    ASSERT_FALSE(cat_interrupt_source_set(0x7fff));
    ASSERT_TRUE(pipe.write_byte());
    ASSERT_EQ(cat_time_delay(10), CAT_RET_OK);
    ASSERT_EQ(row->wake_count, 1);
    cat_interrupt_waiter_unregister(&row->waiter);
    cat_interrupt_source_clear();
}

namespace {
struct reused_watch_s {
    uv_poll_t watcher;
    int fd;
    int hits;
};
static int hits_of(const reused_watch_s *watch)
{
    return watch->hits;
}
void reused_watch_cb(uv_poll_t *handle, int status, int events)
{
    reused_watch_s *watch = cat_container_of(handle, reused_watch_s, watcher);

    if (status == 0 && (events & UV_READABLE) != 0) {
        char byte;
        (void) read(watch->fd, &byte, 1);
        watch->hits++;
    }
}
}

/* The retired handle keeps the descriptor number it was initialised with, and
 * the close then asks the kernel to forget that number. This test asks whether
 * that close can disturb a handle the embedder made on the same number */
TEST(cat_interrupt, a_retired_source_does_not_disturb_a_reused_descriptor)
{
    interrupt_pipe_s first;
    ASSERT_TRUE(first.open());
    int reused_fd = first.fds[0];
    ASSERT_TRUE(cat_interrupt_source_set(reused_fd));

    reused_watch_s *watch = new reused_watch_s();
    watch->fd = -1;
    watch->hits = 0;

    /* retire the source, then hand the number to a fresh pipe */
    cat_interrupt_source_clear();
    ASSERT_EQ(close(first.fds[0]), 0);
    first.fds[0] = -1;
    interrupt_pipe_s second;
    ASSERT_TRUE(second.open());
    ASSERT_EQ(second.fds[0], reused_fd);

    /* the embedder watches the reused number before the retired handle closes */
    watch->fd = second.fds[0];
    ASSERT_EQ(uv_poll_init_socket(&CAT_EVENT_G(loop), &watch->watcher, second.fds[0]), 0);
    ASSERT_EQ(uv_poll_start(&watch->watcher, UV_READABLE, reused_watch_cb), 0);
    DEFER(uv_close((uv_handle_t *) &watch->watcher, [](uv_handle_t *handle) {
        delete cat_container_of(handle, reused_watch_s, watcher);
    }));

    /* let the new watcher reach the kernel first, so the retired close then has
     * something to remove */
    ASSERT_EQ(cat_time_delay(20), CAT_RET_OK);

    /* a new source closes the retired handle, whose number is now in use */
    interrupt_pipe_s third;
    ASSERT_TRUE(third.open());
    ASSERT_TRUE(cat_interrupt_source_set(third.fds[0]));
    DEFER(cat_interrupt_source_clear());
    ASSERT_EQ(cat_time_delay(20), CAT_RET_OK);

    ASSERT_TRUE(second.write_byte());
    ASSERT_EQ(cat_time_delay(50), CAT_RET_OK);
    ASSERT_EQ(hits_of(watch), 1);
}

/* A wait that owns no handle would let the loop return before the source fires.
 * The watcher keeps the loop alive while a waiter depends on it */
TEST(cat_interrupt, a_waiter_without_its_own_handle_keeps_the_loop_alive)
{
    struct row_s {
        cat_interrupt_waiter_t waiter;
        int wake_count;
    };

    interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));

    row_s *row = new row_s();
    DEFER(delete row);
    row->wake_count = 0;
    row->waiter.wake = [](cat_interrupt_waiter_t *waiter) {
        row_s *self = cat_container_of(waiter, row_s, waiter);
        self->wake_count++;
    };

    /* the waiter is the only work left, and it owns no timer or poll handle */
    cat_interrupt_waiter_register(&row->waiter);
    ASSERT_TRUE(pipe.write_byte());
    ASSERT_EQ(cat_time_delay(20), CAT_RET_OK);
    ASSERT_EQ(row->wake_count, 1);

    cat_interrupt_source_clear();
}

TEST(cat_interrupt, a_source_wakes_a_registered_waiter)
{
    interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));
    /* The clear has to run before the pipe is closed, because the watcher still
     * names the descriptor */

    interrupt_probe_s probe;
    interrupt_probe_init(&probe);
    interrupt_probe_register(&probe);
    DEFER(interrupt_probe_unregister(&probe));
    DEFER(cat_interrupt_source_clear());

    ASSERT_TRUE(pipe.write_byte());
    /* The loop polls the descriptor during this wait */
    ASSERT_EQ(cat_time_delay(10), CAT_RET_OK);

    ASSERT_EQ(probe.wake_count, 1);
    ASSERT_TRUE(cat_interrupt_waiter_is_interrupted(&probe.waiter));
}

TEST(cat_interrupt, a_source_wakes_every_registered_waiter)
{
    interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));
    DEFER(cat_interrupt_source_clear());

    interrupt_probe_s probes[3];
    for (int n = 0; n < 3; n++) {
        /* The first waiter leaves the queue from its own wake callback */
        interrupt_probe_init(&probes[n], n == 0 ? cat_true : cat_false);
        interrupt_probe_register(&probes[n]);
    }
    DEFER({
        for (auto &probe : probes) {
            interrupt_probe_unregister(&probe);
        }
    });

    ASSERT_TRUE(pipe.write_byte());
    ASSERT_EQ(cat_time_delay(10), CAT_RET_OK);

    for (auto &probe : probes) {
        ASSERT_EQ(probe.wake_count, 1);
        ASSERT_TRUE(cat_interrupt_waiter_is_interrupted(&probe.waiter));
    }
    ASSERT_FALSE(probes[0].registered);
}

TEST(cat_interrupt, an_unregistered_waiter_is_not_woken)
{
    interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));

    interrupt_probe_s probe;
    interrupt_probe_init(&probe);
    interrupt_probe_register(&probe);
    DEFER(interrupt_probe_unregister(&probe));
    DEFER(cat_interrupt_source_clear());
    interrupt_probe_unregister(&probe);

    ASSERT_TRUE(pipe.write_byte());
    ASSERT_EQ(cat_time_delay(10), CAT_RET_OK);

    ASSERT_EQ(probe.wake_count, 0);
    ASSERT_FALSE(cat_interrupt_waiter_is_interrupted(&probe.waiter));
}

TEST(cat_interrupt, a_waiter_without_a_source_is_not_woken)
{
    /* Nothing is watching, so the registration is accepted and never
     * interrupted */
    cat_interrupt_source_clear();

    interrupt_probe_s probe;
    interrupt_probe_init(&probe);
    interrupt_probe_register(&probe);
    DEFER(interrupt_probe_unregister(&probe));
    DEFER(cat_interrupt_source_clear());

    ASSERT_EQ(cat_time_delay(10), CAT_RET_OK);

    ASSERT_EQ(probe.wake_count, 0);
    ASSERT_FALSE(cat_interrupt_waiter_is_interrupted(&probe.waiter));
}

TEST(cat_interrupt, clearing_the_source_wakes_registered_waiters)
{
    interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));

    interrupt_probe_s probe;
    interrupt_probe_init(&probe);
    interrupt_probe_register(&probe);
    DEFER(interrupt_probe_unregister(&probe));
    DEFER(cat_interrupt_source_clear());

    cat_interrupt_source_clear();

    /* The clear wakes the waiter itself, without waiting for the loop */
    ASSERT_EQ(probe.wake_count, 1);
    ASSERT_TRUE(cat_interrupt_waiter_is_interrupted(&probe.waiter));

    /* The stopped source does not wake it again */
    ASSERT_EQ(cat_time_delay(10), CAT_RET_OK);
    ASSERT_EQ(probe.wake_count, 1);
}

TEST(cat_interrupt, closing_the_watcher_allows_a_new_source)
{
    interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));

    interrupt_probe_s probe;
    interrupt_probe_init(&probe);
    interrupt_probe_register(&probe);
    DEFER(interrupt_probe_unregister(&probe));
    DEFER(cat_interrupt_source_clear());

    /* The runtime shutdown closes the watcher the same way. Closing does not
     * interrupt: the waiter belongs to a runtime that is going away */
    cat_interrupt_poll_close();
    ASSERT_EQ(cat_time_delay(1), CAT_RET_OK);
    ASSERT_EQ(probe.wake_count, 0);
    ASSERT_FALSE(cat_interrupt_waiter_is_interrupted(&probe.waiter));

    /* A closed watcher can be replaced by a new source */
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));
    DEFER(cat_interrupt_source_clear());
    ASSERT_TRUE(pipe.write_byte());
    ASSERT_EQ(cat_time_delay(10), CAT_RET_OK);

    ASSERT_EQ(probe.wake_count, 1);
    ASSERT_TRUE(cat_interrupt_waiter_is_interrupted(&probe.waiter));
}

#ifdef CAT_THREAD_SAFE
/* A runtime of its own, so the shutdown runs with a source set and a waiter
 * still registered. The watcher is allocated, so ASAN reports a leak if the
 * close callback misses it, and a watcher that keeps the loop referenced makes
 * the shutdown hang. */
TEST(cat_interrupt, a_runtime_with_a_source_shuts_down)
{
    struct runtime_worker_s {
        uv_sem_t done;
        cat_bool_t runtime_ok;
        cat_bool_t run_ok;
        cat_bool_t source_ok;
        cat_bool_t waiter_ok;
        cat_bool_t stop_ok;
        cat_bool_t shutdown_ok;
        cat_bool_t close_ok;
    } worker = {};

    ASSERT_EQ(uv_sem_init(&worker.done, 0), 0);
    DEFER(uv_sem_destroy(&worker.done));

    uv_thread_t tid;
    ASSERT_EQ(uv_thread_create(&tid, [](void *data) {
        struct runtime_worker_s *self = (struct runtime_worker_s *) data;
        interrupt_probe_s probe;
        interrupt_pipe_s pipe;

        interrupt_probe_init(&probe);

        self->runtime_ok = cat_runtime_init_all();

        if (self->runtime_ok) {
            self->run_ok = cat_run(CAT_RUN_EASY);
        }

        if (self->run_ok && pipe.open()) {
            self->source_ok = cat_interrupt_source_set(pipe.fds[0]);
            if (self->source_ok) {
                interrupt_probe_register(&probe);
                (void) pipe.write_byte();
                (void) cat_time_delay(10);
                self->waiter_ok = probe.wake_count == 1 &&
                    cat_interrupt_waiter_is_interrupted(&probe.waiter);
                /* The waiter stays registered and the source stays set, so the
                 * shutdown below sees both */
            }
        }

        self->stop_ok = cat_stop();
        self->shutdown_ok = cat_runtime_shutdown_all();
        self->close_ok = cat_runtime_close_all();

        uv_sem_post(&self->done);
    }, &worker), 0);

    uv_sem_wait(&worker.done);
    ASSERT_EQ(uv_thread_join(&tid), 0);

    ASSERT_TRUE(worker.runtime_ok);
    ASSERT_TRUE(worker.run_ok);
    ASSERT_TRUE(worker.source_ok);
    ASSERT_TRUE(worker.waiter_ok);
    ASSERT_TRUE(worker.stop_ok);
    ASSERT_TRUE(worker.shutdown_ok);
    ASSERT_TRUE(worker.close_ok);
}
#endif /* CAT_THREAD_SAFE */

#endif /* CAT_OS_WIN */
