/*==============================================================================
@file    drvset.h

@author  Daniel Zorychta

@brief   Driver set (drvset) partition header: drivers built separately from the kernel.

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
 * The drvset partition contains drivers (modules) built separately from the
 * kernel and programmed to flash memory at __OS_DRVSET_FLASH_ADDRESS__. The
 * partition starts with _drvset_header_t. At start the kernel checks the
 * header, initializes .data and .bss of the partition (RAM reserved by the
 * kernel: __OS_DRVSET_RAM_SIZE__ KiB at the start of main RAM region) and
 * registers modules after modules built into the kernel and file systems
 * (mounted by name when not found among file systems of the kernel). Drivers
 * and file systems call the
 * kernel through the kernel symbol table (kernel/ksymtab.h).
 *
 * Interrupts: the partition contains a table of interrupt handlers indexed by
 * IRQ number (irq_table, flash). Interrupts that are not handled by the kernel
 * image go to the default handler of the kernel vector table, which calls the
 * handler of the partition (_drvset_irq_dispatch()). Handlers use the same
 * names as in the kernel (e.g. USART1_IRQHandler), so the driver code is the
 * same as in the kernel. Drivers can also register shared handlers
 * (DRV_IRQ_SHARED(), irq_shared table in flash): all handlers of the interrupt
 * are called. An interrupt can be handled by the partition only if it is not
 * handled by the kernel image (checked at start). No RAM is used.
 *
 * The partition does not support C++ constructors (.init_array).
 */

#ifndef _DRVSET_H_
#define _DRVSET_H_

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include "kernel/sysapi.h"
#include "kernel/ksymtab.h"
#include "drivers/drvif.h"
#include "fs/fsif.h"

/*==============================================================================
  Exported macros
==============================================================================*/
/** drvset header magic number ("DRVS") */
#define _DRVSET_MAGIC 0x53565244UL

/** drvset header version */
#define _DRVSET_HEADER_VERSION 1

/*==============================================================================
  Exported object types
==============================================================================*/
/** drvset interrupt handler */
typedef void (*_drvset_irq_handler_t)(void);

/** drvset partition header (first object of partition) */
typedef struct {
        u32_t magic;                         /**< _DRVSET_MAGIC */
        u16_t header_version;                /**< _DRVSET_HEADER_VERSION */
        u16_t ksymtab_version;               /**< _KSYMTAB_VERSION used to build drvset */
        u32_t ksymtab_count;                 /**< number of kernel symbols known at drvset build (_KSYM_COUNT) */
        u32_t image_size;                    /**< partition image size in bytes */
        const void *data_load;               /**< initial values of .data (flash) */
        void *data_start;                    /**< .data start (RAM) */
        void *data_end;                      /**< .data end (RAM) */
        void *bss_start;                     /**< .bss start (RAM) */
        void *bss_end;                       /**< .bss end (RAM) */
        const struct _module_entry *modules; /**< module table */
        u32_t modules_count;                 /**< number of modules */
        const _drvset_irq_handler_t *irq_table; /**< interrupt handlers indexed by IRQ number (NULL: not used) */
        u32_t irq_count;                     /**< number of entries of irq_table */
        const void *ksymtab_vector;          /**< address of kernel vector _KVECTOR_KSYMTAB used to link drvset */
        const struct _drv_irq_shared *irq_shared; /**< shared interrupt handlers (DRV_IRQ_SHARED()) */
        u32_t irq_shared_count;              /**< number of shared interrupt handlers */
        const struct _FS_entry *filesystems; /**< file system table */
        u32_t filesystems_count;             /**< number of file systems */
} _drvset_header_t;

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/
#if (__OS_ENABLE_DRVSET__ > 0) && !defined(__DNX_MODULE__)
extern int _drvset_init(void);
extern size_t _drvset_get_module_count(void);
extern const struct _module_entry *_drvset_get_module(size_t n);
extern const struct vfs_FS_itf *_drvset_get_filesystem(const char *name);
extern int _drvset_irq_dispatch(u32_t irq);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _DRVSET_H_ */
/*==============================================================================
  End of file
==============================================================================*/
