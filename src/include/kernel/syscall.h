/*==============================================================================
@file    syscall.h

@author  Daniel Zorychta

@brief   System call handling

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

#ifndef _KRNSPACE_SYSCALL_H_
#define _KRNSPACE_SYSCALL_H_

#ifdef __DNX_MODULE__
#error "Kernel internal header. Drivers include drivers/driver.h, file systems include fs/fs.h."
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include <kernel/process.h>

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
#define REBOOT_TO_ADDRESS_KEY       0x3626681b
#define REBOOT_TO_ADDRESS_VALIDATOR 0xdb526861

/*==============================================================================
  Exported object types
==============================================================================*/
struct reboot_to_address {
        u32_t key;
        u32_t address;
        u32_t address_validator;
};

/*==============================================================================
  Exported objects
==============================================================================*/
extern struct _process *_kworker_proc;
extern const char *const dnx_RTOS_version;
extern const char *const dnx_RTOS_platform_name;
extern struct reboot_to_address reboot_address __attribute__((section(".noinit")));

/*==============================================================================
  Exported functions
==============================================================================*/
extern int _syscall_init();
extern int _syscall_kworker_process(int, char **);

/*==============================================================================
  Exported inline functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _KRNSPACE_SYSCALL_H_ */
/*==============================================================================
  End of file
==============================================================================*/
