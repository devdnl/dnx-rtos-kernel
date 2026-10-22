/*==============================================================================
File    memblock_ioctl.h

Author  Daniel Zorychta

Brief   Memory Block as file representation

        Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

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
@defgroup drv-memblock MEMBLOCK Driver

\section drv-memblock-desc Description
Driver handles mapping of memory block as file representation. The specified
memory address can be mapped as file.

\section drv-memblock-sup-arch Supported architectures
\li noarch

\section drv-memblock-ddesc Details
\subsection drv-memblock-ddesc-num Meaning of major and minor numbers
Major and minor numbers set independent device in system.

\subsubsection drv-memblock-ddesc-numres Numeration restrictions
Major and minor numbers can be used to create new node. Different minor number
is treated as different device even major number is the same.

\subsection drv-memblock-ddesc-init Driver initialization
To initialize driver the following code can be used:

@code
driver_init("MEMBLOCK", 0, 0, "/dev/memblk1");
@endcode
@code
driver_init("MEMBLOCK", 1, 0, "/dev/memblk2");
@endcode

\subsection drv-memblock-ddesc-release Driver release
To release driver the following code can be used:
@code
driver_release("MEMBLOCK", 0, 0);
@endcode
@code
driver_release("MEMBLOCK", 1, 0);
@endcode

\subsection drv-memblock-ddesc-cfg Driver configuration
Driver configuration should be done before usage and after initialization.
The best place to do this is user application. To configure particular MEMBLOCK
device the ioctl() function shall be used:

@code
#include <stdio.h>
#include <stdbool.h>
#include <sys/ioctl.h>

static const char *dev_path = "/dev/memblk-1";

FILE *dev = fopen(dev_path, "r+");
if (dev) {
      static const MEMBLOCK_config_t cfg = {
           .memory_address = 0x08004000,
           .block_size     = 8192,
           .access         = O_RDWR,
      };

      if (ioctl(fileno(dev), IOCTL_MEMBLK__SET_CONFIGURATION, &cfg) != 0) {
            perror(dev_path);
      }

      fclose(dev);
} else {
      perror(dev_path);
}

\subsection drv-memblock-ddesc-write Data write
The same as to regular file.

\subsection drv-memblock-ddesc-read Data read
The same as from regular file.

@{
*/

#ifndef _MEMBLOCK_IOCTL_H_
#define _MEMBLOCK_IOCTL_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/ioctl_macros.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
/**
 *  @brief  Set MEMBLOCK configuration.
 *  @param  [WR] @ref MEMBLOCK_config_t * MEMBLOCK peripheral configuration
 *  @return On success 0 is returned, otherwise -1.
 */
#define IOCTL_MEMBLOCK__SET_CONFIGURATION _IOW(MEMBLOCK, 0x00, const MEMBLOCK_config_t *)

/**
 *  @brief  Gets MEMBLOCK configuration
 *  @param  [RD] @ref MEMBLOCK_config_t * MEMBLOCK peripheral configuration
 *  @return On success 0 is returned, otherwise -1.
 */
#define IOCTL_MEMBLOCK__GET_CONFIGURATION _IOR(MEMBLOCK, 0x01, MEMBLOCK_config_t *)

/*==============================================================================
  Exported object types
==============================================================================*/
typedef struct {
        uint32_t memory_address; /*< Memory address. */
        uint32_t block_size;     /*< Block size in bytes. */
        uint32_t access;         /*< Access: O_RDWR, O_RDONLY, O_WRONLY. */
} MEMBLOCK_config_t;

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

#endif /* _MEMBLOCK_IOCTL_H_ */
/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
