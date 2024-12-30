/*==============================================================================
@file    port_helper.h

@author  Daniel Zorychta

@brief   Port helper functions.

@note    Copyright (C) 2024 Daniel Zorychta <daniel.zorychta@gmail.com>

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

#ifndef _PORT_HELPER_H_
#define _PORT_HELPER_H_

/*==============================================================================
  Include files
==============================================================================*/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
#define configSMP_SPINLOCK_0 14
#define configSMP_SPINLOCK_1 15
#define configTICK_CORE      0

/*==============================================================================
  Exported object types
==============================================================================*/
typedef volatile uint32_t spin_lock_t;

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/
extern uint8_t      get_core_num(void);
extern spin_lock_t *spin_lock_instance(uint8_t lock_num);
extern void         multicore_fifo_drain(void);
extern void         multicore_fifo_clear_irq(void);
extern void         multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack_bottom,
                size_t stack_size_bytes);
/*==============================================================================
  Exported inline functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _PORT_HELPER_H_ */
/*==============================================================================
  End of file
==============================================================================*/
