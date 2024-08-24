/*==============================================================================
File    stm32h7xx_hal_qspi.h

Author  Daniel Zorychta

Brief   QSPI HAL definitions.

        Copyright (C) 2024 Daniel Zorychta <daniel.zorychta@gmail.com>

        This program is free software; you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation and modified by the dnx RTOS exception.

        NOTE: The modification  to the GPL is  included to allow you to
              distribute a combined work that includes dnx RTOS without
              being obliged to provide the source  code for proprietary
              components outside of the dnx RTOS.

        The dnx RTOS  is  distributed  in the hope  that  it will be useful,
        but WITHOUT  ANY  WARRANTY;  without  even  the implied  warranty of
        MERCHANTABILITY  or  FITNESS  FOR  A  PARTICULAR  PURPOSE.  See  the
        GNU General Public License for more details.

        Full license text is available on the following file: doc/license.txt.

==============================================================================*/

/**
@defgroup SRC_SYSTEM_DRIVERS_QSPI_STM32H7_STM32H7XX_HAL_QSPI_DEF_H_ SRC_SYSTEM_DRIVERS_QSPI_STM32H7_STM32H7XX_HAL_QSPI_DEF_H_

Detailed Doxygen description.
*/
/**@{*/

#pragma once

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "stm32h7/stm32h7xx.h"
#include "stm32h7/dma_ddi.h"
#include "stm32h7/lib/misc.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(ARCH_stm32h7)
#if defined(QUADSPI)

/*==============================================================================
  Exported macros
==============================================================================*/
#define HAL_QSPI_MODULE_ENABLED

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

#endif
#endif

#ifdef __cplusplus
}
#endif

/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
