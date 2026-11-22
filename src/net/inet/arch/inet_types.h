/*==============================================================================
File     inet_types.h

Author   Daniel Zorychta

Brief

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
@defgroup INET_TYPES_H_ INET_TYPES_H_

Detailed Doxygen description.
*/
/**@{*/

#ifndef _INET_TYPES_H_
#define _INET_TYPES_H_

/*==============================================================================
  Include files
==============================================================================*/
#include <stdbool.h>
#include "kernel/sysfunc.h"
#include "lwip/netif.h"
#include "drivers/ioctl_requests.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/

/*==============================================================================
  Exported object types
==============================================================================*/
typedef struct {
        kmtx_t *access;
        ksem_t *thread_exit;
        kfile_t *if_file;
        tid_t if_thread;
        struct netif netif;
        uint rx_packets;
        uint tx_packets;
        uint rx_bytes;
        uint tx_bytes;
        bool ready;
        bool disconnected;
        bool configured;
        bool thread_run;
        bool netif_added;
        u8_t frame[1524];
} inet_t;

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/

/*==============================================================================
  Exported inline functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _INET_TYPES_H_ */

/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
