/*==============================================================================
@file    ethif.h

@author  Daniel Zorychta

@brief   Generic Ethernet interface ioctl request codes.

@note    Copyright (C) 2024 Daniel Zorychta <daniel.zorychta@gmail.com>

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

#ifndef _ETHIF_IOCTL_H_
#define _ETHIF_IOCTL_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "../ioctl_macros.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
/**
 * Maximal Ethernet packet size.
 */
#define IOCTL_ETHIF_MAX_PACKET_SIZE 1518U

/**
 * @brief  Set MAC address.
 * @param  [WR] @ref ETHIF_config_t: pointer to configuration object.
 * @return On success 0 is returned, otherwise -1 and @ref errno code is set.
 */
#define IOCTL_ETHIF__CONFIGURE _IOW(ETHIF, 0x80, ETHIF_config_t *)

/**
 * @brief  Receive packet to chain buffer.
 * @param  [RD] @ref ETHIF_packet_t*       chain buffer reference (each chain must have
 * allocated memory!).
 * @return On success 0 is returned, otherwise -1 and @ref errno code is set.
 */
#define IOCTL_ETHIF__RECEIVE_PACKET _IOR(ETHIF, 0x81, ETHIF_packet_t *)

/**
 * @brief  Send packet from chain buffer.
 * @param  [WR] @ref ETHIF_packet_t*              chain buffer reference.
 * @return On success 0 is returned, otherwise -1 and @ref errno code is set.
 */
#define IOCTL_ETHIF__SEND_PACKET _IOW(ETHIF, 0x82, ETHIF_packet_t *)

/**
 * @brief  Starts Ethernet interface.
 * @return On success 0 is returned, otherwise -1 and @ref errno code is set.
 */
#define IOCTL_ETHIF__START _IO(ETHIF, 0x83)

/**
 * @brief  Stop Ethernet interface.
 * @return On success 0 is returned, otherwise -1 and @ref errno code is set.
 */
#define IOCTL_ETHIF__STOP _IO(ETHIF, 0x84)

/**
 * @brief  Return link status.
 * @param  [RD] @ref ETHIF_status_t*        link status.
 * @return On success 0 is returned, otherwise -1 and @ref errno code is set.
 */
#define IOCTL_ETHIF__GET_STATUS _IOR(ETHIF, 0x85, ETHIF_status_t *)

/**
 * @brief  Set RX timeout.
 * @param  [RD] @ref u32_t* rx timeout in milliseconds.
 * @return On success 0 is returned, otherwise -1 and @ref errno code is set.
 */
#define IOCTL_ETHIF__SET_RX_TIMEOUT _IOW(ETHIF, 0x86, const u32_t *)

/**
 * @brief  Set RX timeout.
 * @param  [RD] @ref u32_t* tx timeout in milliseconds.
 * @return On success 0 is returned, otherwise -1 and @ref errno code is set.
 */
#define IOCTL_ETHIF__SET_TX_TIMEOUT _IOW(ETHIF, 0x87, const u32_t *)

/*==============================================================================
  Exported object types
==============================================================================*/
/**
 * Type represent packet chain.
 */
typedef struct {
        void *payload;
        u16_t length;
} ETHIF_packet_t;

/**
 * Type represent ethernet status.
 */
typedef struct {
        enum ETHIF_state {
                ETHIF_STATE__RESET,
                ETHIF_STATE__READY,
                ETHIF_STATE__ERROR,
        } state;

        enum ETHIF_link_status {
                ETHIF_LINK_STATUS__DISCONNECTED,
                ETHIF_LINK_STATUS__CONNECTED,
                ETHIF_LINK_STATUS__PHY_ERROR,
        } link_status;

        enum ETHIF_speed {
                ETHIF_SPEED__10Mbps,
                ETHIF_SPEED__100Mbps,
        } speed;

        enum ETHIF_duplex {
                ETHIF_DUPLEX__HALF,
                ETHIF_DUPLEX__FULL,
        } duplex;

        bool  configured;
        u8_t  MAC[6];
        u64_t rx_packets;
        u64_t tx_packets;
        u64_t rx_bytes;
        u64_t tx_bytes;
        u32_t rx_dropped_frames;
} ETHIF_status_t;

/**
 * Type used for Ethernet interface configuration.
 */
typedef struct {
        u8_t              MAC[6];
        enum ETHIF_speed  speed;
        enum ETHIF_duplex duplex;
        bool              auto_negotiation;
} ETHIF_config_t;

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

#endif /* _ETHIF_IOCTL_H_ */

/*==============================================================================
  End of file
==============================================================================*/
