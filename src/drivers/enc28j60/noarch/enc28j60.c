/*==============================================================================
File    enc28j60.c

Author  Daniel Zorychta

Brief   ENC28J60 chip driver

        Copyright (C) 2024 Daniel Zorychta <daniel.zorychta@gmail.com>

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

/*
 * NOTE:
 * This driver is not calculating packet checksums of IP, UDP, TCP, ICMP, and ICMP6
 * packets! Enable checksum calculation for this packets in TPCIP stack.
 */

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "../enc28j60_ioctl.h"
#include "spi/spi_ioctl.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define MUTEX_TIMEOUT 10000

/*
 * ENC28J60 Control Registers
 * Control register definitions are a combination of address,
 * bank number, and Ethernet/MAC/PHY indicator bits.
 * - Register address   (bits 0-4)
 * - Bank number        (bits 5-6)
 * - MAC/MII indicator  (bit 7)
 */
#define ADDR_MASK 0x1F
#define BANK_MASK 0x60
#define SPRD_MASK 0x80
/* All-bank registers */
#define EIE   0x1B
#define EIR   0x1C
#define ESTAT 0x1D
#define ECON2 0x1E
#define ECON1 0x1F
/* Bank 0 registers */
#define ERDPTL   (0x00 | 0x00)
#define ERDPTH   (0x01 | 0x00)
#define EWRPTL   (0x02 | 0x00)
#define EWRPTH   (0x03 | 0x00)
#define ETXSTL   (0x04 | 0x00)
#define ETXSTH   (0x05 | 0x00)
#define ETXNDL   (0x06 | 0x00)
#define ETXNDH   (0x07 | 0x00)
#define ERXSTL   (0x08 | 0x00)
#define ERXSTH   (0x09 | 0x00)
#define ERXNDL   (0x0A | 0x00)
#define ERXNDH   (0x0B | 0x00)
#define ERXRDPTL (0x0C | 0x00)
#define ERXRDPTH (0x0D | 0x00)
#define ERXWRPTL (0x0E | 0x00)
#define ERXWRPTH (0x0F | 0x00)
#define EDMASTL  (0x10 | 0x00)
#define EDMASTH  (0x11 | 0x00)
#define EDMANDL  (0x12 | 0x00)
#define EDMANDH  (0x13 | 0x00)
#define EDMADSTL (0x14 | 0x00)
#define EDMADSTH (0x15 | 0x00)
#define EDMACSL  (0x16 | 0x00)
#define EDMACSH  (0x17 | 0x00)
/* Bank 1 registers */
#define EHT0    (0x00 | 0x20)
#define EHT1    (0x01 | 0x20)
#define EHT2    (0x02 | 0x20)
#define EHT3    (0x03 | 0x20)
#define EHT4    (0x04 | 0x20)
#define EHT5    (0x05 | 0x20)
#define EHT6    (0x06 | 0x20)
#define EHT7    (0x07 | 0x20)
#define EPMM0   (0x08 | 0x20)
#define EPMM1   (0x09 | 0x20)
#define EPMM2   (0x0A | 0x20)
#define EPMM3   (0x0B | 0x20)
#define EPMM4   (0x0C | 0x20)
#define EPMM5   (0x0D | 0x20)
#define EPMM6   (0x0E | 0x20)
#define EPMM7   (0x0F | 0x20)
#define EPMCSL  (0x10 | 0x20)
#define EPMCSH  (0x11 | 0x20)
#define EPMOL   (0x14 | 0x20)
#define EPMOH   (0x15 | 0x20)
#define EWOLIE  (0x16 | 0x20)
#define EWOLIR  (0x17 | 0x20)
#define ERXFCON (0x18 | 0x20)
#define EPKTCNT (0x19 | 0x20)
/* Bank 2 registers */
#define MACON1 (0x00 | 0x40 | SPRD_MASK)
/* #define MACON2       (0x01|0x40|SPRD_MASK) */
#define MACON3   (0x02 | 0x40 | SPRD_MASK)
#define MACON4   (0x03 | 0x40 | SPRD_MASK)
#define MABBIPG  (0x04 | 0x40 | SPRD_MASK)
#define MAIPGL   (0x06 | 0x40 | SPRD_MASK)
#define MAIPGH   (0x07 | 0x40 | SPRD_MASK)
#define MACLCON1 (0x08 | 0x40 | SPRD_MASK)
#define MACLCON2 (0x09 | 0x40 | SPRD_MASK)
#define MAMXFLL  (0x0A | 0x40 | SPRD_MASK)
#define MAMXFLH  (0x0B | 0x40 | SPRD_MASK)
#define MAPHSUP  (0x0D | 0x40 | SPRD_MASK)
#define MICON    (0x11 | 0x40 | SPRD_MASK)
#define MICMD    (0x12 | 0x40 | SPRD_MASK)
#define MIREGADR (0x14 | 0x40 | SPRD_MASK)
#define MIWRL    (0x16 | 0x40 | SPRD_MASK)
#define MIWRH    (0x17 | 0x40 | SPRD_MASK)
#define MIRDL    (0x18 | 0x40 | SPRD_MASK)
#define MIRDH    (0x19 | 0x40 | SPRD_MASK)
/* Bank 3 registers */
#define MAADR1  (0x00 | 0x60 | SPRD_MASK)
#define MAADR0  (0x01 | 0x60 | SPRD_MASK)
#define MAADR3  (0x02 | 0x60 | SPRD_MASK)
#define MAADR2  (0x03 | 0x60 | SPRD_MASK)
#define MAADR5  (0x04 | 0x60 | SPRD_MASK)
#define MAADR4  (0x05 | 0x60 | SPRD_MASK)
#define EBSTSD  (0x06 | 0x60)
#define EBSTCON (0x07 | 0x60)
#define EBSTCSL (0x08 | 0x60)
#define EBSTCSH (0x09 | 0x60)
#define MISTAT  (0x0A | 0x60 | SPRD_MASK)
#define EREVID  (0x12 | 0x60)
#define ECOCON  (0x15 | 0x60)
#define EFLOCON (0x17 | 0x60)
#define EPAUSL  (0x18 | 0x60)
#define EPAUSH  (0x19 | 0x60)
/* PHY registers */
#define PHCON1  0x00
#define PHSTAT1 0x01
#define PHHID1  0x02
#define PHHID2  0x03
#define PHCON2  0x10
#define PHSTAT2 0x11
#define PHIE    0x12
#define PHIR    0x13
#define PHLCON  0x14

/* ENC28J60 EIE Register Bit Definitions */
#define EIE_INTIE  0x80
#define EIE_PKTIE  0x40
#define EIE_DMAIE  0x20
#define EIE_LINKIE 0x10
#define EIE_TXIE   0x08
#define EIE_WOLIE  0x04
#define EIE_TXERIE 0x02
#define EIE_RXERIE 0x01

/* ENC28J60 EIR Register Bit Definitions */
#define EIR_PKTIF  0x40
#define EIR_DMAIF  0x20
#define EIR_LINKIF 0x10
#define EIR_TXIF   0x08
#define EIR_WOLIF  0x04
#define EIR_TXERIF 0x02
#define EIR_RXERIF 0x01

/* ENC28J60 ESTAT Register Bit Definitions */
#define ESTAT_INT     0x80
#define ESTAT_LATECOL 0x10
#define ESTAT_RXBUSY  0x04
#define ESTAT_TXABRT  0x02
#define ESTAT_CLKRDY  0x01

/* ENC28J60 ECON2 Register Bit Definitions */
#define ECON2_AUTOINC 0x80
#define ECON2_PKTDEC  0x40
#define ECON2_PWRSV   0x20
#define ECON2_VRPS    0x08

/* ENC28J60 ECON1 Register Bit Definitions */
#define ECON1_TXRST  0x80
#define ECON1_RXRST  0x40
#define ECON1_DMAST  0x20
#define ECON1_CSUMEN 0x10
#define ECON1_TXRTS  0x08
#define ECON1_RXEN   0x04
#define ECON1_BSEL1  0x02
#define ECON1_BSEL0  0x01

/* ENC28J60 MACON1 Register Bit Definitions */
#define MACON1_LOOPBK  0x10
#define MACON1_TXPAUS  0x08
#define MACON1_RXPAUS  0x04
#define MACON1_PASSALL 0x02
#define MACON1_MARXEN  0x01

/* ENC28J60 MACON2 Register Bit Definitions */
#define MACON2_MARST   0x80
#define MACON2_RNDRST  0x40
#define MACON2_MARXRST 0x08
#define MACON2_RFUNRST 0x04
#define MACON2_MATXRST 0x02
#define MACON2_TFUNRST 0x01

/* ENC28J60 MACON3 Register Bit Definitions */
#define MACON3_PADCFG2 0x80
#define MACON3_PADCFG1 0x40
#define MACON3_PADCFG0 0x20
#define MACON3_TXCRCEN 0x10
#define MACON3_PHDRLEN 0x08
#define MACON3_HFRMLEN 0x04
#define MACON3_FRMLNEN 0x02
#define MACON3_FULDPX  0x01

/* ENC28J60 MICMD Register Bit Definitions */
#define MICMD_MIISCAN 0x02
#define MICMD_MIIRD   0x01

/* ENC28J60 MISTAT Register Bit Definitions */
#define MISTAT_NVALID 0x04
#define MISTAT_SCAN   0x02
#define MISTAT_BUSY   0x01

/* ENC28J60 ERXFCON Register Bit Definitions */
#define ERXFCON_UCEN  0x80
#define ERXFCON_ANDOR 0x40
#define ERXFCON_CRCEN 0x20
#define ERXFCON_PMEN  0x10
#define ERXFCON_MPEN  0x08
#define ERXFCON_HTEN  0x04
#define ERXFCON_MCEN  0x02
#define ERXFCON_BCEN  0x01

/* ENC28J60 PHY PHCON1 Register Bit Definitions */
#define PHCON1_PRST    0x8000
#define PHCON1_PLOOPBK 0x4000
#define PHCON1_PPWRSV  0x0800
#define PHCON1_PDPXMD  0x0100

/* ENC28J60 PHY PHSTAT1 Register Bit Definitions */
#define PHSTAT1_PFDPX  0x1000
#define PHSTAT1_PHDPX  0x0800
#define PHSTAT1_LLSTAT 0x0004
#define PHSTAT1_JBSTAT 0x0002

/* ENC28J60 PHY PHSTAT2 Register Bit Definitions */
#define PHSTAT2_TXSTAT  (1 << 13)
#define PHSTAT2_RXSTAT  (1 << 12)
#define PHSTAT2_COLSTAT (1 << 11)
#define PHSTAT2_LSTAT   (1 << 10)
#define PHSTAT2_DPXSTAT (1 << 9)
#define PHSTAT2_PLRITY  (1 << 5)

/* ENC28J60 PHY PHCON2 Register Bit Definitions */
#define PHCON2_FRCLINK 0x4000
#define PHCON2_TXDIS   0x2000
#define PHCON2_JABBER  0x0400
#define PHCON2_HDLDIS  0x0100

/* ENC28J60 PHY PHIE Register Bit Definitions */
#define PHIE_PLNKIE (1 << 4)
#define PHIE_PGEIE  (1 << 1)

/* ENC28J60 PHY PHIR Register Bit Definitions */
#define PHIR_PLNKIF (1 << 4)
#define PHIR_PGEIF  (1 << 1)

/* ENC28J60 Packet Control Byte Bit Definitions */
#define PKTCTRL_PHUGEEN   0x08
#define PKTCTRL_PPADEN    0x04
#define PKTCTRL_PCRCEN    0x02
#define PKTCTRL_POVERRIDE 0x01

/* ENC28J60 Transmit Status Vector */
#define TSV_TXBYTECNT       0
#define TSV_TXCOLLISIONCNT  16
#define TSV_TXCRCERROR      20
#define TSV_TXLENCHKERROR   21
#define TSV_TXLENOUTOFRANGE 22
#define TSV_TXDONE          23
#define TSV_TXMULTICAST     24
#define TSV_TXBROADCAST     25
#define TSV_TXPACKETDEFER   26
#define TSV_TXEXDEFER       27
#define TSV_TXEXCOLLISION   28
#define TSV_TXLATECOLLISION 29
#define TSV_TXGIANT         30
#define TSV_TXUNDERRUN      31
#define TSV_TOTBYTETXONWIRE 32
#define TSV_TXCONTROLFRAME  48
#define TSV_TXPAUSEFRAME    49
#define TSV_BACKPRESSUREAPP 50
#define TSV_TXVLANTAGFRAME  51

#define TSV_SIZE            7
#define TSV_BYTEOF(x)       ((x) / 8)
#define TSV_BITMASK(x)      (1 << ((x) % 8))
#define TSV_GETBIT(x, y)    (((x)[TSV_BYTEOF(y)] & TSV_BITMASK(y)) ? 1 : 0)

/* ENC28J60 Receive Status Vector */
#define RSV_RXLONGEVDROPEV  16
#define RSV_CARRIEREV       18
#define RSV_CRCERROR        20
#define RSV_LENCHECKERR     21
#define RSV_LENOUTOFRANGE   22
#define RSV_RXOK            23
#define RSV_RXMULTICAST     24
#define RSV_RXBROADCAST     25
#define RSV_DRIBBLENIBBLE   26
#define RSV_RXCONTROLFRAME  27
#define RSV_RXPAUSEFRAME    28
#define RSV_RXUNKNOWNOPCODE 29
#define RSV_RXTYPEVLAN      30

#define RSV_SIZE            6
#define RSV_BITMASK(x)      (1 << ((x) - 16))
#define RSV_GETBIT(x, y)    (((x) & RSV_BITMASK(y)) ? 1 : 0)

/* SPI operation codes */
#define ENC28J60_READ_CTRL_REG  0x00
#define ENC28J60_READ_BUF_MEM   0x3A
#define ENC28J60_WRITE_CTRL_REG 0x40
#define ENC28J60_WRITE_BUF_MEM  0x7A
#define ENC28J60_BIT_FIELD_SET  0x80
#define ENC28J60_BIT_FIELD_CLR  0xA0
#define ENC28J60_SOFT_RESET     0xFF

/* maximum ethernet frame length */
#define MAX_FRAMELEN 1518

/* Preferred half duplex: LEDA: Link status LEDB: Rx/Tx activity */
#define ENC28J60_LAMPS_MODE     0x3476

#define ENC28J60_MEMORY_SIZE    8192
#define ENC28J60_MAX_FRAME_SIZE 1518

#define ENC28J60_TXEND          (ENC28J60_MEMORY_SIZE - 1)
#define ENC28J60_TXSTART        (ENC28J60_MEMORY_SIZE - ENC28J60_MAX_FRAME_SIZE - 2)

/*
 * From errata:
 *
 * Module: Memory (Ethernet Buffer)
 * The receive hardware maintains an internal Write Pointer that defines the area in the
 * receive buffer where bytes arriving over the Ethernet are written. This internal Write
 * Pointer should be updated with the value stored in ERXST whenever the Receive Buffer
 * Start Pointer, ERXST, or the Receive Buffer End Pointer, ERXND, is written to by the
 * host microcontroller. Sometimes, when ERXST or ERXND is written to, the exact value,
 * 0000h, is stored in the Internal Receive Write Pointer instead of the ERXST address.
 *
 * Work around
 * Use the lower segment of the buffer memory for the receive buffer, starting at address
 * 0000h. For example, use the range (0000h to n) for the receive buffer, and ((n + 1) –
 * 8191) for the transmit buffer.
 */
#define ENC28J60_RXSTART 0
#define ENC28J60_RXEND   (ENC28J60_TXSTART - 1)

/*
 * Word registers. Each has a L and H, L being the lower address and H the upper.
 * H is always L + 1. Thus the write_register_word simply writes address and address + 1
 *
 * Here the L is dropped to make it consistent with the datasheet naming
 */
#define EDMAST       EDMASTL
#define EDMAND       EDMANDL
#define ERXND        ERXNDL
#define EDMACS       EDMACSL
#define EBSTCS       EBSTCSL
#define MIWR         MIWRL
#define MIRD         MIRDL
#define ERDPT        ERDPTL
#define EWRPT        EWRPTL
#define ERXWRPT      ERXWRPTL
#define ERXST        ERXSTL
#define ERXRDPT      ERXRDPTL
#define ETXST        ETXSTL
#define ETXND        ETXNDL
#define MAIPG        MAIPGL
#define MAMXFL       MAMXFLL

#define MACON2       (0x01 | 0x40 | SPRD_MASK)
#define MACON4_DEFER 0x40

#define ALL_BANKS    0xE0

#define RSV_RX_OK    0b010000000u
#define TSV_TX_DONE  0b010000000u

/*
 * Built-in self test (BIST) bits
 */
#define EBSTCON_PSV2                 0x80
#define EBSTCON_PSV1                 0x40
#define EBSTCON_PSV0                 0x20
#define EBSTCON_PSEL                 0x10
#define EBSTCON_TMSEL1               0x08
#define EBSTCON_TMSEL0               0x04
#define EBSTCON_TME                  0x02
#define EBSTCON_BISTST               0x01

#define BIST_RANDOM_FILL             0x00
#define BIST_ADDRESS_FILL            EBSTCON_TMSEL0
#define BIST_PATTERN_SHIFT           EBSTCON_TMSEL1

#define ENC28J60_TRANSMIT_TIMEOUT_MS 500

#if (ENC28J60_RXSTART < 0)
#error "ENC28J60_RXSTART < 0"
#endif

#if (ENC28J60_RXSTART >= ENC28J60_RXEND)
#error "ENC28J60_RXSTART >= ENC28J60_RXEND"
#endif

#if ((ENC28J60_RXEND % 2) == 0)
#error "ENC28J60_RXEND must be an odd number; see ENC28J60 errata"
#endif

#if (ENC28J60_TXSTART < 0)
#error "ENC28J60_TXSTART < 0"
#endif

#if (ENC28J60_TXSTART >= ENC28J60_TXEND)
#error "ENC28J60_TXSTART >= ENC28J60_TXEND"
#endif

#if (ENC28J60_TXEND >= ENC28J60_MEMORY_SIZE)
#error "ENC28J60_TXEND > ENC28J60_MEMORY_SIZE"
#endif

#if ENC28J60_TRANSMIT_TIMEOUT_MS <= 0
#error "ENC28J60_TRANSMIT_TIMEOUT_MS <= 0"
#endif

/*==============================================================================
  Local object types
==============================================================================*/
typedef uint8_t enc28j60_addr_t;
typedef uint8_t enc28j60_reg_t;

typedef struct {
        kfile_t          *spi;
        kmtx_t           *mutex;
        u8_t              major;
        u8_t              minor;
        ENC28J60_config_t config;
        ETHIF_config_t    ethif_config;
        bool              configured;
        bool              run;
        u32_t             rx_timeout_ms;
        u32_t             tx_timeout_ms;

        struct {
                u8_t  bank;
                u16_t next_packet_pointer;
        } enc28j60;

        struct {
                u32_t rx_packets;
                u32_t tx_packets;
                u64_t rx_bytes;
                u64_t tx_bytes;
        } stats;
} ENC28J60_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int eth_start(ENC28J60_t *hdl);

static int eth_stop(ENC28J60_t *hdl);

static int eth_get_info(ENC28J60_t *hdl, ETHIF_status_t *status);

static int eth_flush(ENC28J60_t *hdl);

static int eth_packet_receive(ENC28J60_t *hdl, ETHIF_packet_t *pkt);

static int eth_packet_send(ENC28J60_t *hdl, const ETHIF_packet_t *pkt);

static int enc28j60_write_op(ENC28J60_t *hdl, const enc28j60_reg_t op,
        enc28j60_addr_t address, uint8_t value);

static int enc28j60_read_op(ENC28J60_t *hdl, const enc28j60_reg_t op,
        enc28j60_addr_t address, uint8_t *value);

static int enc28j60_set_bank(ENC28J60_t *hdl, enc28j60_addr_t address);

static int enc28j60_set_register_bit(ENC28J60_t *hdl, enc28j60_addr_t address,
        uint8_t mask);

static int enc28j60_clear_register_bit(ENC28J60_t *hdl, enc28j60_addr_t address,
        uint8_t mask);

static int enc28j60_write_register_byte(ENC28J60_t *hdl, enc28j60_addr_t address,
        uint8_t value);

static uint8_t enc28j60_read_register_byte(ENC28J60_t *hdl, enc28j60_addr_t address,
        uint8_t *value);

static int enc28j60_write_register_word(ENC28J60_t *hdl, enc28j60_addr_t address,
        uint16_t value);

static int enc28j60_read_register_word(ENC28J60_t *hdl, enc28j60_addr_t address,
        uint16_t *value);

static int enc28j60_buffer_write(ENC28J60_t *hdl, const uint8_t *buffer, size_t length);

static int enc28j60_buffer_read(ENC28J60_t *hdl, uint8_t *buffer, size_t length);

static int enc28j60_reset(ENC28J60_t *hdl);

static inline int enc28j60_phy_wait(ENC28J60_t *hdl);

static int enc28j60_phy_write(ENC28J60_t *hdl, enc28j60_addr_t address, uint16_t value);

static int enc28j60_phy_read(ENC28J60_t *hdl, enc28j60_addr_t address, uint16_t *value);

static int enc28j60_configure(ENC28J60_t *hdl);

static int enc25j60_run_built_in_self_test(ENC28J60_t *hdl, u8_t address_fill);

static int enc28j60_get_link_status(ENC28J60_t *hdl, enum ETHIF_link_status *link_status,
        enum ETHIF_duplex *duplex);

static size_t enc28j60_tx_packet(ENC28J60_t *hdl, const uint8_t *buffer,
        size_t buffer_size);

static int enc28j60_rx_packet(ENC28J60_t *hdl, void *buffer, uint16_t *buffer_size);

/*==============================================================================
  Local object
==============================================================================*/
MODULE_NAME(ENC28J60);

/*==============================================================================
  Exported object
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Initialize device.
 *
 * @param[out]          **device_handle        device allocated memory
 * @param[in ]            major                major device number
 * @param[in ]            minor                minor device number
 * @param[in ]            config               optional module configuration
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_INIT(ENC28J60, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        int err = EFAULT;

        if (minor != 0) {
                return EINVAL;
        }

        err = sys_zalloc(sizeof(ENC28J60_t), device_handle);
        if (err) {
                return err;
        }

        ENC28J60_t *hdl = *device_handle;
        hdl->major      = major;
        hdl->minor      = minor;

        err = sys_mutex_create(KMTX_TYPE_NORMAL, &hdl->mutex);
        if (err) {
                sys_free(device_handle);
                return err;
        }

        if (config) {
                hdl->config = *cast(ENC28J60_config_t *, config);
        }

        return err;
}

//==============================================================================
/**
 * @brief Release device.
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_RELEASE(ENC28J60, void *device_handle)
{
        ENC28J60_t *hdl = device_handle;

        int err;

        if (not hdl->run) {
                kmtx_t *mtx = hdl->mutex;

                err = sys_mutex_lock(mtx, _WAIT_FOREVER_MS);
                if (!err) {

                        hdl->mutex = NULL;
                        sys_sleep_ms(MUTEX_TIMEOUT + MUTEX_TIMEOUT / 2);

                        sys_mutex_destroy(mtx);
                        sys_free(&device_handle);
                }
        } else {
                err = EBUSY;
        }

        return err;
}

//==============================================================================
/**
 * @brief Open device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           flags                  file operation flags (O_RDONLY, O_WRONLY,
 * O_RDWR)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_OPEN(ENC28J60, void *device_handle, u32_t flags)
{
        UNUSED_ARG2(device_handle, flags);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Close device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           force                  device force close (true)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_CLOSE(ENC28J60, void *device_handle, bool force)
{
        UNUSED_ARG2(device_handle, force);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Write data to device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]          *src                    data source
 * @param[in ]           count                  number of bytes to write
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *wrctr                  number of written bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_WRITE(ENC28J60, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos,
        size_t *wrctr, struct vfs_fattr fattr)
{
        UNUSED_ARG2(fpos, fattr);

        ENC28J60_t *hdl = device_handle;

        int    err = EPERM;
        size_t n   = 0;

        while (count > 0) {
                size_t len = min(count, IOCTL_ENC28J60_MAX_PACKET_SIZE);

                ETHIF_packet_t pkt;
                pkt.payload = const_cast(src);
                pkt.length  = len;
                err         = eth_packet_send(hdl, &pkt);
                if (!err) {
                        src += len;
                        count -= len;
                        n += len;
                } else {
                        break;
                }
        }

        *wrctr = n;

        return err;
}

//==============================================================================
/**
 * @brief Read data from device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *rdctr                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_READ(ENC28J60, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos,
        size_t *rdctr, struct vfs_fattr fattr)
{
        UNUSED_ARG2(fpos, fattr);

        ENC28J60_t *hdl = device_handle;

        ETHIF_packet_t pkt;
        pkt.payload = dst;
        pkt.length  = min(count, IOCTL_ENC28J60_MAX_PACKET_SIZE);
        int err     = eth_packet_receive(hdl, &pkt);
        if (!err) {
                *rdctr = pkt.length;
        }

        return err;
}

//==============================================================================
/**
 * @brief IO control.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           request                request
 * @param[in ][out]     *arg                    request's argument
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_IOCTL(ENC28J60, void *device_handle, int request, void *arg)
{
        ENC28J60_t *hdl = device_handle;

        int err = EINVAL;

        switch (request) {
        case IOCTL_ENC28J60__CONFIGURE:
                if (arg) {
                        hdl->config = *cast(ENC28J60_config_t *, arg);
                        err         = 0;
                }
                break;

        case IOCTL_ETHIF__CONFIGURE:
                if (arg) {
                        hdl->ethif_config = *cast(ETHIF_config_t *, arg);
                        hdl->configured   = true;
                        err               = 0;
                }
                break;

        case IOCTL_ETHIF__SEND_PACKET:
                if (arg) {
                        err = eth_packet_send(hdl, arg);
                }
                break;

        case IOCTL_ETHIF__RECEIVE_PACKET:
                if (arg) {
                        err = eth_packet_receive(hdl, arg);
                }
                break;

        case IOCTL_ETHIF__SET_RX_TIMEOUT:
                if (arg) {
                        hdl->rx_timeout_ms = *cast(u32_t *, arg);
                }
                break;

        case IOCTL_ETHIF__SET_TX_TIMEOUT:
                if (arg) {
                        hdl->tx_timeout_ms = *cast(u32_t *, arg);
                }
                break;

        case IOCTL_ETHIF__START:
                err = eth_start(hdl);
                break;

        case IOCTL_ETHIF__STOP:
                err = eth_stop(hdl);
                break;

        case IOCTL_ETHIF__GET_STATUS:
                if (arg) {
                        err = eth_get_info(hdl, arg);
                }
                break;

        default:
                return EBADRQC;
        }

        return err;
}

//==============================================================================
/**
 * @brief Flush device.
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_FLUSH(ENC28J60, void *device_handle)
{
        ENC28J60_t *hdl = device_handle;
        int         err = eth_flush(hdl);
        return err;
}

//==============================================================================
/**
 * @brief Device information.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *device_stat            device status
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_STAT(ENC28J60, void *device_handle, struct vfs_dev_stat *device_stat)
{
        UNUSED_ARG1(device_handle);

        device_stat->st_size  = 0;
        device_stat->st_major = 0;
        device_stat->st_minor = 0;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Start Ethernet interface.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int eth_start(ENC28J60_t *hdl)
{
        int err = sys_mutex_lock(hdl->mutex, _WAIT_FOREVER_MS);
        if (err) {
                return err;
        }

        if (hdl->run) {
                err = EPERM;
                goto finish;
        }

        if (not hdl->configured) {
                err = EPERM;
                goto finish;
        }

        err = sys_fopen(hdl->config.spi_path, O_RDWR, 0, &hdl->spi);
        if (err) {
                goto finish;
        }

        err = enc25j60_run_built_in_self_test(hdl, BIST_ADDRESS_FILL);
        if (err) {
                dev_dbg(hdl, "self test failed!");
                goto finish;
        } else {
                dev_dbg(hdl, "self test passed.");
        }

        u8_t revision;
        err = enc28j60_read_register_byte(hdl, EREVID, &revision);
        if (err) {
                dev_dbg(hdl, "unable to read revision!");
                goto finish;
        } else {
                dev_dbg(hdl, "chip revision: %02Xh", revision);
        }

        err = enc28j60_configure(hdl);
        if (err) {
                dev_dbg(hdl, "configuration failed!");
                goto finish;
        }

        hdl->run = true;

finish:
        if (err) {
                if (hdl->spi) {
                        sys_fclose(hdl->spi);
                        hdl->spi = NULL;
                }
        }

        sys_mutex_unlock(hdl->mutex);

        return err;
}

//==============================================================================
/**
 * @brief  Stop Ethernet interface.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int eth_stop(ENC28J60_t *hdl)
{
        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
        if (!err) {
                if (hdl->run) {
                        enc28j60_reset(hdl);

                        err = sys_fclose(hdl->spi);
                        if (!err) {
                                hdl->run = false;
                        }
                } else {
                        err = 0;
                }

                sys_mutex_unlock(hdl->mutex);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Get Ethernet interface info data.
 *
 * @param  hdl          device handle
 * @param  status       status data
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int eth_get_info(ENC28J60_t *hdl, ETHIF_status_t *status)
{
        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
        if (err) {
                return err;
        }

        if (hdl->run) {
                err = enc28j60_get_link_status(hdl, &status->link_status,
                        &status->duplex);
                if (not err) {
                        status->speed  = ETHIF_SPEED__10Mbps;
                        status->duplex = hdl->ethif_config.duplex;
                } else {
                        status->link_status = ETHIF_LINK_STATUS__PHY_ERROR;
                }
        } else {
                status->link_status = ETHIF_LINK_STATUS__PHY_ERROR;
        }

        memcpy(status->MAC, hdl->ethif_config.MAC, sizeof(status->MAC));

        status->configured        = hdl->configured;
        status->rx_bytes          = hdl->stats.rx_bytes;
        status->tx_bytes          = hdl->stats.tx_bytes;
        status->rx_packets        = hdl->stats.rx_packets;
        status->tx_packets        = hdl->stats.tx_packets;
        status->state             = hdl->run ? ETHIF_STATE__READY : ETHIF_STATE__RESET;
        status->rx_dropped_frames = 0;

        sys_mutex_unlock(hdl->mutex);

        return err;
}

//==============================================================================
/**
 * @brief  Flush Ethernet interface Tx buffers.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int eth_flush(ENC28J60_t *hdl)
{
        UNUSED_ARG1(hdl);
        return 0;
}

//==============================================================================
/**
 * @brief  Send packet.
 *
 * @param  hdl          device handle
 * @param  pkt          packet
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int eth_packet_send(ENC28J60_t *hdl, const ETHIF_packet_t *pkt)
{
        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
        if (!err) {
                err = enc28j60_tx_packet(hdl, pkt->payload, pkt->length);
                if (not err) {
                        hdl->stats.tx_packets++;
                        hdl->stats.tx_bytes += pkt->length;
                } else {
                        dev_dbg(hdl, "unable to send packet, error: %d", err);
                }

                sys_mutex_unlock(hdl->mutex);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Receive packet.
 *
 * @param  hdl          device handle
 * @param  pkt          packet data
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int eth_packet_receive(ENC28J60_t *hdl, ETHIF_packet_t *pkt)
{
        int err = hdl->run ? ESUCC : EPERM;

        clock_t tref = sys_get_uptime_ms();

        while (!err) {
                err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
                if (!err) {
                        err = enc28j60_rx_packet(hdl, pkt->payload, &pkt->length);
                        if (err) {
                                // quit loop

                        } else if (pkt->length > 0) {
                                hdl->stats.rx_packets++;
                                hdl->stats.rx_bytes += pkt->length;

                        } else {
                                sys_mutex_unlock(hdl->mutex);

                                if (sys_is_time_expired(tref, hdl->rx_timeout_ms)) {
                                        err = ETIME;
                                        break;
                                }

                                sys_sleep_ms(5);
                                continue;
                        }

                        sys_mutex_unlock(hdl->mutex);
                }

                break;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Write operation.
 *
 * @param  hdl          device handle
 * @param  op           operation type
 * @param  address      register address
 * @param  value        register value
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc28j60_write_op(ENC28J60_t *hdl, const enc28j60_reg_t op,
        enc28j60_addr_t address, uint8_t value)
{
        size_t wrcnt = 0;
        u8_t   tx[2] = {op | (address & ADDR_MASK), value};
        return sys_fwrite(tx, sizeof(tx), &wrcnt, hdl->spi);
}

//==============================================================================
/**
 * @brief  Read operation.
 *
 * @param  hdl          device handle
 * @param  op           operation type
 * @param  address      register address
 * @param  value        register value
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc28j60_read_op(ENC28J60_t *hdl, const enc28j60_reg_t op,
        enc28j60_addr_t address, uint8_t *value)
{
        u8_t tx[3] = {op | (address & ADDR_MASK), 0x00, 0x00};
        u8_t rx[3] = {0x00, 0x00, 0x00};

        SPI_transceive_t t;
        t.count     = (address & SPRD_MASK) ? 3 : 2;
        t.next      = NULL;
        t.tx_buffer = tx;
        t.rx_buffer = rx;
        t.separated = true;

        int err = sys_ioctl(hdl->spi, IOCTL_SPI__TRANSCEIVE, &t);
        if (not err) {
                *value = (address & SPRD_MASK) ? rx[2] : rx[1];
        }

        return err;
}

//==============================================================================
/**
 * @brief  Set bank.
 *
 * @param  hdl          device handle
 * @param  address      register address
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc28j60_set_bank(ENC28J60_t *hdl, enc28j60_addr_t address)
{
        int err = 0;

        /*
         * Registers EIE, EIR, ESTAT, ECON2, ECON1
         * are present in all banks, no need to switch bank
         */
        address &= BANK_MASK;

        if ((address != ALL_BANKS) and (address != hdl->enc28j60.bank)) {

                size_t wrcnt;
                u8_t   tx[2];

                tx[0] = (ENC28J60_BIT_FIELD_CLR | ADDR_MASK);
                tx[1] = (ECON1_BSEL1 | ECON1_BSEL0);

                err = sys_fwrite(tx, sizeof(tx), &wrcnt, hdl->spi);
                if (not err) {
                        tx[0] = (ENC28J60_BIT_FIELD_SET | ADDR_MASK);
                        tx[1] = (address >> 5);

                        err = sys_fwrite(tx, sizeof(tx), &wrcnt, hdl->spi);
                        if (not err) {
                                hdl->enc28j60.bank = address;
                        }
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Set register bit.
 *
 * @param  hdl          device handle
 * @param  address      register address
 * @param  mask         mask
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc28j60_set_register_bit(ENC28J60_t *hdl, enc28j60_addr_t address,
        uint8_t mask)
{
        int err = enc28j60_set_bank(hdl, address);
        if (not err) {
                err = enc28j60_write_op(hdl, ENC28J60_BIT_FIELD_SET, address, mask);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Clear register bit.
 *
 * @param  hdl          device handle
 * @param  address      register address
 * @param  mask         register mask
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc28j60_clear_register_bit(ENC28J60_t *hdl, enc28j60_addr_t address,
        uint8_t mask)
{
        int err = enc28j60_set_bank(hdl, address);
        if (not err) {
                err = enc28j60_write_op(hdl, ENC28J60_BIT_FIELD_CLR, address, mask);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Write byte to register.
 *
 * @param  hdl          device handle
 * @param  address      register address
 * @param  value        register value
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc28j60_write_register_byte(ENC28J60_t *hdl, enc28j60_addr_t address,
        uint8_t value)
{
        int err = enc28j60_set_bank(hdl, address);
        if (not err) {
                err = enc28j60_write_op(hdl, ENC28J60_WRITE_CTRL_REG, address, value);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Read byte from register.
 *
 * @param  hdl          device handle
 * @param  address      register address
 * @param  value        register value
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static uint8_t enc28j60_read_register_byte(ENC28J60_t *hdl, enc28j60_addr_t address,
        uint8_t *value)
{
        int err = enc28j60_set_bank(hdl, address);
        if (not err) {
                err = enc28j60_read_op(hdl, ENC28J60_READ_CTRL_REG, address, value);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Write word to register.
 *
 * @param  hdl          device handle
 * @param  address      register address
 * @param  value        register value
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc28j60_write_register_word(ENC28J60_t *hdl, enc28j60_addr_t address,
        uint16_t value)
{
        int err = enc28j60_set_bank(hdl, address);

        if (not err) {
                err = enc28j60_write_op(hdl, ENC28J60_WRITE_CTRL_REG, (address + 0),
                        (value & 0xFF));
        }

        if (not err) {
                err = enc28j60_write_op(hdl, ENC28J60_WRITE_CTRL_REG, (address + 1),
                        (value >> 8));
        }

        return err;
}

//==============================================================================
/**
 * @brief  Read word from register.
 *
 * @param  hdl          device handle
 * @param  address      register address
 * @param  value        register value
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc28j60_read_register_word(ENC28J60_t *hdl, enc28j60_addr_t address,
        uint16_t *value)
{
        u8_t low  = 0;
        u8_t high = 0;

        int err = enc28j60_set_bank(hdl, address);

        if (not err) {
                err = enc28j60_read_op(hdl, ENC28J60_READ_CTRL_REG, (address + 0), &low);
        }

        if (not err) {
                err = enc28j60_read_op(hdl, ENC28J60_READ_CTRL_REG, (address + 1), &high);
        }

        if (not err) {
                *value = (cast(u16_t, high) << 8) | cast(u16_t, low);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Write chip buffer.
 *
 * @param  hdl          device handle
 * @param  buffer       buffer
 * @param  length       buffer length
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc28j60_buffer_write(ENC28J60_t *hdl, const uint8_t *buffer, size_t length)
{
        u8_t cmd_op[1] = {ENC28J60_WRITE_BUF_MEM};

        SPI_transceive_t data;
        data.tx_buffer = buffer;
        data.rx_buffer = NULL;
        data.count     = length;
        data.separated = false;
        data.next      = NULL;

        SPI_transceive_t cmd;
        cmd.tx_buffer = cmd_op;
        cmd.rx_buffer = NULL;
        cmd.count     = sizeof(cmd_op);
        cmd.separated = false;
        cmd.next      = &data;

        return sys_ioctl(hdl->spi, IOCTL_SPI__TRANSCEIVE, &cmd);
}

//==============================================================================
/**
 * @brief  Read chip buffer.
 *
 * @param  hdl          device handle
 * @param  buffer       buffer
 * @param  length       buffer length
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc28j60_buffer_read(ENC28J60_t *hdl, uint8_t *buffer, size_t length)
{
        u8_t cmd_op[1] = {ENC28J60_READ_BUF_MEM};

        SPI_transceive_t data;
        data.tx_buffer = NULL;
        data.rx_buffer = buffer;
        data.count     = length;
        data.separated = false;
        data.next      = NULL;

        SPI_transceive_t cmd;
        cmd.tx_buffer = cmd_op;
        cmd.rx_buffer = NULL;
        cmd.count     = sizeof(cmd_op);
        cmd.separated = false;
        cmd.next      = &data;

        return sys_ioctl(hdl->spi, IOCTL_SPI__TRANSCEIVE, &cmd);
}

//==============================================================================
/**
 * @brief  Software chip reset.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc28j60_reset(ENC28J60_t *hdl)
{
        size_t wrcnt;
        int    err = sys_fwrite(&(uint8_t) {ENC28J60_SOFT_RESET}, 1, &wrcnt, hdl->spi);
        if (not err) {
                /*
                 * Rev. B7 Silicon Errata:
                 *
                 * After sending an SPI Reset command, the PHY clock is stopped
                 * but the ESTAT.CLKRDY bit is not cleared. Therefore,
                 * polling the CLKRDY bit will not work to detect if
                 * the PHY is ready.
                 *
                 * Additionally, the hardware start-up time of 300 μs
                 * may expire before the device is ready to operate.
                 *
                 * Work around:
                 * After issuing the Reset command, wait for at least
                 * 1 ms in firmware for the device to be ready.
                 */
                sys_sleep_ms(2);

                /*
                 * Whenever a reset is done, ECON1 will inevitable need to be cleared next
                 */
                err = enc28j60_write_register_byte(hdl, ECON1, 0);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Wait for PHY operation to be finished.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static inline int enc28j60_phy_wait(ENC28J60_t *hdl)
{
        uint8_t timeout = 3;

        do {
                /*
                 * From datasheet: Wait 10.24 μs.
                 * Poll the MISTAT.BUSY bit to be certain that the operation is complete.
                 *
                 * Adding a safety margin
                 */
                sys_sleep_us(14);

                u8_t mistat;
                int  err = enc28j60_read_register_byte(hdl, MISTAT, &mistat);
                if (err) {
                        return err;
                }

                if (not(mistat & MISTAT_BUSY)) {
                        return 0;
                }

        } while (--timeout);

        return ETIME;
}

//==============================================================================
/**
 * @brief  Write PHY register.
 *
 * @param  hdl          device handle
 * @param  address      register address
 * @param  value        value
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc28j60_phy_write(ENC28J60_t *hdl, enc28j60_addr_t address, uint16_t value)
{
        int err = enc28j60_write_register_byte(hdl, MIREGADR, address);
        if (err) {
                return err;
        }

        err = enc28j60_write_register_word(hdl, MIWR, value);
        if (err) {
                return err;
        }

        return enc28j60_phy_wait(hdl);
}

//==============================================================================
/**
 * @brief  Read PHY register.
 *
 * @param  hdl          device handle
 * @param  address      register address
 * @param  value        value
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc28j60_phy_read(ENC28J60_t *hdl, enc28j60_addr_t address, uint16_t *value)
{
        int err = enc28j60_write_register_byte(hdl, MIREGADR, address);
        if (err) {
                return err;
        }

        err = enc28j60_write_register_byte(hdl, MICMD, MICMD_MIIRD);
        if (err) {
                return err;
        }

        err = enc28j60_phy_wait(hdl);
        if (err) {
                return err;
        }

        err = enc28j60_write_register_byte(hdl, MICMD, 0);
        if (err) {
                return err;
        }

        return enc28j60_read_register_word(hdl, MIRD, value);
}

//==============================================================================
/**
 * @brief  Configure chip.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc28j60_configure(ENC28J60_t *hdl)
{
        bool full_duplex = (hdl->ethif_config.duplex == ETHIF_DUPLEX__FULL);

        int err = enc28j60_reset(hdl);
        if (err) {
                return err;
        }

        /*
         * Auto increment the buffer pointer
         */
        err = enc28j60_write_register_byte(hdl, ECON2, ECON2_AUTOINC);
        if (err) {
                return err;
        }

        /*
         * Set the ENC28J60 receive buffer start and end address
         */
        err = enc28j60_write_register_word(hdl, ERXST, ENC28J60_RXSTART);
        if (err) {
                return err;
        }

        err = enc28j60_write_register_word(hdl, ERXND, ENC28J60_RXEND);
        if (err) {
                return err;
        }

        /*
         * Set the next packet pointer
         */
        hdl->enc28j60.next_packet_pointer = ENC28J60_RXSTART;
        err = enc28j60_write_register_word(hdl, ERXRDPT, ENC28J60_RXSTART);
        if (err) {
                return err;
        }

        /*
         * Set the ENC28J60 transmit buffer start and end address
         */
        err = enc28j60_write_register_word(hdl, ETXST, ENC28J60_TXSTART);
        if (err) {
                return err;
        }

        err = enc28j60_write_register_word(hdl, ETXND, ENC28J60_TXEND);
        if (err) {
                return err;
        }

        /*
         * ERXFCON: Receive filter
         *
         * UCEN: Unicast Filter Enable bit
         *   1 = Packets with a destination address matching the local MAC address will be
         *       accepted
         *   0 = Filter disabled
         *
         * ANDOR: AND/OR Filter Select bit
         *   1 = AND: Packets will be rejected unless all enabled filters accept the
         *       packet
         *   0 = OR: Packets will be accepted unless all enabled filters reject the
         *       packet
         *
         * CRCEN: Post-Filter CRC Check Enable bit
         *   1 = All packets with an invalid CRC will be discarded
         *   0 = The CRC validity will be ignored
         *
         * PMEN: Pattern Match Filter Enable bit
         *   1 = Packets which meet the Pattern Match criteria will be accepted
         *   0 = Filter disabled
         *
         * MPEN: Magic Packet Filter Enable bit
         *   1 = Magic Packets for the local MAC address will be accepted
         *   0 = Filter disabled
         *
         * HTEN: Hash Table Filter Enable bit
         *   1 = Packets which meet the Hash Table criteria will be accepted
         *   0 = Filter disabled
         *
         * MCEN: Multicast Filter Enable bit
         *   1 = Packets which have the Least Significant bit set in the destination
         *       address will be accepted
         *   0 = Filter disabled
         *
         * BCEN: Broadcast Filter Enable bit
         *   1 = Packets which have a destination address of FF-FF-FF-FF-FF-FF will be
         *       accepted
         *   0 = Filter disabled
         */
        err = enc28j60_write_register_byte(hdl, ERXFCON,
                (ERXFCON_UCEN | ERXFCON_CRCEN | ERXFCON_BCEN));
        if (err) {
                return err;
        }

        /*
         * Initialize MAC
         */

        /*
         * 1. Set the MARXEN bit in MACON1 to enable the MAC to receive frames. If using
         * full duplex, most applications should also set TXPAUS and RXPAUS to allow IEEE
         * defined flow control to function.
         */
        err = enc28j60_write_register_byte(hdl, MACON1,
                (MACON1_MARXEN | MACON1_TXPAUS | MACON1_RXPAUS));
        if (err) {
                return err;
        }

        /*
         * 2. Configure the PADCFG, TXCRCEN and FULDPX bits of MACON3. Most applications
         * should enable automatic padding to at least 60 bytes and always append a valid
         * CRC. For convenience, many applications may wish to set the FRMLNEN bit as well
         * to enable frame length status reporting. The FULDPX bit should be set if the
         * application will be connected to a full-duplex configured remote node;
         * otherwise, it should be left clear.
         */
        if (full_duplex) {
                err = enc28j60_write_register_byte(hdl, MACON3,
                        (MACON3_PADCFG0 | MACON3_TXCRCEN | MACON3_FRMLNEN |
                                MACON3_FULDPX));
                if (err) {
                        return err;
                }
        } else {
                err = enc28j60_write_register_byte(hdl, MACON3,
                        (MACON3_PADCFG0 | MACON3_TXCRCEN | MACON3_FRMLNEN));
                if (err) {
                        return err;
                }

                /*
                 * 3. Configure the bits in MACON4. For conformance to the IEEE 802.3
                 * standard, set the DEFER bit.
                 */
                err = enc28j60_write_register_byte(hdl, MACON4, MACON4_DEFER);
                if (err) {
                        return err;
                }
        }

        /*
         * 4. Program the MAMXFL registers with the maximum frame length to be permitted
         * to be received or transmitted. Normal network nodes are designed to handle
         * packets that are 1518 bytes or less.
         */
        err = enc28j60_write_register_word(hdl, MAMXFLL, ENC28J60_MAX_FRAME_SIZE);
        if (err) {
                return err;
        }

        /*
         * 5. Configure the Back-to-Back Inter-Packet Gap register, MABBIPG. Most
         * applications will program this register with 15h when Full-Duplex mode is used
         * and 12h when Half-Duplex mode is used.
         *
         * 6. Configure the Non-Back-to-Back Inter-Packet Gap register low byte, MAIPGL.
         * Most applications will program this register with 12h.
         *
         * 7. If half duplex is used, the Non-Back-to-Back Inter-Packet Gap register high
         * byte, MAIPGH, should be programmed. Most applications will program this
         * register to 0Ch.
         */
        if (full_duplex) {
                err = enc28j60_write_register_word(hdl, MAIPG, 0x0012u);
                if (err) {
                        return err;
                }

                err = enc28j60_write_register_byte(hdl, MABBIPG, 0x15u);
                if (err) {
                        return err;
                }
        } else {
                err = enc28j60_write_register_word(hdl, MAIPGL, 0x0C12u);
                if (err) {
                        return err;
                }

                err = enc28j60_write_register_byte(hdl, MABBIPG, 0x12u);
                if (err) {
                        return err;
                }
        }

        /*
         * 8. If Half-Duplex mode is used, program the Retransmission and Collision Window
         * registers, MACLCON1 and MACLCON2. Most applications will not need to change the
         * default Reset values. If the network is spread over exceptionally long cables,
         * the default value of MACLCON2 may need to be increased.
         */
        err = enc28j60_write_register_byte(hdl, MACON2, 0);
        if (err) {
                return err;
        }

        /*
         * 9. Program the local MAC address into the MAADR1:MAADR6 registers.
         */
        err = enc28j60_write_register_byte(hdl, MAADR5, hdl->ethif_config.MAC[0]);
        if (err) {
                return err;
        }

        err = enc28j60_write_register_byte(hdl, MAADR4, hdl->ethif_config.MAC[1]);
        if (err) {
                return err;
        }

        err = enc28j60_write_register_byte(hdl, MAADR3, hdl->ethif_config.MAC[2]);
        if (err) {
                return err;
        }

        err = enc28j60_write_register_byte(hdl, MAADR2, hdl->ethif_config.MAC[3]);
        if (err) {
                return err;
        }

        err = enc28j60_write_register_byte(hdl, MAADR1, hdl->ethif_config.MAC[4]);
        if (err) {
                return err;
        }

        err = enc28j60_write_register_byte(hdl, MAADR0, hdl->ethif_config.MAC[5]);
        if (err) {
                return err;
        }

        err = enc28j60_phy_write(hdl, PHLCON, ENC28J60_LAMPS_MODE);
        if (err) {
                return err;
        }

        if (full_duplex) {
                err = enc28j60_phy_write(hdl, PHCON1, PHCON1_PDPXMD);
                if (err) {
                        return err;
                }

                err = enc28j60_phy_write(hdl, PHCON2, 0);
                if (err) {
                        return err;
                }
        } else {
                err = enc28j60_phy_write(hdl, PHCON1, 0);
                if (err) {
                        return err;
                }

                err = enc28j60_phy_write(hdl, PHCON2, PHCON2_HDLDIS);
                if (err) {
                        return err;
                }
        }

        err = enc28j60_phy_write(hdl, PHIE, 0);
        if (err) {
                return err;
        }

        err = enc28j60_clear_register_bit(hdl, EIR,
                (EIR_DMAIF | EIR_LINKIF | EIR_TXIF | EIR_TXERIF | EIR_RXERIF |
                        EIR_PKTIF));
        if (err) {
                return err;
        }

        err = enc28j60_clear_register_bit(hdl, EIE,
                (EIE_INTIE | EIE_PKTIE | EIE_DMAIE | EIE_LINKIE | EIE_TXIE | EIE_TXERIE |
                        EIE_RXERIE));
        if (err) {
                return err;
        }

        /*
         * RXEN: Receive Enable bit
         *   1 = Packets which pass the current filter configuration will be
         * written into the receive buffer 0 = All packets received will be
         * ignored
         */
        err = enc28j60_set_register_bit(hdl, ECON1, ECON1_RXEN);
        if (err) {
                return err;
        }

        /*
         * Write the transmission control register as the first byte of the
         * output packet. We write 0x00 to indicate that the default
         * configuration (the values in MACON3) will be used.
         */
        err = enc28j60_write_register_word(hdl, EWRPT, ENC28J60_TXSTART);
        if (err) {
                return err;
        }

        err = enc28j60_write_op(hdl, ENC28J60_WRITE_BUF_MEM, 0, 0);
        if (err) {
                return err;
        }

        /*
         * Wait time for the link to become active
         */
        sys_sleep_ms(100);

        return err;
}

//==============================================================================
/**
 * @brief  Run built-in tests.
 *
 * @param  hdl          device handle
 * @param  mode         test mode
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc25j60_run_built_in_self_test(ENC28J60_t *hdl, u8_t mode)
{
        int err = enc28j60_reset(hdl);
        if (err) {
                return err;
        }

        /*
         * From the datasheet.
         *
         * To use the BIST:
         */

        /*
         * 1. Program the EDMAST register pair to 0000h.
         */
        err = enc28j60_write_register_word(hdl, EDMAST, 0);
        if (err) {
                return err;
        }

        /*
         * 2. Program EDMAND and ERXND register pairs to 1FFFh.
         */
        err = enc28j60_write_register_word(hdl, EDMAND, ENC28J60_MEMORY_SIZE - 1);
        if (err) {
                return err;
        }

        err = enc28j60_write_register_word(hdl, ERXND, ENC28J60_MEMORY_SIZE - 1);
        if (err) {
                return err;
        }

        /*
         * 3. Configure the DMA for checksum generation by setting CSUMEN in ECON1.
         */
        err = enc28j60_set_register_bit(hdl, ECON1, ECON1_CSUMEN);
        if (err) {
                return err;
        }

        /*
         * 5. Enable Test mode, select the desired test, select the desired port
         * configuration for the test.
         * 6. Start the BIST by setting EBSTCON.BISTST
         *
         * Additionally further down:
         *
         * To ensure full testing, the test should be redone with the Port Select bit,
         * PSEL, altered. When not using Address Fill mode, additional tests may be done
         * with different seed values to gain greater confidence that the memory is
         * working as expected
         */
        if (mode == BIST_ADDRESS_FILL) {
                /*
                 * In Address Fill mode, the BIST controller will write the low byte of
                 * each memory address into the associated buffer location. As an example,
                 * after the BIST is operated, the location 0000h should have 00h in it,
                 * location 0001h should have 01h in it, location 0E2Ah should have 2Ah in
                 * it and so on. With this fixed memory pattern, the BIST and DMA modules
                 * should always generate a checksum of F807h. The host controller may use
                 * Address Fill mode to confirm that the BIST and DMA modules themselves
                 * are both operating as intended.
                 */
                err = enc28j60_write_register_byte(hdl, EBSTCON,
                        (EBSTCON_TME | EBSTCON_BISTST | mode));
                if (err) {
                        return err;
                }
        } else {
                /*
                 * 4. Write the seed/initial shift value byte to the EBSTSD register
                 * (this is not necessary if Address Fill mode is used).
                 */
                err = enc28j60_write_register_byte(hdl, EBSTSD, 0b10101010u);
                if (err) {
                        return err;
                }

                if (mode == BIST_RANDOM_FILL) {
                        /*
                         * In Random Data Fill mode, the BIST controller will write
                         * pseudo-random data into the buffer. The random data is
                         * generated by a Linear Feedback Shift Register (LFSR)
                         * implementation. The random number generator is seeded by the
                         * initial contents of the EBSTSD register and the register will
                         * have new contents when the BIST is finished.
                         *
                         * Because of the LFSR implementation, an initial seed of zero
                         * will generate a continuous pattern of zeros. As a result, a
                         * non-zero seed value will likely perform a more extensive memory
                         * test.
                         *
                         * Selecting the same seed for two separate trials will allow a
                         * repeat of the same test.
                         */

                        err = enc28j60_write_register_byte(hdl, EBSTCON,
                                (EBSTCON_TME | EBSTCON_PSEL | EBSTCON_BISTST | mode));
                        if (err) {
                                return err;
                        }
                } else {
                        return EFAULT;
                }
        }

        uint8_t count = 8;
        uint8_t ebstcon;
        do {
                sys_sleep_us(100);
                if (0 == --count) {
                        break;
                }

                err = enc28j60_read_op(hdl, ENC28J60_READ_CTRL_REG, EBSTCON, &ebstcon);
                if (err) {
                        return err;
                }

        } while (ebstcon & EBSTCON_BISTST);

        if (count) {
                err = enc28j60_clear_register_bit(hdl, EBSTCON, EBSTCON_TME);
                if (err) {
                        return err;
                }
        } else {
                return EFAULT;
        }

        /*
         * 7. Start the DMA checksum by setting DMAST in ECON1. The DMA controller will
         * read the memory at the same rate the BIST controller will write to it, so the
         * DMA can be started any time after the BIST is started.
         */
        err = enc28j60_set_register_bit(hdl, ECON1, ECON1_DMAST);
        if (err) {
                return err;
        }

        /*
         * 8. Wait for the DMA to complete by polling the DMAST bit or receiving the DMA
         * interrupt (if enabled).
         *
         * But further down:
         *
         * At any time during a test, the test can be canceled by clearing the BISTST,
         * DMAST and TME bits. While the BIST is filling memory, the EBSTSD register
         * should not be accessed, nor should any configuration changes occur.
         *
         * When the BIST completes its memory fill and checksum generation,
         * the BISTST bit will automatically be cleared.
         *
         * The BIST module requires one main clock cycle for each byte that it writes into
         * the RAM. The DMA module's checksum implementation requires the same time but it
         * can be started immediately after the BIST is started. As a result, the minimum
         * time required to do one test pass is slightly greater than 327.68 μs.
         */
        count = 8;
        uint8_t econ1;
        do {
                sys_sleep_us(100);
                if (0 == --count) {
                        break;
                }

                err = enc28j60_read_op(hdl, ENC28J60_READ_CTRL_REG, ECON1, &econ1);
                if (err) {
                        return err;
                }
        } while (econ1 & ECON1_DMAST);

        if (count) {
                err = enc28j60_write_register_byte(hdl, EBSTCON, 0);
                if (err) {
                        return err;
                }

                err = enc28j60_set_register_bit(hdl, ECON1, 0);
                if (err) {
                        return err;
                }
        } else {
                return EFAULT;
        }

        /*
         * 9. Compare the EDMACS registers with the EBSTCS registers
         */
        uint16_t edmacs;
        err = enc28j60_read_register_word(hdl, EDMACS, &edmacs);
        if (err) {
                return err;
        }

        uint16_t ebstcs;
        err = enc28j60_read_register_word(hdl, EBSTCS, &ebstcs);
        if (err) {
                return err;
        }

        if (edmacs != ebstcs) {
                return EFAULT;

        } else if ((mode == BIST_ADDRESS_FILL) && (0xF807u != edmacs)) {
                return EFAULT;

        } else {
                return err;
        }
}

//==============================================================================
/**
 * @brief  Get link status.
 *
 * @param  hdl          device handle
 * @param  link_status  link status
 * @param  duplex       link duplex
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc28j60_get_link_status(ENC28J60_t *hdl, enum ETHIF_link_status *link_status,
        enum ETHIF_duplex *duplex)
{
        u16_t phstat2;
        int   err = enc28j60_phy_read(hdl, PHSTAT2, &phstat2);
        if (not err) {
                if (not(phstat2 & PHSTAT2_LSTAT)) {
                        *link_status = ETHIF_LINK_STATUS__DISCONNECTED;
                        *duplex      = ETHIF_DUPLEX__HALF;

                } else if (phstat2 & PHSTAT2_DPXSTAT) {
                        *link_status = ETHIF_LINK_STATUS__CONNECTED;
                        *duplex      = ETHIF_DUPLEX__FULL;

                } else {
                        *link_status = ETHIF_LINK_STATUS__CONNECTED;
                        *duplex      = ETHIF_DUPLEX__HALF;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Reset TX logic.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc28j60_reset_tx_logic(ENC28J60_t *hdl)
{
        int err = enc28j60_set_register_bit(hdl, ECON1, ECON1_TXRST);
        if (err) {
                return err;
        }

        sys_sleep_us(14);

        err = enc28j60_clear_register_bit(hdl, ECON1, ECON1_TXRST);
        if (err) {
                return err;
        }

        return enc28j60_clear_register_bit(hdl, EIR, (EIR_TXERIF | EIR_TXIF));
}

//==============================================================================
/**
 * @brief  Transmit packet. Ethernet frame CRC will be added automatically by
 *         chip. Buffer should not contains ethernet frame CRC at the end.
 *
 * @param  hdl          device handle
 * @param  buffer       buffer to send
 * @param  buffer_size  buffer size
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static size_t enc28j60_tx_packet(ENC28J60_t *hdl, const uint8_t *buffer,
        size_t buffer_size)
{
        int err = EINVAL;

        if (buffer_size < 14) {
                dev_dbg(hdl, "packet too small: %u bytes", buffer_size);
                return err;
        }

        if (buffer_size >= ENC28J60_MAX_FRAME_SIZE) {
                dev_dbg(hdl, "packet too big: %u bytes", buffer_size);
                return err;
        }

        /*
         * Reset transmit memory for next write
         */
        err = enc28j60_write_register_word(hdl, ETXST, ENC28J60_TXSTART);
        if (err) {
                return err;
        }

        /*
         * Note: control byte on position (ENC28J60_TXSTART + 0) set in configuration to
         * 0.
         */

        err = enc28j60_write_register_word(hdl, EWRPT, ENC28J60_TXSTART + 1);
        if (err) {
                return err;
        }

        err = enc28j60_buffer_write(hdl, buffer, buffer_size);
        if (err) {
                return err;
        }

        /*
         * Appropriately program the ETXND Pointer. It should point to the last
         * byte in the data payload.
         */
        err = enc28j60_write_register_word(hdl, ETXND, (ENC28J60_TXSTART + buffer_size));
        if (err) {
                return err;
        }

        err = enc28j60_clear_register_bit(hdl, EIR, EIR_TXIF | EIR_TXERIF);
        if (err) {
                return err;
        }

        /*
         * Start the transmission process by setting ECON1.TXRTS
         */
        err = enc28j60_set_register_bit(hdl, ECON1, ECON1_TXRTS);
        if (err) {
                return err;
        }

        /*
         * Wait for transmit to complete
         */
        clock_t tref = sys_get_uptime_ms();
        uint8_t econ1;
        while (true) {
                err = enc28j60_read_register_byte(hdl, ECON1, &econ1);
                if (err) {
                        return err;
                }

                if ((econ1 & ECON1_TXRTS) == 0) {
                        break;
                }

                if ((sys_get_uptime_ms() - tref >= ENC28J60_TRANSMIT_TIMEOUT_MS)) {
                        enc28j60_reset_tx_logic(hdl);
                        return ETIME;
                }
        }

        /*
         * From errata:
         *
         *  Module: Transmit Logic
         *
         *  In Half-Duplex mode, a hardware transmission abort - caused by excessive
         * collisions, a late collision or excessive deferrals - may stall the internal
         * transmit logic. The next packet transmit initiated by the host controller may
         * never succeed. That is, ECON1.TXRTS could remain set indefinitely.
         *
         *  Work around
         *  Before attempting to transmit a packet (setting ECON1.TXRTS), reset the
         * internal transmit logic by setting ECON1.TXRST and then clearing ECON1.TXRST.
         * The host controller may wish to issue this Reset before any packet is
         * transmitted (for simplicity), or it may wish to conditionally reset the
         * internal transmit logic based on the Transmit Error Interrupt Flag
         * (EIR.TXERIF), which will become set whenever a transmit abort occurs.
         *
         *  Clearing ECON1.TXRST may cause a new transmit error interrupt event (with
         * EIR.TXERIF becoming set). Therefore, the interrupt flag should be cleared after
         * the Reset is completed.
         */
        u8_t eir;
        err = enc28j60_read_register_byte(hdl, EIR, &eir);
        if (err) {
                return EIO;
        }

        if (eir & EIR_TXERIF) {
                enc28j60_reset_tx_logic(hdl);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Correct ERXRDPT register value according to chip errata.
 *
 * @param  value        value to correct
 *
 * @return Corrected value.
 */
//==============================================================================
static inline uint16_t enc28j60_erxrdpt_errata(const uint16_t value)
{
        /*
         * From errata:
         *
         * Module: Memory (Ethernet Buffer)
         *
         * The receive hardware may corrupt the circular receive buffer (including the
         * Next Packet Pointer and receive status vector fields) when an even value
         * is programmed into the ERXRDPTH:ERXRDPTL registers.
         *
         * Work around:
         *
         * Ensure that only odd addresses are written to the ERXRDPT registers.
         * Assuming that ERXND contains an odd value, many applications can derive
         * a suitable value to write to ERXRDPT by subtracting one from the
         * Next Packet Pointer (a value always ensured to be even because of hardware
         * padding) and then compensating for a potential ERXST to ERXND wraparound.
         *
         * Assuming that the receive buffer area does not span the 1FFFh to 0000h memory
         * boundary, the logic in Example 1 will ensure that ERXRDPT is programmed with an
         * odd value:
         *
         * EXAMPLE1:
         *
         * if (Next Packet Pointer - 1 < ERXST) or (Next Packet Pointer - 1 > ERXND)
         *  then:
         *    ERXRDPT = ERXND
         *  else:
         *    ERXRDPT = Next Packet Pointer - 1
         */
        if ((value <= ENC28J60_RXSTART) or (value >= ENC28J60_RXEND)) {
                return ENC28J60_RXEND;
        } else {
                return value - 1;
        }
}

//==============================================================================
/**
 * @brief  Return size of next RX packet. Packet do not contains ethernet frame
 *         CRC at the end.
 *
 * @param  hdl          device handle
 * @param  buffer       destination buffer
 * @param  buffer_size  number of received bytes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int enc28j60_rx_packet(ENC28J60_t *hdl, void *buffer, uint16_t *buffer_size)
{
        *buffer_size = 0;

        u8_t epktcnt;
        int  err = enc28j60_read_register_byte(hdl, EPKTCNT, &epktcnt);
        if (err) {
                return err;
        }

        if (epktcnt == 0) {
                return 0;
        }

        /*
         * Advance the packet pointer to the start of the next
         * packet
         */
        err = enc28j60_write_register_word(hdl, ERXRDPT,
                enc28j60_erxrdpt_errata(hdl->enc28j60.next_packet_pointer));
        if (err) {
                return err;
        }

        /*
         * Set the read pointer to the start of the next packet
         */
        err = enc28j60_write_register_word(hdl, ERDPT, hdl->enc28j60.next_packet_pointer);
        if (err) {
                return err;
        }

        /*
         * Parse the receive status vector
         *   bytes 0-1: next packet pointer
         *   bytes 2-3: packet length
         *   bytes 4-5: status bits
         */
        uint8_t rsv[RSV_SIZE];
        err = enc28j60_buffer_read(hdl, rsv, sizeof(rsv));
        if (err) {
                return err;
        }

        hdl->enc28j60.next_packet_pointer = (cast(u16_t, rsv[1]) << 8) | rsv[0];

        /*
         * Decrement the packet counter by 1
         */
        err = enc28j60_set_register_bit(hdl, ECON2, ECON2_PKTDEC);
        if (err) {
                return err;
        }

        if (rsv[4] & RSV_RX_OK) {
                /*
                 * Frame size decreased by packet CRC.
                 */
                *buffer_size = ((cast(u16_t, rsv[3]) << 8) | rsv[2]) - 4;

                if (*buffer_size > 0) {
                        err = enc28j60_buffer_read(hdl, buffer, *buffer_size);
                }
        } else {
                err = EFAULT;
        }

        return err;
}

/*==============================================================================
  End of file
==============================================================================*/
