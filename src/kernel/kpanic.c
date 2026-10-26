/*==============================================================================
@file    kpanic.c

@author  Daniel Zorychta

@brief   Kernel panic handling

@note    Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

         This program is free software; you can redistribute it and/or modify
         it under the terms of the GNU General Public License as published by
         the Free Software Foundation and modified by the dnx RTOS exception.

         NOTE: The modification  to the GPL is  included to allow you to
               distribute a combined work that includes dnx RTOS without
               being obliged to provide the source  code for proprietary
               components outside of the dnx RTOS.

         The dnx RTOS  is  distributed  in the hope  that  it will be useful,
         but WITHOUT  ANY  WARRANTY;  without  even  the implied  warranty of
         MERCHANTABILITY  or  FITNESS  FOR  A  PARTICULAR  PURPOSE.  See  the
         GNU General Public License for more details.

         Full license text is available on the following file: doc/license.txt.


==============================================================================*/

/*==============================================================================
  Include files
==============================================================================*/
#include "kernel/kpanic.h"
#include "config.h"
#include "kernel/khooks.h"
#include "kernel/kwrapper.h"
#include "kernel/printk.h"
#include "kernel/process.h"
#include "kernel/sysfunc.h"
#include "lib/cast.h"
#include "lib/crc32.h"
#include "lib/vt100.h"
#include "mm/mm.h"
#include <string.h>

/*==============================================================================
  Local macros
==============================================================================*/
#define _KERNEL_PANIC_DESC_VALID1 0x7C63EE8E
#define PANIC_LOOP_COUNT          1000
#define KILL_TIMEOUT_MS           1000

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        uint32_t valid1; /* validation value 1 */
        uint32_t CRC32;  /* CRC32 */

        struct {
                char name[64];
                u32_t flags;
                enum _kernel_panic_desc_cause cause;
                pid_t pid;
                tid_t tid;
                u8_t syscall;
                u8_t core;
        } content;
} kernel_panic_desc;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static void catch_process(enum _kernel_panic_desc_cause suggested_cause, bool isr, u32_t exc_num);
static bool report_kernel_panic(_process_t *proc, enum _kernel_panic_desc_cause cause, u32_t exc_num);
static void restart_system(void);
static const char *get_cause_str(enum _kernel_panic_desc_cause cause);
static void app_crash_handler(void *arg);

/*==============================================================================
  Local objects
==============================================================================*/
static kernel_panic_desc kpanic_desc __attribute__((section(".noinit")));
static u32_t kpanic_ctr;
static _process_t *kpanic_trap_proc;
static task_t *kpanic_app_crash_task;
static ksem_t *kpanic_sem;

static const char *CAUSE[] = {
        [_KERNEL_PANIC_DESC_CAUSE_SEGFAULT] = "SEGFAULT",
        [_KERNEL_PANIC_DESC_CAUSE_STACKOVF] = "STACKOVF",
        [_KERNEL_PANIC_DESC_CAUSE_CPUFAULT_1] = "CPUFAULT1",
        [_KERNEL_PANIC_DESC_CAUSE_CPUFAULT_2] = "CPUFAULT2",
        [_KERNEL_PANIC_DESC_CAUSE_CPUFAULT_3] = "CPUFAULT3",
        [_KERNEL_PANIC_DESC_CAUSE_CPUFAULT_4] = "CPUFAULT4",
        [_KERNEL_PANIC_DESC_CAUSE_INTERNAL_1] = "INTERNAL1",
        [_KERNEL_PANIC_DESC_CAUSE_INTERNAL_2] = "INTERNAL2",
        [_KERNEL_PANIC_DESC_CAUSE_INTERNAL_3] = "INTERNAL3",
        [_KERNEL_PANIC_DESC_CAUSE_INTERNAL_4] = "INTERNAL4",
        [_KERNEL_PANIC_DESC_CAUSE_UNKNOWN] = "UNKNOWN",
};

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  External objects
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Initialize kernel panic handler.
 * @param  None
 * @return One of errno value.
 */
//==============================================================================
int _kernel_panic_init(void)
{
        int err = _semaphore_create(1, 0, &kpanic_sem);
        if (err) {
                kpanic_sem = NULL;
                return err;
        }

        err = _task_create(app_crash_handler, "", _STACK_DEPTH_LOW, NULL, &kpanic_app_crash_task);
        if (!err) {
                _task_set_priority(kpanic_app_crash_task, _PRIORITY_REALTIME);
        } else {
                kpanic_app_crash_task = NULL;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function check if the kernel panic occurred in the last session
 *
 * @param  info         kernel panic info
 *
 * @return If kernel panic occurred in the last session then true is returned,
 *         otherwise false.
 */
//==============================================================================
bool _kernel_panic_info(struct _kernel_panic_info *info)
{
        bool occurred = (kpanic_desc.valid1 == _KERNEL_PANIC_DESC_VALID1);

        if (occurred) {
                u32_t CRC = calc_crc32(&kpanic_desc.content, sizeof(kpanic_desc.content));
                occurred = (CRC == kpanic_desc.CRC32);
        }

        if (occurred) {
#if (__OS_SYSTEM_MSG_ENABLE__ > 0)
                const char *panic_type;

                if (kpanic_desc.content.flags & _KERNEL_PANIC_FLAG__KERNEL_PANIC) {
                        panic_type = "KERNEL PANIC";
                } else {
                        panic_type = "APP CRASH";
                }

                printk("%s: '%s' [%u:%d:%d,%s,%s,%u]", panic_type, kpanic_desc.content.name, kpanic_desc.content.core,
                        kpanic_desc.content.pid, kpanic_desc.content.tid, get_cause_str(kpanic_desc.content.cause),
                        (kpanic_desc.content.flags & _KERNEL_PANIC_FLAG__KERNEL_SPACE) ? "KRN" : "USR",
                        kpanic_desc.content.syscall);

                _cpuctl_print_exception();
#endif

                if (info) {
                        info->cause = kpanic_desc.content.cause;

                        info->cause_str = get_cause_str(kpanic_desc.content.cause);

                        sys_strlcpy(info->name, kpanic_desc.content.name, sizeof(info->name));

                        info->flags = kpanic_desc.content.flags;
                        info->pid = kpanic_desc.content.pid;
                        info->tid = kpanic_desc.content.tid;
                        info->syscall = kpanic_desc.content.syscall;
                        info->core = kpanic_desc.content.core;
                }

                kpanic_desc.valid1 = 0;
                kpanic_desc.CRC32 = 0;
        }

        return occurred;
}

//==============================================================================
/**
 * @brief  Function report kernel panic
 *
 * @param  suggest_cause        suggested cause
 */
//==============================================================================
void _kernel_panic_report(enum _kernel_panic_desc_cause suggested_cause)
{
        /*
         * NOTE: critical section is used instead of _ISR_disable() because
         *       interrupts disabled directly are enabled again by the first
         *       FreeRTOS critical section exit (e.g. in _semaphore_signal()).
         */
        _critical_section_begin();
        catch_process(suggested_cause, false, 0);
        _critical_section_end();
}

//==============================================================================
/**
 * @brief  Function report kernel panic
 *
 * @param  suggest_cause        suggested cause
 */
//==============================================================================
void _kernel_panic_report_from_ISR(enum _kernel_panic_desc_cause suggested_cause)
{
        _kernel_panic_report_from_exception(suggested_cause, 0);
}

//==============================================================================
/**
 * @brief  Function report kernel panic from CPU exception handler.
 *
 * @param  suggest_cause        suggested cause
 * @param  exc_num              number of exception active when fault occurred
 *                              (0: thread mode). Fault in exception context
 *                              cannot be assigned to any process.
 */
//==============================================================================
void _kernel_panic_report_from_exception(enum _kernel_panic_desc_cause suggested_cause, u32_t exc_num)
{
        catch_process(suggested_cause, true, exc_num);

        if (suggested_cause != _KERNEL_PANIC_DESC_CAUSE_STACKOVF) {
                _task_yield_from_ISR(true);
        }
}

//==============================================================================
/**
 * @brief  Function catch current process.
 *
 * @param  suggest_cause        suggested cause
 * @param  isr                  from ISR
 * @param  exc_num              number of exception active when fault occurred
 */
//==============================================================================
static void catch_process(enum _kernel_panic_desc_cause suggested_cause, bool isr, u32_t exc_num)
{
        _process_t *proc = _process_get_active();

        /*
         * NOTE: all checks are done without locks (ISR variants) because
         *       the kernel state can be inconsistent and the function is
         *       called in the critical section or in the exception handler.
         */
        bool is_process = report_kernel_panic(proc, suggested_cause, exc_num);

        bool restart = (kpanic_desc.content.flags & _KERNEL_PANIC_FLAG__PANIC_LOOP);

        restart = restart or not _mm_check_consistency(true);
        restart = restart or not _process_is_consistent(true);

        // ISR, kernel task, idle task or crash handler cannot be killed
        restart = restart or not is_process;

        /*
         * Crash in the kernel space (syscall) leaves kernel objects in unknown
         * state (locked mutexes, operation counters, references). Thread
         * cannot exit the syscall and process cannot be terminated and its
         * resources cannot be released safely.
         */
        restart = restart or (kpanic_desc.content.flags & _KERNEL_PANIC_FLAG__KERNEL_SPACE);
        restart = restart or (proc == _kworker_proc);

        // crash handler not available
        restart = restart or (kpanic_app_crash_task == NULL) or (kpanic_sem == NULL);

        // previous crash of other process is not handled yet
        restart = restart or (kpanic_trap_proc && (kpanic_trap_proc != proc));

        if (restart) {
                restart_system();

        } else {
                kpanic_trap_proc = proc;

                /*
                 * Switch exception handing to special task with the highest
                 * priority. Semaphore is used instead of task suspend/resume
                 * to not lose the event when handler is still running.
                 */
                if (isr) {
                        _semaphore_signal_from_ISR(kpanic_sem, NULL);
                } else {
                        _semaphore_signal(kpanic_sem);
                }
        }
}

//==============================================================================
/**
 * @brief  Mark report as kernel panic and restart system.
 */
//==============================================================================
static void restart_system(void)
{
        kpanic_desc.content.flags |= _KERNEL_PANIC_FLAG__KERNEL_PANIC;
        kpanic_desc.CRC32 = calc_crc32(&kpanic_desc.content, sizeof(kpanic_desc.content));
        _cpuctl_restart_system();
}

//==============================================================================
/**
 * @brief  Report kernel panic/application crash.
 *
 * @param  proc         process
 * @param  cause        kernel panic/application crash cause
 * @param  exc_num      number of exception active when fault occurred
 *
 * @return True if crash occurred in valid process, false otherwise.
 */
//==============================================================================
static bool report_kernel_panic(_process_t *proc, enum _kernel_panic_desc_cause cause, u32_t exc_num)
{
        bool is_process = false;

        kpanic_desc.content.cause = cause;
        kpanic_desc.valid1 = _KERNEL_PANIC_DESC_VALID1;

        kpanic_desc.content.flags = (++kpanic_ctr >= PANIC_LOOP_COUNT) ? _KERNEL_PANIC_FLAG__PANIC_LOOP : 0;
        kpanic_desc.content.core = _cpuctl_get_cpu_id();

        if (exc_num != 0) {
                sys_scnprintf(kpanic_desc.content.name, sizeof(kpanic_desc.content.name), "<ISR> %u", exc_num);

                kpanic_desc.content.pid = 0;
                kpanic_desc.content.tid = 0;
                kpanic_desc.content.syscall = 0xFF;
                kpanic_desc.content.flags |= _KERNEL_PANIC_FLAG__KERNEL_SPACE;

        } else if (_process_get_pid(proc, &kpanic_desc.content.pid) == 0) {

                is_process = true;

                sys_scnprintf(kpanic_desc.content.name, sizeof(kpanic_desc.content.name), "%s",
                        _process_get_name(proc));

                kpanic_desc.content.tid = _process_get_active_thread(proc, true);

                kpanic_desc.content.flags |= _process_is_kernelspace(proc, kpanic_desc.content.tid, true)
                        ? _KERNEL_PANIC_FLAG__KERNEL_SPACE
                        : 0;

                kpanic_desc.content.syscall = _process_get_curr_syscall(proc, kpanic_desc.content.tid, true);

        } else {
                task_t *task = _task_get_handle();

                sys_scnprintf(kpanic_desc.content.name, sizeof(kpanic_desc.content.name), "<task> %p", task);

                kpanic_desc.content.pid = 0;
                kpanic_desc.content.tid = 0;
                kpanic_desc.content.syscall = 0xFF;
                kpanic_desc.content.flags |= _KERNEL_PANIC_FLAG__KERNEL_SPACE;
        }

        kpanic_desc.CRC32 = calc_crc32(&kpanic_desc.content, sizeof(kpanic_desc.content));

        return is_process;
}

//==============================================================================
/**
 * @brief  Return cause string.
 *
 * @param  cause        kernel panic/application crash cause
 *
 * @return Cause string.
 */
//==============================================================================
static const char *get_cause_str(enum _kernel_panic_desc_cause cause)
{
        if ((cast(size_t, cause) < ARRAY_SIZE(CAUSE)) && CAUSE[cause]) {
                return CAUSE[cause];
        } else {
                return CAUSE[_KERNEL_PANIC_DESC_CAUSE_UNKNOWN];
        }
}

//==============================================================================
/**
 * @brief  Application crash handler.
 *
 * @param  arg          task's argument
 */
//==============================================================================
static void app_crash_handler(void *arg)
{
        UNUSED_ARG1(arg);

        while (true) {
                if (_semaphore_wait(kpanic_sem, _WAIT_FOREVER_MS) != ESUCC) {
                        continue;
                }

                _critical_section_begin();
                _process_t *proc = kpanic_trap_proc;
                pid_t pid = kpanic_desc.content.pid;
                tid_t tid = kpanic_desc.content.tid;
                _critical_section_end();

                if (not proc) {
                        continue;
                }

                /*
                 * NOTE: only crashed thread is suspended (it runs infinite loop).
                 *       Other threads can wait in syscalls, they are canceled
                 *       by kill request and parked at kernel space boundary.
                 *       Suspended thread in syscall would block process
                 *       termination forever.
                 */
                if ((tid < __OS_PROCESS_MAX_THREADS__) && proc->taskdata[tid].task) {
                        _task_suspend(proc->taskdata[tid].task);
                }

                _printk("*** APP CRASH: '%s' [%u:%d:%d,%s,%s,%u] ***", _process_get_name(proc),
                        kpanic_desc.content.core, pid, kpanic_desc.content.tid,
                        get_cause_str(kpanic_desc.content.cause),
                        (kpanic_desc.content.flags & _KERNEL_PANIC_FLAG__KERNEL_SPACE) ? "KRN" : "USR",
                        kpanic_desc.content.syscall);

                _cpuctl_print_exception();

                /*
                 * NOTE: process list should not be locked by crashed thread
                 *       (crash in the kernel space restarts system), timeout
                 *       is a protection against unexpected kernel state.
                 */
                if (_process_kill_timeout(pid, KILL_TIMEOUT_MS) == ETIME) {
                        _critical_section_begin();
                        restart_system();
                }

                _critical_section_begin();
                kpanic_desc.CRC32 = 0;
                kpanic_trap_proc = NULL;
                kpanic_ctr = 0;
                _critical_section_end();
        }
}

/*==============================================================================
  End of file
==============================================================================*/
