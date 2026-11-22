/*==============================================================================
File    w5500.c

Author  Daniel Zorychta

Brief   W5500 SPI Ethernet controller (MACRAW mode)

        Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

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

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "../w5500_ioctl.h"
#include "spi/spi_ioctl.h"
#include "gpio/gpio_ddi.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define MUTEX_TIMEOUT  10000
#define CMD_TIMEOUT_MS 20
#define TX_TIMEOUT_MS  100
#define RX_POLL_MS     2
#define MAX_FRAME      1514U
#define MIN_FRAME      14U

/* blocks (BSB) */
#define BLK_COMMON    0x00
#define BLK_S0_REG    0x01
#define BLK_S0_TX     0x02
#define BLK_S0_RX     0x03
#define BLK_SOCKET(n) (((n) * 4) + 1)

#define CTRL_WRITE 0x04

/* common registers */
#define REG_MR       0x0000
#define REG_SHAR     0x0009
#define REG_PHYCFGR  0x002E
#define REG_VERSIONR 0x0039

#define MR_RST  0x80
#define VERSION 0x04

#define PHYCFGR_RST      0x80
#define PHYCFGR_OPMD     0x40
#define PHYCFGR_OPMDC(x) ((x) << 3)
#define PHYCFGR_DPX      0x04
#define PHYCFGR_SPD      0x02
#define PHYCFGR_LNK      0x01

/* socket registers */
#define Sn_MR         0x0000
#define Sn_CR         0x0001
#define Sn_IR         0x0002
#define Sn_SR         0x0003
#define Sn_RXBUF_SIZE 0x001E
#define Sn_TXBUF_SIZE 0x001F
#define Sn_TX_FSR     0x0020
#define Sn_TX_WR      0x0024
#define Sn_RX_RSR     0x0026
#define Sn_RX_RD      0x0028

#define Sn_MR_MACRAW 0x04
#define Sn_MR_MFEN   0x80

#define Sn_CR_OPEN  0x01
#define Sn_CR_CLOSE 0x10
#define Sn_CR_SEND  0x20
#define Sn_CR_RECV  0x40

#define Sn_IR_SENDOK 0x10

#define SOCK_MACRAW 0x42

#define S0_BUF_KB 16

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        kfile_t *spi;
        kmtx_t *mtx;
        W5500_config_t conf;
        char spi_path[64];
        ETHIF_config_t eth;
        bool configured;
        bool run;
        u32_t rx_timeout_ms;
        u32_t tx_timeout_ms;
        struct {
                u64_t rx_packets;
                u64_t tx_packets;
                u64_t rx_bytes;
                u64_t tx_bytes;
                u32_t rx_dropped;
        } stats;
        u8_t major;
        u8_t minor;
} W5500_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int eth_start(W5500_t *hdl);
static int eth_stop(W5500_t *hdl);
static int eth_status(W5500_t *hdl, ETHIF_status_t *status);
static int eth_send(W5500_t *hdl, const ETHIF_packet_t *pkt);
static int eth_receive(W5500_t *hdl, ETHIF_packet_t *pkt);

/*==============================================================================
  Local object
==============================================================================*/
MODULE_NAME(W5500);

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Initialize device.
 * @param[out]          **device_handle        device allocated memory
 * @param[in ]            major                major device number
 * @param[in ]            minor                minor device number
 * @param[in ]            config               optional module configuration
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_INIT(W5500, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        if (minor != 0) {
                return ENODEV;
        }

        int err = sys_zalloc(sizeof(W5500_t), device_handle);
        if (!err) {
                W5500_t *hdl = *device_handle;
                hdl->major = major;
                hdl->minor = minor;
                hdl->tx_timeout_ms = TX_TIMEOUT_MS;

                err = sys_mutex_create(KMTX_TYPE_RECURSIVE, &hdl->mtx);
                if (!err) {
                        if (config) {
                                hdl->conf = *cast(const W5500_config_t *, config);
                        }
                } else {
                        sys_free(device_handle);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Release device.
 * @param[in ]          *device_handle          device allocated memory
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_RELEASE(W5500, void *device_handle)
{
        W5500_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mtx, 0);
        if (!err) {
                if (hdl->run) {
                        sys_mutex_unlock(hdl->mtx);
                        return EBUSY;
                }

                kmtx_t *mtx = hdl->mtx;
                sys_mutex_unlock(mtx);
                sys_mutex_destroy(mtx);
                sys_free(&device_handle);
        }

        return err;
}

//==============================================================================
/**
 * @brief Open device.
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           flags                  file operation flags
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_OPEN(W5500, void *device_handle, u32_t flags)
{
        UNUSED_ARG2(device_handle, flags);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Close device.
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           force                  device force close (true)
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_CLOSE(W5500, void *device_handle, bool force)
{
        UNUSED_ARG2(device_handle, force);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Write data to device.
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]          *src                    data source
 * @param[in ]           count                  number of bytes to write
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *wrcnt                  number of written bytes
 * @param[in ]           fattr                  file attributes
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_WRITE(W5500, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG2(fpos, fattr);

        ETHIF_packet_t pkt = {.payload = const_cast(src), .length = count};

        int err = (count > MAX_FRAME) ? EINVAL : eth_send(device_handle, &pkt);
        if (!err) {
                *wrcnt = count;
        }

        return err;
}

//==============================================================================
/**
 * @brief Read data from device.
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *rdcnt                  number of read bytes
 * @param[in ]           fattr                  file attributes
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_READ(W5500, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt, struct vfs_fattr fattr)
{
        UNUSED_ARG2(fpos, fattr);

        ETHIF_packet_t pkt = {.payload = dst, .length = min(count, IOCTL_ETHIF_MAX_PACKET_SIZE)};

        int err = eth_receive(device_handle, &pkt);
        if (!err) {
                *rdcnt = pkt.length;
        }

        return err;
}

//==============================================================================
/**
 * @brief IO control.
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           request                request
 * @param[in ][out]     *arg                    request's argument
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_IOCTL(W5500, void *device_handle, int request, void *arg)
{
        W5500_t *hdl = device_handle;

        if ((request == IOCTL_ETHIF__START) || (request == IOCTL_ETHIF__STOP)) {
                return (request == IOCTL_ETHIF__START) ? eth_start(hdl) : eth_stop(hdl);
        }

        if (!arg) {
                return EINVAL;
        }

        int err = ESUCC;

        switch (request) {
        case IOCTL_W5500__CONFIGURE:
        case IOCTL_W5500__CONFIGURE_STR:
                err = sys_mutex_lock(hdl->mtx, MUTEX_TIMEOUT);
                if (!err) {
                        if (hdl->run) {
                                err = EBUSY;

                        } else if (request == IOCTL_W5500__CONFIGURE) {
                                hdl->conf = *cast(const W5500_config_t *, arg);

                        } else {
                                memset(&hdl->conf, 0, sizeof(hdl->conf));
                                if (sys_opt_get_str_copy(arg, "spi_path", hdl->spi_path, sizeof(hdl->spi_path))) {
                                        hdl->conf.spi_path = hdl->spi_path;
                                }
                                int port = sys_opt_get_int(arg, "reset_port", -1);
                                int pin = sys_opt_get_int(arg, "reset_pin", -1);
                                hdl->conf.reset_pin_used = (port >= 0) && (pin >= 0);
                                hdl->conf.reset_port_idx = port;
                                hdl->conf.reset_pin_idx = pin;
                        }

                        if (!err && !hdl->conf.spi_path) {
                                err = EINVAL;
                        }

                        sys_mutex_unlock(hdl->mtx);
                }
                break;

        case IOCTL_ETHIF__CONFIGURE:
                hdl->eth = *cast(const ETHIF_config_t *, arg);
                hdl->configured = true;
                break;

        case IOCTL_ETHIF__SEND_PACKET:
                err = eth_send(hdl, arg);
                break;

        case IOCTL_ETHIF__RECEIVE_PACKET:
                err = eth_receive(hdl, arg);
                break;

        case IOCTL_ETHIF__SET_RX_TIMEOUT:
                hdl->rx_timeout_ms = *cast(const u32_t *, arg);
                break;

        case IOCTL_ETHIF__SET_TX_TIMEOUT:
                hdl->tx_timeout_ms = *cast(const u32_t *, arg);
                break;

        case IOCTL_ETHIF__GET_STATUS:
                err = eth_status(hdl, arg);
                break;

        default:
                err = EBADRQC;
                break;
        }

        return err;
}

//==============================================================================
/**
 * @brief Flush device.
 * @param[in ]          *device_handle          device allocated memory
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_FLUSH(W5500, void *device_handle)
{
        UNUSED_ARG1(device_handle);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Device information.
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *device_stat            device status
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_STAT(W5500, void *device_handle, struct vfs_dev_stat *device_stat)
{
        UNUSED_ARG1(device_handle);
        device_stat->st_size = 0;
        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function transfers data of register block.
 * @param  hdl          driver handle
 * @param  addr         offset address
 * @param  blk          block (BSB)
 * @param  tx           data to write (NULL: read)
 * @param  rx           read data
 * @param  len          data size
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int xfer(W5500_t *hdl, u16_t addr, u8_t blk, const void *tx, void *rx, size_t len)
{
        u8_t hdr[3] = {addr >> 8, addr, (blk << 3) | (tx ? CTRL_WRITE : 0)};

        SPI_transceive_t tdata = {
                .tx_buffer = tx,
                .rx_buffer = rx,
                .count = len,
                .separated = false,
                .next = NULL,
        };

        SPI_transceive_t thdr = {
                .tx_buffer = hdr,
                .rx_buffer = NULL,
                .count = sizeof(hdr),
                .separated = false,
                .next = &tdata,
        };

        return sys_ioctl(hdl->spi, IOCTL_SPI__TRANSCEIVE, &thdr);
}

static int write8(W5500_t *hdl, u16_t addr, u8_t blk, u8_t val)
{
        return xfer(hdl, addr, blk, &val, NULL, 1);
}

static int read8(W5500_t *hdl, u16_t addr, u8_t blk, u8_t *val)
{
        return xfer(hdl, addr, blk, NULL, val, 1);
}

static int write16(W5500_t *hdl, u16_t addr, u8_t blk, u16_t val)
{
        u8_t b[2] = {val >> 8, val};
        return xfer(hdl, addr, blk, b, NULL, 2);
}

//==============================================================================
/**
 * @brief  Function reads 16-bit register changed by the chip (read until two
 *         reads are equal).
 */
//==============================================================================
static int read16(W5500_t *hdl, u16_t addr, u8_t blk, u16_t *val)
{
        u8_t b[2];
        u16_t prev = 0;

        for (int i = 0; i < 8; i++) {
                int err = xfer(hdl, addr, blk, NULL, b, 2);
                if (err) {
                        return err;
                }

                *val = (b[0] << 8) | b[1];
                if ((i > 0) && (*val == prev)) {
                        return ESUCC;
                }
                prev = *val;
        }

        return EIO;
}

//==============================================================================
/**
 * @brief  Function executes socket 0 command.
 * @param  hdl          driver handle
 * @param  cmd          command
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int socket_cmd(W5500_t *hdl, u8_t cmd)
{
        int err = write8(hdl, Sn_CR, BLK_S0_REG, cmd);

        clock_t tref = sys_get_uptime_ms();
        while (!err) {
                u8_t cr;
                err = read8(hdl, Sn_CR, BLK_S0_REG, &cr);
                if (!err && (cr == 0)) {
                        break;
                }

                if (sys_is_time_expired(tref, CMD_TIMEOUT_MS)) {
                        err = ETIME;
                } else {
                        sys_sleep_ms(1);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function opens socket 0 in MACRAW mode.
 * @param  hdl          driver handle
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int socket_open(W5500_t *hdl)
{
        u8_t sr = 0;

        int err = write8(hdl, Sn_MR, BLK_S0_REG, Sn_MR_MFEN | Sn_MR_MACRAW);
        if (!err) {
                err = socket_cmd(hdl, Sn_CR_OPEN);
        }
        if (!err) {
                err = read8(hdl, Sn_SR, BLK_S0_REG, &sr);
        }
        if (!err && (sr != SOCK_MACRAW)) {
                dev_dbg(hdl, "socket not opened (SR %02Xh)", sr);
                err = EIO;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function initializes chip.
 * @param  hdl          driver handle
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int chip_init(W5500_t *hdl)
{
        if (hdl->conf.reset_pin_used) {
                _GPIO_DDI_clear_pin(hdl->conf.reset_port_idx, hdl->conf.reset_pin_idx);
                sys_sleep_ms(1);
                _GPIO_DDI_set_pin(hdl->conf.reset_port_idx, hdl->conf.reset_pin_idx);
                sys_sleep_ms(2);
        }

        int err = write8(hdl, REG_MR, BLK_COMMON, MR_RST);

        clock_t tref = sys_get_uptime_ms();
        u8_t val = MR_RST;
        while (!err && (val & MR_RST)) {
                err = read8(hdl, REG_MR, BLK_COMMON, &val);
                if (!err && (val & MR_RST) && sys_is_time_expired(tref, CMD_TIMEOUT_MS)) {
                        err = ENODEV;
                }
        }

        if (!err) {
                err = read8(hdl, REG_VERSIONR, BLK_COMMON, &val);
                if (!err && (val != VERSION)) {
                        dev_dbg(hdl, "chip not detected (version %02Xh)", val);
                        err = ENODEV;
                }
        }

        /* socket 0: all memory */
        for (u8_t s = 0; !err && (s < 8); s++) {
                u8_t kb[2] = {(s == 0) ? S0_BUF_KB : 0, (s == 0) ? S0_BUF_KB : 0};
                err = xfer(hdl, Sn_RXBUF_SIZE, BLK_SOCKET(s), kb, NULL, sizeof(kb));
        }

        if (!err) {
                err = xfer(hdl, REG_SHAR, BLK_COMMON, hdl->eth.MAC, NULL, sizeof(hdl->eth.MAC));
        }

        if (!err) {
                u8_t opmdc = 7; /* all capable, auto-negotiation */
                if (!hdl->eth.auto_negotiation) {
                        opmdc = ((hdl->eth.speed == ETHIF_SPEED__100Mbps) ? 2 : 0) |
                                ((hdl->eth.duplex == ETHIF_DUPLEX__FULL) ? 1 : 0);
                }

                u8_t phy = PHYCFGR_OPMD | PHYCFGR_OPMDC(opmdc);
                err = write8(hdl, REG_PHYCFGR, BLK_COMMON, phy);
                if (!err) {
                        err = write8(hdl, REG_PHYCFGR, BLK_COMMON, phy | PHYCFGR_RST);
                }
        }

        if (!err) {
                err = socket_open(hdl);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function starts interface.
 * @param  hdl          driver handle
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int eth_start(W5500_t *hdl)
{
        int err = sys_mutex_lock(hdl->mtx, MUTEX_TIMEOUT);
        if (!err) {
                if (hdl->run || !hdl->configured || !hdl->conf.spi_path) {
                        err = EPERM;
                } else {
                        err = sys_fopen(hdl->conf.spi_path, O_RDWR, 0, &hdl->spi);
                        if (!err) {
                                err = chip_init(hdl);
                                if (err) {
                                        sys_fclose(hdl->spi);
                                        hdl->spi = NULL;
                                } else {
                                        hdl->run = true;
                                }
                        }
                }

                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function stops interface.
 * @param  hdl          driver handle
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int eth_stop(W5500_t *hdl)
{
        int err = sys_mutex_lock(hdl->mtx, MUTEX_TIMEOUT);
        if (!err) {
                if (hdl->run) {
                        socket_cmd(hdl, Sn_CR_CLOSE);
                        write8(hdl, REG_MR, BLK_COMMON, MR_RST);
                        sys_fclose(hdl->spi);
                        hdl->spi = NULL;
                        hdl->run = false;
                }

                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function reads interface status.
 * @param  hdl          driver handle
 * @param  status       status
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int eth_status(W5500_t *hdl, ETHIF_status_t *status)
{
        int err = sys_mutex_lock(hdl->mtx, MUTEX_TIMEOUT);
        if (!err) {
                u8_t phy = 0;

                status->link_status = ETHIF_LINK_STATUS__PHY_ERROR;
                status->speed = hdl->eth.speed;
                status->duplex = hdl->eth.duplex;

                if (hdl->run && (read8(hdl, REG_PHYCFGR, BLK_COMMON, &phy) == ESUCC)) {
                        status->link_status = (phy & PHYCFGR_LNK) ? ETHIF_LINK_STATUS__CONNECTED
                                                                  : ETHIF_LINK_STATUS__DISCONNECTED;
                        status->speed = (phy & PHYCFGR_SPD) ? ETHIF_SPEED__100Mbps : ETHIF_SPEED__10Mbps;
                        status->duplex = (phy & PHYCFGR_DPX) ? ETHIF_DUPLEX__FULL : ETHIF_DUPLEX__HALF;
                }

                memcpy(status->MAC, hdl->eth.MAC, sizeof(status->MAC));
                status->state = hdl->run ? ETHIF_STATE__READY : ETHIF_STATE__RESET;
                status->configured = hdl->configured;
                status->rx_packets = hdl->stats.rx_packets;
                status->tx_packets = hdl->stats.tx_packets;
                status->rx_bytes = hdl->stats.rx_bytes;
                status->tx_bytes = hdl->stats.tx_bytes;
                status->rx_dropped_frames = hdl->stats.rx_dropped;

                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function sends frame.
 * @param  hdl          driver handle
 * @param  pkt          frame
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int eth_send(W5500_t *hdl, const ETHIF_packet_t *pkt)
{
        if ((pkt->length < MIN_FRAME) || (pkt->length > MAX_FRAME)) {
                return EINVAL;
        }

        int err = sys_mutex_lock(hdl->mtx, MUTEX_TIMEOUT);
        if (err) {
                return err;
        }

        if (!hdl->run) {
                err = EPERM;
                goto finish;
        }

        clock_t tref = sys_get_uptime_ms();
        for (;;) {
                u16_t fsr;
                err = read16(hdl, Sn_TX_FSR, BLK_S0_REG, &fsr);
                if (err || (fsr >= pkt->length)) {
                        break;
                }

                if (sys_is_time_expired(tref, hdl->tx_timeout_ms)) {
                        err = ETIME;
                        break;
                }
                sys_sleep_ms(1);
        }

        u16_t ptr = 0;
        if (!err) {
                err = read16(hdl, Sn_TX_WR, BLK_S0_REG, &ptr);
        }
        if (!err) {
                err = xfer(hdl, ptr, BLK_S0_TX, pkt->payload, NULL, pkt->length);
        }
        if (!err) {
                err = write16(hdl, Sn_TX_WR, BLK_S0_REG, ptr + pkt->length);
        }
        if (!err) {
                err = socket_cmd(hdl, Sn_CR_SEND);
        }

        tref = sys_get_uptime_ms();
        while (!err) {
                u8_t ir;
                err = read8(hdl, Sn_IR, BLK_S0_REG, &ir);
                if (!err && (ir & Sn_IR_SENDOK)) {
                        err = write8(hdl, Sn_IR, BLK_S0_REG, Sn_IR_SENDOK);
                        break;
                }

                if (sys_is_time_expired(tref, hdl->tx_timeout_ms)) {
                        err = ETIME;
                } else {
                        sys_sleep_ms(1);
                }
        }

        if (!err) {
                hdl->stats.tx_packets++;
                hdl->stats.tx_bytes += pkt->length;
        } else {
                dev_dbg(hdl, "frame not sent (%d)", err);
        }

finish:
        sys_mutex_unlock(hdl->mtx);
        return err;
}

//==============================================================================
/**
 * @brief  Function receives frame from RX memory.
 * @param  hdl          driver handle
 * @param  pkt          frame buffer (length: buffer size, received size)
 * @param  size         number of bytes in RX memory
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int frame_read(W5500_t *hdl, ETHIF_packet_t *pkt, u16_t size)
{
        u16_t ptr;
        u8_t hdr[2];

        int err = read16(hdl, Sn_RX_RD, BLK_S0_REG, &ptr);
        if (!err) {
                err = xfer(hdl, ptr, BLK_S0_RX, NULL, hdr, sizeof(hdr));
        }
        if (err) {
                return err;
        }

        u16_t len = (hdr[0] << 8) | hdr[1];
        if ((len < (MIN_FRAME + 2)) || (len > (MAX_FRAME + 2)) || (len > size)) {
                /* memory corrupted: socket reopen */
                dev_dbg(hdl, "invalid frame size %u", len);
                hdl->stats.rx_dropped++;
                err = socket_cmd(hdl, Sn_CR_CLOSE);
                if (!err) {
                        err = socket_open(hdl);
                }
                return err ? err : EIO;
        }

        u16_t n = min(pkt->length, len - 2);
        err = xfer(hdl, ptr + 2, BLK_S0_RX, NULL, pkt->payload, n);
        if (!err) {
                err = write16(hdl, Sn_RX_RD, BLK_S0_REG, ptr + len);
        }
        if (!err) {
                err = socket_cmd(hdl, Sn_CR_RECV);
        }
        if (!err) {
                pkt->length = n;
                hdl->stats.rx_packets++;
                hdl->stats.rx_bytes += n;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function receives frame (waits RX timeout).
 * @param  hdl          driver handle
 * @param  pkt          frame buffer (length: buffer size, received size)
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int eth_receive(W5500_t *hdl, ETHIF_packet_t *pkt)
{
        clock_t tref = sys_get_uptime_ms();

        for (;;) {
                int err = sys_mutex_lock(hdl->mtx, MUTEX_TIMEOUT);
                if (err) {
                        return err;
                }

                u16_t size = 0;
                if (!hdl->run) {
                        err = EPERM;
                } else {
                        err = read16(hdl, Sn_RX_RSR, BLK_S0_REG, &size);
                        if (!err && size) {
                                err = frame_read(hdl, pkt, size);
                        }
                }

                sys_mutex_unlock(hdl->mtx);

                if (err || size) {
                        return err;
                }

                if (sys_is_time_expired(tref, hdl->rx_timeout_ms)) {
                        return ETIME;
                }

                sys_sleep_ms(RX_POLL_MS);
        }
}

/*==============================================================================
  End of file
==============================================================================*/
