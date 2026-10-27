/*==============================================================================
@file    eth.c

@author  Daniel Zorychta

@brief   Ethernet interface driver (POSIX simulator). Frames are sent and
         received by the host TAP interface (simulator option --tap).

@note    Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

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

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "posix/host/host.h"
#include "../eth_ioctl.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define MUTEX_TIMEOUT    _WAIT_FOREVER_MS
#define RX_POLL_INTERVAL 1

/*==============================================================================
  Local object types
==============================================================================*/
struct eth {
        kmtx_t *mutex;
        ETHIF_config_t config;
        u32_t rx_timeout_ms;
        u32_t tx_timeout_ms;
        bool configured;
        bool run;
        u8_t major;
        u8_t minor;

        struct {
                u64_t rx_packets;
                u64_t tx_packets;
                u64_t rx_bytes;
                u64_t tx_bytes;
                u32_t rx_dropped_frames;
        } stats;
};

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int eth_start(struct eth *hdl);
static int eth_stop(struct eth *hdl);
static int eth_get_info(struct eth *hdl, ETHIF_status_t *status);
static int packet_receive(struct eth *hdl, ETHIF_packet_t *pkt);
static int packet_send(struct eth *hdl, const ETHIF_packet_t *pkt);
static bool is_frame_accepted(struct eth *hdl, const u8_t *frame, size_t len);

/*==============================================================================
  Local objects
==============================================================================*/
MODULE_NAME(ETH);

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  External objects
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Initialize device
 *
 * @param[out]          **device_handle        device allocated memory
 * @param[in ]            major                major device number
 * @param[in ]            minor                minor device number
 * @param[in ]            config               optional module configuration
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_INIT(ETH, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        if ((major != 0) || (minor != 0)) {
                return ENODEV;
        }

        if (not _host_eth_is_available()) {
                dev_dbg2(major, minor, "interface not connected (use --tap <name>)");
                return ENODEV;
        }

        int err = sys_zalloc(sizeof(struct eth), device_handle);
        if (!err) {
                struct eth *hdl = *device_handle;

                hdl->major = major;
                hdl->minor = minor;
                hdl->rx_timeout_ms = _WAIT_FOREVER_MS;
                hdl->tx_timeout_ms = _WAIT_FOREVER_MS;

                err = sys_mutex_create(KMTX_TYPE_NORMAL, &hdl->mutex);
                if (!err) {
                        if (config) {
                                hdl->config = *cast(ETHIF_config_t *, config);
                                hdl->configured = true;
                        }
                } else {
                        sys_free(device_handle);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Release device
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_RELEASE(ETH, void *device_handle)
{
        struct eth *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
        if (!err) {
                kmtx_t *mutex = hdl->mutex;
                hdl->mutex = NULL;
                sys_mutex_unlock(mutex);
                sys_mutex_destroy(mutex);
                sys_free(&device_handle);
        }

        return err;
}

//==============================================================================
/**
 * @brief Open device
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           flags                  file operation flags (O_RDONLY, O_WRONLY, O_RDWR)
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_OPEN(ETH, void *device_handle, u32_t flags)
{
        UNUSED_ARG2(device_handle, flags);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Close device
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           force                  device force close (true)
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_CLOSE(ETH, void *device_handle, bool force)
{
        UNUSED_ARG2(device_handle, force);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Write data to device (data is sent as single frame).
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]          *src                    data source
 * @param[in ]           count                  number of bytes to write
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *wrctr                  number of written bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_WRITE(ETH, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrctr,
        struct vfs_fattr fattr)
{
        UNUSED_ARG2(fpos, fattr);

        struct eth *hdl = device_handle;

        int err = ESUCC;
        size_t n = 0;

        while (count > 0) {
                size_t len = min(count, cast(size_t, IOCTL_ETHIF_MAX_PACKET_SIZE));

                ETHIF_packet_t pkt;
                pkt.payload = const_cast(src);
                pkt.length = len;

                err = packet_send(hdl, &pkt);
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
 * @brief Read data from device (single frame is read).
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *rdctr                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_READ(ETH, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdctr, struct vfs_fattr fattr)
{
        UNUSED_ARG2(fpos, fattr);

        struct eth *hdl = device_handle;

        ETHIF_packet_t pkt;
        pkt.payload = dst;
        pkt.length = min(count, cast(size_t, IOCTL_ETHIF_MAX_PACKET_SIZE));

        int err = packet_receive(hdl, &pkt);
        if (!err) {
                *rdctr = pkt.length;
        }

        return err;
}

//==============================================================================
/**
 * @brief IO control
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           request                request
 * @param[in ][out]     *arg                    request's argument
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_IOCTL(ETH, void *device_handle, int request, void *arg)
{
        struct eth *hdl = device_handle;

        int err = EINVAL;

        switch (request) {
        case IOCTL_ETHIF__CONFIGURE:
                if (arg) {
                        err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
                        if (!err) {
                                hdl->config = *cast(ETHIF_config_t *, arg);
                                hdl->configured = true;
                                sys_mutex_unlock(hdl->mutex);
                        }
                }
                break;

        case IOCTL_ETHIF__SEND_PACKET:
                if (arg) {
                        err = packet_send(hdl, arg);
                }
                break;

        case IOCTL_ETHIF__RECEIVE_PACKET:
                if (arg) {
                        err = packet_receive(hdl, arg);
                }
                break;

        case IOCTL_ETHIF__SET_RX_TIMEOUT:
                if (arg) {
                        hdl->rx_timeout_ms = *cast(u32_t *, arg);
                        err = ESUCC;
                }
                break;

        case IOCTL_ETHIF__SET_TX_TIMEOUT:
                if (arg) {
                        hdl->tx_timeout_ms = *cast(u32_t *, arg);
                        err = ESUCC;
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
                err = EBADRQC;
                break;
        }

        return err;
}

//==============================================================================
/**
 * @brief Flush device
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_FLUSH(ETH, void *device_handle)
{
        UNUSED_ARG1(device_handle);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Device information
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *device_stat            device status
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_STAT(ETH, void *device_handle, struct vfs_dev_stat *device_stat)
{
        UNUSED_ARG1(device_handle);

        device_stat->st_size = 0;

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
static int eth_start(struct eth *hdl)
{
        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
        if (!err) {
                if (hdl->configured) {
                        /* drop frames received before start */
                        u8_t frame[64];
                        while (_host_eth_read(frame, sizeof(frame)) > 0) {
                                hdl->stats.rx_dropped_frames++;
                        }

                        hdl->run = true;
                } else {
                        err = EPERM;
                }

                sys_mutex_unlock(hdl->mutex);
        }

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
static int eth_stop(struct eth *hdl)
{
        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
        if (!err) {
                hdl->run = false;
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
static int eth_get_info(struct eth *hdl, ETHIF_status_t *status)
{
        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
        if (!err) {
                if (hdl->run) {
                        status->link_status = _host_eth_is_link_up() ? ETHIF_LINK_STATUS__CONNECTED
                                                                     : ETHIF_LINK_STATUS__DISCONNECTED;
                        status->state = ETHIF_STATE__READY;
                } else {
                        status->link_status = ETHIF_LINK_STATUS__DISCONNECTED;
                        status->state = ETHIF_STATE__RESET;
                }

                status->speed = ETHIF_SPEED__100Mbps;
                status->duplex = ETHIF_DUPLEX__FULL;
                status->configured = hdl->configured;
                memcpy(status->MAC, hdl->config.MAC, sizeof(status->MAC));
                status->rx_bytes = hdl->stats.rx_bytes;
                status->tx_bytes = hdl->stats.tx_bytes;
                status->rx_packets = hdl->stats.rx_packets;
                status->tx_packets = hdl->stats.tx_packets;
                status->rx_dropped_frames = hdl->stats.rx_dropped_frames;

                sys_mutex_unlock(hdl->mutex);
        }

        return err;
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
static int packet_send(struct eth *hdl, const ETHIF_packet_t *pkt)
{
        if (not hdl->run) {
                return EPERM;
        }

        if (pkt->length > IOCTL_ETHIF_MAX_PACKET_SIZE) {
                return EINVAL;
        }

        clock_t tref = sys_get_uptime_ms();

        for (;;) {
                int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
                if (err) {
                        return err;
                }

                int n = _host_eth_write(pkt->payload, pkt->length);

                if (n > 0) {
                        hdl->stats.tx_packets++;
                        hdl->stats.tx_bytes += pkt->length;
                }

                sys_mutex_unlock(hdl->mutex);

                if (n > 0) {
                        return ESUCC;
                } else if (n < 0) {
                        return EIO;
                } else if (sys_is_time_expired(tref, hdl->tx_timeout_ms)) {
                        return ETIME;
                }

                sys_sleep_ms(RX_POLL_INTERVAL);
        }
}

//==============================================================================
/**
 * @brief  Receive packet. Function waits for packet by RX timeout.
 *
 * @param  hdl          device handle
 * @param  pkt          packet (length is buffer size at input)
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int packet_receive(struct eth *hdl, ETHIF_packet_t *pkt)
{
        if (not hdl->run) {
                return EPERM;
        }

        clock_t tref = sys_get_uptime_ms();

        for (;;) {
                int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT);
                if (err) {
                        return err;
                }

                int n = _host_eth_read(pkt->payload, pkt->length);

                bool accepted = (n > 0) && is_frame_accepted(hdl, pkt->payload, cast(size_t, n));

                if (accepted) {
                        hdl->stats.rx_packets++;
                        hdl->stats.rx_bytes += cast(u64_t, n);
                } else if (n > 0) {
                        hdl->stats.rx_dropped_frames++;
                }

                sys_mutex_unlock(hdl->mutex);

                if (accepted) {
                        pkt->length = cast(u16_t, n);
                        return ESUCC;

                } else if (n < 0) {
                        return EIO;

                } else if (n == 0) {
                        if (sys_is_time_expired(tref, hdl->rx_timeout_ms)) {
                                return ETIME;
                        }

                        sys_sleep_ms(RX_POLL_INTERVAL);
                }
        }
}

//==============================================================================
/**
 * @brief  Function check if frame is addressed to the interface (MAC address
 *         filter): own MAC address, broadcast or multicast.
 *
 * @param  hdl          device handle
 * @param  frame        Ethernet frame
 * @param  len          frame length
 *
 * @return If frame is accepted then true is returned.
 */
//==============================================================================
static bool is_frame_accepted(struct eth *hdl, const u8_t *frame, size_t len)
{
        if (len < 14) {
                return false;
        }

        /* broadcast and multicast frames have the lowest bit of the first byte set */
        if (frame[0] & 0x01) {
                return true;
        }

        return memcmp(frame, hdl->config.MAC, sizeof(hdl->config.MAC)) == 0;
}

/*==============================================================================
  End of file
==============================================================================*/
