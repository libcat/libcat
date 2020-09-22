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

#include "cat_coroutine.h"

#if defined(CAT_OS_UNIX_LIKE) && defined(CAT_DEBUG)
#include <sys/mman.h>
#ifdef PROT_NONE
#define CAT_COROUTINE_USE_MPEOTECT
#endif
#endif

#ifdef HAVE_VALGRIND
#include <valgrind/valgrind.h>
#endif

/* coroutine */

static cat_coroutine_stack_size_t cat_coroutine_align_stack_size(size_t size)
{
    if (size == 0) {
        size = CAT_COROUTINE_G(default_stack_size);
    } else if (unlikely(size < CAT_COROUTINE_MIN_STACK_SIZE)) {
        size = CAT_COROUTINE_MIN_STACK_SIZE;
    } else if (unlikely(size > CAT_COROUTINE_MAX_STACK_SIZE)) {
        size = CAT_COROUTINE_MAX_STACK_SIZE;
    } else {
        size = CAT_MEMORY_ALIGNED_SIZE_EX(size, CAT_COROUTINE_STACK_ALIGNED_SIZE);
    }

    return size;
}

CAT_API CAT_GLOBALS_DECLARE(cat_coroutine)

CAT_GLOBALS_CTOR_DECLARE_SZ(cat_coroutine)

CAT_API cat_bool_t cat_coroutine_module_init(void)
{
    CAT_GLOBALS_REGISTER(cat_coroutine, CAT_GLOBALS_CTOR(cat_coroutine), NULL);
    return cat_true;
}

CAT_API cat_bool_t cat_coroutine_runtime_init(void)
{
    /* register coroutine resume */
    cat_coroutine_register_resume(cat_coroutine_resume_standard);

    /* init options */
    cat_coroutine_set_default_stack_size(CAT_COROUTINE_RECOMMENDED_STACK_SIZE);
    cat_coroutine_set_dead_lock_log_type(CAT_LOG_TYPE_WARNING);

    /* init info */
    CAT_COROUTINE_G(last_id) = 0;
    CAT_COROUTINE_G(count) = 0;
    CAT_COROUTINE_G(peak_count) = 0;
    CAT_COROUTINE_G(round) = 0;

    /* init main coroutine properties */
    do {
        cat_coroutine_t *main_coroutine = &CAT_COROUTINE_G(_main);
        main_coroutine->id = CAT_COROUTINE_MAIN_ID;
        main_coroutine->start_time = cat_time_msec();
        main_coroutine->flags = CAT_COROUTINE_FLAG_ON_STACK;
        main_coroutine->state = CAT_COROUTINE_STATE_RUNNING;
        main_coroutine->opcodes = CAT_COROUTINE_OPCODE_NONE;
        main_coroutine->round = ++CAT_COROUTINE_G(round);
        main_coroutine->from = NULL;
        main_coroutine->previous = NULL;
        main_coroutine->stack = NULL;
        main_coroutine->stack_size = 0;
        memset(&main_coroutine->context, ~0, sizeof(cat_coroutine_context_t));
        main_coroutine->function = NULL;
#ifdef CAT_COROUTINE_USE_UCONTEXT
        main_coroutine->transfer_data = NULL;
#endif
#ifdef HAVE_VALGRIND
        main_coroutine->valgrind_stack_id = UINT32_MAX;
#endif
        CAT_COROUTINE_G(main) = main_coroutine;
        CAT_COROUTINE_G(current) = main_coroutine;

        CAT_COROUTINE_G(last_id) = main_coroutine->id + 1;
        CAT_COROUTINE_G(count)++;
        CAT_COROUTINE_G(peak_count)++;
    } while (0);

    /* scheduler */
    CAT_COROUTINE_G(scheduler) = NULL;

    return cat_true;
}

CAT_API cat_coroutine_stack_size_t cat_coroutine_set_default_stack_size(size_t size)
{
    cat_coroutine_stack_size_t original_size = CAT_COROUTINE_G(default_stack_size);
    CAT_COROUTINE_G(default_stack_size) = cat_coroutine_align_stack_size(size);
    return original_size;
}

CAT_API cat_bool_t cat_coroutine_set_dead_lock_log_type(cat_log_type_t type)
{
    CAT_COROUTINE_G(dead_lock_log_type) = type;
    return cat_true;
}

CAT_API cat_coroutine_resume_t cat_coroutine_register_resume(cat_coroutine_resume_t resume)
{
    cat_coroutine_resume_t origin_resume = cat_coroutine_resume;
    cat_coroutine_resume = resume;
    return origin_resume;
}

CAT_API cat_coroutine_t *cat_coroutine_register_main(cat_coroutine_t *coroutine)
{
    cat_coroutine_t *original_main = CAT_COROUTINE_G(main);

    if (original_main != coroutine) {
        if (original_main != NULL) {
            memcpy(coroutine, original_main, sizeof(*coroutine));
        }
        CAT_COROUTINE_G(main) = coroutine;
        if (original_main == CAT_COROUTINE_G(current)) {
            CAT_COROUTINE_G(current) = coroutine;
        }
    }

    return original_main;
}

/* globals */
CAT_API cat_coroutine_stack_size_t cat_coroutine_get_default_stack_size(void)
{
    return CAT_COROUTINE_G(default_stack_size);
}

CAT_API cat_log_type_t cat_coroutine_get_dead_lock_log_type(void)
{
    return CAT_COROUTINE_G(dead_lock_log_type);
}

CAT_API cat_coroutine_t *cat_coroutine_get_current(void)
{
    return CAT_COROUTINE_G(current);
}

CAT_API cat_coroutine_id_t cat_coroutine_get_current_id(void)
{
    return CAT_COROUTINE_G(current)->id;
}

CAT_API cat_coroutine_t *cat_coroutine_get_main(void)
{
    return CAT_COROUTINE_G(main);
}

CAT_API cat_coroutine_t *cat_coroutine_get_scheduler(void)
{
    return CAT_COROUTINE_G(scheduler);
}

CAT_API cat_coroutine_id_t cat_coroutine_get_last_id(void)
{
    return CAT_COROUTINE_G(last_id);
}

CAT_API cat_coroutine_count_t cat_coroutine_get_count(void)
{
    return CAT_COROUTINE_G(count);
}

CAT_API cat_coroutine_count_t cat_coroutine_get_real_count(void)
{
    cat_coroutine_count_t count = CAT_COROUTINE_G(count);

    if (CAT_COROUTINE_G(scheduler) != NULL) {
        count++;
    }

    return count;
}

CAT_API cat_coroutine_count_t cat_coroutine_get_peak_count(void)
{
    return CAT_COROUTINE_G(peak_count);
}

CAT_API cat_coroutine_round_t cat_coroutine_get_current_round(void)
{
    return CAT_COROUTINE_G(round);
}

static void cat_coroutine_context_function(cat_coroutine_transfer_t transfer)
{
    cat_coroutine_t *coroutine = CAT_COROUTINE_G(current);
    if (unlikely(++CAT_COROUTINE_G(count) > CAT_COROUTINE_G(peak_count))) {
        CAT_COROUTINE_G(peak_count) = CAT_COROUTINE_G(count);
    }
    cat_debug(COROUTINE, "Start (count=" CAT_COROUTINE_COUNT_FMT ")", CAT_COROUTINE_G(count));
#ifdef CAT_COROUTINE_USE_UCONTEXT
    CAT_ASSERT(transfer.data == NULL);
    transfer.data = coroutine->transfer_data;
#else
    /* update origin's context */
    coroutine->from->context = transfer.from_context;
#endif
    /* start time */
    coroutine->start_time = cat_time_msec();
    /* execute function */
    transfer.data = coroutine->function(transfer.data);
    /* is finished */
    coroutine->state = CAT_COROUTINE_STATE_FINISHED;
    /* finished */
    CAT_COROUTINE_G(count)--;
    cat_debug(COROUTINE, "Finished (count=" CAT_COROUTINE_COUNT_FMT ")", CAT_COROUTINE_G(count));
    /* yield to previous */
    cat_coroutine_t *previous_coroutine = coroutine->previous;
    CAT_ASSERT(previous_coroutine != NULL);
    cat_coroutine_jump(previous_coroutine, transfer.data);
    /* never here */
    CAT_NEVER_HERE(COROUTINE, "Coroutine is dead");
}

CAT_API void cat_coroutine_init(cat_coroutine_t *coroutine)
{
    coroutine->state = CAT_COROUTINE_STATE_INIT;
}

CAT_API cat_coroutine_t *cat_coroutine_create(cat_coroutine_t *coroutine, cat_coroutine_function_t function)
{
    return cat_coroutine_create_ex(coroutine, function, 0);
}

CAT_API cat_coroutine_t *cat_coroutine_create_ex(cat_coroutine_t *coroutine, cat_coroutine_function_t function, size_t stack_size)
{
    cat_coroutine_stack_t *stack, *stack_end;
    size_t real_stack_size;
    cat_coroutine_context_t context;

    /* align stack size */
    stack_size = cat_coroutine_align_stack_size(stack_size);
    /* alloc memory */
    stack = (cat_coroutine_stack_t *) cat_sys_malloc(stack_size);
    if (unlikely(!stack)) {
        cat_update_last_error_of_syscall("Malloc for stack failed with size %zu", stack_size);
        return NULL;
    }
    /* calculations */
    real_stack_size = coroutine ? stack_size : (stack_size - sizeof(cat_coroutine_t));
    stack_end = (cat_coroutine_stack_t *) (((char *) stack) + real_stack_size);
    /* determine the position of the coroutine */
    if (!coroutine) {
        coroutine = (cat_coroutine_t *) stack_end;
        coroutine->flags = CAT_COROUTINE_FLAG_NONE;
    } else {
        coroutine->flags = CAT_COROUTINE_FLAG_ON_STACK;
    }
    /* make context */
#ifdef CAT_COROUTINE_USE_UCONTEXT
    if (unlikely(getcontext(&context) == -1)) {
        cat_sys_free(stack);
        cat_update_last_error_ez("Ucontext getcontext failed");
        return NULL;
    }
    context.uc_stack.ss_sp = stack;
    context.uc_stack.ss_size = available_size;
    context.uc_stack.ss_flags = 0;
    context.uc_link = NULL;
    cat_coroutine_context_make(&context, (void (*)(void)) &cat_coroutine_context_function, 1, NULL);
#else
    context = cat_coroutine_context_make((void *) stack_end, real_stack_size, cat_coroutine_context_function);
#endif
    /* protect stack */
#ifdef CAT_COROUTINE_USE_MPEOTECT
    if (unlikely(mprotect(cat_getpageof(stack) + cat_getpagesize(), cat_getpagesize(), PROT_NONE) != 0)) {
        cat_syscall_failure(NOTICE, COROUTINE, "Protect stack failed");
    }
#endif
    /* init coroutine properties */
    coroutine->id = CAT_COROUTINE_G(last_id)++;
    coroutine->state = CAT_COROUTINE_STATE_READY;
    coroutine->opcodes = CAT_COROUTINE_OPCODE_NONE;
    coroutine->round = 0;
    coroutine->from = NULL;
    coroutine->previous = NULL;
    coroutine->start_time = 0;
    coroutine->stack = stack;
    coroutine->stack_size = stack_size;
    coroutine->context = context;
    coroutine->function = function;
#ifdef CAT_COROUTINE_USE_UCONTEXT
    coroutine->transfer_data = NULL;
#endif
#ifdef HAVE_VALGRIND
    coroutine->valgrind_stack_id = VALGRIND_STACK_REGISTER(stack_end, stack);
#endif
    cat_debug(COROUTINE, "Create R" CAT_COROUTINE_ID_FMT " with stack = %p, stack_size = %zu, function = %p on the %s",
                        coroutine->id, stack, stack_size, function, ((void *) coroutine) == ((void*) stack_end) ? "stack" : "heap");

    return coroutine;
}

CAT_API void cat_coroutine_close(cat_coroutine_t *coroutine)
{
    cat_coroutine_stack_t *stack = coroutine->stack;

    if (stack == NULL) {
        cat_core_error(COROUTINE, "Unready coroutine or double close");
    }
    if (unlikely(cat_coroutine_is_alive(coroutine))) {
        cat_core_error(COROUTINE, "Unable to close an active coroutine");
    }
#ifdef HAVE_VALGRIND
    VALGRIND_STACK_DEREGISTER(coroutine->valgrind_stack_id);
#endif
#ifdef CAT_COROUTINE_USE_MPEOTECT
    if (unlikely(mprotect(cat_getpageof(stack) + cat_getpagesize(), cat_getpagesize(), PROT_READ | PROT_WRITE) != 0)) {
        cat_syscall_failure(NOTICE, COROUTINE, "Unprotect stack failed");
    }
#endif
    /* fast free */
    coroutine->state = CAT_COROUTINE_STATE_DEAD;
    coroutine->stack = NULL;
    cat_sys_free(stack);
}

CAT_API cat_bool_t cat_coroutine_jump_precheck(const cat_coroutine_t *coroutine)
{
    cat_coroutine_t *current_coroutine = CAT_COROUTINE_G(current);

    if (coroutine == current_coroutine->previous) {
        return cat_true;
    }

    if (unlikely(!cat_coroutine_is_available(coroutine))) {
        cat_update_last_error(CAT_ESRCH, "Coroutine is not available");
        return cat_false;
    }
    if (unlikely(coroutine == current_coroutine)) {
        cat_update_last_error(CAT_EBUSY, "Coroutine is running");
        return cat_false;
    }
    if (unlikely(coroutine->previous != NULL)) {
        cat_update_last_error(CAT_EBUSY, "Coroutine is in progress");
        return cat_false;
    }
    if (unlikely(coroutine->flags & CAT_COROUTINE_FLAG_SCHEDULER)) {
        cat_update_last_error(CAT_EMISUSE, "Resume scheduler coroutine is not allowed");
        return cat_false;
    }
    if (unlikely(
        (coroutine->opcodes & CAT_COROUTINE_OPCODE_WAIT) &&
        (current_coroutine != coroutine->waiter.coroutine)
    )) {
        cat_update_last_error(CAT_EAGAIN, "Coroutine is waiting for someone else");
        return cat_false;
    }

    return cat_true;
}

CAT_API cat_data_t *cat_coroutine_jump(cat_coroutine_t *coroutine, cat_data_t *data)
{
    cat_coroutine_t *current_coroutine = CAT_COROUTINE_G(current);

    cat_debug(COROUTINE, "Jump to R" CAT_COROUTINE_ID_FMT, coroutine->id);
    /* update from */
    coroutine->from = current_coroutine;
    /* solve origin */
    if (current_coroutine->previous == coroutine) {
        /* if it is yield, break the origin */
        current_coroutine->previous = NULL;
    } else {
        /* it is not yield */
        CAT_ASSERT(coroutine->previous == NULL);
        /* current becomes target's origin */
        coroutine->previous = current_coroutine;
    }
    /* swap ptr */
    CAT_COROUTINE_G(current) = coroutine;
    /* update state */
    if (current_coroutine->state == CAT_COROUTINE_STATE_RUNNING) {
        /* maybe finished or locked */
        current_coroutine->state = CAT_COROUTINE_STATE_WAITING;
    }
    coroutine->state = CAT_COROUTINE_STATE_RUNNING;
    /* reset the opcode */
    coroutine->opcodes = CAT_COROUTINE_OPCODE_NONE;
    /* round++ */
    coroutine->round = ++CAT_COROUTINE_G(round);
    /* resume */
#ifdef CAT_COROUTINE_USE_UCONTEXT
    coroutine->transfer_data = data;
    cat_coroutine_context_jump(&current_coroutine->context, &coroutine->context);
#else
    cat_coroutine_transfer_t transfer = cat_coroutine_context_jump(coroutine->context, data);
#endif
    coroutine = current_coroutine->from;
    CAT_ASSERT(coroutine != NULL);
    if (unlikely(coroutine->state == CAT_COROUTINE_STATE_FINISHED)) {
        if (!(coroutine->flags & CAT_COROUTINE_FLAG_MANUAL_CLOSE)) {
            cat_coroutine_close(coroutine);
        }
        /* we can not set `from` to NULL here, because third-party wrapper may use it later */
    }
#ifndef CAT_COROUTINE_USE_UCONTEXT
    else {
        /* update context */
        current_coroutine->from->context = transfer.from_context;
    }

    return transfer.data;
#else
    return current_coroutine->transfer_data;
#endif
}

CAT_API cat_bool_t cat_coroutine_resume_standard(cat_coroutine_t *coroutine, cat_data_t *data, cat_data_t **retval)
{
    if (likely(!(coroutine->opcodes & CAT_COROUTINE_OPCODE_CHECKED))) {
        if (unlikely(!cat_coroutine_jump_precheck(coroutine))) {
            return cat_false;
        }
    }

    data = cat_coroutine_jump(coroutine, data);

    if (retval != NULL) {
        *retval = data;
    } else {
        CAT_ASSERT(data == NULL && "Unexpected non-empty data, resource may leak");
    }

    return cat_true;
}

CAT_API cat_bool_t cat_coroutine_yield(cat_data_t *data, cat_data_t **retval)
{
    cat_coroutine_t *coroutine = CAT_COROUTINE_G(current)->previous;
    cat_bool_t ret;

    if (unlikely(coroutine == NULL)) {
        cat_update_last_error(CAT_EMISUSE, "Coroutine has nowhere to go");
        return cat_false;
    }

    ret = cat_coroutine_resume(coroutine, data, retval);

    CAT_ASSERT(ret);

    return ret;
}

/* properties */
CAT_API cat_coroutine_id_t cat_coroutine_get_id(const cat_coroutine_t *coroutine)
{
    return coroutine->id;
}

CAT_API cat_coroutine_t *cat_coroutine_get_from(const cat_coroutine_t *coroutine)
{
    return coroutine->from;
}

CAT_API cat_coroutine_t *cat_coroutine_get_previous(const cat_coroutine_t *coroutine)
{
    return coroutine->previous;
}

CAT_API cat_coroutine_stack_size_t cat_coroutine_get_stack_size(const cat_coroutine_t *coroutine)
{
    return coroutine->stack_size;
}

/* status */

CAT_API cat_bool_t cat_coroutine_is_available(const cat_coroutine_t *coroutine)
{
    return coroutine->state < CAT_COROUTINE_STATE_FINISHED && coroutine->state > CAT_COROUTINE_STATE_INIT;
}

CAT_API cat_bool_t cat_coroutine_is_alive(const cat_coroutine_t *coroutine)
{
    return coroutine->state < CAT_COROUTINE_STATE_FINISHED && coroutine->state > CAT_COROUTINE_STATE_READY;
}

CAT_API const char *cat_coroutine_state_name(cat_coroutine_state_t state)
{
    switch (state) {
#define CAT_COROUTINE_STATE_NAME_GEN(name, unused, value) case CAT_COROUTINE_STATE_##name: return value;
    CAT_COROUTINE_STATE_MAP(CAT_COROUTINE_STATE_NAME_GEN)
#undef CAT_COROUTINE_STATE_NAME_GEN
    }
    CAT_NEVER_HERE(COROUTINE, "Unknown state %d", state);
}

CAT_API cat_coroutine_state_t cat_coroutine_get_state(const cat_coroutine_t *coroutine)
{
    return coroutine->state;
}

CAT_API const char *cat_coroutine_get_state_name(const cat_coroutine_t *coroutine)
{
    return cat_coroutine_state_name(coroutine->state);
}

CAT_API cat_coroutine_opcodes_t cat_coroutine_get_opcodes(const cat_coroutine_t *coroutine)
{
    return coroutine->opcodes;
}

CAT_API void cat_coroutine_set_opcodes(cat_coroutine_t *coroutine, cat_coroutine_opcodes_t opcodes)
{
    coroutine->opcodes = opcodes;
}

CAT_API cat_coroutine_round_t cat_coroutine_get_round(const cat_coroutine_t *coroutine)
{
    return coroutine->round;
}

CAT_API cat_msec_t cat_coroutine_get_start_time(const cat_coroutine_t *coroutine)
{
    return coroutine->start_time;
}

CAT_API cat_msec_t cat_coroutine_get_elapsed(const cat_coroutine_t *coroutine)
{
    if (unlikely(coroutine->state < CAT_COROUTINE_STATE_RUNNING)) {
        return 0;
    }
    return cat_time_msec() - coroutine->start_time;
}

CAT_API char *cat_coroutine_get_elapsed_as_string(const cat_coroutine_t *coroutine)
{
    return cat_time_format_msec(cat_coroutine_get_elapsed(coroutine));
}

/* scheduler */

CAT_API cat_bool_t cat_coroutine_register_scheduler(cat_coroutine_t *coroutine)
{
    if (CAT_COROUTINE_G(scheduler) != NULL) {
        cat_update_last_error(CAT_EMISUSE, "Only one scheduler coroutine is allowed in the same thread");
        return cat_false;
    }
    coroutine->flags |= CAT_COROUTINE_FLAG_SCHEDULER;
    CAT_COROUTINE_G(scheduler) = coroutine;
    CAT_COROUTINE_G(count)--;

    return cat_true;
}

CAT_API cat_coroutine_t *cat_coroutine_unregister_scheduler(void)
{
    cat_coroutine_t *scheduler = CAT_COROUTINE_G(scheduler);

    if (scheduler == NULL) {
        cat_update_last_error(CAT_EMISUSE, "No scheduler is available");
        return NULL;
    }
    CAT_COROUTINE_G(count)++;
    CAT_COROUTINE_G(scheduler) = NULL;
    scheduler->flags ^= CAT_COROUTINE_FLAG_SCHEDULER;

    return scheduler;
}

CAT_API cat_bool_t cat_coroutine_scheduler_run(cat_coroutine_t *scheduler)
{
    if (!cat_coroutine_resume(scheduler, NULL, NULL)) {
        cat_update_last_error_with_previous("Resume schedule failed");
        return cat_false;
    }
    if (!cat_coroutine_is_alive(scheduler)) {
        cat_update_last_error(CAT_UNKNOWN, "Run scheduler failed by unknwon reason");
        return cat_false;
    }
    if (!cat_coroutine_register_scheduler(scheduler)) {
        cat_update_last_error_with_previous("Register scheduler failed");
        return cat_false;
    }

    return cat_true;
}

CAT_API cat_coroutine_t *cat_coroutine_scheduler_stop(void)
{
    cat_coroutine_t *scheduler = cat_coroutine_unregister_scheduler();
    cat_bool_t manual_close; /* if not, scheduler will be inaccessible after exchange */

    if (scheduler == NULL) {
        cat_update_last_error_with_previous("Unregister scheduler failed");
        return NULL;
    }
    if (!cat_coroutine_is_alive(scheduler)) {
        cat_update_last_error(CAT_EMISUSE, "Scheduler is not running");
        return NULL;
    }
    manual_close = !!(scheduler->flags & CAT_COROUTINE_FLAG_MANUAL_CLOSE);
    if (cat_coroutine_exchange_with_previous() != scheduler) {
        cat_update_last_error_with_previous("Exchange scheduler failed");
        return NULL;
    }
    if (manual_close && cat_coroutine_is_available(scheduler)) {
        cat_update_last_error(CAT_UNKNOWN, "Unexpected status of scheduler (still avilable)");
        return NULL;
    }

    return scheduler;
}

/* special */

CAT_API void cat_coroutine_disable_auto_close(cat_coroutine_t *coroutine)
{
    coroutine->flags |= CAT_COROUTINE_FLAG_MANUAL_CLOSE;
}

CAT_API cat_coroutine_t *cat_coroutine_exchange_with_previous(void)
{
    cat_coroutine_t *current_coroutine = CAT_COROUTINE_G(current);
    cat_coroutine_t *previous_coroutine = current_coroutine->previous;

    if (previous_coroutine == NULL) {
        cat_update_last_error(CAT_EMISUSE, "Exchange failed: no coroutine to exchange");
        return NULL;
    }
    /* exchange scheduling order */
    current_coroutine->previous = previous_coroutine->previous;
    /* make previous dangly  */
    previous_coroutine->previous = NULL;
    /* resume to previous (to be the root) */
    if (!cat_coroutine_resume(previous_coroutine, NULL, NULL)) {
        cat_update_last_error_with_previous("Exchange failed: resume failed");
        return NULL;
    }

    return previous_coroutine;
}

CAT_API cat_bool_t cat_coroutine_wait_for(cat_coroutine_t *who)
{
    cat_coroutine_t *current_coroutine = CAT_COROUTINE_G(current);

    current_coroutine->opcodes |= CAT_COROUTINE_OPCODE_WAIT;
    current_coroutine->waiter.coroutine = who;
    if (unlikely(!cat_coroutine_yield(NULL, NULL))) {
        current_coroutine->opcodes ^= CAT_COROUTINE_OPCODE_WAIT;
        return cat_false;
    }

    return cat_true;
}

CAT_API cat_bool_t cat_coroutine_is_locked(cat_coroutine_t *coroutine)
{
    return coroutine->state == CAT_COROUTINE_STATE_LOCKED;
}

CAT_API void cat_coroutine_lock(void)
{
    cat_coroutine_t *coroutine = CAT_COROUTINE_G(current);
    coroutine->state = CAT_COROUTINE_STATE_LOCKED;
    CAT_COROUTINE_G(count)--;
    if (!cat_coroutine_yield(NULL, NULL)) {
        cat_core_error_with_last(COROUTINE, "Lock coroutine failed");
    }
    CAT_COROUTINE_G(count)++;
}

CAT_API void cat_coroutine_unlock(cat_coroutine_t *coroutine)
{
    if (coroutine->state != CAT_COROUTINE_STATE_LOCKED) {
        cat_core_error(COROUTINE, "Unlock an unlocked coroutine");
    }
    coroutine->state = CAT_COROUTINE_STATE_WAITING;
    if (!cat_coroutine_resume(coroutine, NULL, NULL)) {
        cat_core_error_with_last(COROUTINE, "Unlock coroutine failed");
    }
}

/* helper */

CAT_API cat_coroutine_t *cat_coroutine_run(cat_coroutine_t *coroutine, cat_coroutine_function_t function, cat_data_t *data)
{
    cat_bool_t ret;

    coroutine = cat_coroutine_create(NULL, function);

    if (unlikely(coroutine == NULL)) {
        return NULL;
    }

    ret = cat_coroutine_resume(coroutine, data, NULL);

    if (unlikely(!ret)) {
        cat_coroutine_close(coroutine);
        return NULL;
    }

    return coroutine;
}
