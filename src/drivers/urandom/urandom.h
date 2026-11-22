/*==============================================================================
File    urandom.h

Author  Daniel Zorychta

Brief   Random number generator: interface of low level drivers (entropy sources)

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

#ifndef _URANDOM_H_
#define _URANDOM_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "urandom_ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported functions
==============================================================================*/
/**
 * @brief  Enable entropy source. Called by each driver instance.
 * @return URANDOM_SOURCE__HARDWARE when hardware noise source works,
 *         URANDOM_SOURCE__WEAK otherwise (only device specific data and timers).
 */
extern URANDOM_source_t _URANDOM_LLD__init(void);

/**
 * @brief  Disable entropy source. Called by each released driver instance.
 */
extern void _URANDOM_LLD__deinit(void);

/**
 * @brief  Read entropy. Output is mixed by the driver (need not be uniform).
 *         Weak source fills buffer with device specific data and timers.
 * @param  dst          destination buffer
 * @param  len          number of bytes
 * @return One of errno value (EIO: hardware source failed, buffer not valid).
 */
extern int _URANDOM_LLD__read(u8_t *dst, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* _URANDOM_H_ */
/*==============================================================================
  End of file
==============================================================================*/
