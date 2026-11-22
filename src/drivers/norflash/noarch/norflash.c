/*==============================================================================
File    norflash.c

Author  Daniel Zorychta

Brief   Serial NOR flash memory (SPI, QSPI)

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
#include "../norflash_ioctl.h"
#include "spi/spi_ioctl.h"
#if __ENABLE_QSPI__ > 0
#include "qspi/qspi_ioctl.h"
#endif

/*==============================================================================
  Local macros
==============================================================================*/
#define SECTOR_SIZE     4096U
#define PAGE_SIZE       256U
#define CMP_CHUNK       64U
#define SIZE_3BYTE_ADDR (16UL * 1024 * 1024)

#define CMD_WRSR       0x01
#define CMD_PP         0x02
#define CMD_READ       0x03
#define CMD_RDSR       0x05
#define CMD_WREN       0x06
#define CMD_FAST_READ  0x0B
#define CMD_FAST_READ4 0x0C
#define CMD_PP4        0x12
#define CMD_READ4      0x13
#define CMD_SE         0x20
#define CMD_SE4        0x21
#define CMD_SFDP       0x5A
#define CMD_GBULK      0x98
#define CMD_RDID       0x9F
#define CMD_RDP        0xAB

#define SR_WIP       (1 << 0)
#define SR_BP_MASK   0x3C
#define SR_KEEP_MASK 0x40 /* QE (Macronix, ISSI) */

#define TIMEOUT_PP_MS   20
#define TIMEOUT_SE_MS   2000
#define TIMEOUT_WRSR_MS 1000

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        kfile_t *dev;
        kmtx_t *mtx;
        u32_t size;
        u32_t erase_count;
        u8_t id[3];
        bool sfdp;
        u8_t addr_bytes;
        u8_t op_read;
        u8_t op_pp;
        u8_t op_se;
        u8_t dummy;
        NORFLASH_interface_t interface;
        u8_t major;
        u8_t minor;
} NORFLASH_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int configure(NORFLASH_t *hdl, const NORFLASH_config_t *cfg);
static int command(NORFLASH_t *hdl, u8_t op, u32_t addr, u8_t addr_bytes, u8_t dummy, const u8_t *tx, u8_t *rx,
        size_t len);
static int write_enable(NORFLASH_t *hdl);
static int wait_ready(NORFLASH_t *hdl, u32_t timeout_ms);
static int write_sector(NORFLASH_t *hdl, u32_t base, u32_t off, const u8_t *src, u32_t n);

/*==============================================================================
  Local object
==============================================================================*/
MODULE_NAME(NORFLASH);

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
API_MOD_INIT(NORFLASH, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        if (minor != 0) {
                return ENODEV;
        }

        int err = sys_zalloc(sizeof(NORFLASH_t), device_handle);
        if (!err) {
                NORFLASH_t *hdl = *device_handle;
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
API_MOD_RELEASE(NORFLASH, void *device_handle)
{
        NORFLASH_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mtx, 0);
        if (!err) {
                kmtx_t *mtx = hdl->mtx;
                sys_mutex_unlock(mtx);
                sys_mutex_destroy(mtx);

                if (hdl->dev) {
                        sys_fclose(hdl->dev);
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
API_MOD_OPEN(NORFLASH, void *device_handle, u32_t flags)
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
API_MOD_CLOSE(NORFLASH, void *device_handle, bool force)
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
API_MOD_WRITE(NORFLASH, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        NORFLASH_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (!err) {
                if (!hdl->dev) {
                        err = ENODEV;

                } else if (*fpos >= hdl->size) {
                        err = ESPIPE;

                } else {
                        u32_t pos = *fpos;
                        count = min(count, hdl->size - pos);

                        while (!err && count) {
                                u32_t off = pos % SECTOR_SIZE;
                                u32_t n = min(count, SECTOR_SIZE - off);

                                err = write_sector(hdl, pos - off, off, src, n);
                                if (!err) {
                                        pos += n;
                                        src += n;
                                        count -= n;
                                        *wrcnt += n;
                                }
                        }
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
API_MOD_READ(NORFLASH, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        NORFLASH_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (!err) {
                if (!hdl->dev) {
                        err = ENODEV;

                } else if (*fpos >= hdl->size) {
                        err = ESPIPE;

                } else {
                        count = min(count, hdl->size - cast(u32_t, *fpos));
                        err = command(hdl, hdl->op_read, *fpos, hdl->addr_bytes, hdl->dummy, NULL, dst, count);
                        if (!err) {
                                *rdcnt = count;
                        }
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
API_MOD_IOCTL(NORFLASH, void *device_handle, int request, void *arg)
{
        NORFLASH_t *hdl = device_handle;

        if (!arg) {
                return EINVAL;
        }

        int err = EBADRQC;

        switch (request) {
        case IOCTL_NORFLASH__CONFIGURE:
                err = configure(hdl, arg);
                break;

        case IOCTL_NORFLASH__CONFIGURE_STR: {
                static const char *const IFACE[] = {"spi", "qspi"};
                char path[64];

                NORFLASH_config_t cfg;
                cfg.path = sys_opt_get_str_copy(arg, "path", path, sizeof(path)) ? path : NULL;
                cfg.interface = sys_opt_get_enum(arg, "interface", IFACE, ARRAY_SIZE(IFACE), 0);
                cfg.size = sys_opt_get_int(arg, "size", 0);
                cfg.unprotect = sys_opt_get_bool(arg, "unprotect", false);

                err = (cast(int, cfg.interface) < 0) ? EINVAL : configure(hdl, &cfg);
                break;
        }

        case IOCTL_NORFLASH__GET_INFO:
                err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
                if (!err) {
                        if (hdl->dev) {
                                NORFLASH_info_t *info = arg;
                                memcpy(info->jedec_id, hdl->id, sizeof(info->jedec_id));
                                info->sfdp = hdl->sfdp;
                                info->addr_bytes = hdl->addr_bytes;
                                info->size = hdl->size;
                                info->sector_size = SECTOR_SIZE;
                                info->erase_count = hdl->erase_count;
                        } else {
                                err = ENODEV;
                        }
                        sys_mutex_unlock(hdl->mtx);
                }
                break;

        default:
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
API_MOD_FLUSH(NORFLASH, void *device_handle)
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
API_MOD_STAT(NORFLASH, void *device_handle, struct vfs_dev_stat *device_stat)
{
        NORFLASH_t *hdl = device_handle;

        device_stat->st_size = hdl->size;
        device_stat->st_blksize = SECTOR_SIZE;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function executes memory command.
 * @param  hdl          driver handle
 * @param  op           instruction
 * @param  addr         address
 * @param  addr_bytes   number of address bytes (0: no address)
 * @param  dummy        number of dummy bytes (8 clock cycles each)
 * @param  tx           data to send (NULL: no data or read)
 * @param  rx           received data (NULL: no data or write)
 * @param  len          data size
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int command(NORFLASH_t *hdl, u8_t op, u32_t addr, u8_t addr_bytes, u8_t dummy, const u8_t *tx, u8_t *rx,
        size_t len)
{
#if __ENABLE_QSPI__ > 0
        if (hdl->interface == NORFLASH_INTERFACE__QSPI) {
                QSPI_command_t qcmd;
                memset(&qcmd, 0, sizeof(qcmd));
                qcmd.instruction_mode = QSPI_MODE__SINGLE;
                qcmd.instruction = op;
                qcmd.address_mode = addr_bytes ? QSPI_MODE__SINGLE : QSPI_MODE__NONE;
                qcmd.address_size = (addr_bytes == 4) ? QSPI_SIZE__32_BITS : QSPI_SIZE__24_BITS;
                qcmd.address = addr;
                qcmd.alternate_byte_mode = QSPI_MODE__NONE;
                qcmd.data_mode = len ? QSPI_MODE__SINGLE : QSPI_MODE__NONE;
                qcmd.dummy_cycles = dummy * 8;
                qcmd.data_size = len;
                qcmd.direction = rx ? QSPI_DIRECTION__READ : QSPI_DIRECTION__WRITE;
                qcmd.data = rx ? cast(void *, rx) : cast(void *, tx);

                return sys_ioctl(hdl->dev, IOCTL_QSPI__COMMAND, &qcmd);
        }
#endif
        u8_t hdr[6];
        u8_t n = 0;

        hdr[n++] = op;
        while (addr_bytes) {
                hdr[n++] = addr >> (--addr_bytes * 8);
        }
        while (dummy--) {
                hdr[n++] = 0;
        }

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
                .count = n,
                .separated = false,
                .next = len ? &tdata : NULL,
        };

        return sys_ioctl(hdl->dev, IOCTL_SPI__TRANSCEIVE, &thdr);
}

//==============================================================================
/**
 * @brief  Function reads status register.
 * @param  hdl          driver handle
 * @param  sr           status register
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int read_status(NORFLASH_t *hdl, u8_t *sr)
{
        return command(hdl, CMD_RDSR, 0, 0, 0, NULL, sr, 1);
}

//==============================================================================
/**
 * @brief  Function sets write enable latch.
 * @param  hdl          driver handle
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int write_enable(NORFLASH_t *hdl)
{
        return command(hdl, CMD_WREN, 0, 0, 0, NULL, NULL, 0);
}

//==============================================================================
/**
 * @brief  Function waits for end of program, erase or status write.
 * @param  hdl          driver handle
 * @param  timeout_ms   timeout
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int wait_ready(NORFLASH_t *hdl, u32_t timeout_ms)
{
        clock_t tref = sys_get_uptime_ms();

        for (;;) {
                u8_t sr;
                int err = read_status(hdl, &sr);
                if (err or not(sr & SR_WIP)) {
                        return err;
                }

                if (sys_is_time_expired(tref, timeout_ms)) {
                        dev_dbg(hdl, "operation timeout");
                        return ETIME;
                }

                sys_sleep_ms(1);
        }
}

//==============================================================================
/**
 * @brief  Function reads parameters of memory from SFDP table.
 * @param  hdl          driver handle
 * @param  size         memory size
 * @param  op_se        4 KiB erase instruction (0: not supported)
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int read_sfdp(NORFLASH_t *hdl, u32_t *size, u8_t *op_se)
{
        u8_t buf[16];

        int err = command(hdl, CMD_SFDP, 0, 3, 1, NULL, buf, sizeof(buf));
        if (err) {
                return err;
        }

        /* signature "SFDP", first parameter header: JEDEC basic flash table */
        if ((memcmp(buf, "SFDP", 4) != 0) || (buf[8] != 0x00) || (buf[11] < 2)) {
                return ENOTSUP;
        }

        u32_t ptr = buf[12] | (buf[13] << 8) | (buf[14] << 16);
        err = command(hdl, CMD_SFDP, ptr, 3, 1, NULL, buf, 8);
        if (err) {
                return err;
        }

        u32_t dw1 = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (cast(u32_t, buf[3]) << 24);
        u32_t dw2 = buf[4] | (buf[5] << 8) | (buf[6] << 16) | (cast(u32_t, buf[7]) << 24);

        *op_se = ((dw1 & 3) == 1) ? (dw1 >> 8) : 0;

        if (dw2 & 0x80000000) {
                u32_t n = dw2 & 0x7FFFFFFF;
                *size = ((n >= 3) && (n < 35)) ? (1UL << (n - 3)) : 0;
        } else {
                *size = (dw2 >> 3) + 1;
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function identifies memory.
 * @param  hdl          driver handle
 * @param  size         memory size from configuration (0: detected)
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int identify(NORFLASH_t *hdl, u32_t size)
{
        /* release from deep power-down (tRES1: 3 us) */
        int err = command(hdl, CMD_RDP, 0, 0, 0, NULL, NULL, 0);
        if (err) {
                return err;
        }
        sys_sleep_ms(1);

        err = command(hdl, CMD_RDID, 0, 0, 0, NULL, hdl->id, sizeof(hdl->id));
        if (err) {
                return err;
        }

        u8_t all = hdl->id[0] & hdl->id[1] & hdl->id[2];
        u8_t any = hdl->id[0] | hdl->id[1] | hdl->id[2];
        if ((all == 0xFF) || (any == 0x00)) {
                dev_dbg(hdl, "memory not responding");
                return ENODEV;
        }

        u32_t detected = 0;
        u8_t op_se = CMD_SE;
        hdl->sfdp = read_sfdp(hdl, &detected, &op_se) == ESUCC;

        if (!hdl->sfdp) {
                u8_t cap = hdl->id[2];
                detected = ((cap >= 0x10) && (cap <= 0x1F)) ? (1UL << cap) : 0;
                op_se = CMD_SE;
        }

        if (op_se == 0) {
                dev_dbg(hdl, "4 KiB erase not supported");
                return ENOTSUP;
        }

        if (size == 0) {
                size = detected;
        }

        if ((size < SECTOR_SIZE) || (size % SECTOR_SIZE)) {
                dev_dbg(hdl, "unknown memory size (JEDEC ID %02X%02X%02X)", hdl->id[0], hdl->id[1], hdl->id[2]);
                return ENODEV;
        }

        bool qspi = hdl->interface == NORFLASH_INTERFACE__QSPI;
        bool addr4 = size > SIZE_3BYTE_ADDR;

        hdl->size = size;
        hdl->addr_bytes = addr4 ? 4 : 3;
        hdl->op_pp = addr4 ? CMD_PP4 : CMD_PP;
        hdl->op_se = addr4 ? CMD_SE4 : op_se;
        hdl->dummy = qspi ? 1 : 0;
        hdl->op_read = qspi ? (addr4 ? CMD_FAST_READ4 : CMD_FAST_READ) : (addr4 ? CMD_READ4 : CMD_READ);

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function clears block protection.
 * @param  hdl          driver handle
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int unprotect(NORFLASH_t *hdl)
{
        u8_t sr = 0;

        int err = write_enable(hdl);
        if (!err) {
                err = command(hdl, CMD_GBULK, 0, 0, 0, NULL, NULL, 0);
        }
        if (!err) {
                err = wait_ready(hdl, TIMEOUT_WRSR_MS);
        }
        if (!err) {
                err = read_status(hdl, &sr);
        }
        if (!err && (sr & (SR_BP_MASK | 0x80))) {
                u8_t nsr = sr & SR_KEEP_MASK;
                err = write_enable(hdl);
                if (!err) {
                        err = command(hdl, CMD_WRSR, 0, 0, 0, &nsr, NULL, 1);
                }
                if (!err) {
                        err = wait_ready(hdl, TIMEOUT_WRSR_MS);
                }
                if (!err) {
                        err = read_status(hdl, &sr);
                }
                if (!err && (sr & SR_BP_MASK)) {
                        dev_dbg(hdl, "protection cannot be cleared (SR %02Xh)", sr);
                        err = EACCES;
                }
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
static int configure(NORFLASH_t *hdl, const NORFLASH_config_t *cfg)
{
        if (!cfg->path || (cfg->interface > NORFLASH_INTERFACE__QSPI)) {
                return EINVAL;
        }

#if __ENABLE_QSPI__ == 0
        if (cfg->interface == NORFLASH_INTERFACE__QSPI) {
                return ENOTSUP;
        }
#endif

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (!err) {
                if (hdl->dev) {
                        sys_fclose(hdl->dev);
                        hdl->dev = NULL;
                }

                hdl->size = 0;
                hdl->erase_count = 0;
                hdl->interface = cfg->interface;

                err = sys_fopen(cfg->path, O_RDWR, 0, &hdl->dev);
                if (!err) {
                        err = identify(hdl, cfg->size);

                        if (!err && cfg->unprotect) {
                                err = unprotect(hdl);
                        }

                        if (err) {
                                sys_fclose(hdl->dev);
                                hdl->dev = NULL;
                                hdl->size = 0;
                        }
                }

                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function compares memory with data.
 * @param  hdl          driver handle
 * @param  addr         memory address
 * @param  src          data
 * @param  n            data size (in one sector)
 * @param  pages        bit mask of pages (of sector) that differ
 * @param  erase        data cannot be programmed without erase
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int compare(NORFLASH_t *hdl, u32_t addr, const u8_t *src, u32_t n, u16_t *pages, bool *erase)
{
        u8_t buf[CMP_CHUNK];

        *pages = 0;
        *erase = false;

        for (u32_t i = 0; i < n; i += CMP_CHUNK) {
                u32_t c = min(CMP_CHUNK, n - i);

                int err = command(hdl, hdl->op_read, addr + i, hdl->addr_bytes, hdl->dummy, NULL, buf, c);
                if (err) {
                        return err;
                }

                for (u32_t j = 0; j < c; j++) {
                        u8_t o = buf[j];
                        u8_t d = src[i + j];
                        if (o != d) {
                                *pages |= 1 << (((addr + i + j) % SECTOR_SIZE) / PAGE_SIZE);
                                *erase |= (o & d) != d;
                        }
                }
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function writes data in sector.
 * @param  hdl          driver handle
 * @param  base         sector address
 * @param  off          offset of data in sector
 * @param  src          data
 * @param  n            data size
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int write_sector(NORFLASH_t *hdl, u32_t base, u32_t off, const u8_t *src, u32_t n)
{
        u16_t pages;
        bool erase;

        int err = compare(hdl, base + off, src, n, &pages, &erase);
        if (err || (pages == 0)) {
                return err;
        }

        if (erase) {
                if (n != SECTOR_SIZE) {
                        dev_dbg(hdl, "partial sector write needs erase");
                        return EINVAL;
                }

                err = write_enable(hdl);
                if (!err) {
                        err = command(hdl, hdl->op_se, base, hdl->addr_bytes, 0, NULL, NULL, 0);
                }
                if (!err) {
                        err = wait_ready(hdl, TIMEOUT_SE_MS);
                }
                if (err) {
                        return err;
                }

                hdl->erase_count++;

                /* program pages with data other than 0xFF */
                pages = 0;
                for (u32_t i = 0; i < SECTOR_SIZE; i++) {
                        if (src[i] != 0xFF) {
                                pages |= 1 << (i / PAGE_SIZE);
                                i |= PAGE_SIZE - 1;
                        }
                }
        }

        for (u32_t p = 0; !err && (p < SECTOR_SIZE / PAGE_SIZE); p++) {
                if (pages & (1 << p)) {
                        u32_t ps = max(p * PAGE_SIZE, off);
                        u32_t pe = min((p + 1) * PAGE_SIZE, off + n);

                        err = write_enable(hdl);
                        if (!err) {
                                err = command(hdl, hdl->op_pp, base + ps, hdl->addr_bytes, 0, src + (ps - off), NULL,
                                        pe - ps);
                        }
                        if (!err) {
                                err = wait_ready(hdl, TIMEOUT_PP_MS);
                        }
                }
        }

        /* verification: protected or worn out memory */
        if (!err) {
                err = compare(hdl, base + off, src, n, &pages, &erase);
                if (!err && pages) {
                        dev_dbg(hdl, "verification error at %Xh", base);
                        err = EIO;
                }
        }

        return err;
}

/*==============================================================================
  End of file
==============================================================================*/
