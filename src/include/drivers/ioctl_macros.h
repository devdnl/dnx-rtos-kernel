/*==============================================================================
@file    ioctl_macros.h

@author  Daniel Zorychta

@brief   ioctl build macros.

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

#ifndef _IOCTL_MACROS_H_
#define _IOCTL_MACROS_H_

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/

/*==============================================================================
  Exported macros
==============================================================================*/
#define _IO(g, n)      ((0 << 16) | (((0) & 0xFF) << 8) | ((int)(n) & 0xFF))
#define _IOR(g, n, t)  ((1 << 16) | ((sizeof(*((t)(NULL))) & 0xFF) << 8) | ((int)(n) & 0xFF))
#define _IOW(g, n, t)  ((2 << 16) | ((sizeof(*((t)(NULL))) & 0xFF) << 8) | ((int)(n) & 0xFF))
#define _IOWR(g, n, t) ((3 << 16) | ((sizeof(*((t)(NULL))) & 0xFF) << 8) | ((int)(n) & 0xFF))

/*==============================================================================
  Exported object types
==============================================================================*/

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/

/*==============================================================================
  Exported inline functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _IOCTL_MACROS_H_ */
/*==============================================================================
  End of file
==============================================================================*/
