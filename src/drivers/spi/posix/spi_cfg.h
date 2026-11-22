/*==============================================================================
@file    spi_cfg.h

@author  Daniel Zorychta

@brief   SPI configuration of the POSIX simulator.

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

#ifndef _SPI_CFG_H_
#define _SPI_CFG_H_

/*==============================================================================
  Include files
==============================================================================*/

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
/*
 *  SPI default configuration (for all devices)
 */
#define _SPI_DEFAULT_CFG_CLK_DIVIDER SPI_CLK_DIV__4
#define _SPI_DEFAULT_CFG_MODE        SPI_MODE__0
#define _SPI_DEFAULT_CFG_MSB_FIRST   true

#ifdef __cplusplus
}
#endif

#endif /* _SPI_CFG_H_ */
/*==============================================================================
  End of file
==============================================================================*/
