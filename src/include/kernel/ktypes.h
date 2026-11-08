/*==============================================================================
@file    ktypes.h

@author  Daniel Zorychta

@brief   This file contains kernel types

@note    Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

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

#ifndef _KTYPES_H_
#define _KTYPES_H_

#ifdef __DNX_MODULE__
#error "Kernel internal header. Drivers include drivers/driver.h, file systems include fs/fs.h."
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include "lib/sys/types.h"
#include "kernel/kres.h"
#include <FreeRTOS.h>
#include <stdbool.h>

/*==============================================================================
  Exported symbolic constants/macros
==============================================================================*/

/*==============================================================================
  Exported symbolic constants/macros
==============================================================================*/

/*==============================================================================
  Exported types, enums definitions
==============================================================================*/
/** KERNELSPACE: task type */
typedef void task_t;

/** KERNELSPACE: task function type */
typedef void (*task_func_t)(void *);

/** KERNELSPACE: semaphore type */
typedef struct ksem {
        res_header_t header;
        void *object;
        StaticSemaphore_t buffer;
} ksem_t;

/** KERNELSPACE: queue type */
typedef struct kqueue {
        res_header_t header;
        void *object;
        StaticQueue_t buffer;
        uint8_t storage[];
} kqueue_t;

/** KERNELSPACE: mutex type */
typedef struct kmtx {
        res_header_t header;
        void *object;
        StaticSemaphore_t buffer;
        bool recursive;
} kmtx_t;

/** KERNELSPACE: flag type */
typedef struct kflag {
        res_header_t header;
        void *object;
        StaticEventGroup_t buffer;
} kflag_t;

/*==============================================================================
   Exported object declarations
==============================================================================*/

/*==============================================================================
  Exported function prototypes
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _KTYPES_H_ */
/*==============================================================================
  End of file
==============================================================================*/
