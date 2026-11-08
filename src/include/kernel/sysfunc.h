/*==============================================================================
@file    sysfunc.h

@author  Daniel Zorychta

@brief   Kernel internal header: public API and kernel internals.

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

#ifndef _SYSFUNC_H_
#define _SYSFUNC_H_

#ifdef __DNX_MODULE__
#error "Kernel internal header. Drivers include drivers/driver.h, file systems include fs/fs.h."
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include "kernel/sysapi.h"
#include "cpu/cpuctl.h"
#include "drivers/drvctrl.h"
#include "fs/vfs.h"
#include "kernel/errno.h"
#include "kernel/kwrapper.h"
#include "kernel/printk.h"
#include "kernel/process.h"
#include "kernel/syscall.h"
#include "kernel/time.h"
#include "lib/conv.h"
#include "lib/llist.h"
#include "lib/misc.h"
#include "lib/stropt.h"
#include "lib/sys/types.h"
#include "lib/unarg.h"
#include "lib/vfprintf.h"
#include "lib/vsnprintf.h"
#include "lib/vsscanf.h"
#include <stdbool.h>

#endif /* _SYSFUNC_H_ */

/*==============================================================================
  End of file
==============================================================================*/
