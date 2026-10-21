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

#ifndef CAT_OS_WAIT_H
#define CAT_OS_WAIT_H
#ifdef __cplusplus
extern "C" {
#endif

#include "cat.h"

#ifdef CAT_OS_UNIX_LIKE
# include <sys/resource.h>

/* we believe it's always available on nowadays machines,
 * we can consider checking it only when someone met this problem :) */
# define CAT_OS_WAIT_HAVE_RUSAGE 1
#endif

CAT_API cat_bool_t cat_os_wait_module_init(void);
CAT_API cat_bool_t cat_os_wait_module_shutdown(void);
CAT_API cat_bool_t cat_os_wait_runtime_init(void);
CAT_API cat_bool_t cat_os_wait_runtime_shutdown(void);

#ifdef CAT_OS_UNIX_LIKE
/**
 * 协程安全的 fork 封装
 * 子进程中自动调用 cat_event_fork() 重建事件循环
 * @return 父进程返回子进程 PID，子进程返回 0，失败返回 -1
 */
CAT_API cat_pid_t cat_os_fork(void);
#endif

/*
 * Wait for a child process, without blocking the thread that waits.
 *
 * Return the pid of the child that changed state, 0 when WNOHANG is set and no
 * child is ready, and -1 on error. A wait that runs out of time returns -1 with
 * CAT_ETIMEDOUT, so a timeout is not a child state.
 *
 * A stopped child is reported even when WUNTRACED is not set, because the
 * bookkeeping keeps every status it takes from the kernel.
 *
 * Do not wait for the same child with this API and with cat_process_*: the
 * SIGCHLD handler here can take the exit status of a process that uv_spawn()
 * started, and the cat_process wait then never completes.
 *
 * pid 0 is not supported (CAT_ENOTSUP). pid -1 waits for any child.
 */
CAT_API cat_pid_t cat_os_wait(int *status);
CAT_API cat_pid_t cat_os_wait_ex(int *status, cat_msec_t timeout);
CAT_API cat_pid_t cat_os_waitpid(cat_pid_t pid, int *status, int options);
CAT_API cat_pid_t cat_os_waitpid_ex(cat_pid_t pid, int *status, int options, cat_msec_t timeout);

#ifdef CAT_OS_WAIT_HAVE_RUSAGE
CAT_API cat_pid_t cat_os_wait3(int *status, int options, struct rusage *rusage);
CAT_API cat_pid_t cat_os_wait3_ex(int *status, int options, struct rusage *rusage, cat_msec_t timeout);
CAT_API cat_pid_t cat_os_wait4(cat_pid_t pid, int *status, int options, struct rusage *rusage);
CAT_API cat_pid_t cat_os_wait4_ex(cat_pid_t pid, int *status, int options, struct rusage *rusage, cat_msec_t timeout);
#endif /* CAT_OS_WAIT_HAVE_RUSAGE */

#ifdef __cplusplus
}
#endif
#endif /* CAT_OS_WAIT_H */
