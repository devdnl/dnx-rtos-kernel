/*==============================================================================
@file    printk.h

@author  Daniel Zorychta

@brief   Kernel print support

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

#ifndef _PRINTK_H_
#define _PRINTK_H_

#ifdef __DNX_MODULE__
#error "Kernel internal header. Drivers include drivers/driver.h, file systems include fs/fs.h."
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include "config.h"
#include <stdarg.h>

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

/*==============================================================================
  Exported functions
==============================================================================*/
#if (__OS_SYSTEM_MSG_ENABLE__ > 0)
int _printk_init(void);
size_t _printk_read(char *, size_t, const struct timeval *, struct timeval *);
void _printk_clear(void);
void _printk(const char *, ...);
void _vprintk(const char *, va_list);
#else
#define _printk_init()
#define _printk(...)
#define _printk_read(str, len, timestamp_ms)
#define _printk_clear()
#endif

/*==============================================================================
  Exported inline functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _PRINTK_H_ */
/*==============================================================================
  End of file
==============================================================================*/
