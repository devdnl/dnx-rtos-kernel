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
#if __CPU_FAMILY__ == _RP2040_
#define _CPUCTL_PLATFORM_NAME "ARM Cortex-M0+ RP2040"
#else
#define _CPUCTL_PLATFORM_NAME "Unknown"
#endif
#define _CPUCTL_VENDOR_NAME "Raspberry Pi"
#define _CPUCTL_BYTE_ORDER  _BYTE_ORDER_LITTLE_ENDIAN

/* renames of interrupts vectors for kernel purposes */
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
#define vPortSVCHandler     SVC_Handler

/* names of memory regions used by kernel */
#define _CPUCTL_STACK_REGION_FLAGS _MM_FLAG__DMA

/* cache mangement functions */
#define _cpuctl_clean_dcache()
#define _cpuctl_clean_icache()
#define _cpuctl_clean_invalidate_dcache()
#define _cpuctl_invalidate_dcache()
#define _cpuctl_invalidate_dcache_by_addr(...)
#define _cpuctl_clean_dcache_by_addr(...)
#define _cpuctl_clean_invalidate_dcache_by_addr(...)

/* CACHE macros */
#define _CPUCTL_CACHE_ALIGN 1

/* Number of cores */
#define _CPUCTL_CORE_NUM 2

/*==============================================================================
  Exported types, enums definitions
==============================================================================*/
enum {
        low_power_mode__sleep = 0,
};

typedef enum {
        Reset_IRQn = -15,
        NonMaskableInt_IRQn = -14,
        HardFault_IRQn = -13,
        SVCall_IRQn = -5,
        PendSV_IRQn = -2,
        SysTick_IRQn = -1,
        TIMER_IRQ_0_IRQn = 0,
        TIMER_IRQ_1_IRQn = 1,
        TIMER_IRQ_2_IRQn = 2,
        TIMER_IRQ_3_IRQn = 3,
        PWM_IRQ_WRAP_IRQn = 4,
        USBCTRL_IRQ_IRQn = 5,
        XIP_IRQ_IRQn = 6,
        PIO0_IRQ_0_IRQn = 7,
        PIO0_IRQ_1_IRQn = 8,
        PIO1_IRQ_0_IRQn = 9,
        PIO1_IRQ_1_IRQn = 10,
        DMA_IRQ_0_IRQn = 11,
        DMA_IRQ_1_IRQn = 12,
        IO_IRQ_BANK0_IRQn = 13,
        IO_IRQ_QSPI_IRQn = 14,
        SIO_IRQ_PROC0_IRQn = 15,
        SIO_IRQ_PROC1_IRQn = 16,
        CLOCKS_IRQ_IRQn = 17,
        SPI0_IRQ_IRQn = 18,
        SPI1_IRQ_IRQn = 19,
        UART0_IRQ_IRQn = 20,
        UART1_IRQ_IRQn = 21,
        ADC_IRQ_FIFO_IRQn = 22,
        I2C0_IRQ_IRQn = 23,
        I2C1_IRQ_IRQn = 24,
        RTC_IRQ_IRQn = 25
} IRQn_Type;

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

#ifdef __cplusplus
}
#endif

#endif /* _CPUCTL_H_ */
/*==============================================================================
  End of file
==============================================================================*/
