/*==============================================================================
@file    port_helper.h

@author  Daniel Zorychta

@brief   Port helper functions.

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
extern uint8_t get_core_num(void);
extern spin_lock_t *spin_lock_instance(uint8_t lock_num);
extern bool multicore_fifo_rvalid(void);
extern void multicore_fifo_drain(void);
extern void multicore_fifo_clear_irq(void);
extern uint32_t multicore_fifo_pop_blocking(void);
extern void multicore_fifo_push_blocking(uint32_t data);
extern void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack_bottom, size_t stack_size_bytes);
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
