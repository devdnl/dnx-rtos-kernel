/*==============================================================================
@file    drvctrl.h

@author  Daniel Zorychta

@brief   Modules support.

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

#ifndef _DRVCTRL_H_
#define _DRVCTRL_H_

#ifdef __DNX_MODULE__
#error "Kernel internal header. Drivers include drivers/driver.h, file systems include fs/fs.h."
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include <stdbool.h>
#include "fs/vfs.h"
#include "drivers/driver_registration.h"
#include "drivers/drvif.h"

/*==============================================================================
  Exported macros
==============================================================================*/

/** maximum number of modules (static modules and modules of drvset partition) */
#if (__OS_ENABLE_DRVSET__ > 0)
#define _MODULES_MAX (_drvreg_number_of_modules + __OS_DRVSET_MAX_MODULES__)
#else
#define _MODULES_MAX _drvreg_number_of_modules
#endif

/*==============================================================================
  Exported object types
==============================================================================*/

/*
 * To lock device, system uses PID of syscall's client.
 * Doxygen documentation in drivers/driver.h.
 */
typedef pid_t dev_lock_t;

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/
extern int _driver_init(const char *, u8_t, u8_t, const char *, const void *, dev_t *);
extern int _driver_release(dev_t);
extern int _driver_open(dev_t, u32_t);
extern int _driver_close(dev_t, bool);
extern int _driver_write(dev_t, const u8_t *, size_t, fpos_t *, size_t *, struct vfs_fattr);
extern int _driver_read(dev_t, u8_t *, size_t, fpos_t *, size_t *, struct vfs_fattr);
extern int _driver_ioctl(dev_t, int, void *);
extern int _driver_flush(dev_t);
extern int _driver_stat(dev_t, struct vfs_dev_stat *);
extern mode_t _driver_get_type(dev_t);
extern int _module_get_instance(const char *, u8_t, u8_t, void **);
extern const char *_module_get_name(size_t);
extern size_t _module_get_count(void);
extern int _module_get_ID(const char *);
extern ssize_t _module_get_number_of_instances(size_t);
extern int _device_lock(dev_lock_t *);
extern int _device_unlock(dev_lock_t *, bool);
extern int _device_get_access(dev_lock_t *);
extern bool _device_is_locked(dev_lock_t *);

/*==============================================================================
  Exported inline functions
==============================================================================*/
//==============================================================================
/**
 * @brief  Create dev_t-type value by using module id, major and minor numbers
 *
 * @param  modid        module id
 * @param  major        module major number
 * @param  minor        module minor number
 *
 * @return Device ID
 */
//==============================================================================
static inline dev_t _dev_t__create(u16_t modid, u8_t major, u8_t minor)
{
        return ((modid & 0x7FFF) << 16) | ((major & 0xFF) << 8) | (minor & 0xFF);
}

//==============================================================================
/**
 * @brief  Extract module id from dev_t-type value
 *
 * @param  devid        device id
 *
 * @return Module ID
 */
//==============================================================================
static inline u16_t _dev_t__extract_modno(dev_t devid)
{
        return (devid >> 16) & 0x7FFF;
}

//==============================================================================
/**
 * @brief  Extract major number from dev_t-type value
 *
 * @param  devid        device id
 *
 * @return Major number
 */
//==============================================================================
static inline u8_t _dev_t__extract_major(dev_t devid)
{
        return (devid >> 8) & 0xFF;
}

//==============================================================================
/**
 * @brief  Extract minor number from dev_t-type value
 *
 * @param  devid        device id
 *
 * @return Minor number
 */
//==============================================================================
static inline u8_t _dev_t__extract_minor(dev_t devid)
{
        return (devid >> 0) & 0xFF;
}

#ifdef __cplusplus
}
#endif

#endif /* _DRVCTRL_H_ */
/*==============================================================================
  End of file
==============================================================================*/
