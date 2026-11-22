/*==============================================================================
@file    i2c_cfg.h

@author  Daniel Zorychta

@brief   This driver support I2C peripherals.

@note    Copyright (C) 2026  Daniel Zorychta <daniel.zorychta@gmail.com>

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

#ifndef _I2C_CFG_H_
#define _I2C_CFG_H_

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/

/*==============================================================================
  Exported symbolic constants/macros
==============================================================================*/
#define _I2C_DUTY_CYCLE_1_2  0
#define _I2C_DUTY_CYCLE_16_9 1

#define _I2C1_USE_DMA    __I2C1_USE_DMA__
#define _I2C1_FREQUENCY  __I2C1_SCL_FREQ__
#define _I2C1_DUTY_CYCLE __I2C1_DUTY_CYCLE__

#define _I2C2_USE_DMA    __I2C2_USE_DMA__
#define _I2C2_FREQUENCY  __I2C2_SCL_FREQ__
#define _I2C2_DUTY_CYCLE __I2C2_DUTY_CYCLE__

#define _I2C3_USE_DMA    __I2C3_USE_DMA__
#define _I2C3_FREQUENCY  __I2C3_SCL_FREQ__
#define _I2C3_DUTY_CYCLE __I2C3_DUTY_CYCLE__

/*==============================================================================
  Exported types, enums definitions
==============================================================================*/

/*==============================================================================
  Exported object declarations
==============================================================================*/

/*==============================================================================
  Exported function prototypes
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _I2C_CFG_H_ */
/*==============================================================================
  End of file
==============================================================================*/
