/*==============================================================================
@file    gpio_cfg.h

@author  Daniel Zorychta

@brief   GPIO configuration (POSIX simulator).

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

#ifndef _GPIO_CFG_H_
#define _GPIO_CFG_H_

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include <config.h>

/*==============================================================================
  Exported symbolic constants/macros
==============================================================================*/
/** number of pins in single virtual port */
#define _GPIO_POSIX_PINS_IN_PORT 32

#ifdef __cplusplus
}
#endif

#endif /* _GPIO_CFG_H_ */
/*==============================================================================
  End of file
==============================================================================*/
