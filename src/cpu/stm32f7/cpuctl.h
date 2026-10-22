/*==============================================================================
@file    cpuctl.h

@author  Daniel Zorychta

@brief   This file support CPU control

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

#ifndef _CPUCTL_H_
#define _CPUCTL_H_

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include "config.h"
#include "lib/sys/types.h"

/*==============================================================================
  Exported symbolic constants/macros
==============================================================================*/
/* CPU/platform name */
#define _CPUCTL_PLATFORM_NAME "ARM Cortex-M7F STM32F7xx"
#define _CPUCTL_VENDOR_NAME   "STMicroelectronics"
#define _CPUCTL_BYTE_ORDER    _BYTE_ORDER_LITTLE_ENDIAN

/* renames of interrupts vectors for kernel purposes */
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
#define vPortSVCHandler     SVC_Handler

/* names of memory regions used by kernel */
#define _CPUCTL_STACK_REGION_FLAGS _MM_FLAG__DMA | _MM_FLAG__CACHE

/* CACHE macros */
#define _CPUCTL_CACHE_ALIGN 32

/* Number of cores */
#define _CPUCTL_CORE_NUM 1

/*==============================================================================
  Exported types, enums definitions
==============================================================================*/
enum {
        low_power_mode__sleep = 0,
        low_power_mode__stop_reg_on = 1,
        low_power_mode__stop_reg_off = 2,
        low_power_mode__standby = 3,
};

/*==============================================================================
  Exported objects
==============================================================================*/
extern const char *_cpuctl_preferred_APP_RAM[];
extern const char *_cpuctl_preferred_FS_RAM[];
extern const char *_cpuctl_fast_mem[];

/*==============================================================================
  Exported function prototypes
==============================================================================*/
extern void _cpuctl_init(void);
extern void _cpuctl_restart_system(void);
extern void _cpuctl_shutdown_system(void);
extern void _cpuctl_sleep(void);
extern void _cpuctl_update_system_clocks(void);
extern void _cpuctl_delay_us(u16_t);
extern void _cpuctl_print_exception(void);
extern int _cpuctl_low_power_mode(int mode);
extern u8_t _cpuctl_get_cpu_id(void);

#if (__OS_MONITOR_CPU_LOAD__ > 0)
extern void _cpuctl_init_CPU_load_counter(void);
extern u32_t _cpuctl_get_CPU_load_counter_delta(void);
#endif

/* cache management functions */
extern void _cpuctl_clean_dcache(void);
extern void _cpuctl_clean_icache(void);
extern void _cpuctl_clean_invalidate_dcache(void);
extern void _cpuctl_invalidate_dcache(void);
extern void _cpuctl_invalidate_dcache_by_addr(void *addr, u32_t size);
extern void _cpuctl_clean_dcache_by_addr(void *addr, u32_t size);
extern void _cpuctl_clean_invalidate_dcache_by_addr(void *addr, u32_t size);

#ifdef __cplusplus
}
#endif

#endif /* _CPUCTL_H_ */
/*==============================================================================
  End of file
==============================================================================*/
