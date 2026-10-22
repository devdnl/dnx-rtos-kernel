/*==============================================================================
File    memblock.c

Author  Daniel Zorychta

Brief   Memory Block as file representation

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
#include "../memblock_ioctl.h"

/*==============================================================================
  Local macros
==============================================================================*/

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        u8_t major;
        u8_t minor;
        MEMBLOCK_config_t config;
} MEMBLOCK_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  Local object
==============================================================================*/
MODULE_NAME(MEMBLOCK);

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
API_MOD_INIT(MEMBLOCK, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        int err = sys_zalloc(sizeof(MEMBLOCK_t), device_handle);
        if (!err) {
                MEMBLOCK_t *hdl = *device_handle;

                hdl->major = major;
                hdl->minor = minor;

                if (config) {
                        hdl->config = *cast(const MEMBLOCK_config_t *, config);
                }
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
API_MOD_RELEASE(MEMBLOCK, void *device_handle)
{
        int err = sys_free(&device_handle);
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
API_MOD_OPEN(MEMBLOCK, void *device_handle, u32_t flags)
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
API_MOD_CLOSE(MEMBLOCK, void *device_handle, bool force)
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
API_MOD_WRITE(MEMBLOCK, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrctr,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        MEMBLOCK_t *hdl = device_handle;

        if (not(hdl->config.access & (O_RDWR | O_WRONLY))) {
                return EROFS;
        }

        if (*fpos >= hdl->config.block_size) {
                *wrctr = 0;
                return ESUCC;
        }

        size_t left = (hdl->config.block_size - *fpos);
        count = min(count, left);
        *wrctr = count;

        void *addr = cast(void *, (hdl->config.memory_address + cast(u32_t, *fpos)));

        memcpy(addr, src, count);

        return ESUCC;
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
API_MOD_READ(MEMBLOCK, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdctr,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        MEMBLOCK_t *hdl = device_handle;

        if ((hdl->config.access & O_WRONLY) == O_WRONLY) {
                return EPERM;
        }

        if (*fpos >= hdl->config.block_size) {
                *rdctr = 0;
                return ESUCC;
        }

        size_t left = (hdl->config.block_size - *fpos);
        count = min(count, left);
        *rdctr = count;

        void *addr = cast(void *, (hdl->config.memory_address + cast(u32_t, *fpos)));

        memcpy(dst, addr, count);

        return ESUCC;
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
API_MOD_IOCTL(MEMBLOCK, void *device_handle, int request, void *arg)
{
        MEMBLOCK_t *hdl = device_handle;

        int err = EIO;

        switch (request) {
        case IOCTL_MEMBLOCK__SET_CONFIGURATION:
                if (arg) {
                        hdl->config = *cast(MEMBLOCK_config_t *, arg);
                        err = ESUCC;
                } else {
                        err = EINVAL;
                }
                break;

        case IOCTL_MEMBLOCK__GET_CONFIGURATION:
                if (arg) {
                        *cast(MEMBLOCK_config_t *, arg) = hdl->config;
                        err = ESUCC;
                } else {
                        err = EINVAL;
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
 * @brief Flush device.
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_FLUSH(MEMBLOCK, void *device_handle)
{
        UNUSED_ARG1(device_handle);
        return ESUCC;
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
API_MOD_STAT(MEMBLOCK, void *device_handle, struct vfs_dev_stat *device_stat)
{
        MEMBLOCK_t *hdl = device_handle;

        device_stat->st_size = hdl->config.block_size;

        return ESUCC;
}

/*==============================================================================
  End of file
==============================================================================*/
