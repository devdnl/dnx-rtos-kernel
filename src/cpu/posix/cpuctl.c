/*==============================================================================
@file    cpuctl.c

@author  Daniel Zorychta

@brief   This file support CPU control (POSIX simulator).

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
#include "posix/cpuctl.h"
#include "posix/host/host.h"
#include "config.h"
#include "kernel/kwrapper.h"
#include "kernel/printk.h"
#include "kernel/errno.h"
#include "mm/mm.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/
/*
 * NOTE: Size of boot stack must be the same as POSIX_BOOT_STACK_SIZE - 512
 *       (kernel Makefile). The boot stack is not used by the simulator,
 *       this memory is registered by kworker as additional heap region.
 */
#define BOOT_STACK_SIZE (64 * 1024)

/*==============================================================================
  Local types, enums definitions
==============================================================================*/

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  Local object definitions
==============================================================================*/
static _mm_region_t ram;

/*==============================================================================
  Exported object definitions
==============================================================================*/
const char *_cpuctl_preferred_APP_RAM[] = {__CPU_PREFFERED_APP_RAM__, NULL};
const char *_cpuctl_preferred_FS_RAM[] = {__CPU_PREFFERED_FS_RAM__, NULL};
const char *_cpuctl_fast_mem[] = {__CPU_PREFFERED_FAST_RAM__, NULL};

/* memory used as linker symbols: __ram_start, __stack_start */
u8_t _posix_boot_stack[BOOT_STACK_SIZE] __attribute__((aligned(64)));

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Basic (first) CPU configuration. This function is called before
 *         system start.
 */
//==============================================================================
void _cpuctl_init(void)
{
#if (__OS_MONITOR_CPU_LOAD__ > 0)
        _cpuctl_init_CPU_load_counter();
#endif

        size_t size = 0;
        void *mem = _host_get_ram(&size);

        _mm_register_region(&ram, mem, size, _MM_FLAG__DMA, "SRAM");
}

//==============================================================================
/**
 * @brief  This function restart CPU (simulator is started again).
 */
//==============================================================================
void _cpuctl_restart_system(void)
{
        _host_restart();
}

//==============================================================================
/**
 * @brief  This function shutdown CPU (simulator is terminated).
 */
//==============================================================================
void _cpuctl_shutdown_system(void)
{
        _host_exit(0);
}

//==============================================================================
/**
 * @brief  Start counter used for CPU load measurement.
 */
//==============================================================================
#if (__OS_MONITOR_CPU_LOAD__ > 0)
void _cpuctl_init_CPU_load_counter(void)
{
        _cpuctl_get_CPU_load_counter_delta();
}
#endif

//==============================================================================
/**
 * @brief  Function returns time since last call (CPU load counter) in
 *         microseconds.
 *
 * @return Counter delta.
 */
//==============================================================================
#if (__OS_MONITOR_CPU_LOAD__ > 0)
u32_t _cpuctl_get_CPU_load_counter_delta(void)
{
        static u64_t last;

        u64_t now = _host_get_monotonic_us();
        u32_t delta = (u32_t)(now - last);
        last = now;

        return delta;
}
#endif

//==============================================================================
/**
 * @brief  Function sleep CPU (host thread) until next tick.
 */
//==============================================================================
void _cpuctl_sleep(void)
{
        _host_sleep_us(1000000 / __OS_TASK_SCHED_FREQ__);
}

//==============================================================================
/**
 * @brief  Function set selected low power mode.
 *
 * @param  mode         low power mode
 *
 * @return One of errno value.
 */
//==============================================================================
int _cpuctl_low_power_mode(int mode)
{
        return (mode == low_power_mode__sleep) ? ESUCC : EINVAL;
}

//==============================================================================
/**
 * @brief  Function return CPU ID.
 *
 * @return CPU ID.
 */
//==============================================================================
u8_t _cpuctl_get_cpu_id(void)
{
        return 0;
}

//==============================================================================
/**
 * @brief  Function update system clocks (nothing to do).
 */
//==============================================================================
void _cpuctl_update_system_clocks(void)
{
}

//==============================================================================
/**
 * @brief  Function delay execution by selected microseconds (busy wait).
 *
 * @param  microseconds         number of microseconds
 */
//==============================================================================
void _cpuctl_delay_us(u16_t microseconds)
{
        u64_t end = _host_get_monotonic_us() + microseconds;

        while (_host_get_monotonic_us() < end) {
        }
}

//==============================================================================
/**
 * @brief  Function print exception information.
 */
//==============================================================================
void _cpuctl_print_exception(void)
{
        _printk("Host: %s", _host_get_fault_info());
}

/*==============================================================================
  End of file
==============================================================================*/
