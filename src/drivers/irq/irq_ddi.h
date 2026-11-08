/*==============================================================================
File    irq_ddi.h

Author  Daniel Zorychta

Brief   IRQ driver interface for other drivers (external interrupts, EXTI).

        Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

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

/*
 * Driver interface (DDI) of the IRQ module used by other drivers (also
 * drivers of the drvset partition). A driver attaches a callback to an EXTI
 * line: the line is not available for IRQ devices (driver_init()) and lines
 * of shared NVIC interrupts (EXTI9_5, EXTI15_10) can be used by many drivers.
 * The callback is called in the interrupt (_CPU_IRQ_SAFE_PRIORITY_), pending
 * flag of the line is cleared by the IRQ module.
 */

#ifndef _IRQ_DDI_H_
#define _IRQ_DDI_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "irq_ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported object types
==============================================================================*/
/**
 * Callback of EXTI line, called in the interrupt.
 *
 * @param  line         EXTI line (0-15)
 * @param  arg          argument given to _IRQ_DDI_attach()
 *
 * @return true if higher priority task was woken (sys_*_from_ISR() functions),
 *         otherwise false.
 */
typedef bool (*_IRQ_DDI_cb_t)(u8_t line, void *arg);

/*==============================================================================
  Exported functions
==============================================================================*/
//==============================================================================
/**
 * @brief  Function attaches callback to EXTI line and enables the line.
 *
 * @param  line         EXTI line (0-15), pin number of GPIO port
 * @param  port         GPIO port of line: 0 for PA, 1 for PB, ...
 * @param  mode         trigger (IRQ_CONFIG__TRIGGER_ON_FALLING_EDGE,
 *                      IRQ_CONFIG__TRIGGER_ON_RISING_EDGE,
 *                      IRQ_CONFIG__TRIGGER_ON_BOTH_EDGES)
 * @param  cb           callback
 * @param  arg          callback argument
 *
 * @return One of errno value: EADDRINUSE when line is used by other driver or
 *         IRQ device, ENODEV when IRQ module is not built into the kernel.
 */
//==============================================================================
extern int _IRQ_DDI_attach(u8_t line, u8_t port, IRQ_config_t mode, _IRQ_DDI_cb_t cb, void *arg);

//==============================================================================
/**
 * @brief  Function disables EXTI line and detaches its callback.
 *
 * @param  line         EXTI line (0-15)
 *
 * @return One of errno value.
 */
//==============================================================================
extern int _IRQ_DDI_detach(u8_t line);

#ifdef __cplusplus
}
#endif

#endif /* _IRQ_DDI_H_ */
/*==============================================================================
  End of file
==============================================================================*/
