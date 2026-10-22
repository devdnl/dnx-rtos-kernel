/*=============================================================================
File     netm.h

Author   Daniel Zorychta

Brief    Network management.

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
@addtogroup dnx-net-h
*/
/**@{*/

#ifndef _NETM_H_
#define _NETM_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "lib/sys/types.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
/*------------------------------------------------------------------------------
  INET NETWORK FAMILY
------------------------------------------------------------------------------*/
/** Macro creates IPv4 address for INET family network. */
#define NETM_INET_IPv4(a, b, c, d) (((a & 0xFF) << 24) | ((b & 0xFF) << 16) | ((c & 0xFF) << 8) | ((d & 0xFF)))

/** Macro creates ANY address for INET family network. */
#define NETM_INET_IPv4_ANY NETM_INET_IPv4(0, 0, 0, 0)

/** Macro creates LOOPBACK address for INET family network. */
#define NETM_INET_IPv4_LOOPBACK NETM_INET_IPv4(127, 0, 0, 1)

/** Macro creates BROADCAST address for INET family network. */
#define NETM_INET_IPv4_BROADCAST NETM_INET_IPv4(255, 255, 255, 255)

/** Macro gets part <i>a</i> of INET family network address. */
#define NETM_INET_IPv4_a(ip) ((ip >> 24) & 0xFF)

/** Macro gets part <i>b</i> of INET family network address. */
#define NETM_INET_IPv4_b(ip) ((ip >> 16) & 0xFF)

/** Macro gets part <i>c</i> of INET family network address. */
#define NETM_INET_IPv4_c(ip) ((ip >> 8) & 0xFF)

/** Macro gets part <i>d</i> of INET family network address. */
#define NETM_INET_IPv4_d(ip) ((ip >> 0) & 0xFF)

#define NETM_INET_PROTOCOL_HOPOPT          0
#define NETM_INET_PROTOCOL_ICMP            1
#define NETM_INET_PROTOCOL_IGMP            2
#define NETM_INET_PROTOCOL_GGP             3
#define NETM_INET_PROTOCOL_IP_IN_IP        4
#define NETM_INET_PROTOCOL_ST              5
#define NETM_INET_PROTOCOL_TCP             6
#define NETM_INET_PROTOCOL_CBT             7
#define NETM_INET_PROTOCOL_EGP             8
#define NETM_INET_PROTOCOL_IGP             9
#define NETM_INET_PROTOCOL_BBN_RCC_MON     10
#define NETM_INET_PROTOCOL_NVP_II          11
#define NETM_INET_PROTOCOL_PUP             12
#define NETM_INET_PROTOCOL_ARGUS           13
#define NETM_INET_PROTOCOL_EMCON           14
#define NETM_INET_PROTOCOL_XNET            15
#define NETM_INET_PROTOCOL_CHAOS           16
#define NETM_INET_PROTOCOL_UDP             17
#define NETM_INET_PROTOCOL_MUX             18
#define NETM_INET_PROTOCOL_DCN_MEAS        19
#define NETM_INET_PROTOCOL_HMP             20
#define NETM_INET_PROTOCOL_PRM             21
#define NETM_INET_PROTOCOL_XNS_IDP         22
#define NETM_INET_PROTOCOL_TRUNK_1         23
#define NETM_INET_PROTOCOL_TRUNK_2         24
#define NETM_INET_PROTOCOL_LEAF_1          25
#define NETM_INET_PROTOCOL_LEAF_2          26
#define NETM_INET_PROTOCOL_RDP             27
#define NETM_INET_PROTOCOL_IRTP            28
#define NETM_INET_PROTOCOL_ISO_TP4         29
#define NETM_INET_PROTOCOL_NETBLT          30
#define NETM_INET_PROTOCOL_MFE_NSP         31
#define NETM_INET_PROTOCOL_MERIT_INP       32
#define NETM_INET_PROTOCOL_DCCP            33
#define NETM_INET_PROTOCOL_3PC             34
#define NETM_INET_PROTOCOL_IDPR            35
#define NETM_INET_PROTOCOL_XTP             36
#define NETM_INET_PROTOCOL_DDP             37
#define NETM_INET_PROTOCOL_IDPR_CMTP       38
#define NETM_INET_PROTOCOL_TP              39
#define NETM_INET_PROTOCOL_IL              40
#define NETM_INET_PROTOCOL_IPV6            41
#define NETM_INET_PROTOCOL_SDRP            42
#define NETM_INET_PROTOCOL_IPV6_ROUTE      43
#define NETM_INET_PROTOCOL_IPV6_FRAG       44
#define NETM_INET_PROTOCOL_IDRP            45
#define NETM_INET_PROTOCOL_RSVP            46
#define NETM_INET_PROTOCOL_GRE             47
#define NETM_INET_PROTOCOL_DSR             48
#define NETM_INET_PROTOCOL_BNA             49
#define NETM_INET_PROTOCOL_ESP             50
#define NETM_INET_PROTOCOL_AH              51
#define NETM_INET_PROTOCOL_I_NLSP          52
#define NETM_INET_PROTOCOL_SWIPE           53
#define NETM_INET_PROTOCOL_NARP            54
#define NETM_INET_PROTOCOL_MOBILE          55
#define NETM_INET_PROTOCOL_TLSP            56
#define NETM_INET_PROTOCOL_SKIP            57
#define NETM_INET_PROTOCOL_IPV6_ICMP       58
#define NETM_INET_PROTOCOL_IPV6_NONXT      59
#define NETM_INET_PROTOCOL_IPV6_OPTS       60
#define NETM_INET_PROTOCOL_ANY             61
#define NETM_INET_PROTOCOL_CFTP            62
#define NETM_INET_PROTOCOL_SAT_EXPAK       64
#define NETM_INET_PROTOCOL_KRYPTOLAN       65
#define NETM_INET_PROTOCOL_RVD             66
#define NETM_INET_PROTOCOL_IPPC            67
#define NETM_INET_PROTOCOL_SAT_MON         69
#define NETM_INET_PROTOCOL_VISA            70
#define NETM_INET_PROTOCOL_IPCU            71
#define NETM_INET_PROTOCOL_CPNX            72
#define NETM_INET_PROTOCOL_CPHB            73
#define NETM_INET_PROTOCOL_WSN             74
#define NETM_INET_PROTOCOL_PVP             75
#define NETM_INET_PROTOCOL_BR_SAT_MON      76
#define NETM_INET_PROTOCOL_SUN_ND          77
#define NETM_INET_PROTOCOL_WB_MON          78
#define NETM_INET_PROTOCOL_WB_EXPAK        79
#define NETM_INET_PROTOCOL_ISO_IP          80
#define NETM_INET_PROTOCOL_VMTP            81
#define NETM_INET_PROTOCOL_SECURE_VMTP     82
#define NETM_INET_PROTOCOL_VINES           83
#define NETM_INET_PROTOCOL_TTP             84
#define NETM_INET_PROTOCOL_IPTM            84
#define NETM_INET_PROTOCOL_NSFNET_IGP      85
#define NETM_INET_PROTOCOL_DGP             86
#define NETM_INET_PROTOCOL_TCF             87
#define NETM_INET_PROTOCOL_EIGRP           88
#define NETM_INET_PROTOCOL_OSPF            89
#define NETM_INET_PROTOCOL_SPRITE_RPC      90
#define NETM_INET_PROTOCOL_LARP            91
#define NETM_INET_PROTOCOL_MTP             92
#define NETM_INET_PROTOCOL_AX              93
#define NETM_INET_PROTOCOL_OS              94
#define NETM_INET_PROTOCOL_MICP            95
#define NETM_INET_PROTOCOL_SCC_SP          96
#define NETM_INET_PROTOCOL_ETHERIP         97
#define NETM_INET_PROTOCOL_ENCAP           98
#define NETM_INET_PROTOCOL_GMTP            100
#define NETM_INET_PROTOCOL_IFMP            101
#define NETM_INET_PROTOCOL_PNNI            102
#define NETM_INET_PROTOCOL_PIM             103
#define NETM_INET_PROTOCOL_ARIS            104
#define NETM_INET_PROTOCOL_SCPS            105
#define NETM_INET_PROTOCOL_QNX             106
#define NETM_INET_PROTOCOL_A               107
#define NETM_INET_PROTOCOL_IPCOMP          108
#define NETM_INET_PROTOCOL_SNP             109
#define NETM_INET_PROTOCOL_COMPAQ_PEER     110
#define NETM_INET_PROTOCOL_IPX_IN_IP       111
#define NETM_INET_PROTOCOL_VRRP            112
#define NETM_INET_PROTOCOL_PGM             113
#define NETM_INET_PROTOCOL_L2TP            115
#define NETM_INET_PROTOCOL_DDX             116
#define NETM_INET_PROTOCOL_IATP            117
#define NETM_INET_PROTOCOL_STP             118
#define NETM_INET_PROTOCOL_SRP             119
#define NETM_INET_PROTOCOL_UTI             120
#define NETM_INET_PROTOCOL_SMP             121
#define NETM_INET_PROTOCOL_SM              122
#define NETM_INET_PROTOCOL_PTP             123
#define NETM_INET_PROTOCOL_IS_IS           124
#define NETM_INET_PROTOCOL_FIRE            125
#define NETM_INET_PROTOCOL_CRTP            126
#define NETM_INET_PROTOCOL_CRUDP           127
#define NETM_INET_PROTOCOL_SSCOPMCE        128
#define NETM_INET_PROTOCOL_IPLT            129
#define NETM_INET_PROTOCOL_SPS             130
#define NETM_INET_PROTOCOL_PIPE            131
#define NETM_INET_PROTOCOL_SCTP            132
#define NETM_INET_PROTOCOL_FC              133
#define NETM_INET_PROTOCOL_RSVP_E2E_IGNORE 134
#define NETM_INET_PROTOCOL_MOBILITY        135
#define NETM_INET_PROTOCOL_UDPLITE         136
#define NETM_INET_PROTOCOL_MPLS_IN_IP      137
#define NETM_INET_PROTOCOL_MANET           138
#define NETM_INET_PROTOCOL_HIP             139
#define NETM_INET_PROTOCOL_SHIM6           140
#define NETM_INET_PROTOCOL_WESP            141
#define NETM_INET_PROTOCOL_ROHC            142
#define NETM_INET_PROTOCOL_ETHERNET        143
#define NETM_INET_PROTOCOL_AGGFRAG         144
#define NETM_INET_PROTOCOL_NSH             145

/*------------------------------------------------------------------------------
  SIPC NETWORK FAMILY
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
  CANNET NETWORK FAMILY
------------------------------------------------------------------------------*/
#define NETM_CANNET_MAX_PORT       7
#define NETM_CANNET_CAN_ID_MASK    0x10003FFF
#define NETM_CANNET_ADDR_BROADCAST 0x3FFF
#define NETM_CANNET_ADDR_ANY       0x0000

/*==============================================================================
  Exported object types
==============================================================================*/
/*------------------------------------------------------------------------------
  GENERIC NETWORK
------------------------------------------------------------------------------*/
/** Network family. */
typedef enum {
        NETM_FAMILY__INET,                    //!< Internet network.
        NETM_FAMILY__SIPC,                    //!< Serial Inter-Processor Communication
        NETM_FAMILY__CANNET,                  //!< CAN network Communication
        NETM_FAMILY__UNIX,                    //!< Unix Domain Socket Communication
        NETM_FAMILY__UDS = NETM_FAMILY__UNIX, //!< Unix Domain Socket Communication
        _NETM_FAMILY__COUNT
} NETM_family_t;

/** Socket type. */
typedef enum {
        NETM_SOCKET_TYPE__DATAGRAM, //!< DATAGRAM type socket
        NETM_SOCKET_TYPE__STREAM,   //!< STREAM type socket
        NETM_SOCKET_TYPE__RAW,      //!< RAW type socket
        NETM_SOCKET_TYPE_COUNT,     //!< number of protocols
} NETM_socket_type_t;

/** Control flags. */
typedef enum {
        NETM_FLAGS__NONE = 0,           //!< Flags not set.
        NETM_FLAGS__NOCOPY = (1 << 0),  //!< Buffer is not internally copy.
        NETM_FLAGS__COPY = (1 << 1),    //!< Buffer is internally copy.
        NETM_FLAGS__MORE = (1 << 2),    //!< More transfers.
        NETM_FLAGS__REWIND = (1 << 3),  //!< Read stream index is rewind.
        NETM_FLAGS__FREEBUF = (1 << 4), //!< Skip unread bytes after read and free buffer.
} NETM_flags_t;

/** Socket shutdown direction. */
typedef enum {
        NETM_SHUT__RD = (1 << 0),                         //!< Shutdown read direction.
        NETM_SHUT__WR = (1 << 1),                         //!< Shutdown write direction.
        NETM_SHUT__RDWR = (NETM_SHUT__RD | NETM_SHUT__WR) //!< Shutdown both directions.
} NETM_shut_t;

/** Generic socket address. This type accept all network family addresses. */
typedef void NETM_generic_sockaddr_t;

/** Generic interface configuration. This type accept all network family configurations.
 */
typedef void NETM_generic_config_t;

/** Generic network status. This type accept all network family statuses. */
typedef void NETM_generic_status_t;

/** Socket object definition. Protected object fields. */
typedef struct ksocket ksocket_t;

/*------------------------------------------------------------------------------
  INET NETWORK FAMILY
------------------------------------------------------------------------------*/
/** INET network address: IPv4. */
typedef uint32_t NETM_INET_IPv4_t;

/** INET socket address. This type contains IPv4 address and port. */
typedef struct {
        NETM_INET_IPv4_t addr; /*!< IPv4 address.*/
        u16_t port;            /*!< Port.*/
} NETM_INET_sockaddr_t;

/** INET network state. */
typedef enum {
        NETM_INET_STATE__NOT_CONFIGURED,   //!< Network not configured.
        NETM_INET_STATE__STATIC_IP,        //!< Static IP configuration.
        NETM_INET_STATE__DHCP_CONFIGURING, //!< DHCP address is configuring.
        NETM_INET_STATE__DHCP_CONFIGURED,  //!< DHCP address configured.
        NETM_INET_STATE__LINK_DISCONNECTED //!< Network interface not connected.
} NETM_INET_state_t;

/** INET configuration mode. */
typedef enum {
        NETM_INET_MODE__STATIC,      //!< Configure static IP.
        NETM_INET_MODE__DHCP_START,  //!< Start DHCP client.
        NETM_INET_MODE__DHCP_INFORM, //!< Inform DHCP server.
        NETM_INET_MODE__DHCP_RENEW,  //!< Renew DHCP connection.
} NETM_INET_mode_t;

/** INET configuration. */
typedef struct {
        NETM_INET_mode_t mode;    /*!< Configuration mode.*/
        NETM_INET_IPv4_t address; /*!< Address if static mode selected.*/
        NETM_INET_IPv4_t mask;    /*!< Network mask if static mode selected.*/
        NETM_INET_IPv4_t gateway; /*!< Gateway address if static mode selected.*/
} NETM_INET_config_t;

/** INET status. */
typedef struct {
        NETM_INET_state_t state;  /*!< Connection state.*/
        NETM_INET_IPv4_t address; /*!< Connection address.*/
        NETM_INET_IPv4_t mask;    /*!< Connection network mask.*/
        NETM_INET_IPv4_t gateway; /*!< Connection gateway.*/
        u8_t hw_addr[6];          /*!< MAC address.*/
        u64_t tx_bytes;           /*!< Number of transmitted bytes.*/
        u64_t rx_bytes;           /*!< Number of received bytes.*/
        u64_t tx_packets;         /*!< Number of transmitted packets.*/
        u64_t rx_packets;         /*!< Number of received packets.*/
} NETM_INET_status_t;

/*------------------------------------------------------------------------------
  SIPC NETWORK FAMILY
------------------------------------------------------------------------------*/
/** SIPC network state. */
typedef enum {
        NETM_SIPC_STATE__DOWN, //!< Network not configured.
        NETM_SIPC_STATE__UP,   //!< Network configured.
} NETM_SIPC_state_t;

/** SIPC configuration. */
typedef struct {
        u16_t MTU; //!< Maximal Transfer Unit
} NETM_SIPC_config_t;

/** SIPC status. */
typedef struct {
        NETM_SIPC_state_t state; /*!< Connection state.*/
        u16_t MTU;               /*!< Transfer size. */
        u64_t tx_bytes;          /*!< Number of transmitted bytes.*/
        u64_t rx_bytes;          /*!< Number of received bytes.*/
        u64_t tx_packets;        /*!< Number of transmitted packets.*/
        u64_t rx_packets;        /*!< Number of received packets.*/
} NETM_SIPC_status_t;

/** SIPC socket address. */
typedef struct {
        u8_t port; /*!< Port.*/
} NETM_SIPC_sockaddr_t;

/*------------------------------------------------------------------------------
  CANNET NETWORK FAMILY
------------------------------------------------------------------------------*/
/** CANNET network state. */
typedef enum {
        NETM_CANNET_STATE__DOWN, //!< Network not configured.
        NETM_CANNET_STATE__UP,   //!< Network configured.
} NETM_CANNET_state_t;

/** CANNET configuration. */
typedef struct {
        u16_t addr; //!< device address.
} NETM_CANNET_config_t;

/** CANNET status. */
typedef struct {
        NETM_CANNET_state_t state; /*!< Connection state.*/
        u16_t addr;                /*!< Address.*/
        u16_t MTU;                 /*!< Transfer size. */
        u64_t tx_bytes;            /*!< Number of transmitted bytes.*/
        u64_t rx_bytes;            /*!< Number of received bytes.*/
        u64_t tx_packets;          /*!< Number of transmitted packets.*/
        u64_t rx_packets;          /*!< Number of received packets.*/
} NETM_CANNET_status_t;

/** SIPC socket address. */
typedef struct {
        u16_t addr; /*!< Address.*/
        u8_t port;  /*!< Port.*/
} NETM_CANNET_sockaddr_t;

/*------------------------------------------------------------------------------
  UDS NETWORK FAMILY
------------------------------------------------------------------------------*/
/** UDS network state. */
typedef enum {
        NETM_UDS_STATE__DOWN, //!< Network not configured.
        NETM_UDS_STATE__UP,   //!< Network configured.
} NETM_UDS_state_t;

/** UDS configuration. */
typedef struct {
        u32_t stub;
} NETM_UDS_config_t;

/** UDS status. */
typedef struct {
        NETM_UDS_state_t state; /*!< Connection state.*/
        u64_t tx_bytes;         /*!< Number of transmitted bytes.*/
        u64_t rx_bytes;         /*!< Number of received bytes.*/
        u64_t tx_packets;       /*!< Number of transmitted packets.*/
        u64_t rx_packets;       /*!< Number of received packets.*/
} NETM_UDS_status_t;

/** UDS socket address. */
typedef struct {
        char path[64];
} NETM_UDS_sockaddr_t;

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/
#ifndef DOXYGEN
extern int _net_init(void);
extern int _net_ifadd(const char *, NETM_family_t, const char *);
extern int _net_ifrm(const char *);
extern int _net_ifdelete(const char *);
extern int _net_iflist(char **, size_t, size_t *);
extern int _net_ifup(const char *, const NETM_generic_config_t *, size_t);
extern int _net_ifdown(const char *);
extern int _net_ifstatus(const char *, NETM_family_t *, NETM_generic_status_t *, size_t);
extern int _net_gethostbyname(const char *, const char *, NETM_generic_sockaddr_t *, size_t);
extern int _net_socket_create(const char *, NETM_socket_type_t, u8_t, ksocket_t **);
extern int _net_socket_destroy(ksocket_t *);
extern int _net_socket_bind(ksocket_t *, const NETM_generic_sockaddr_t *, size_t);
extern int _net_socket_listen(ksocket_t *);
extern int _net_socket_accept(ksocket_t *, ksocket_t **);
extern int _net_socket_recv(ksocket_t *, void *, size_t, NETM_flags_t, size_t *);
extern int _net_socket_recvfrom(ksocket_t *, void *, size_t, NETM_flags_t, NETM_generic_sockaddr_t *, size_t, size_t *);
extern int _net_socket_send(ksocket_t *, const void *, size_t, NETM_flags_t, size_t *);
extern int _net_socket_sendto(ksocket_t *, const void *, size_t, NETM_flags_t, const NETM_generic_sockaddr_t *, size_t,
        size_t *);
extern int _net_socket_set_recv_timeout(ksocket_t *, uint32_t);
extern int _net_socket_set_send_timeout(ksocket_t *, uint32_t);
extern int _net_socket_get_recv_timeout(ksocket_t *, uint32_t *);
extern int _net_socket_get_send_timeout(ksocket_t *, uint32_t *);
extern int _net_socket_connect(ksocket_t *, const NETM_generic_sockaddr_t *, size_t);
extern int _net_socket_disconnect(ksocket_t *);
extern int _net_socket_shutdown(ksocket_t *, NETM_shut_t);
extern int _net_socket_getaddress(ksocket_t *, NETM_generic_sockaddr_t *, size_t);
extern u16_t _net_hton_u16(NETM_family_t, u16_t);
extern u32_t _net_hton_u32(NETM_family_t, u32_t);
extern u64_t _net_hton_u64(NETM_family_t, u64_t);
#endif

/*==============================================================================
  Exported inline functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _NETM_H_ */

/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
