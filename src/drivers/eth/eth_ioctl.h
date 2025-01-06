/*==============================================================================
@file    ETH_ioctl.h

@author  Daniel Zorychta

@brief   This driver support Ethernet interface ioctl request codes.

@note    Copyright (C) 2014 Daniel Zorychta <daniel.zorychta@gmail.com>

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
@defgroup drv-eth ETH Driver

\section drv-eth-desc Description
Driver handles Ethernet MAC peripheral. In general usage this driver is handled
directly by protocol stack.

\section drv-eth-sup-arch Supported architectures
\li stm32f1
\li stm32f4
\li stm32f7

\section drv-eth-ddesc Details
\subsection drv-eth-ddesc-num Meaning of major and minor numbers
The major number determines selection of Ethernet peripheral. Minor number
has no meaning and should be set to 0.

\subsection drv-eth-ddesc-init Driver initialization
To initialize driver the following code can be used:

@code
driver_init("ETH", 0, 0, "/dev/eth0");
@endcode

\subsection drv-eth-ddesc-release Driver release
To release driver the following code can be used:
@code
driver_release("ETH", 0, 0);
@endcode

\subsection drv-eth-ddesc-cfg Driver configuration
Entire driver configuration is realized by using configuration files in
the <tt>./config</tt> directory or by using Configtool.

\subsection drv-eth-ddesc-write Data write
Data to driver can be written as for regular file but if buffer is bigger than
MTU then buffer is send in few parts.

\subsection drv-eth-ddesc-read Data read
Data from driver can be received as regular file but with exception that
read data buffer should be a size of ETH_MAX_PACKET_SIZE.

\subsection drv-eth-ddesc-conf Peripheral start
There are only two steps that should be done to start Ethernet peripheral:

\li Ethernet device configuration (@ref IOCTL_ETHIF__CONFIGURE),
\li Ethernet peripheral start (@ref IOCTL_ETHIF__START).

After this operations packets will be received and transmitted.

\subsection drv-eth-ddesc-pkthdl Packet handling
To send and receive packets driver uses special data chains (@ref ETHIF_packet_t).
This object is a chain buffer that sum of all chain links payloads is a size of
entire packet. Object can contain only single packet. In both cases, receiving
and transmitting, the chain buffer should be created by user.

\subsubsection drv-eth-ddesc-pktrcv Packet receiving
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

\subsubsection drv-eth-ddesc-pktrans Packet transmitting
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

#ifndef _ETH_IOCTL_H_
#define _ETH_IOCTL_H_

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
 * Max packet size
 */
#define IOCTL_ETH_MAX_PACKET_SIZE IOCTL_ETHIF_MAX_PACKET_SIZE

/*
 * NOTE: all requests are defined in ethernet class file: class/ethif.h.
 */

/*==============================================================================
  Exported object types
==============================================================================*/
/*
 * NOTE: all types defined in ethernet class file: class/ethif.h.
 */

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

#endif /* _ETH_IOCTL_H_ */
/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
