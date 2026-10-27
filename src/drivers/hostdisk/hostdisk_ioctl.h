/*==============================================================================
File    hostdisk_ioctl.h

Author  Daniel Zorychta

Brief   Host disk driver (POSIX simulator).

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
@defgroup drv-hostdisk HOSTDISK Driver

\section drv-hostdisk-desc Description
Driver gives access to the file of host operating system as to the block
device. Driver is available only in the POSIX simulator. Files are connected
to disks by simulator program options:

@code
dnx-sim --disk 0=romfs.bin:ro --disk 1=sdcard.img --disk 2=flash.img:ro
@endcode

\section drv-hostdisk-sup-arch Supported architectures
\li posix

\section drv-hostdisk-ddesc Details
\subsection drv-hostdisk-ddesc-num Meaning of major and minor numbers
Major number selects host disk (simulator option --disk <major>=<file>).

\subsubsection drv-hostdisk-ddesc-numres Numeration restrictions
Major number: 0-7. Minor number is not used and should be set to 0.

\subsection drv-hostdisk-ddesc-init Driver initialization
To initialize driver the following code can be used:

@code
driver_init("HOSTDISK", 0, 0, "/dev/hd0");
@endcode

\subsection drv-hostdisk-ddesc-release Driver release
To release driver the following code can be used:
@code
driver_release("HOSTDISK", 0, 0);
@endcode

\subsection drv-hostdisk-ddesc-cfg Driver configuration
No driver configuration.

\subsection drv-hostdisk-ddesc-write Data write
The same as regular file.

\subsection drv-hostdisk-ddesc-read Data read
The same as regular file.

@{
*/

#ifndef _HOSTDISK_IOCTL_H_
#define _HOSTDISK_IOCTL_H_

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

#endif /* _HOSTDISK_IOCTL_H_ */
/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
