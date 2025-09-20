/*==============================================================================
@file    main.c

@author  Daniel Zorychta

@brief   This file provide system initialization and RTOS start.

@note    Copyright (C) 2024  Daniel Zorychta <daniel.zorychta@gmail.com>

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

/*==============================================================================
  Include files
==============================================================================*/
#include "cpu/cpuctl.h"
#include "mm/heap.h"
#include "mm/mm.h"
#include "mm/shm.h"
#include "fs/vfs.h"
#include "lib/unarg.h"
#include "kernel/syscall.h"
#include "kernel/kpanic.h"
#include "kernel/kwrapper.h"
#include "kernel/kworker.h"
#include "kernel/sysfunc.h"
#include "kernel/khooks.h"
#include "net/netm.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/

/*==============================================================================
  Local types, enums definitions
==============================================================================*/

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  Local object definitions
==============================================================================*/

/*==============================================================================
  Exported object definitions
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Main function
 */
//==============================================================================
int main(void)
{
        _mm_init();
        _printk_init();

        printk("Welcome to dnx RTOS %s!", dnx_RTOS_version);
        printk("Running on platform %s", dnx_RTOS_platform_name);
        printk("CPU cores: %u\n", _CPUCTL_CORE_NUM);

        _cpuctl_init();
        _kernel_panic_init();
#if __OS_ENABLE_SHARED_MEMORY__ > 0
        _shm_init();
#endif
        _vfs_init();
#if _ENABLE_NETWORK_
        _net_init();
#endif
        _process_init();
        _syscall_init();
        _kworker_init();
        _kernel_start();

        return -1;
}

//==============================================================================
/**
 * @brief  Low-level init function. Function called before memory initialization.
 */
//==============================================================================
void low_level_init_0(void)
{
        if (reboot_address.key == REBOOT_TO_ADDRESS_KEY) {

                if ((reboot_address.address ^ REBOOT_TO_ADDRESS_VALIDATOR)
                        == reboot_address.address_validator) {

                        void (*call_address)(void) = cast(void*, reboot_address.address);

                        memset(&reboot_address, 0, sizeof(reboot_address));

                        call_address();
                }
        }
}

/*==============================================================================
  End of file
==============================================================================*/
