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

/* cat_api.h does not aggregate the interrupt header yet */
#include "cat_interrupt.h"

TEST(cat_time, nsec)
{
    cat_nsec_t s = cat_time_nsec();
    ASSERT_EQ(cat_sys_usleep(100), 0);
    ASSERT_GT(cat_time_nsec() - s, 10);
}

TEST(cat_time, msec)
{
    cat_msec_t s = cat_time_msec();
    const cat_nsec_t start = cat_time_nsec();
    const cat_nsec_t timeout = static_cast<cat_nsec_t>(TEST_IO_TIMEOUT) * 1000 * 1000;
    // The fast clock may not tick during one native wait; bound observed progress with the precise monotonic clock.
    do {
        ASSERT_EQ(cat_sys_usleep(3000), 0);
    } while (cat_time_msec() - s <= 1 && cat_time_nsec() - start < timeout);
    ASSERT_GT(cat_time_msec() - s, 1);
}

TEST(cat_time, nsec2)
{
    cat_nsec_t s = cat_time_nsec2();
    ASSERT_EQ(cat_sys_usleep(100), 0);
    ASSERT_GT(cat_time_nsec2() - s, 10);
}

TEST(cat_time, msec2)
{
    cat_nsec_t s = cat_time_msec2();
    ASSERT_EQ(cat_sys_usleep(3000), 0);
    ASSERT_GT(cat_time_msec2() - s, 1);
}

TEST(cat_time, microtime)
{
    double s = cat_microtime();
    ASSERT_EQ(cat_sys_usleep(3000), 0);
    ASSERT_GT(cat_microtime() - s, 0.001);
}

TEST(cat_time, msec_cached)
{
    ASSERT_TRUE(cat_coroutine_wait_all()); // call it before timing test
    cat_msec_t s = cat_time_msec_cached();
    ASSERT_EQ(cat_time_msec_cached() - s, 0);
    ASSERT_EQ(cat_time_msleep(3), 0);
    ASSERT_GT(cat_time_msec_cached() - s, 0);
}

TEST(cat_time, msleep)
{
    ASSERT_TRUE(cat_coroutine_wait_all()); // call it before timing test
    cat_msec_t s = cat_time_msec();
    ASSERT_EQ(cat_time_msleep(10), 0);
    s = cat_time_msec() - s;
    ASSERT_GE(s, 5);
}

TEST(cat_time, usleep)
{
    ASSERT_TRUE(cat_coroutine_wait_all()); // call it before timing test
    cat_msec_t s = cat_time_msec();
    ASSERT_EQ(cat_time_usleep(10 * 1000), 0);
    s = cat_time_msec() - s;
    ASSERT_GE(s, 5);
}

TEST(cat_time, nanosleep)
{
    ASSERT_TRUE(cat_coroutine_wait_all()); // call it before timing test
    cat_msec_t s = cat_time_msec();
    cat_timespec time = { 0, 10 * 1000 * 1000 };
    ASSERT_EQ(cat_time_nanosleep(&time, nullptr), 0);
    s = cat_time_msec() - s;
    ASSERT_GE(s, 5);
}

TEST(cat_time, sleep_zero)
{
    cat_coroutine_switches_t switches = cat_coroutine_get_global_switches();
    cat_event_round_t round = cat_event_get_round();
    ASSERT_TRUE(cat_time_delay(0));
    ASSERT_GT(cat_coroutine_get_global_switches(), switches);
    ASSERT_GT(cat_event_get_round(), round);
}

static std::string time_format_msec(cat_msec_t msec)
{
    char *s = cat_time_format_msec(msec);
    DEFER(cat_free(s));
    return std::string(s);
}

TEST(cat_time, format_msec)
{
    ASSERT_EQ(time_format_msec(1ULL), "1ms");
    ASSERT_EQ(time_format_msec(10ULL), "10ms");
    ASSERT_EQ(time_format_msec(100ULL), "100ms");
    ASSERT_EQ(time_format_msec(1000ULL), "1s0ms");
    ASSERT_EQ(time_format_msec(1001ULL), "1s1ms");
    ASSERT_EQ(time_format_msec(60ULL * 1000ULL), "1m0s0ms");
    ASSERT_EQ(time_format_msec(60ULL * 1000ULL + 1000ULL + 1ULL), "1m1s1ms");
    ASSERT_EQ(time_format_msec(60ULL * 60ULL * 1000ULL), "1h0m0s0ms");
    ASSERT_EQ(time_format_msec(24ULL * 60ULL * 60ULL * 1000ULL + 60ULL * 60ULL * 1000ULL + 60ULL * 1000ULL + 1000ULL + 1ULL), "1d1h1m1s1ms");
    ASSERT_EQ(time_format_msec(365ULL * 24ULL * 60ULL * 60ULL * 1000ULL), "365d0h0m0s0ms");
}

TEST(cat_time, delay_cancel)
{
    cat_coroutine_t *coroutine = co([] {
        ASSERT_EQ(cat_time_delay(CAT_TIMEOUT_FOREVER), CAT_RET_NONE);
    });
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
}

TEST(cat_time, sleep_cancel)
{
    cat_coroutine_t *coroutine = co([] {
        ASSERT_EQ(cat_time_sleep(1), 1);
        ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
    });
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
}

TEST(cat_time, msleep_cancel)
{
    cat_coroutine_t *coroutine = co([] {
        ASSERT_EQ(cat_time_msleep(1), 1);
        ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
    });
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
}

TEST(cat_time, msleep_cancel2)
{
    ASSERT_TRUE(cat_coroutine_wait_all()); // call it before timing test
    cat_coroutine_t *waiter = cat_coroutine_get_current();
    bool done = false;
    co([&] {
        ASSERT_EQ(cat_time_msleep(10), 0);
        done = true;
        // cancel the sleep of main
        cat_coroutine_resume(waiter, nullptr, nullptr);
    });
    cat_msec_t s = cat_time_msec();
    ASSERT_GT(cat_time_msleep(100), 0);
    s = cat_time_msec() - s;
    ASSERT_GE(s, 5);
    ASSERT_EQ(done, true);
    ASSERT_EQ(cat_time_msleep(1), 0);
    ASSERT_EQ(cat_time_msleep(1), 0);
    ASSERT_EQ(cat_time_msleep(1), 0);
}

TEST(cat_time, usleep_cancel)
{
    cat_coroutine_t *coroutine = co([] {
        ASSERT_EQ(cat_time_usleep(1000), -1);
        ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
    });
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
}

TEST(cat_time, nanosleep_cancel)
{
    cat_coroutine_t *coroutine = co([] {
        struct cat_timespec req;
        struct cat_timespec rem;
        req.tv_sec = 1;
        req.tv_nsec = 1000 * 1000;
        ASSERT_EQ(cat_time_nanosleep(&req, &rem), -1);
        ASSERT_EQ(rem.tv_sec, 1);
        ASSERT_EQ(rem.tv_nsec, 1000 * 1000);
        ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
    });
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
}

TEST(cat_time, nanosleep_inval)
{
    struct cat_timespec req;
    req.tv_sec = -1;
    req.tv_nsec = 0;
    ASSERT_EQ(cat_time_nanosleep(&req, nullptr), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    req.tv_sec = 0;
    req.tv_nsec = -1;
    ASSERT_EQ(cat_time_nanosleep(&req, nullptr), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    req.tv_sec = 1;
    req.tv_nsec = 999999999 + 1;
    ASSERT_EQ(cat_time_nanosleep(&req, nullptr), -1);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

TEST(cat_time, tv2to)
{
    struct timeval tv;
    ASSERT_EQ(cat_time_tv2to(nullptr), -1);
    tv.tv_sec = -1;
    tv.tv_usec = 0;
    ASSERT_EQ(cat_time_tv2to(&tv), -1);
    tv.tv_sec = 1;
    tv.tv_usec = 1;
    ASSERT_EQ(cat_time_tv2to(&tv), 1000);
    tv.tv_usec = 1000;
    ASSERT_EQ(cat_time_tv2to(&tv), 1001);
}

TEST(cat_time, blocking)
{
    cat_coroutine_t *coroutine = co([] {
        cat_msec_t r = cat_time_msleep(1);
        ASSERT_EQ(r, 1);
        ASSERT_EQ(cat_get_last_error_code(), CAT_ECANCELED);
    });
    cat_sys_usleep(10);
    ASSERT_TRUE(cat_time_delay(0));
    ASSERT_TRUE(cat_coroutine_resume(coroutine, nullptr, nullptr));
}

/* A delay must not come back before the time it was asked to wait. Coming back later is
 * allowed, because a busy scheduler or the clock resolution can push the wakeup out, but
 * an early return breaks anything that relies on the delay having elapsed.
 *
 * The timer has a millisecond resolution, so a delay may legitimately come back that much
 * early. Compare in microseconds, because a millisecond comparison would round the one
 * millisecond of allowed granularity away and report a compliant delay as early */

#define TEST_DELAY_GRANULARITY_US 1000

TEST(cat_time, delay_does_not_return_early)
{
    for (cat_timeout_t timeout : { (cat_timeout_t) 10, (cat_timeout_t) 50, (cat_timeout_t) 200 }) {
        SCOPED_TRACE(timeout);
        co([timeout] {
            cat_msec_t begin = cat_time_nsec();
            ASSERT_EQ(cat_time_delay(timeout), CAT_RET_OK);
            cat_msec_t elapsed_us = (cat_time_nsec() - begin) / 1000;
            EXPECT_GE(elapsed_us + TEST_DELAY_GRANULARITY_US, (cat_msec_t) timeout * 1000)
                << "returned after " << elapsed_us << "us, before the " << timeout << "ms it was given";
        });
        ASSERT_TRUE(cat_coroutine_wait_all());
    }
}

#ifdef CAT_OS_WIN

/* A Windows process has no signal to forward, and uv_poll only accepts sockets,
 * so there is no descriptor to use as an interrupt source */
TEST(cat_time, interruptible_waits_are_not_available)
{
    SKIP_IF_(true, "Interrupt sources are not available on Windows");
}

#else

#include <unistd.h>

namespace {

/* The read end is the interrupt source, the write end stands in for the signal
 * handler that forwards the signal */
struct time_interrupt_pipe_s {
    int fds[2];
    int write_count;

    time_interrupt_pipe_s()
    {
        fds[0] = -1;
        fds[1] = -1;
        write_count = 0;
    }

    ~time_interrupt_pipe_s()
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
        if (write(fds[1], "!", 1) != 1) {
            return false;
        }
        write_count++;
        return true;
    }
};

/* A timer callback runs after the poll of its round, so a byte written here is
 * reported to the waiter in the next loop round */
void time_interrupt_write(cat_time_timeout_t *timer, cat_data_t *data)
{
    time_interrupt_pipe_s *pipe = (time_interrupt_pipe_s *) data;

    (void) timer;
    ASSERT_TRUE(pipe->write_byte());
}

cat_msec_t time_elapsed_msec(cat_nsec_t begin)
{
    return (cat_time_nsec() - begin) / (1000 * 1000);
}

} // namespace

TEST(cat_time, wait_ex_is_interrupted_by_a_source)
{
    time_interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));
    DEFER(cat_interrupt_source_clear());
    cat_time_timeout_t *writer = cat_time_timeout_init(time_interrupt_write, &pipe, 10);
    ASSERT_NE(writer, nullptr);
    DEFER(cat_time_timeout_close(writer));

    cat_nsec_t begin = cat_time_nsec();
    bool ret = cat_time_wait_ex(1000, CAT_TIME_FLAG_INTERRUPTIBLE);
    cat_msec_t elapsed = time_elapsed_msec(begin);

    ASSERT_FALSE(ret);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINTR);
    ASSERT_GE(elapsed, 5);
    ASSERT_LT(elapsed, 500);
}

TEST(cat_time, delay_ex_is_interrupted_by_a_source)
{
    time_interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));
    DEFER(cat_interrupt_source_clear());
    cat_time_timeout_t *writer = cat_time_timeout_init(time_interrupt_write, &pipe, 10);
    ASSERT_NE(writer, nullptr);
    DEFER(cat_time_timeout_close(writer));

    cat_nsec_t begin = cat_time_nsec();
    cat_ret_t ret = cat_time_delay_ex(1000, CAT_TIME_FLAG_INTERRUPTIBLE);
    cat_msec_t elapsed = time_elapsed_msec(begin);

    ASSERT_EQ(ret, CAT_RET_ERROR);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINTR);
    ASSERT_GE(elapsed, 5);
    ASSERT_LT(elapsed, 500);
}

TEST(cat_time, msleep_ex_reports_the_remaining_time)
{
    time_interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));
    DEFER(cat_interrupt_source_clear());
    cat_time_timeout_t *writer = cat_time_timeout_init(time_interrupt_write, &pipe, 10);
    ASSERT_NE(writer, nullptr);
    DEFER(cat_time_timeout_close(writer));

    cat_nsec_t begin = cat_time_nsec();
    cat_msec_t remaining = cat_time_msleep_ex(1000, CAT_TIME_FLAG_INTERRUPTIBLE);
    cat_msec_t elapsed = time_elapsed_msec(begin);

    ASSERT_EQ(cat_get_last_error_code(), CAT_EINTR);
    /* the unslept time is what the deadline has left, neither the whole request
     * nor the zero that an expired timer reports */
    ASSERT_GT(remaining, 0);
    ASSERT_LE(remaining, 1000 - 5);
    ASSERT_GE(elapsed, 5);
    ASSERT_LT(elapsed, 500);
}

TEST(cat_time, wait_ex_forever_is_interrupted_by_a_source)
{
    time_interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));
    DEFER(cat_interrupt_source_clear());
    cat_time_timeout_t *writer = cat_time_timeout_init(time_interrupt_write, &pipe, 10);
    ASSERT_NE(writer, nullptr);
    DEFER(cat_time_timeout_close(writer));

    cat_bool_t interrupted = cat_false;
    /* CAT_TIMEOUT_FOREVER has no timer, so the main coroutine keeps the loop
     * alive with a delay while the child waits without a deadline */
    co([&] {
        interrupted = !cat_time_wait_ex(CAT_TIMEOUT_FOREVER, CAT_TIME_FLAG_INTERRUPTIBLE) &&
            cat_get_last_error_code() == CAT_EINTR;
    });
    ASSERT_EQ(cat_time_delay(100), CAT_RET_OK);
    ASSERT_TRUE(cat_coroutine_wait_all());

    ASSERT_TRUE(interrupted);
}

TEST(cat_time, delay_ex_forever_is_interrupted_by_a_source)
{
    time_interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));
    DEFER(cat_interrupt_source_clear());
    cat_time_timeout_t *writer = cat_time_timeout_init(time_interrupt_write, &pipe, 10);
    ASSERT_NE(writer, nullptr);
    DEFER(cat_time_timeout_close(writer));

    cat_bool_t interrupted = cat_false;
    co([&] {
        interrupted = cat_time_delay_ex(CAT_TIMEOUT_FOREVER, CAT_TIME_FLAG_INTERRUPTIBLE) == CAT_RET_ERROR &&
            cat_get_last_error_code() == CAT_EINTR;
    });
    ASSERT_EQ(cat_time_delay(100), CAT_RET_OK);
    ASSERT_TRUE(cat_coroutine_wait_all());

    ASSERT_TRUE(interrupted);
}

TEST(cat_time, delay_ignores_a_source_without_the_flag)
{
    time_interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));
    DEFER(cat_interrupt_source_clear());
    cat_time_timeout_t *writer = cat_time_timeout_init(time_interrupt_write, &pipe, 10);
    ASSERT_NE(writer, nullptr);
    DEFER(cat_time_timeout_close(writer));

    cat_clear_last_error();
    cat_nsec_t begin = cat_time_nsec();
    cat_ret_t ret = cat_time_delay(100);
    cat_msec_t elapsed = time_elapsed_msec(begin);

    /* the default variant is not registered, so the signal leaves it alone */
    ASSERT_EQ(ret, CAT_RET_OK);
    ASSERT_NE(cat_get_last_error_code(), CAT_EINTR);
    ASSERT_GE(elapsed + TEST_DELAY_GRANULARITY_US / 1000, 100);
    ASSERT_LT(elapsed, 500);
}

TEST(cat_time, wait_ignores_a_source_without_the_flag)
{
    time_interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));
    DEFER(cat_interrupt_source_clear());
    cat_time_timeout_t *writer = cat_time_timeout_init(time_interrupt_write, &pipe, 10);
    ASSERT_NE(writer, nullptr);
    DEFER(cat_time_timeout_close(writer));

    cat_clear_last_error();
    cat_nsec_t begin = cat_time_nsec();
    bool ret = cat_time_wait(100);
    cat_msec_t elapsed = time_elapsed_msec(begin);

    /* a wake would return true, so the timeout result proves the signal did not
     * end the wait */
    ASSERT_FALSE(ret);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
    ASSERT_GE(elapsed + TEST_DELAY_GRANULARITY_US / 1000, 100);
    ASSERT_LT(elapsed, 500);
}

TEST(cat_time, wait_ex_reports_the_timeout_without_a_signal)
{
    time_interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));
    DEFER(cat_interrupt_source_clear());

    cat_nsec_t begin = cat_time_nsec();
    bool ret = cat_time_wait_ex(10, CAT_TIME_FLAG_INTERRUPTIBLE);
    cat_msec_t elapsed = time_elapsed_msec(begin);

    /* registration alone must not turn the expiry into an interrupt */
    ASSERT_FALSE(ret);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
    ASSERT_GE(elapsed + TEST_DELAY_GRANULARITY_US / 1000, 10);
    ASSERT_LT(elapsed, 500);
}

TEST(cat_time, a_signal_after_the_timeout_does_not_report_eintr)
{
    time_interrupt_pipe_s pipe;
    ASSERT_TRUE(pipe.open());
    ASSERT_TRUE(cat_interrupt_source_set(pipe.fds[0]));
    DEFER(cat_interrupt_source_clear());
    /* the byte arrives after the wait has already expired, so the timeout wins */
    cat_time_timeout_t *writer = cat_time_timeout_init(time_interrupt_write, &pipe, 50);
    ASSERT_NE(writer, nullptr);
    DEFER(cat_time_timeout_close(writer));

    cat_nsec_t begin = cat_time_nsec();
    bool ret = cat_time_wait_ex(10, CAT_TIME_FLAG_INTERRUPTIBLE);
    cat_msec_t elapsed = time_elapsed_msec(begin);

    ASSERT_FALSE(ret);
    ASSERT_EQ(cat_get_last_error_code(), CAT_ETIMEDOUT);
    ASSERT_GE(elapsed + TEST_DELAY_GRANULARITY_US / 1000, 10);
    ASSERT_LT(elapsed, 50);

    /* let the late byte arrive and be drained with no waiter registered */
    ASSERT_EQ(cat_time_delay(60), CAT_RET_OK);
    ASSERT_EQ(pipe.write_count, 1);
}

#endif /* CAT_OS_WIN */
