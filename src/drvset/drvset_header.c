/*==============================================================================
@file    drvset_header.c

@author  Daniel Zorychta

@brief   Header of drvset partition (drivers built separately from the kernel).

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
 * The file is not a part of the kernel. It is compiled by the drvset build
 * (tools/drvset of the workspace) and creates the partition header
 * (drivers/drvset.h), the first object of the partition image. The build
 * generates two files used here:
 *   drvset_modules.h   DRVSET_MODULE_LIST(X): X(module_name, type) for each
 *                      module, type is S_IFCHR or S_IFBLK
 *   drvset_irq.h       DRVSET_IRQ_LIST(X): X(irq_number, handler_name) for
 *                      each interrupt of the CPU (from the kernel vector table)
 *   drvset_fs.h        DRVSET_FS_LIST(X): X(name, prefix) for each file system,
 *                      name is used by mount, prefix is the name used by
 *                      API_FS_*() of file system source, DRVSET_FS_COUNT
 *                      (file system table: drvset_fs.c)
 *
 * Interrupt handlers are weak references: handlers that are not defined by
 * drivers are NULL in the table, the interrupt is not used by the partition.
 * Shared handlers (DRV_IRQ_SHARED()) are collected by the linker script from
 * the .irq_shared section.
 */

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "drivers/drvset.h"
#include "drvset_modules.h"
#include "drvset_irq.h"
#include "drvset_fs.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define IMPORT_MODULE(name, type) _IMPORT_MODULE_INTERFACE(name);
#define MODULE_ENTRY(name, type)  _MODULE_INTERFACE(name, type),
#define IMPORT_HANDLER(irq, name) extern void name(void) __attribute__((weak));
#define HANDLER_ENTRY(irq, name)  [irq] = name,


/*==============================================================================
  External objects
==============================================================================*/
/* symbols of linker script (drvset.ld), addresses are values */
extern const char __drvset_image_size[];
extern const char __drvset_data_load[];
extern char __drvset_data_start[];
extern char __drvset_data_end[];
extern char __drvset_bss_start[];
extern char __drvset_bss_end[];
extern const char __ksymtab_vector[];
extern const struct _drv_irq_shared __drvset_irq_shared_start[];
extern const char __drvset_irq_shared_count[];

DRVSET_MODULE_LIST(IMPORT_MODULE)

DRVSET_IRQ_LIST(IMPORT_HANDLER)

/* file system table (drvset_fs.c, file systems use fs/fs.h, drivers drivers/driver.h) */
extern const struct _FS_entry _drvset_filesystems[];

/*==============================================================================
  Local objects
==============================================================================*/
static const struct _module_entry modules[] = {DRVSET_MODULE_LIST(MODULE_ENTRY)};

static const _drvset_irq_handler_t irq_table[DRVSET_IRQ_COUNT] = {DRVSET_IRQ_LIST(HANDLER_ENTRY)};

/*==============================================================================
  Exported objects
==============================================================================*/
const _drvset_header_t _drvset_header __attribute__((section(".drvset_header"), used)) = {
        .magic = _DRVSET_MAGIC,
        .header_version = _DRVSET_HEADER_VERSION,
        .ksymtab_version = _KSYMTAB_VERSION,
        .ksymtab_count = _KSYM_COUNT,
        .image_size = (u32_t)(uintptr_t)__drvset_image_size,
        .data_load = __drvset_data_load,
        .data_start = __drvset_data_start,
        .data_end = __drvset_data_end,
        .bss_start = __drvset_bss_start,
        .bss_end = __drvset_bss_end,
        .modules = modules,
        .modules_count = sizeof(modules) / sizeof(modules[0]),
        .irq_table = irq_table,
        .irq_count = DRVSET_IRQ_COUNT,
        .ksymtab_vector = __ksymtab_vector,
        .irq_shared = __drvset_irq_shared_start,
        .irq_shared_count = (u32_t)(uintptr_t)__drvset_irq_shared_count,
        .filesystems = _drvset_filesystems,
        .filesystems_count = DRVSET_FS_COUNT,
};

/*==============================================================================
  End of file
==============================================================================*/
