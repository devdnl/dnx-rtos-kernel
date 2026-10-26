/*==============================================================================
@file    khooks.c

@author  Daniel Zorychta

@brief   This file support all kernel hooks

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

#pragma GCC optimize("O3")

/*==============================================================================
  Include files
==============================================================================*/
#include "kernel/khooks.h"
#include "config.h"
#include "cpu/cpuctl.h"
#include "kernel/kpanic.h"
#include "kernel/printk.h"
#include "kernel/process.h"
#include "lib/misc.h"
#include "lib/unarg.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/

/*==============================================================================
  Local types, enums definitions
==============================================================================*/

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  Local object definitions
==============================================================================*/
static u32_t sec_divider;
static u64_t sanity_check_tref;

#if __OS_ENABLE_SYS_ASSERT__ > 0
static bool assert_hook_suspend;
#endif

static StaticTask_t xIdleTaskTCB[_CPUCTL_CORE_NUM];
static StackType_t uxIdleTaskStack[_CPUCTL_CORE_NUM][_STACK_DEPTH_MINIMAL];

/*==============================================================================
  Exported object definitions
==============================================================================*/
u32_t _uptime_counter_sec = 0;
u64_t _tick_counter = 0;
extern CPU_load_t CPU_load[_CPUCTL_CORE_NUM];

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Application Idle hook (idle task - don't use loop)
 */
//==============================================================================
void vApplicationIdleHook(void)
{
        u64_t now = _kernel_get_uptime_ms();
        if (now - sanity_check_tref >= 1000) {

                sanity_check_tref = now;

                if (!_mm_check_consistency(false)) {
                        _printk("Inconsistent heap data!");
                }

                if (!_process_is_consistent(false)) {
                        _printk("Inconsistent process data!");
                }
        }

/*
 * Sleep CPU for single tick to save energy.
 */
#if (__OS_SLEEP_ON_IDLE__ > 0)
        _cpuctl_sleep();
#endif
}

//==============================================================================
/**
 * @brief  Passive idle hook.
 */
//==============================================================================
void vApplicationPassiveIdleHook(void)
{
        __asm volatile("nop");
}

//==============================================================================
/**
 * @brief Stack overflow hook
 */
//==============================================================================
void vApplicationStackOverflowHook(TaskHandle_t taskHdl, char *taskName)
{
        UNUSED_ARG2(taskHdl, taskName);
        _kernel_panic_report_from_ISR(_KERNEL_PANIC_DESC_CAUSE_STACKOVF);
}

//==============================================================================
/**
 * @brief Hook when system tick was increased
 */
//==============================================================================
void vApplicationTickHook(void)
{
        _tick_counter++;

#if (__OS_MONITOR_CPU_LOAD__ > 0)
        u8_t cpu_id = _cpuctl_get_cpu_id();
        CPU_load[cpu_id].CPU_total_time += _cpuctl_get_CPU_load_counter_delta();
#endif

        if (++sec_divider >= configTICK_RATE_HZ) {
                sec_divider = 0;
                _uptime_counter_sec++;
                _calculate_CPU_load_from_IRQ();
        }
}

//==============================================================================
/**
 * @brief Memory for idle task.
 */
//==============================================================================
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer,
        uint32_t *pulIdleTaskStackSize)
{
        /* Pass out a pointer to the StaticTask_t structure in which the Idle task's
        state will be stored. */
        *ppxIdleTaskTCBBuffer = &xIdleTaskTCB[0];

        /* Pass out the array that will be used as the Idle task's stack. */
        *ppxIdleTaskStackBuffer = uxIdleTaskStack[0];

        /* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
        Note that, as the array is necessarily of type StackType_t,
        configMINIMAL_STACK_SIZE is specified in words, not bytes. */
        *pulIdleTaskStackSize = ARRAY_SIZE(uxIdleTaskStack[0]);
}

//==============================================================================
/**
 * @brief
 *
 * @param  ?
 *
 * @return ?
 */
//==============================================================================
void vApplicationGetPassiveIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer,
        uint32_t *pulIdleTaskStackSize, BaseType_t xPassiveIdleTaskIndex)
{
        BaseType_t cpu_id = xPassiveIdleTaskIndex + 1;

        /* Pass out a pointer to the StaticTask_t structure in which the Idle task's
        state will be stored. */
        *ppxIdleTaskTCBBuffer = &xIdleTaskTCB[cpu_id];

        /* Pass out the array that will be used as the Idle task's stack. */
        *ppxIdleTaskStackBuffer = uxIdleTaskStack[cpu_id];

        /* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
        Note that, as the array is necessarily of type StackType_t,
        configMINIMAL_STACK_SIZE is specified in words, not bytes. */
        *pulIdleTaskStackSize = ARRAY_SIZE(uxIdleTaskStack[cpu_id]);
}

//==============================================================================
/**
 * @brief Function return uptime counter in seconds
 *
 * @return uptime counter
 */
//==============================================================================
u32_t _get_uptime_counter(void)
{
        return _uptime_counter_sec;
}

#if __OS_ENABLE_SYS_ASSERT__ > 0
//==============================================================================
/**
 * @brief  Function is called when assertion is not meet.
 *
 * @param  assert       if true then no action
 * @param  msg          message
 */
//==============================================================================
void _assert_hook(bool assert, const char *msg)
{
        if (!assert) {
                if (!assert_hook_suspend) {
                        if (msg) {
                                _printk("System assert: %s", msg);
                        } else {
                                _printk("System assert occurred!");
                        }
                }
        }
}

//==============================================================================
/**
 * @brief  Function enable/disable assert hook.
 *
 * @param  suspend              suspend
 */
//==============================================================================
void _assert_hook_suspend(bool suspend)
{
        assert_hook_suspend = suspend;
}
#endif

/*==============================================================================
  End of file
==============================================================================*/
