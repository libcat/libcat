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


#include "cat_os_wait.h"

#ifdef CAT_OS_WAIT

#include "cat_signal.h"
#include "cat_event.h"
#include "cat_time.h"

#include "uv/tree.h"

#include <sys/wait.h>

typedef struct cat_os_wait_task_s {
    cat_queue_t node;
    cat_pid_t pid;
    int status;
    cat_coroutine_t *coroutine;
} cat_os_wait_task_t;

typedef struct cat_os_waitpid_task_s {
    RB_ENTRY(cat_os_waitpid_task_s) tree_entry;
    cat_pid_t pid;
    int status;
    size_t waiter_count;
    cat_queue_t waiters;
} cat_os_waitpid_task_t;

RB_HEAD(cat_os_waitpid_task_tree_s, cat_os_waitpid_task_s);

typedef struct cat_os_child_process_stat_s {
    RB_ENTRY(cat_os_child_process_stat_s) tree_entry;
    cat_pid_t pid;
    int status;
} cat_os_child_process_stat_t;

RB_HEAD(cat_os_child_process_stat_tree_s, cat_os_child_process_stat_s);

CAT_GLOBALS_STRUCT_BEGIN(cat_os_wait)
    size_t sigchld_watcher_count;
    uv_signal_t sigchld_watcher;
    /* coroutines that hangs on os_wait() */
    cat_queue_t waiter_list;
    /* coroutines that hangs on os_waitpid() */
    struct cat_os_waitpid_task_tree_s waitpid_task_tree;
    /* those child processes that are recycled */
    struct cat_os_child_process_stat_tree_s child_process_state_tree;
CAT_GLOBALS_STRUCT_END(cat_os_wait)

CAT_API CAT_GLOBALS_DECLARE(cat_os_wait)

CAT_GLOBALS_CTOR_DECLARE_SZ(cat_os_wait)

extern CAT_API CAT_GLOBALS_DECLARE(cat_os_wait)

#define CAT_OS_WAIT_G(x) CAT_GLOBALS_GET(cat_os_wait, x)

static int cat_os__waitpid_task_compare(cat_os_waitpid_task_t* t1, cat_os_waitpid_task_t* t2)
{
    if (t1->pid < t2->pid) {
        return -1;
    }
    if (t1->pid > t2->pid) {
        return 1;
    }
    return 0;
}

RB_GENERATE_STATIC(cat_os_waitpid_task_tree_s,
                   cat_os_waitpid_task_s, tree_entry,
                   cat_os__waitpid_task_compare);

static int cat_os__child_process_state_compare(cat_os_child_process_stat_t* s1, cat_os_child_process_stat_t* s2)
{
    if (s1->pid < s2->pid) {
        return -1;
    }
    if (s1->pid > s2->pid) {
        return 1;
    }
    return 0;
}

RB_GENERATE_STATIC(cat_os_child_process_stat_tree_s,
                   cat_os_child_process_stat_s, tree_entry,
                   cat_os__child_process_state_compare);

static size_t cat_os_waitpid_dispatch(void)
{
    size_t new_count = 0;

    CAT_LOG_DEBUG(OS, "waitpid dispatch");
    while (1) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG | WUNTRACED);
        if (pid <= 0) {
            break;
        }
        CAT_LOG_DEBUG(OS, "waitpid() = %d, exited=%u, exit_status=%d, if_signaled=%u, termsig=%d, if_stopped=%u, stopsig=%d",
            pid, WIFEXITED(status), WEXITSTATUS(status), WIFSIGNALED(status), WTERMSIG(status), WIFSTOPPED(status), WSTOPSIG(status));
        do {
            /* try to notify waitpid() waiters */
            cat_os_waitpid_task_t *waitpid_task, lookup;
            lookup.pid = pid;
            waitpid_task = RB_FIND(cat_os_waitpid_task_tree_s, &CAT_OS_WAIT_G(waitpid_task_tree), &lookup);
            if (waitpid_task != NULL) {
                size_t waiter_count = waitpid_task->waiter_count;
                cat_coroutine_t *coroutine;
                while ((coroutine = cat_queue_front_data(&waitpid_task->waiters, cat_coroutine_t, waiter.node))) {
                    CAT_ASSERT(pid == waitpid_task->pid);
                    waitpid_task->status = status;
                    CAT_LOG_DEBUG(OS, "Notify a waitpid() waiter");
                    cat_coroutine_schedule(coroutine, OS, "WaitPid");
                    /* Ensure that newly joined waiters will not be woken up (pid maybe reused) */
                    if (--waiter_count == 0) {
                        break;
                    }
                }
                break;
            }
            /* try to notify one wait() waiter */
            if (!cat_queue_empty(&CAT_OS_WAIT_G(waiter_list))) {
                cat_os_wait_task_t *wait_task = cat_queue_front_data(&CAT_OS_WAIT_G(waiter_list), cat_os_wait_task_t, node);
                CAT_ASSERT(wait_task->pid == -1);
                wait_task->pid = pid;
                wait_task->status = status;
                CAT_LOG_DEBUG(OS, "Notify a wait() waiter");
                cat_coroutine_schedule(wait_task->coroutine, OS, "Wait");
                break;
            }
            /* save status info if nobody cares */
            CAT_LOG_DEBUG(OS, "Stat info will be saved");
            cat_os_child_process_stat_t *child_process_state = (cat_os_child_process_stat_t *) cat_malloc(sizeof(*child_process_state));
            if (unlikely(child_process_state == NULL)) {
                CAT_SYSCALL_FAILURE(WARNING, OS, "Malloc for OS child process state failed");
                break;
            }
            child_process_state->pid = pid;
            child_process_state->status = status;
            RB_INSERT(cat_os_child_process_stat_tree_s, &CAT_OS_WAIT_G(child_process_state_tree), child_process_state);
            new_count++;
        } while (0);
    }

    return new_count;
}

static void cat_os_wait_sigchld_handler(uv_signal_t* handle, int signum)
{
    CAT_ASSERT(signum == CAT_SIGCHLD);
    CAT_LOG_DEBUG(OS, "SIGCHLD received");
    (void) cat_os_waitpid_dispatch();
}

static cat_always_inline void cat_os_wait_sigchld_watcher_start(void)
{
    (void) uv_signal_start(&CAT_OS_WAIT_G(sigchld_watcher), cat_os_wait_sigchld_handler, CAT_SIGCHLD);
    CAT_OS_WAIT_G(sigchld_watcher_count)++;
}

static cat_always_inline void cat_os_wait_sigchld_watcher_end(void)
{
    if (--CAT_OS_WAIT_G(sigchld_watcher_count) == 0) {
        (void) uv_signal_stop(&CAT_OS_WAIT_G(sigchld_watcher));
    }
}

static cat_pid_t cat_os__waitpid(cat_pid_t pid, int *status, int options, cat_msec_t timeout, const char *type)
{
    /* If pid is 0, the call waits for any child process in the process group of the caller.
     * but we can not recognize which process is in the same group for now. */
    if (unlikely(pid == 0)) {
        cat_update_last_error(CAT_ENOTSUP, "OS waitpid() with zero pid is not supported yet");
        return -1;
    }

    do {
        cat_os_child_process_stat_t *child_process_state = NULL;
        if (pid < 0) {
            cat_os_child_process_stat_t* tmp_child_process_state_iterator;
            RB_FOREACH_SAFE(child_process_state, cat_os_child_process_stat_tree_s, &CAT_OS_WAIT_G(child_process_state_tree), tmp_child_process_state_iterator) {
                break;
            }
        } else {
            cat_os_child_process_stat_t lookup;
            lookup.pid = pid;
            child_process_state = RB_FIND(cat_os_child_process_stat_tree_s, &CAT_OS_WAIT_G(child_process_state_tree), &lookup);
        }
        if (child_process_state != NULL) {
            RB_REMOVE(cat_os_child_process_stat_tree_s, &CAT_OS_WAIT_G(child_process_state_tree), child_process_state);
            pid = child_process_state->pid;
            *status = child_process_state->status;
            cat_free(child_process_state);
            return pid;
        }
    } while (cat_os_waitpid_dispatch() > 0);

    cat_os_wait_task_t wait_task;
    cat_os_waitpid_task_t *waitpid_task;
    if (pid < 0) {
        wait_task.pid = -1;
        wait_task.status = 0;
        wait_task.coroutine = CAT_COROUTINE_G(current);
        cat_queue_push_back(&CAT_OS_WAIT_G(waiter_list), &wait_task.node);
    } else {
        cat_os_waitpid_task_t lookup;
        lookup.pid = pid;
        waitpid_task = RB_FIND(cat_os_waitpid_task_tree_s, &CAT_OS_WAIT_G(waitpid_task_tree), &lookup);
        if (waitpid_task == NULL) {
            waitpid_task = (cat_os_waitpid_task_t *) cat_malloc(sizeof(*waitpid_task));
            if (unlikely(waitpid_task == NULL)) {
                cat_update_last_error_of_syscall("Malloc for waitpid task failed");
                return -1;
            }
            waitpid_task->pid = pid;
            waitpid_task->status = 0;
            waitpid_task->waiter_count = 0;
            cat_queue_init(&waitpid_task->waiters);
            RB_INSERT(cat_os_waitpid_task_tree_s, &CAT_OS_WAIT_G(waitpid_task_tree), waitpid_task);
        }
        waitpid_task->waiter_count++;
        cat_queue_push_back(&waitpid_task->waiters, &CAT_COROUTINE_G(current)->waiter.node);
    }
    
    cat_bool_t ret = cat_time_wait(timeout);

    if (pid < 0) {
        pid = wait_task.pid;
        *status = wait_task.status;
        cat_queue_remove(&wait_task.node);
    } else {
        CAT_ASSERT(pid == waitpid_task->pid);
        *status = waitpid_task->status;
        cat_queue_remove(&CAT_COROUTINE_G(current)->waiter.node);
        if (--waitpid_task->waiter_count == 0) {
            RB_REMOVE(cat_os_waitpid_task_tree_s, &CAT_OS_WAIT_G(waitpid_task_tree), waitpid_task);
            cat_free(waitpid_task);
        }
    }

    if (unlikely(!ret)) {
        cat_update_last_error_with_previous("OS %s wait failed", type);
        return -1;
    }

    return pid;
}

static cat_pid_t cat_os__waitpid_wrapper(cat_pid_t pid, int *status, int options, cat_msec_t timeout, const char *type)
{
    cat_os_wait_sigchld_watcher_start();
    pid = cat_os__waitpid(pid, status, options, timeout, type);
    cat_os_wait_sigchld_watcher_end();
    return pid;
}

CAT_API cat_pid_t cat_os_wait(int *status)
{
    return cat_os_wait_ex(status, CAT_TIMEOUT_FOREVER);
}

CAT_API cat_pid_t cat_os_wait_ex(int *status, cat_msec_t timeout)
{
    return cat_os__waitpid_wrapper(-1, status, 0, timeout, "wait()");
}

CAT_API cat_pid_t cat_os_waitpid(cat_pid_t pid, int *status, int options)
{
    return cat_os_waitpid_ex(pid, status, options, CAT_TIMEOUT_FOREVER);
}

CAT_API cat_pid_t cat_os_waitpid_ex(cat_pid_t pid, int *status, int options, cat_msec_t timeout)
{
    return cat_os__waitpid_wrapper(pid, status, 0, timeout, "waitpid()");
}

CAT_API cat_bool_t cat_os_wait_module_init(void)
{
    CAT_GLOBALS_REGISTER(cat_os_wait, CAT_GLOBALS_CTOR(cat_os_wait), NULL);

    return cat_true;
}

CAT_API cat_bool_t cat_os_wait_runtime_init(void)
{
    uv_signal_init(cat_event_loop, &CAT_OS_WAIT_G(sigchld_watcher));
    cat_queue_init(&CAT_OS_WAIT_G(waiter_list));

    return cat_true;
}

CAT_API cat_bool_t cat_os_wait_runtime_shutdown(void)
{
    uv_close((uv_handle_t *) &CAT_OS_WAIT_G(sigchld_watcher), NULL);

    return cat_true;
}

#endif /* CAT_OS_WAIT */
