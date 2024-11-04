/*=========================================================================*//**
@file    kpanic.c

@author  Daniel Zorychta

@brief   Kernel panic handling

@note    Copyright (C) 2024 Daniel Zorychta <daniel.zorychta@gmail.com>

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


*//*==========================================================================*/

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

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        uint32_t valid1; /* validation value 1 */
        uint32_t CRC32;  /* CRC32 */

        struct {
                const char                   *name;        /* process name       */
                enum _kernel_panic_desc_cause cause;       /* kernel panic cause */
                pid_t                         pid;         /* process ID         */
                tid_t                         tid;         /* thread ID          */
                bool                          kernelspace; /* kernel space       */
                u8_t                          syscall;     /* syscall            */
        } content;
} kernel_panic_desc;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static void catch_process(enum _kernel_panic_desc_cause suggested_cause, bool isr);
static void report_kernel_panic(_process_t *proc, enum _kernel_panic_desc_cause cause, bool isr);
static void app_crash_handler(void *arg);

/*==============================================================================
  Local objects
==============================================================================*/
static kernel_panic_desc kpanic_desc __attribute__((section(".noinit")));
static u32_t             kpanic_ctr;
static _process_t       *kpanic_trap_proc;
static task_t           *kpanic_app_crash_task;

#if ((__OS_SYSTEM_MSG_ENABLE__ > 0) && (__OS_PRINTF_ENABLE__ > 0))
static const char *CAUSE[] = {
        "SEGFAULT",
        "STACKOVF",
        "CPUFAULT",
        "INTERNAL1",
        "INTERNAL2",
        "INTERNAL3",
        "INTERNAL4",
        "PANICLOOP",
        "UNKNOWN",
};
#endif

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
        _task_create(app_crash_handler, "", _STACK_DEPTH_LOW, NULL, &kpanic_app_crash_task);
        _task_set_priority(kpanic_app_crash_task, _PRIORITY_REALTIME);

        return 0;
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
                occurred  = (CRC == kpanic_desc.CRC32);
        }

        if (occurred) {
#if ((__OS_SYSTEM_MSG_ENABLE__ > 0) && (__OS_PRINTF_ENABLE__ > 0))
                const char *panic_type;

                if ((strncmp(kpanic_desc.content.name, "kworker", 128) == 0) ||
                        (kpanic_desc.content.cause >= _KERNEL_PANIC_DESC_CAUSE_CPUFAULT)) {
                        panic_type = "KERNEL PANIC";
                } else {
                        panic_type = "APP CRASH";
                }

                printk(VT100_FONT_COLOR_RED "%s: '%s' [%d:%d,%s,%s,%u]" VT100_RESET_ATTRIBUTES, panic_type,
                        kpanic_desc.content.name, kpanic_desc.content.pid, kpanic_desc.content.tid,
                        CAUSE[kpanic_desc.content.cause], kpanic_desc.content.kernelspace ? "KRN" : "USR",
                        kpanic_desc.content.syscall);

                _cpuctl_print_exception();
#endif

                if (info) {
                        info->cause = min(kpanic_desc.content.cause, kpanic_desc.content.cause);

                        info->cause_str = CAUSE[info->cause];

                        info->name = (kpanic_desc.content.name == NULL) ? "<unknown>"
                                                                        : kpanic_desc.content.name;

                        info->kernelspace = kpanic_desc.content.kernelspace;
                        info->pid         = kpanic_desc.content.pid;
                        info->tid         = kpanic_desc.content.tid;
                        info->syscall     = kpanic_desc.content.syscall;
                }

                kpanic_desc.valid1 = 0;
                kpanic_desc.CRC32  = 0;
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
        _ISR_disable();
        catch_process(suggested_cause, false);
        _ISR_enable();
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
        catch_process(suggested_cause, true);

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
 */
//==============================================================================
static void catch_process(enum _kernel_panic_desc_cause suggested_cause, bool isr)
{
        kpanic_trap_proc = _process_get_active();

        report_kernel_panic(kpanic_trap_proc, suggested_cause, isr);

        if (++kpanic_ctr > 1000) {
                report_kernel_panic(kpanic_trap_proc, _KERNEL_PANIC_DESC_CAUSE_PANICLOOP, isr);
                _cpuctl_restart_system();

        } else if (not _mm_check_consistency(isr)) {
                _cpuctl_restart_system();

        } else if (not _process_is_consistent(isr)) {
                _cpuctl_restart_system();

        } else if (kpanic_trap_proc == _kworker_proc) {
                _cpuctl_restart_system();

        } else {
                /*
                 * Switch exception handing to special task with the highest
                 * priority.
                 */
                if (isr) {
                        _task_resume_from_ISR(kpanic_app_crash_task);
                } else {
                        _task_resume(kpanic_app_crash_task);
                }
        }
}

//==============================================================================
/**
 * @brief  Report kernel panic/application crash.
 *
 * @param  proc         process
 * @param  cause        kernel panic/application crash cause
 * @param  isr          called from ISR
 */
//==============================================================================
static void report_kernel_panic(_process_t *proc, enum _kernel_panic_desc_cause cause, bool isr)
{
        kpanic_desc.content.cause = cause;
        kpanic_desc.valid1        = _KERNEL_PANIC_DESC_VALID1;

        if (not proc) {
                kpanic_desc.content.name        = "<NULL>";
                kpanic_desc.content.pid         = 0;
                kpanic_desc.content.tid         = 0;
                kpanic_desc.content.kernelspace = true;
                kpanic_desc.content.syscall     = 0xFF;

        } else {
                kpanic_desc.content.name = _process_get_name(proc);

                kpanic_desc.content.tid = _process_get_active_thread(proc, isr);

                _process_get_pid(proc, &kpanic_desc.content.pid);

                kpanic_desc.content.kernelspace = _process_is_kernelspace(proc, kpanic_desc.content.tid, isr);

                kpanic_desc.content.syscall = _process_get_curr_syscall(proc, kpanic_desc.content.tid, isr);
        }

        kpanic_desc.CRC32 = calc_crc32(&kpanic_desc.content, sizeof(kpanic_desc.content));
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
                if (not kpanic_trap_proc) {
                        _task_suspend(kpanic_app_crash_task);
                        continue;
                }

                for (size_t i = 0; i < __OS_PROCESS_MAX_THREADS__; i++) {
                        if (kpanic_trap_proc->taskdata[i].task) {
                                _task_suspend(kpanic_trap_proc->taskdata[i].task);
                        }
                }

                _printk("*** APP CRASH: '%s' [%d:%d,%s,%s,%u] ***", _process_get_name(kpanic_trap_proc),
                        kpanic_desc.content.pid, kpanic_desc.content.tid, CAUSE[kpanic_desc.content.cause],
                        kpanic_desc.content.kernelspace ? "KRN" : "USR", kpanic_desc.content.syscall);

                _process_kill(kpanic_desc.content.pid);

                kpanic_desc.CRC32 = 0;
                kpanic_trap_proc  = NULL;
                kpanic_ctr        = 0;
        }
}

/*==============================================================================
  End of file
==============================================================================*/
