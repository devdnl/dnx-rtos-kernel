/*==============================================================================
@file    spi_cfg.h

@author  Daniel Zorychta

@brief   SPI driver configuration

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

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/

/*==============================================================================
  Exported macros
==============================================================================*/
/*
 *  SPI default configuration (for all devices)
 */
#define _SPI_DEFAULT_CFG_CLK_DIVIDER __SPI_DEFAULT_CLK_DIV__
#define _SPI_DEFAULT_CFG_MODE        __SPI_DEFAULT_MODE__
#define _SPI_DEFAULT_CFG_MSB_FIRST   __SPI_DEFAULT_MSB_FIRST__

/*
 * DMA enable (1) or disable (0)
 */
#define _SPI1_USE_DMA __SPI_SPI1_USE_DMA__
#define _SPI2_USE_DMA __SPI_SPI2_USE_DMA__
#define _SPI3_USE_DMA __SPI_SPI3_USE_DMA__
#define _SPI4_USE_DMA __SPI_SPI4_USE_DMA__
#define _SPI5_USE_DMA __SPI_SPI5_USE_DMA__
#define _SPI6_USE_DMA __SPI_SPI6_USE_DMA__

/*==============================================================================
  Exported object types
==============================================================================*/

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _SPI_CFG_H_ */
/*==============================================================================
  End of file
==============================================================================*/
