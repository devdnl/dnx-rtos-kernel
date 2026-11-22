/*==============================================================================
@file    kworker.h

@author  Daniel Zorychta

@brief   Kernel worker process.

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

#ifndef _KRNSPACE_KWORKER_H_
#define _KRNSPACE_KWORKER_H_

#ifdef __DNX_MODULE__
#error "Kernel internal header. Drivers include drivers/driver.h, file systems include fs/fs.h."
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include <kernel/process.h>

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/

/*==============================================================================
  Exported object types
==============================================================================*/

/*==============================================================================
  Exported objects
==============================================================================*/
extern struct _process *_kworker_proc;

/*==============================================================================
  Exported functions
==============================================================================*/
extern int _kworker_init();
extern int _kworker_process(int, char **);
extern void _kworker_wake(void);

/*==============================================================================
  Exported inline functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _KRNSPACE_KWORKER_H_ */
/*==============================================================================
  End of file
==============================================================================*/
