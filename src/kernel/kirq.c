/*==============================================================================
@file    kirq.c

@author  Daniel Zorychta

@brief   Shared interrupt dispatcher (DRV_IRQ_SHARED() of kernel drivers and
         interrupts of the drvset partition).

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

/*
 * Interrupts without exclusive handler in the vector table go to the default
 * handler of the vector table, which calls _irq_shared_dispatch(). The
 * dispatcher calls shared handlers of kernel drivers (table .irq_shared of
 * the kernel linker script, DRV_IRQ_SHARED() of drivers/driver.h) and
 * handlers of the drvset partition (drivers/drvset.h). No RAM is used.
 *
 * The file is compiled when the drvset partition is enabled or kernel drivers
 * use shared interrupt handlers (IRQ_SHARED of driver Makefile).
 */

/*==============================================================================
  Include files
==============================================================================*/
#include "config.h"
#include "kernel/kvectors.h"

#if defined(_KIRQ_SHARED_DISPATCH)
#include "kernel/sysfunc.h"
#include "drivers/drvif.h"

#if (__OS_ENABLE_DRVSET__ > 0)
#include "drivers/drvset.h"
#endif

/*==============================================================================
  External objects
==============================================================================*/
/* shared interrupt handlers of kernel drivers (kernel linker script) */
extern const struct _drv_irq_shared __irq_shared_start[];
extern const struct _drv_irq_shared __irq_shared_end[];

#if defined(__KERNEL_IRQ_SHARED__)
/* kernel vector table and its default handler (vectors file of CPU port) */
extern void (*const vectors[256])(void);
extern void __Default_Handler(void);
#endif

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Function calls shared interrupt handlers of current interrupt
 *         (kernel drivers and drvset partition). Function is called by the
 *         default handler of the vector table.
 *
 * @return 1 if interrupt is handled, 0 if not (unexpected interrupt or
 *         exception).
 */
//==============================================================================
int _irq_shared_dispatch(void)
{
        uint32_t ipsr;
        __asm volatile("mrs %0, ipsr" : "=r"(ipsr));

        /* exceptions (vectors 0..15) give big numbers and are not handled */
        uint32_t irq = (ipsr & 0x1FF) - _KVECTOR_IRQ0;

        int handled = 0;

        /* each shared handler checks flags of own device */
        for (const struct _drv_irq_shared *sh = __irq_shared_start; sh < __irq_shared_end; sh++) {
                if (sh->irq == irq) {
                        sh->handler();
                        handled = 1;
                }
        }

#if (__OS_ENABLE_DRVSET__ > 0)
        handled |= _drvset_irq_dispatch(irq);
#endif

        return handled;
}

//==============================================================================
/**
 * @brief  Function reports shared interrupt handlers of kernel drivers that
 *         are not called: interrupt has exclusive handler in the vector table
 *         or does not exist in the CPU.
 */
//==============================================================================
void _irq_shared_init(void)
{
#if defined(__KERNEL_IRQ_SHARED__)
        for (const struct _drv_irq_shared *sh = __irq_shared_start; sh < __irq_shared_end; sh++) {
                if ((sh->irq >= (256 - _KVECTOR_IRQ0)) || (vectors[_KVECTOR_IRQ0 + sh->irq] != __Default_Handler)) {
                        printk("IRQ %u: shared handler is not used (vector of IRQ is used)", (uint)sh->irq);
                }
        }
#endif
}

#endif /* defined(_KIRQ_SHARED_DISPATCH) */

/*==============================================================================
  End of file
==============================================================================*/
