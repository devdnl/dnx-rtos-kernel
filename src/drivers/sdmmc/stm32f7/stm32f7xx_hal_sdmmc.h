/*==============================================================================
File    stm32f7xx_hal_sdmmc.h

Author  Daniel Zorychta

Brief   SDMMC HAL definitions.

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
@defgroup SRC_SYSTEM_DRIVERS_SDIO_STM32F7_STM32F7XX_HAL_DEF_H_ SRC_SYSTEM_DRIVERS_SDIO_STM32F7_STM32F7XX_HAL_DEF_H_

Detailed Doxygen description.
*/
/**@{*/

#pragma once

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "stm32f7/stm32f7xx.h"
#include "stm32f7/dma_ddi.h"
#include "stm32f7/lib/misc.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(ARCH_stm32f7)
#if defined(SDMMC1) || defined(SDMMC2)

/*==============================================================================
  Exported macros
==============================================================================*/
#define HAL_SD_MODULE_ENABLED
#define HAL_MMC_MODULE_ENABLED

/*==============================================================================
  Exported object types
==============================================================================*/
/**
 * @brief  DMA handle Structure definition
 */
typedef struct __DMA_HandleTypeDef {
        u32_t dmad; /*!< DMA descriptor */

        _DMA_DDI_config_t Config; /*!< DMA configuration */

        u32_t Status; /*!< Status flags */

        void *Parent; /*!< Parent object state                    */

        void (*XferCpltCallback)(struct __DMA_HandleTypeDef *hdma); /*!< DMA transfer complete callback         */

        void (*XferErrorCallback)(struct __DMA_HandleTypeDef *hdma); /*!< DMA transfer error callback            */

        __IO uint32_t ErrorCode; /*!< DMA Error code                          */

} DMA_HandleTypeDef;

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/

/*==============================================================================
  Exported inline functions
==============================================================================*/

#endif
#endif

#ifdef __cplusplus
}
#endif

/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
