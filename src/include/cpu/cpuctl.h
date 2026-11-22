/*==============================================================================
@file    cpuctl.h

@author  Daniel Zorychta

@brief   This file support CPU control

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

#ifndef _SYS_CPUCTL_H_
#define _SYS_CPUCTL_H_

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/
#define _BYTE_ORDER_LITTLE_ENDIAN 0
#define _BYTE_ORDER_BIG_ENDIAN    1

#if defined(ARCH_stm32f1)
#include "stm32f1/cpuctl.h"
#elif defined(ARCH_stm32f3)
#include "stm32f3/cpuctl.h"
#elif defined(ARCH_stm32f4)
#include "stm32f4/cpuctl.h"
#elif defined(ARCH_stm32f7)
#include "stm32f7/cpuctl.h"
#elif defined(ARCH_stm32h7)
#include "stm32h7/cpuctl.h"
#elif defined(ARCH_rpcm0)
#include "rpcm0/cpuctl.h"
#elif defined(ARCH_posix)
#include "posix/cpuctl.h"
#else
#error CPU architecture not defined!
#endif

/*==============================================================================
  Exported symbolic constants/macros
==============================================================================*/

/*==============================================================================
  Exported types, enums definitions
==============================================================================*/

/*==============================================================================
  Exported function prototypes
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _SYS_CPUCTL_H_ */
/*==============================================================================
  End of file
==============================================================================*/
