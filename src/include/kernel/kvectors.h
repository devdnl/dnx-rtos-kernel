/*==============================================================================
@file    kvectors.h

@author  Daniel Zorychta

@brief   Vector table entries used as fixed kernel entry points.

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
 * The vector table of each port has 256 entries (1 KiB). Last entries are not
 * used by interrupts of supported CPUs and hold addresses of kernel entry
 * points. Programs (libc) and drivers built separately (drvset) find the
 * kernel by these entries, without any fixed address of kernel code.
 *
 * Entry address = vector table address + 4 * index.
 */

#ifndef _KVECTORS_H_
#define _KVECTORS_H_

/** syscall entry: int syscall(syscall_t no, va_list args), used by libc */
#define _KVECTOR_SYSCALL 255

/** kernel symbol table (kernel/ksymtab.h), used by drvset */
#define _KVECTOR_KSYMTAB 254

/** first vector of interrupts (IRQ 0), previous vectors are CPU exceptions */
#define _KVECTOR_IRQ0 16

/*
 * Shared interrupt dispatcher (kernel/kirq.c): built when the drvset partition
 * is enabled or kernel drivers use shared interrupt handlers (IRQ_SHARED of
 * driver Makefile, DRV_IRQ_SHARED() of drivers/driver.h).
 */
#if ((__OS_ENABLE_DRVSET__ > 0) || defined(__KERNEL_IRQ_SHARED__)) && !defined(ARCH_posix)
#define _KIRQ_SHARED_DISPATCH 1
#endif

#if defined(_KIRQ_SHARED_DISPATCH) && !defined(__DNX_MODULE__) && !defined(__ASSEMBLER__)
/**
 * Called by the default handler of the vector table: calls shared interrupt
 * handlers of kernel drivers and handlers of the drvset partition. Returns 1
 * if interrupt is handled, 0 if not (unexpected interrupt or exception).
 */
extern int _irq_shared_dispatch(void);

/**
 * Reports shared interrupt handlers of kernel drivers that are not called
 * (interrupt with exclusive handler in the vector table).
 */
extern void _irq_shared_init(void);
#endif

#endif /* _KVECTORS_H_ */
/*==============================================================================
  End of file
==============================================================================*/
