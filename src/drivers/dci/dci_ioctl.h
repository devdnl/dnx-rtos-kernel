/*==============================================================================
File    dci_ioctl.h

Author  Daniel Zorychta

Brief   Digital Camera Interface Driver

        Copyright (C) 2026 Daniel Zorychta <daniel.zorychta>

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
@defgroup drv-dci DCI Driver (Digital Camera Interface)

\section drv-dci-desc Description
Driver handles Digital Camera Interface.

\section drv-dci-sup-arch Supported architectures
\li stm32f4

\section drv-dci-ddesc Details
\subsection drv-dci-ddesc-num Meaning of major and minor numbers
Major number means peripheral number of camera interface (camera0, camera1, etc).
Minor number should be set to 0.

\subsubsection drv-dci-ddesc-numres Numeration restrictions
The STM32F4 support only one camera interface, so major number is limited to 0.
Minor number has no meaning and should be set always to 0.

\subsection drv-dci-ddesc-init Driver initialization
To initialize driver the following code can be used:

@code
driver_init("DCI", 0, 0, "/dev/video");
@endcode

\subsection drv-dci-ddesc-release Driver release
To release driver the following code can be used:
@code
driver_release("DCI", 0, 0);
@endcode

\subsection drv-dci-ddesc-cfg Driver configuration
Only static configuration is possible via configuration files (Configtool).

\subsection drv-dci-ddesc-write Data write
Writing is not possible.

\subsection drv-dci-ddesc-read Data read
Only full frame can be read at once. Other transfer sizes are not supported.

\subsection drv-dci-ddesc-ioctl Driver parameters
User can get current driver parameters (related with frame size) by using
ioctl() function with IOCTL_DCI__GET_PARAMS request.

@{
*/

#ifndef _DCI_IOCTL_H_
#define _DCI_IOCTL_H_

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
 * @brief  Get image parameters.
 * @param  [RD] @ref DCI_params_t*        image parameters
 * @return On success 0 is returned, otherwise -1 and @ref errno code is set.
 */
#define IOCTL_DCI__GET_PARAMS _IOR(DCI, 0x00, DCI_params_t *)

/*==============================================================================
  Exported object types
==============================================================================*/
typedef struct {
        u16_t x_resolution;   /*!< X camera resolution */
        u16_t y_resolution;   /*!< Y camera resolution */
        u8_t bytes_per_pixel; /*!< Bytes per pixel     */
        bool JPEG_mode;       /*!< JPEG mode           */
} DCI_params_t;

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

#endif /* _DCI_IOCTL_H_ */
/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
