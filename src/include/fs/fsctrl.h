/*==============================================================================
@file    fsctrl.h

@author  Daniel Zorychta

@brief   Drivers support.

@note    Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

         This program is free software; you can redistribute it and/or modify
         it under the terms of the GNU General Public License version 2 as
         published by the Free Software Foundation.

         The dnx RTOS is distributed in the hope that it will be useful,
         but WITHOUT ANY WARRANTY; without even the implied warranty of
         MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
         GNU General Public License for more details.

         Programs using the kernel by system calls are not covered by the license,
         drivers and file systems of drvset partition: exception, see the license
         file: doc/license.txt.


==============================================================================*/

#ifndef _FSCTRL_
#define _FSCTRL_

#ifdef __DNX_MODULE__
#error "Kernel internal header. Drivers include drivers/driver.h, file systems include fs/fs.h."
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include "fs/vfs.h"

/*==============================================================================
  Exported macros
==============================================================================*/

/*==============================================================================
  Exported object types
==============================================================================*/
/* struct _FS_entry: fs/fsif.h */

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/
extern int _mount(const char *, const struct vfs_path *, const struct vfs_path *, const char *);
extern int _umount(const struct vfs_path *);

#ifdef __cplusplus
}
#endif

#endif /* _FSCTRL_ */
/*==============================================================================
  End of file
==============================================================================*/
