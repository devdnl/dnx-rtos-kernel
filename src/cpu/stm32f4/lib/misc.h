/*==============================================================================
File    misc.h

Author  Daniel Zorychta

Brief   miscellaneous CPU functions.

        Copyright (C) 2018 Daniel Zorychta <daniel.zorychta@gmail.com>

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
@defgroup HDR_H_ HDR_H_

Detailed Doxygen description.
*/
/**@{*/

#ifndef _STM32F4_MISC_H_
#define _STM32F4_MISC_H_

#if defined(ARCH_stm32f4)

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include "kernel/errno.h"
#include "stm32f4xx.h"

/*==============================================================================
  Exported macros
==============================================================================*/
#define NVIC_VectTab_RAM     ((uint32_t)0x20000000)
#define NVIC_VectTab_FLASH   ((uint32_t)0x08000000)

#define NVIC_LP_SEVONPEND    ((uint8_t)0x10)
#define NVIC_LP_SLEEPDEEP    ((uint8_t)0x04)
#define NVIC_LP_SLEEPONEXIT  ((uint8_t)0x02)

#define NVIC_PriorityGroup_0 ((uint32_t)0x700)

#define NVIC_PriorityGroup_1 ((uint32_t)0x600)

#define NVIC_PriorityGroup_2 ((uint32_t)0x500)

#define NVIC_PriorityGroup_3 ((uint32_t)0x400)

#define NVIC_PriorityGroup_4 ((uint32_t)0x300)

#define assert_param(...)
#define HAL_GetTick()      sys_get_uptime_ms()
#define HAL_Delay(_ms)     sys_sleep_ms(_ms)
#define __weak             __attribute__((weak))
#define __HAL_UNLOCK(heth) (heth->Lock = HAL_UNLOCKED)
#define __HAL_LOCK(heth)   (heth->Lock = HAL_LOCKED)
#define HAL_MAX_DELAY      _WAIT_FOREVER_MS

#ifndef UNUSED
#define UNUSED(_x) UNUSED_ARG1(_x)
#endif

#define HAL_DMA_ERROR_NONE 0

/*==============================================================================
  Exported object types
==============================================================================*/
/**
 * @brief  HAL Status structures definition
 */
typedef enum {
        HAL_OK      = ESUCC,
        HAL_TIMEOUT = ETIME,
        HAL_BUSY    = EBUSY,
        HAL_ERROR   = EIO,
} HAL_StatusTypeDef;

/**
 * @brief  HAL lock type.
 */
typedef enum {
        HAL_UNLOCKED,
        HAL_LOCKED
} HAL_LockTypeDef;

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/
extern void NVIC_PriorityGroupConfig(uint32_t NVIC_PriorityGroup);
extern void NVIC_SetVectorTable(uint32_t NVIC_VectTab, uint32_t Offset);

/*==============================================================================
  Exported inline functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif

#endif /* _STM32F4_MISC_H_ */

/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
