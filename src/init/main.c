/*=========================================================================*//**
@file    main.c

@author  Daniel Zorychta

@brief   This file provide system initialization and RTOS start.

@note    Copyright (C) 2012, 2013  Daniel Zorychta <daniel.zorychta@gmail.com>

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


*//*==========================================================================*/

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
#include "kernel/sysfunc.h"
#include "kernel/khooks.h"
#include "net/netm.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/
/* Stack size declared by linker script */
#define STACK_SIZE                      (((size_t)&__stack_size) - 512)

/* Stack start declared by linker script */
#define STACK_START                     ((void *)&__stack_start)

/*==============================================================================
  Local types, enums definitions
==============================================================================*/

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  Local object definitions
==============================================================================*/
static _mm_region_t main_stack;

/*==============================================================================
  Exported object definitions
==============================================================================*/
/** pointer to stack size value */
extern void *__stack_size;

/** pointer to stack start */
extern void *__stack_start;

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Task used to initialize dnx RTOS system. This task decrease stack
 *         usage in startup phase. Task after system initialization is deleted.
 */
//==============================================================================
void dnxinit(void *arg)
{
        UNUSED_ARG1(arg);

        printk("Welcome to dnx RTOS %s!", dnx_RTOS_version);
        printk("Running on platform %s", dnx_RTOS_platform_name);

#if __OS_ENABLE_SHARED_MEMORY__ > 0
        _assert(ESUCC == _shm_init());
#endif

        _assert(ESUCC == _vfs_init());

#if _ENABLE_NETWORK_
        _assert(ESUCC == _net_init());
#endif

        _assert(ESUCC == _syscall_init());

        /*
         * This code reuse the main() stack that after kernel start is abandoned.
         * The stack region is reused for HEAP purposes.
         * If cause problems (strange system behavior, kernel panics) disable
         * this option.
         */
        _mm_register_region(&main_stack, STACK_START, STACK_SIZE, _CPUCTL_STACK_REGION_FLAGS, "STARTSTACK");

        _task_exit();
}

//==============================================================================
/**
 * @brief Main function
 */
//==============================================================================
int main(void)
{
        _assert(ESUCC == _mm_init());
        _cpuctl_init();
        _assert(ESUCC == _kernel_panic_init());
        _assert(ESUCC == _task_create(dnxinit, "", (1024 / sizeof(StackType_t)), NULL, NULL));
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

                        reboot_address.key = 0;
                        reboot_address.address = 0;
                        reboot_address.address_validator = 0;

                        call_address();
                }
        }
}

/*==============================================================================
  End of file
==============================================================================*/
