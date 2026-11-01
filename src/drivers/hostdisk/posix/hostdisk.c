/*==============================================================================
@file    hostdisk.c

@author  Daniel Zorychta

@brief   Host disk driver (POSIX simulator). Driver gives access to host file
         as block device.

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
#include "drivers/class/storage/ioctl.h"
#include "posix/host/host.h"
#include "../hostdisk_ioctl.h"

/*==============================================================================
  Local macros
==============================================================================*/
/* maximal size of single host operation (task switching latency) */
#define MAX_CHUNK_SIZE (64 * 1024)

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        u8_t major;
        u8_t minor;
        u64_t size;
        bool read_only;
        u32_t block_size;   /* emulated block size (0 or 1: none) */
        u32_t buffer_align; /* emulated data buffer alignment (0 or 1: none) */
} HOSTDISK_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int check_constraints(HOSTDISK_t *hdl, const void *buf, size_t count, fpos_t fpos);

/*==============================================================================
  Local object
==============================================================================*/
MODULE_NAME(HOSTDISK);

/*==============================================================================
  Exported object
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/
//==============================================================================
/**
 * @brief Function checks emulated constraints of hardware block device.
 *
 * @param hdl           driver instance
 * @param buf           data buffer
 * @param count         request size
 * @param fpos          request position
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int check_constraints(HOSTDISK_t *hdl, const void *buf, size_t count, fpos_t fpos)
{
        if ((hdl->block_size > 1) &&
                (((cast(u64_t, fpos) % hdl->block_size) != 0) || ((count % hdl->block_size) != 0))) {
                dev_dbg(hdl, "request not aligned to block size (pos %llu, size %u)", cast(u64_t, fpos),
                        cast(uint, count));
                return EINVAL;
        }

        if ((hdl->buffer_align > 1) && ((cast(uintptr_t, buf) % hdl->buffer_align) != 0)) {
                dev_dbg(hdl, "buffer %p not aligned to %u", buf, cast(uint, hdl->buffer_align));
                return EFAULT;
        }

        return ESUCC;
}

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
API_MOD_INIT(HOSTDISK, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        UNUSED_ARG1(config);

        if ((major >= _HOST_DISK_COUNT) || (minor != 0)) {
                return ENODEV;
        }

        u64_t size = 0;
        bool read_only = true;

        if (_host_disk_open(major, &size, &read_only) != 0) {
                dev_dbg2(major, minor, "disk not connected (use --disk %u=<file>)", major);
                return ENODEV;
        }

        int err = sys_zalloc(sizeof(HOSTDISK_t), device_handle);
        if (!err) {
                HOSTDISK_t *hdl = *device_handle;

                hdl->major = major;
                hdl->minor = minor;
                hdl->size = size;
                hdl->read_only = read_only;
        } else {
                _host_disk_close(major);
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
API_MOD_RELEASE(HOSTDISK, void *device_handle)
{
        HOSTDISK_t *hdl = device_handle;

        _host_disk_close(hdl->major);

        return sys_free(&device_handle);
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
API_MOD_OPEN(HOSTDISK, void *device_handle, u32_t flags)
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
API_MOD_CLOSE(HOSTDISK, void *device_handle, bool force)
{
        UNUSED_ARG2(device_handle, force);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Write data to device
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]          *src                    data source
 * @param[in ]           count                  number of bytes to write
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *wrcnt                  number of written bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_WRITE(HOSTDISK, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        HOSTDISK_t *hdl = device_handle;

        if (hdl->read_only) {
                return EROFS;
        }

        int err = check_constraints(hdl, src, count, *fpos);
        if (err) {
                return err;
        }

        *wrcnt = 0;

        if (*fpos >= hdl->size) {
                return ESUCC;
        }

        count = min(count, cast(size_t, hdl->size - *fpos));

        while (*wrcnt < count) {
                size_t len = min(count - *wrcnt, cast(size_t, MAX_CHUNK_SIZE));

                int n = _host_disk_write(hdl->major, *fpos + *wrcnt, &src[*wrcnt], len);
                if (n <= 0) {
                        return EIO;
                }

                *wrcnt += cast(size_t, n);
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief Read data from device
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *rdcnt                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_READ(HOSTDISK, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        HOSTDISK_t *hdl = device_handle;

        int err = check_constraints(hdl, dst, count, *fpos);
        if (err) {
                return err;
        }

        *rdcnt = 0;

        if (*fpos >= hdl->size) {
                return ESUCC;
        }

        count = min(count, cast(size_t, hdl->size - *fpos));

        while (*rdcnt < count) {
                size_t len = min(count - *rdcnt, cast(size_t, MAX_CHUNK_SIZE));

                int n = _host_disk_read(hdl->major, *fpos + *rdcnt, &dst[*rdcnt], len);
                if (n < 0) {
                        return EIO;
                } else if (n == 0) {
                        break;
                }

                *rdcnt += cast(size_t, n);
        }

        return ESUCC;
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
API_MOD_IOCTL(HOSTDISK, void *device_handle, int request, void *arg)
{
        HOSTDISK_t *hdl = device_handle;

        int err = EBADRQC;

        switch (request) {
        case IOCTL_STORAGE__INITIALIZE:
                err = ESUCC;
                break;

        case IOCTL_HOSTDISK__CONFIGURE_STR:
                if (arg) {
                        int bs = sys_stropt_get_int(arg, "block_size", hdl->block_size);
                        int align = sys_stropt_get_int(arg, "buffer_align", hdl->buffer_align);

                        if ((bs >= 0) && (align >= 0) && ((align & (align - 1)) == 0)) {
                                hdl->block_size = bs;
                                hdl->buffer_align = align;
                                err = ESUCC;
                        } else {
                                err = EINVAL;
                        }
                } else {
                        err = EINVAL;
                }
                break;

        default:
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
API_MOD_FLUSH(HOSTDISK, void *device_handle)
{
        HOSTDISK_t *hdl = device_handle;

        return (_host_disk_sync(hdl->major) == 0) ? ESUCC : EIO;
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
API_MOD_STAT(HOSTDISK, void *device_handle, struct vfs_dev_stat *device_stat)
{
        HOSTDISK_t *hdl = device_handle;

        device_stat->st_size = hdl->size;
        device_stat->st_blksize = max(hdl->block_size, 1);
        device_stat->st_rdbufalign = hdl->buffer_align;
        device_stat->st_wrbufalign = hdl->buffer_align;

        return ESUCC;
}

/*==============================================================================
  End of file
==============================================================================*/
