/*==============================================================================
File    adc_ioctl.h

Author  Daniel Zorychta

Brief   ADC Driver

        Copyright (C) 2023 Daniel Zorychta <daniel.zorychta@gmail.com>

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
@defgroup drv-adc ADC Driver

\section drv-adc-desc Description
Driver handles ...

\section drv-adc-sup-arch Supported architectures
\li stm32f1

\section drv-adc-ddesc Details
\subsection drv-adc-ddesc-num Meaning of major and minor numbers
\todo Meaning of major and minor numbers

\subsubsection drv-adc-ddesc-numres Numeration restrictions
\todo Numeration restrictions

\subsection drv-adc-ddesc-init Driver initialization
To initialize driver the following code can be used:

@code
driver_init("ADC", 0, 0, "/dev/ADC0-0");
@endcode
@code
driver_init("ADC", 0, 1, "/dev/ADC0-1");
@endcode

\subsection drv-adc-ddesc-release Driver release
To release driver the following code can be used:
@code
driver_release("ADC", 0, 0);
@endcode
@code
driver_release("ADC", 0, 1);
@endcode

\subsection drv-adc-ddesc-cfg Driver configuration
\todo Driver configuration

\subsection drv-adc-ddesc-write Data write
\todo Data write

\subsection drv-adc-ddesc-read Data read
\todo Data read

@{
*/

#ifndef _ADC_IOCTL_H_
#define _ADC_IOCTL_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/ioctl_macros.h"
#include "sys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
/**
 *  @brief  Read channel.
 *  @param  [WR,RD] adc_measurement_t   channel with RAW data
 *  @return On success 0 is returned, otherwise -1.
 */
#define IOCTL_ADC__READ_CHANNEL _IOR(ADC, 0x00, adc_measurement_t*)

/*==============================================================================
  Exported object types
==============================================================================*/
typedef struct {
        uint8_t  channel;
        uint32_t raw_data;
} adc_measurement_t;

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

#endif /* _ADC_IOCTL_H_ */
/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
