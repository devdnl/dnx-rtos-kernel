/*==============================================================================
@file    drvif.h

@author  Daniel Zorychta

@brief   Driver (module) interface used by kernel and drvset partition.

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

#ifndef _DRVIF_H_
#define _DRVIF_H_

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include "kernel/sysapi.h"

/*==============================================================================
  Exported macros
==============================================================================*/
/* module entry and interface import, API_MOD_* macros are defined in drivers/driver.h */
#define _MODULE_NAME(module_name) #module_name

#define _MODULE_INTERFACE(_modname, _type)                    \
        {                                                     \
                .name = #_modname, .type = _type,             \
                .IF = {.drv_init = _##_modname##_init,        \
                        .drv_release = _##_modname##_release, \
                        .drv_open = _##_modname##_open,       \
                        .drv_close = _##_modname##_close,     \
                        .drv_write = _##_modname##_write,     \
                        .drv_read = _##_modname##_read,       \
                        .drv_ioctl = _##_modname##_ioctl,     \
                        .drv_stat = _##_modname##_stat,       \
                        .drv_flush = _##_modname##_flush }    \
        }

#define _IMPORT_MODULE_INTERFACE(_modname)                                                                  \
        extern API_MOD_INIT(_modname, void **, u8_t, u8_t, const void *config);                             \
        extern API_MOD_RELEASE(_modname, void *);                                                           \
        extern API_MOD_OPEN(_modname, void *, u32_t);                                                       \
        extern API_MOD_CLOSE(_modname, void *, bool);                                                       \
        extern API_MOD_WRITE(_modname, void *, const u8_t *, size_t, fpos_t *, size_t *, struct vfs_fattr); \
        extern API_MOD_READ(_modname, void *, u8_t *, size_t, fpos_t *, size_t *, struct vfs_fattr);        \
        extern API_MOD_IOCTL(_modname, void *, int, void *);                                                \
        extern API_MOD_FLUSH(_modname, void *);                                                             \
        extern API_MOD_STAT(_modname, void *, struct vfs_dev_stat *)

/*==============================================================================
  Exported object types
==============================================================================*/
/** driver interface (functions defined by API_MOD_* macros) */
struct _module_if {
        int (*drv_init)(void **drvhdl, u8_t major, u8_t minor, const void *config);
        int (*drv_release)(void *drvhdl);
        int (*drv_open)(void *drvhdl, u32_t flags);
        int (*drv_close)(void *drvhdl, bool force);
        int (*drv_write)(void *drvhdl, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
                struct vfs_fattr attr);
        int (*drv_read)(void *drvhdl, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt, struct vfs_fattr attr);
        int (*drv_ioctl)(void *drvhdl, int iorq, void *arg);
        int (*drv_flush)(void *drvhdl);
        int (*drv_stat)(void *drvhdl, struct vfs_dev_stat *info);
};

/** module entry (module registration table) */
struct _module_entry {
        const char *name;
        const mode_t type; /* S_IFCHR or S_IFBLK */
        const struct _module_if IF;
};

/** shared interrupt handler entry (DRV_IRQ_SHARED(), drvset partition) */
struct _drv_irq_shared {
        u32_t irq;              /* IRQ number (IRQn_Type) */
        void (*handler)(void);  /* handler called with other handlers of the same IRQ */
};

#ifdef __cplusplus
}
#endif

#endif /* _DRVIF_H_ */
/*==============================================================================
  End of file
==============================================================================*/
