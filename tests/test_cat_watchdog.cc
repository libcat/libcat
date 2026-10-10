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

#ifdef CAT_OS_LINUX
#include <sys/resource.h>
#endif

const std::string keyword = "Watchdog";

static void test_sys_nanosleep_nocancel(cat_nsec_t ns_total)
{
    cat_timespec ts;
    ts.tv_sec = ns_total / (1000 * 1000 * 1000);
    ts.tv_nsec = ns_total % (1000 * 1000 * 1000);
    while (cat_sys_nanosleep(&ts, &ts) != 0);
}

TEST(cat_watchdog, base)
{
    testing::internal::CaptureStderr();

    ASSERT_LT(cat_watchdog_get_threshold(), 0);

    ASSERT_TRUE(cat_watchdog_run(nullptr, 0, 0, nullptr));
    DEFER(cat_watchdog_stop());

    ASSERT_GT(cat_watchdog_get_threshold(), 0);

    co([=] {
        test_sys_nanosleep_nocancel(cat_watchdog_get_quantum() * 100);
    });

    cat_watchdog_stop(); // make sure not output error anymore
    fflush(stderr); // flush stderr to prevent from affecting the next test

    std::string output = testing::internal::GetCapturedStderr();
    std::string::size_type pos = 0;
    size_t count = 0;
    while ((pos = output.find(keyword, pos)) != std::string::npos) {
         count++;
         pos += keyword.length();
    }
    ASSERT_GT(count, 0);
}

TEST(cat_watchdog, single)
{
    // recycle possible residual coroutines
    ASSERT_TRUE(cat_coroutine_wait_all());

    testing::internal::CaptureStderr();
    ([&] {
        ASSERT_TRUE(cat_watchdog_run(nullptr, 0, 0, nullptr));
        DEFER(cat_watchdog_stop());

        test_sys_nanosleep_nocancel(cat_watchdog_get_quantum() * 3);

        cat_watchdog_stop(); // make sure not output error anymore
        fflush(stderr); // flush stderr to prevent from affecting the next test
    })();
    std::string output = testing::internal::GetCapturedStderr();
    printf("%s\n", output.c_str());
    ASSERT_TRUE(output.find(keyword) == std::string::npos);
}

TEST(cat_watchdog, scheduler_blocking)
{
    testing::internal::CaptureStderr();

    ([&] {
        ASSERT_TRUE(cat_watchdog_run(nullptr, 0, 0, nullptr));
        DEFER(cat_watchdog_stop());

        test_sys_nanosleep_nocancel(cat_watchdog_get_quantum() * 3);

        cat_watchdog_stop(); // make sure not output error anymore
        fflush(stderr); // flush stderr to prevent from affecting the next test
    })();

    std::string output = testing::internal::GetCapturedStderr();
    ASSERT_TRUE(output.find(keyword) == std::string::npos);
}

TEST(cat_watchdog, nothing)
{
    struct watchdog_observation : cat_watchdog_t {
        cat_alert_count_t max_alert_count = 0;
    } observation {};
    cat_bool_t stopped = cat_false;

    testing::internal::CaptureStderr();

    ([&] {
        ASSERT_TRUE(cat_watchdog_run(&observation, 0, 0, [](cat_watchdog_t *watchdog) {
            auto *state = static_cast<watchdog_observation *>(watchdog);
            if (watchdog->alert_count > state->max_alert_count) {
                state->max_alert_count = watchdog->alert_count;
            }
            cat_watchdog_alert_standard(watchdog);
        }));
        DEFER(cat_watchdog_stop());

        auto sleeper = [=] {
            cat_timeout_t usec = ((cat_watchdog_get_quantum() / 1000) * 3) / TEST_MAX_REQUESTS;
            size_t n = TEST_MAX_REQUESTS;
            while (n--) {
                cat_time_usleep(usec);
            }
        };

        for (size_t c = TEST_MAX_CONCURRENCY; c--;) {
            co(sleeper);
        }
        sleeper();

        stopped = cat_watchdog_stop(); // make sure not output error anymore
        fflush(stderr); // flush stderr to prevent from affecting the next test
    })();

    std::string output = testing::internal::GetCapturedStderr();
    ASSERT_TRUE(stopped) << output;
    ASSERT_GT(observation.quantum, 0);
    ASSERT_GT(observation.threshold, 0);
    // Allow transient alerts within the threshold, but reject sustained stalls.
    // Keep the peak before scheduling resets alert_count; stop has joined the observer thread.
    cat_alert_count_t max_allowed_alerts = observation.threshold / observation.quantum;
    ASSERT_LE(observation.max_alert_count, max_allowed_alerts)
        << "quantum=" << observation.quantum << ", threshold=" << observation.threshold << '\n' << output;
}

TEST(cat_watchdog, duplicated)
{
    ASSERT_TRUE(cat_watchdog_run(
        nullptr,
        CAT_WATCH_DOG_DEFAULT_QUANTUM + 1 /* code cov */,
        CAT_WATCH_DOG_THRESHOLD_DISABLED, /* code cov */
        cat_watchdog_alert_standard
    ));
    ASSERT_FALSE(cat_watchdog_run(nullptr, 0, 0, nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);
    ASSERT_TRUE(cat_watchdog_stop());
    ASSERT_FALSE(cat_watchdog_stop());
    ASSERT_EQ(cat_get_last_error_code(), CAT_EMISUSE);
}

TEST(cat_watchdog, not_running)
{
    ASSERT_EQ(cat_watchdog_get_quantum(), -1);
}

#if defined(CAT_OS_LINUX) && GTEST_HAS_DEATH_TEST
TEST(cat_watchdog, thread_limit_failure_and_restart)
{
    SKIP_IF_(getuid() == 0, "RLIMIT_NPROC does not restrict root");
    ASSERT_FALSE(cat_watchdog_is_running());
    ASSERT_EXIT({
        struct rlimit original;
        if (getrlimit(RLIMIT_NPROC, &original) != 0) {
            std::exit(1);
        }
        for (bool allocated : {false, true}) {
            struct rlimit restricted = original;
            restricted.rlim_cur = 0;
            if (setrlimit(RLIMIT_NPROC, &restricted) != 0) {
                std::exit(2);
            }
            cat_watchdog_t watchdog;
            bool started = cat_watchdog_run(allocated ? nullptr : &watchdog, 0, 0, nullptr);
            cat_errno_t error = cat_get_last_error_code();
            bool running = cat_watchdog_is_running();
            // Restore the child's limit before recovery and sanitizer shutdown.
            if (setrlimit(RLIMIT_NPROC, &original) != 0) {
                std::exit(3);
            }
            if (started) {
                cat_watchdog_stop();
            }
            if (started || running || error != CAT_EAGAIN) {
                std::exit(4);
            }
            if (!cat_watchdog_run(allocated ? nullptr : &watchdog, 0, 0, nullptr) || !cat_watchdog_stop()) {
                std::exit(5);
            }
        }
        std::exit(0);
    }, testing::ExitedWithCode(0), "");
    ASSERT_FALSE(cat_watchdog_is_running());
}
#endif
