/*==============================================================================
File    enc28j60_ioctl.h

Author  Daniel Zorychta

Brief   ENC28J60 chip driver

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
@defgroup drv-enc28j60 ENC28J60 Driver

\section drv-enc28j60-desc Description
Driver handles Ethernet ENC28J60 chip. In general usage this driver is handled
directly by protocol stack.

\section drv-enc28j60-sup-arch Supported architectures
\li any, SPI interface is a communication medium.

\section drv-enc28j60-ddesc Details
\subsection drv-enc28j60-ddesc-num Meaning of major and minor numbers
The major number determines selection of specified chip. Minor number
has no meaning and should be set to 0.

\subsection drv-enc28j60-ddesc-init Driver initialization
To initialize driver the following code can be used:

@code
driver_init("ENC28J60", 0, 0, "/dev/eth0");
@endcode

\subsection drv-enc28j60-ddesc-release Driver release
To release driver the following code can be used:
@code
driver_release("ENC28J60", 0, 0);
@endcode

\subsection drv-enc28j60-ddesc-cfg Driver configuration
Entire driver configuration is realized by using configuration files in
the <tt>./config</tt> directory or by using Configtool.

\subsection drv-enc28j60-ddesc-write Data write
Data to driver can be written as for regular file but if buffer is bigger than
MTU then buffer is send in few parts.

\subsection drv-enc28j60-ddesc-read Data read
Data from driver can be received as regular file but with exception that
read data buffer should be a size of ETHIF_MAX_PACKET_SIZE.

\subsection drv-enc28j60-ddesc-conf Peripheral start
There are only three steps that should be done to start Ethernet peripheral:

\li Chip configuration (@ref IOCTL_ENC28J60__CONFIGURE),
\li Ethernet interface configuration (@ref IOCTL_ETHIF__CONFIGURE),
\li Ethernet peripheral start (@ref IOCTL_ETHIF__START).

After this operations packets will be received and transmitted.

\subsection drv-enc28j60-ddesc-pkthdl Packet handling
To send and receive packets driver uses special data chains (@ref ETHIF_packet_t).
This object is a chain buffer that sum of all chain links payloads is a size of
entire packet. Object can contain only single packet. In both cases, receiving
and transmitting, the chain buffer should be created by user.

\subsubsection drv-enc28j60-ddesc-pktrcv Packet receiving
Packet receive example code:
\code
// initialization
// ...

FILE *eth = fopen(...);

// ...

// reception thread
while (true) {
        // receive packet from peripheral buffer
        uint8_t buf[ETHIF_MAX_PACKET_SIZE];
        ETHIF_packet_t pkt;
        pkt.payload = buf;
        pkt.length  = sizeof(buf);         // maximal size

        if (ioctl(fileno(eth), IOCTL_ETHIF__RECEIVE_PACKET, &pkt) != 0) {
                // ... error handling
        }

        // ... received data handling
        // size_t len = pkt.length;     // real size of received packet
}
\endcode

\subsubsection drv-enc28j60-ddesc-pktrans Packet transmitting
Packet sending example:
\code
// initialization
// ...

FILE *eth = fopen(...);

// ...

// transmit thread
while (true) {
        // ... data to send should be prepared earlier

        // buffer prepare - buffer can be created dynamically
        uint8_t buf[ETHIF_MAX_PACKET_SIZE];
        buf[0] = ...;
        buf[1] = ...;
        buf[...] = ...;

        ETHIF_packet_t pkt;
        pkt.payload = buf;
        pkt.payload_size = sizeof(buf);

        if (ioctl(fileno(eth), IOCTL_ETHIF__SEND_PACKET, &pkt) != 0) {
                // ... error handling
        }

        // ... packet transmitted
}
\endcode
@{
*/

#ifndef _ENC28J60_IOCTL_H_
#define _ENC28J60_IOCTL_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/ioctl_macros.h"
#include "drivers/class/ethif.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
/**
 * Max packet size.
 */
#define IOCTL_ENC28J60_MAX_PACKET_SIZE IOCTL_ETHIF_MAX_PACKET_SIZE

/**
 * @brief  Set MAC address.
 * @param  [WR] @ref ENC28J60_config_t: pointer to configuration object.
 * @return On success 0 is returned, otherwise -1 and @ref errno code is set.
 */
#define IOCTL_ENC28J60__CONFIGURE _IOW(ENC28J60, 0x00, ENC28J60_config_t *)

/*
 * NOTE: additional requests are defined in ethernet class file: class/ethif.h.
 */

/*==============================================================================
  Exported object types
==============================================================================*/
/*
 * NOTE: additional types defined in ethernet class file: class/ethif.h.
 */

/**
 * Type represent configuration.
 */
typedef struct {
        const char *spi_path;
} ENC28J60_config_t;

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

#endif /* _ENC28J60_IOCTL_H_ */
/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
