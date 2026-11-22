/*==============================================================================
File    ioctl.h

Author  Daniel Zorychta

Brief   IOCTL requests for device class.

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
@defgroup DRIVER_CLASS_DEVICE_IOCTL_H_ DRIVER_CLASS_DEVICE_IOCTL_H_

Detailed Doxygen description.
*/
/**@{*/

#ifndef _DRIVER_CLASS_DEVICE_IOCTL_H_
#define _DRIVER_CLASS_DEVICE_IOCTL_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "../../ioctl_macros.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
/* predefined requests*/
/**
 *  @brief  Configure device by configuration string.
 *
 *  The request is handled by drivers that define request
 *  IOCTL_<MODULE>__CONFIGURE_STR. Keys, values and behavior of keys that are
 *  not given are described in section "Configuration string" of each driver
 *  (file <module>_ioctl.h, table: Key, Value, Omitted, Description). From
 *  shell the string is set by program modprobe:
 *  @code
 *  modprobe -i <MODULE> <major> <minor> <node> -c <string>   # init and configure
 *  modprobe -c <node> <string>                               # configure
 *  @endcode
 *
 *  Syntax: <tt>key=value[,key=value...]</tt>, e.g. "baud=9600,parity=even".
 *  A value ends at the next comma (it cannot contain a comma), keys are case
 *  sensitive, unknown keys are ignored, a key given twice: the first one is
 *  used. Values (column Value of driver tables):
 *  \li number - decimal, hexadecimal with prefix 0x (e.g. 0x50) or binary with
 *      prefix 0b; a leading zero means octal number, so use 8 not 08,
 *  \li bool - 1, yes, on, true, enable, set or 0, no, off, false, disable,
 *      reset,
 *  \li `a`, `b`, `c` - one of names, or index of name (first name is 0),
 *      other value: EINVAL,
 *  \li path - file path, name - text (without commas).
 *
 *  Column Omitted: "not changed" - current value is kept, "required" - EINVAL
 *  when the key is missing, other - value used when the key is missing.
 *
 *  @param  [WR] const char*            driver configuration string.
 *  @return On success (device configured) 0 is returned.
 *          On error -1 is returned and errno is set (EINVAL: invalid or
 *          missing value).
 */
#define IOCTL_DEVICE__CONFIGURE_STR _IOW(DEVICE, 0x80, const char *)

/*==============================================================================
  Exported object types
==============================================================================*/

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

#endif /* _DRIVER_CLASS_DEVICE_IOCTL_H_ */

/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
