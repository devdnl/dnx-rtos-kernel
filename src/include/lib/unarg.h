/*==============================================================================
@file    unarg.h

@author  Daniel Zorychta

@brief   Unused arguments macros.

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

#ifndef _LIB_UNARG_H_
#define _LIB_UNARG_H_

/*==============================================================================
  Include files
==============================================================================*/

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
#define UNUSED_ARG1(_arg1) ((void)_arg1)
#define UNUSED_ARG2(_arg1, _arg2) \
        UNUSED_ARG1(_arg1);       \
        UNUSED_ARG1(_arg2)
#define UNUSED_ARG3(_arg1, _arg2, _arg3) \
        UNUSED_ARG2(_arg1, _arg2);       \
        UNUSED_ARG1(_arg3)
#define UNUSED_ARG4(_arg1, _arg2, _arg3, _arg4) \
        UNUSED_ARG3(_arg1, _arg2, _arg3);       \
        UNUSED_ARG1(_arg4)
#define UNUSED_ARG5(_arg1, _arg2, _arg3, _arg4, _arg5) \
        UNUSED_ARG4(_arg1, _arg2, _arg3, _arg4);       \
        UNUSED_ARG1(_arg5)
#define UNUSED_ARG6(_arg1, _arg2, _arg3, _arg4, _arg5, _arg6) \
        UNUSED_ARG5(_arg1, _arg2, _arg3, _arg4, _arg5);       \
        UNUSED_ARG1(_arg6)
#define UNUSED_ARG7(_arg1, _arg2, _arg3, _arg4, _arg5, _arg6, _arg7) \
        UNUSED_ARG6(_arg1, _arg2, _arg3, _arg4, _arg5, _arg6);       \
        UNUSED_ARG1(_arg7)
#define UNUSED_ARG8(_arg1, _arg2, _arg3, _arg4, _arg5, _arg6, _arg7, _arg8) \
        UNUSED_ARG7(_arg1, _arg2, _arg3, _arg4, _arg5, _arg6, _arg7);       \
        UNUSED_ARG1(_arg8)

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

#endif /* _LIB_UNARG_H_ */
/*==============================================================================
  End of file
==============================================================================*/
