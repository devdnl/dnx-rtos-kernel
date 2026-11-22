/*==============================================================================
File     strlcpy.h

Author   Daniel Zorychta

Brief    Strings copy function (with buffer limitation).

         Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

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

/**
@defgroup STRLCPY_H_ STRLCPY_H_

Detailed Doxygen description.
*/
/**@{*/

#ifndef _STRLCPY_H_
#define _STRLCPY_H_

/*==============================================================================
  Include files
==============================================================================*/
#include <stddef.h>

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
extern size_t _strlcpy(char *dst, const char *src, size_t size);

/*==============================================================================
  Exported inline functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _STRLCPY_H_ */

/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
