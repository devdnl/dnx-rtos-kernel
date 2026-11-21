/*==============================================================================
File    urandom_lld.c

Author  Daniel Zorychta

Brief   Random number generator: entropy source of POSIX simulator (host)

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

/*==============================================================================
  Include files
==============================================================================*/
#include "../urandom.h"
#include "posix/host/host.h"

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Enable entropy source (host random generator).
 */
//==============================================================================
URANDOM_source_t _URANDOM_LLD__init(void)
{
        return URANDOM_SOURCE__HARDWARE;
}

//==============================================================================
/**
 * @brief  Disable entropy source.
 */
//==============================================================================
void _URANDOM_LLD__deinit(void)
{
}

//==============================================================================
/**
 * @brief  Read entropy of host.
 */
//==============================================================================
int _URANDOM_LLD__read(u8_t *dst, size_t len)
{
        return (_host_random(dst, len) == 0) ? ESUCC : EIO;
}

/*==============================================================================
  End of file
==============================================================================*/
