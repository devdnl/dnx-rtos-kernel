/*==============================================================================
File    w5500_ioctl.h

Author  Daniel Zorychta

Brief   W5500 SPI Ethernet controller (MACRAW mode)

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
@defgroup drv-w5500 W5500 Driver

\section drv-w5500-desc Description
Driver handles WIZnet W5500 Ethernet controller connected to SPI device file
(SPI mode 0 or 3, up to 80 MHz). The chip works in MACRAW mode (socket 0 with
all 16 KiB of RX and TX memory, MAC filter: frames to the own MAC address,
broadcast and multicast), the hardware TCP/IP stack of the chip is not used:
the driver is an Ethernet interface (class ETHIF) of the TCP/IP stack of the
system, as ENC28J60 and ETH drivers. Received frames are polled (the
interrupt pin is not used).

\section drv-w5500-sup-arch Supported architectures
\li Any (noarch)

\section drv-w5500-ddesc Details
\subsection drv-w5500-ddesc-num Meaning of major and minor numbers
Major number selects chip, minor number must be 0.

\subsection drv-w5500-ddesc-init Driver initialization
@code
driver_init("W5500", 0, 0, "/dev/eth0");
@endcode

\subsection drv-w5500-ddesc-release Driver release
@code
driver_release("W5500", 0, 0);
@endcode

\subsection drv-w5500-ddesc-cfg Driver configuration
The interface is started as other Ethernet interfaces: @ref IOCTL_ETHIF__CONFIGURE
(MAC address, speed, duplex, auto-negotiation), @ref IOCTL_ETHIF__START (e.g.
by netmgr: <tt>path = /dev/eth0</tt> in netmgr.conf). The chip is identified
by version register (ENODEV: no chip) at start.

\subsection drv-w5500-ddesc-cfgstr Configuration string
Configuration string (@ref IOCTL_W5500__CONFIGURE_STR, program `modprobe -c`) is
a list of `key=value` pairs separated by commas, values are described in @ref
IOCTL_DEVICE__CONFIGURE_STR. All keys are set by each configuration (not given:
value of column Omitted). Configuration is possible only when interface is
stopped (EBUSY).

| Key          | Value  | Omitted  | Description |
|--------------|--------|----------|-------------|
| `spi_path`   | path   | required | SPI device file of the chip (used at IOCTL_ETHIF__START) |
| `reset_port` | number | not used | GPIO port index of RSTn pin (hardware reset at start), used with reset_pin |
| `reset_pin`  | number | not used | GPIO pin index of RSTn pin |

@code
modprobe -i W5500 0 0 /dev/eth0 -c spi_path=/dev/spi-eth,reset_port=1,reset_pin=10
@endcode

\subsection drv-w5500-ddesc-write Data write
Write of the file sends one frame (14..1514 bytes, without FCS).

\subsection drv-w5500-ddesc-read Data read
Read of the file receives one frame (buffer should have
ETHIF_MAX_PACKET_SIZE bytes, longer frames are truncated).

@{
*/

#ifndef _W5500_IOCTL_H_
#define _W5500_IOCTL_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/ioctl_macros.h"
#include "drivers/class/device/ioctl.h"
#include "drivers/class/ethif.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
/**
 * @brief  Configure driver.
 * @param  [WR] @ref W5500_config_t * configuration
 * @return On success 0 is returned, otherwise -1 and @ref errno code is set.
 */
#define IOCTL_W5500__CONFIGURE _IOW(W5500, 0x00, const W5500_config_t *)

/**
 *  @brief  Configure device by configuration string, keys and values:
 *          @ref drv-w5500-ddesc-cfgstr.
 *  @param  [WR] const char * configuration string
 *  @return On success 0 is returned, otherwise -1 and @ref errno is set.
 */
#define IOCTL_W5500__CONFIGURE_STR IOCTL_DEVICE__CONFIGURE_STR

/*
 * NOTE: additional requests are defined in ethernet class file: class/ethif.h.
 */

/*==============================================================================
  Exported object types
==============================================================================*/
/**
 * Driver configuration.
 */
typedef struct {
        const char *spi_path; /*!< SPI device file.*/
        bool reset_pin_used;  /*!< GPIO pin connected to RSTn is used.*/
        u8_t reset_port_idx;  /*!< Port index of RSTn pin (active low).*/
        u8_t reset_pin_idx;   /*!< Pin index of RSTn pin.*/
} W5500_config_t;

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

#endif /* _W5500_IOCTL_H_ */
/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
