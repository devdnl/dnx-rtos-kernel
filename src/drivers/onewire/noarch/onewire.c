/*==============================================================================
File    onewire.c

Author  Daniel Zorychta

Brief   1-Wire bus master on UART

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

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "../onewire_ioctl.h"
#include "uart/uart_ioctl.h"
#include "gpio/gpio_ddi.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define BAUD_RESET      9600
#define BAUD_SLOT       115200
#define RESET_CHAR      0xF0
#define SLOT_1          0xFF
#define SLOT_0          0x00
#define ECHO_TIMEOUT_MS 50

#define CMD_MATCH_ROM 0x55
#define CMD_SKIP_ROM  0xCC

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        kfile_t *uart;
        kmtx_t *mtx;
        ONEWIRE_config_t conf;
        u32_t baud;
        u8_t major;
        u8_t minor;
} ONEWIRE_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int configure(ONEWIRE_t *hdl, const ONEWIRE_config_t *cfg);
static int bus_reset(ONEWIRE_t *hdl, bool *presence);
static int bus_write(ONEWIRE_t *hdl, const u8_t *src, size_t len);
static int bus_read(ONEWIRE_t *hdl, u8_t *dst, size_t len);
static int search(ONEWIRE_t *hdl, ONEWIRE_search_t *s);
static int transfer(ONEWIRE_t *hdl, ONEWIRE_transfer_t *t);

/*==============================================================================
  Local object
==============================================================================*/
MODULE_NAME(ONEWIRE);

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
API_MOD_INIT(ONEWIRE, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        if (minor != 0) {
                return ENODEV;
        }

        int err = sys_zalloc(sizeof(ONEWIRE_t), device_handle);
        if (!err) {
                ONEWIRE_t *hdl = *device_handle;
                hdl->major = major;
                hdl->minor = minor;

                err = sys_mutex_create(KMTX_TYPE_RECURSIVE, &hdl->mtx);

                if (!err && config) {
                        err = configure(hdl, config);
                }

                if (err) {
                        if (hdl->mtx) {
                                sys_mutex_destroy(hdl->mtx);
                        }
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
API_MOD_RELEASE(ONEWIRE, void *device_handle)
{
        ONEWIRE_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mtx, 0);
        if (!err) {
                kmtx_t *mtx = hdl->mtx;
                sys_mutex_unlock(mtx);
                sys_mutex_destroy(mtx);

                if (hdl->uart) {
                        sys_fclose(hdl->uart);
                }

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
API_MOD_OPEN(ONEWIRE, void *device_handle, u32_t flags)
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
API_MOD_CLOSE(ONEWIRE, void *device_handle, bool force)
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
API_MOD_WRITE(ONEWIRE, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG2(fpos, fattr);

        ONEWIRE_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (!err) {
                err = bus_write(hdl, src, count);
                if (!err) {
                        *wrcnt = count;
                }
                sys_mutex_unlock(hdl->mtx);
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
API_MOD_READ(ONEWIRE, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt, struct vfs_fattr fattr)
{
        UNUSED_ARG2(fpos, fattr);

        ONEWIRE_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (!err) {
                err = bus_read(hdl, dst, count);
                if (!err) {
                        *rdcnt = count;
                }
                sys_mutex_unlock(hdl->mtx);
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
API_MOD_IOCTL(ONEWIRE, void *device_handle, int request, void *arg)
{
        ONEWIRE_t *hdl = device_handle;

        if (!arg) {
                return EINVAL;
        }

        if ((request == IOCTL_ONEWIRE__CONFIGURE) || (request == IOCTL_ONEWIRE__CONFIGURE_STR)) {
                ONEWIRE_config_t cfg;
                char path[64];

                if (request == IOCTL_ONEWIRE__CONFIGURE) {
                        cfg = *cast(const ONEWIRE_config_t *, arg);
                } else {
                        cfg.path = sys_opt_get_str_copy(arg, "path", path, sizeof(path)) ? path : NULL;
                        cfg.single_wire = sys_opt_get_bool(arg, "single_wire", false);
                        int port = sys_opt_get_int(arg, "spu_port", -1);
                        int pin = sys_opt_get_int(arg, "spu_pin", -1);
                        cfg.spu_pin_used = (port >= 0) && (pin >= 0);
                        cfg.spu_port_idx = port;
                        cfg.spu_pin_idx = pin;
                }

                return configure(hdl, &cfg);
        }

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (!err) {
                if (!hdl->uart) {
                        err = ENODEV;

                } else if (request == IOCTL_ONEWIRE__RESET) {
                        err = bus_reset(hdl, arg);

                } else if (request == IOCTL_ONEWIRE__SEARCH) {
                        err = search(hdl, arg);

                } else if (request == IOCTL_ONEWIRE__TRANSFER) {
                        err = transfer(hdl, arg);

                } else {
                        err = EBADRQC;
                }

                sys_mutex_unlock(hdl->mtx);
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
API_MOD_FLUSH(ONEWIRE, void *device_handle)
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
API_MOD_STAT(ONEWIRE, void *device_handle, struct vfs_dev_stat *device_stat)
{
        UNUSED_ARG1(device_handle);
        device_stat->st_size = 0;
        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function sets baud rate of UART.
 * @param  hdl          driver handle
 * @param  baud         baud rate
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int set_baud(ONEWIRE_t *hdl, u32_t baud)
{
        if (hdl->baud == baud) {
                return ESUCC;
        }

        UART_config_t cfg = {
                .mode = UART_MODE__NORMAL,
                .parity = UART_PARITY__OFF,
                .stop_bits = UART_STOP_BIT__1,
                .tx_enable = true,
                .rx_enable = true,
                .baud = baud,
                .features = hdl->conf.single_wire ? UART_FEATURE__SINGLE_WIRE : 0,
        };

        int err = sys_ioctl(hdl->uart, IOCTL_UART__SET_CONFIGURATION, &cfg);
        hdl->baud = err ? 0 : baud;

        return err;
}

//==============================================================================
/**
 * @brief  Function sends characters and reads their echo (line state).
 * @param  hdl          driver handle
 * @param  buf          characters to send, received characters
 * @param  len          number of characters
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int uart_xfer(ONEWIRE_t *hdl, u8_t *buf, size_t len)
{
        u8_t junk[8];
        size_t n = 0;

        /* characters received out of transfer (noise) */
        while ((sys_fread(junk, sizeof(junk), &n, hdl->uart) == ESUCC) && (n > 0)) {
        }

        int err = sys_fwrite(buf, len, &n, hdl->uart);
        if (err) {
                return err;
        }

        size_t got = 0;
        clock_t tref = sys_get_uptime_ms();
        u32_t timeout = ECHO_TIMEOUT_MS + ((len * 10 * 1000) / hdl->baud);

        while (got < len) {
                err = sys_fread(buf + got, len - got, &n, hdl->uart);
                if (err) {
                        return err;
                }

                got += n;

                if (got < len) {
                        if (sys_is_time_expired(tref, timeout)) {
                                /* late characters must not be taken as echo of next transfer */
                                sys_sleep_ms(ECHO_TIMEOUT_MS);
                                while ((sys_fread(junk, sizeof(junk), &n, hdl->uart) == ESUCC) && (n > 0)) {
                                }
                                dev_dbg(hdl, "no echo from UART");
                                return EIO;
                        }
                        sys_sleep_ms(1);
                }
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function executes reset pulse.
 * @param  hdl          driver handle
 * @param  presence     presence pulse detected
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int bus_reset(ONEWIRE_t *hdl, bool *presence)
{
        u8_t c = RESET_CHAR;

        int err = set_baud(hdl, BAUD_RESET);
        if (!err) {
                err = uart_xfer(hdl, &c, 1);
        }

        int err2 = set_baud(hdl, BAUD_SLOT);

        if (!err && (c == 0x00)) {
                dev_dbg(hdl, "line shorted");
                err = EIO;
        }

        *presence = (c != RESET_CHAR);

        return err ? err : err2;
}

//==============================================================================
/**
 * @brief  Function executes time slots.
 * @param  hdl          driver handle
 * @param  bits         bits to write (LSB first), 1: read slot
 * @param  nbits        number of bits (up to 32)
 * @param  result       read bits (can be NULL)
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int slots(ONEWIRE_t *hdl, u32_t bits, u8_t nbits, u32_t *result)
{
        u8_t buf[32];

        for (u8_t i = 0; i < nbits; i++) {
                buf[i] = (bits & (1UL << i)) ? SLOT_1 : SLOT_0;
        }

        int err = uart_xfer(hdl, buf, nbits);
        if (!err && result) {
                *result = 0;
                for (u8_t i = 0; i < nbits; i++) {
                        if (buf[i] == SLOT_1) {
                                *result |= 1UL << i;
                        }
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function transfers bytes (write or read slots).
 * @param  hdl          driver handle
 * @param  src          bytes to send (NULL: read slots)
 * @param  dst          received bytes (can be NULL)
 * @param  len          number of bytes
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int bus_bytes(ONEWIRE_t *hdl, const u8_t *src, u8_t *dst, size_t len)
{
        if (!hdl->uart) {
                return ENODEV;
        }

        int err = set_baud(hdl, BAUD_SLOT);

        while (!err && len) {
                size_t n = min(len, 4U); /* 32 slots */
                u32_t bits = 0xFFFFFFFF;

                if (src) {
                        bits = 0;
                        for (size_t i = 0; i < n; i++) {
                                bits |= cast(u32_t, src[i]) << (i * 8);
                        }
                        src += n;
                }

                u32_t res;
                err = slots(hdl, bits, n * 8, &res);
                if (!err && dst) {
                        for (size_t i = 0; i < n; i++) {
                                *dst++ = res >> (i * 8);
                        }
                }

                len -= n;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function writes bytes.
 */
//==============================================================================
static int bus_write(ONEWIRE_t *hdl, const u8_t *src, size_t len)
{
        return bus_bytes(hdl, src, NULL, len);
}

//==============================================================================
/**
 * @brief  Function reads bytes.
 */
//==============================================================================
static int bus_read(ONEWIRE_t *hdl, u8_t *dst, size_t len)
{
        return bus_bytes(hdl, NULL, dst, len);
}

//==============================================================================
/**
 * @brief  Function calculates CRC8 of 1-Wire (polynomial x^8 + x^5 + x^4 + 1).
 * @param  data         data
 * @param  len          data size
 * @return CRC.
 */
//==============================================================================
static u8_t crc8(const u8_t *data, size_t len)
{
        u8_t crc = 0;

        while (len--) {
                crc ^= *data++;
                for (int i = 0; i < 8; i++) {
                        crc = (crc & 1) ? ((crc >> 1) ^ 0x8C) : (crc >> 1);
                }
        }

        return crc;
}

//==============================================================================
/**
 * @brief  Function finds next device (Maxim AN187).
 * @param  hdl          driver handle
 * @param  s            search state
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int search(ONEWIRE_t *hdl, ONEWIRE_search_t *s)
{
        if ((s->cmd != ONEWIRE_SEARCH__ALL) && (s->cmd != ONEWIRE_SEARCH__ALARM)) {
                return EINVAL;
        }

        if (s->last_device) {
                return ENOENT;
        }

        bool presence;
        int err = bus_reset(hdl, &presence);
        if (err) {
                return err;
        }

        if (!presence) {
                s->last_discrepancy = 0;
                return ENOENT;
        }

        err = bus_write(hdl, &s->cmd, 1);

        u8_t last_zero = 0;

        for (u8_t bit = 1; !err && (bit <= 64); bit++) {
                u8_t *byte = &s->rom[(bit - 1) / 8];
                u8_t mask = 1 << ((bit - 1) % 8);

                u32_t r;
                err = slots(hdl, 0x3, 2, &r);
                if (err) {
                        break;
                }

                bool id = r & 1;
                bool cmp = r & 2;

                if (id && cmp) {
                        /* no devices */
                        s->last_discrepancy = 0;
                        return ENOENT;
                }

                bool dir;
                if (id != cmp) {
                        dir = id;
                } else if (bit < s->last_discrepancy) {
                        dir = (*byte & mask) != 0;
                } else {
                        dir = (bit == s->last_discrepancy);
                }

                if (!dir && (id == cmp)) {
                        last_zero = bit;
                }

                if (dir) {
                        *byte |= mask;
                } else {
                        *byte &= ~mask;
                }

                err = slots(hdl, dir, 1, NULL);
        }

        if (!err) {
                s->last_discrepancy = last_zero;
                s->last_device = (last_zero == 0);

                if ((crc8(s->rom, 7) != s->rom[7]) || (s->rom[0] == 0)) {
                        dev_dbg(hdl, "CRC error of ROM code");
                        err = EIO;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function executes transaction.
 * @param  hdl          driver handle
 * @param  t            transaction
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int transfer(ONEWIRE_t *hdl, ONEWIRE_transfer_t *t)
{
        if ((t->tx_len && !t->tx) || (t->rx_len && !t->rx)) {
                return EINVAL;
        }

        bool presence;
        int err = bus_reset(hdl, &presence);
        if (!err && !presence) {
                err = ENODEV;
        }

        if (!err) {
                if (t->rom) {
                        u8_t cmd = CMD_MATCH_ROM;
                        err = bus_write(hdl, &cmd, 1);
                        if (!err) {
                                err = bus_write(hdl, t->rom, 8);
                        }
                } else {
                        u8_t cmd = CMD_SKIP_ROM;
                        err = bus_write(hdl, &cmd, 1);
                }
        }

        if (!err && t->tx_len) {
                err = bus_write(hdl, t->tx, t->tx_len);
        }

        if (!err && t->spu_ms) {
                if (hdl->conf.spu_pin_used) {
                        _GPIO_DDI_set_pin(hdl->conf.spu_port_idx, hdl->conf.spu_pin_idx);
                        sys_sleep_ms(t->spu_ms);
                        _GPIO_DDI_clear_pin(hdl->conf.spu_port_idx, hdl->conf.spu_pin_idx);
                } else {
                        sys_sleep_ms(t->spu_ms);
                }
        }

        if (!err && t->rx_len) {
                err = bus_read(hdl, t->rx, t->rx_len);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function configures driver.
 * @param  hdl          driver handle
 * @param  cfg          configuration
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int configure(ONEWIRE_t *hdl, const ONEWIRE_config_t *cfg)
{
        if (!cfg->path) {
                return EINVAL;
        }

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (!err) {
                if (hdl->uart) {
                        sys_fclose(hdl->uart);
                        hdl->uart = NULL;
                }

                hdl->conf = *cfg;
                hdl->conf.path = NULL;
                hdl->baud = 0;

                err = sys_fopen(cfg->path, O_RDWR, 0, &hdl->uart);
                if (!err) {
                        err = sys_ioctl(hdl->uart, IOCTL_VFS__NON_BLOCKING_RD_MODE);
                        if (!err) {
                                err = set_baud(hdl, BAUD_SLOT);
                        }

                        if (err) {
                                sys_fclose(hdl->uart);
                                hdl->uart = NULL;
                        }
                }

                if (!err && cfg->spu_pin_used) {
                        _GPIO_DDI_clear_pin(cfg->spu_port_idx, cfg->spu_pin_idx);
                }

                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

/*==============================================================================
  End of file
==============================================================================*/
