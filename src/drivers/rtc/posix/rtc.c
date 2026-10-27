/*==============================================================================
File    rtc.c

Author  Daniel Zorychta

Brief   Real-Time Clock Module (POSIX simulator). Time is taken from the host
        clock, a written time is stored as offset to the host time.

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
#include "posix/host/host.h"
#include "../rtc_ioctl.h"

/*==============================================================================
  Local macros
==============================================================================*/

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        i64_t offset;
        u8_t major;
        u8_t minor;
} RTC_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static i64_t get_host_time(void);

/*==============================================================================
  Local objects
==============================================================================*/
MODULE_NAME(RTC);

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
API_MOD_INIT(RTC, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        UNUSED_ARG1(config);

        if ((major != 0) or (minor != 0)) {
                return ENODEV;
        }

        int err = sys_zalloc(sizeof(RTC_t), device_handle);
        if (!err) {
                RTC_t *hdl = *device_handle;
                hdl->major = major;
                hdl->minor = minor;
                hdl->offset = 0;
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
API_MOD_RELEASE(RTC, void *device_handle)
{
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
API_MOD_OPEN(RTC, void *device_handle, u32_t flags)
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
API_MOD_CLOSE(RTC, void *device_handle, bool force)
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
API_MOD_WRITE(RTC, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        RTC_t *hdl = device_handle;

        count = count > sizeof(time_t) ? sizeof(time_t) : count;

        time_t timer = 0;
        memcpy(&timer, src, count);

        hdl->offset = cast(i64_t, timer) - get_host_time();

        *wrcnt = count;
        *fpos = 0;

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
API_MOD_READ(RTC, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt, struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        RTC_t *hdl = device_handle;

        count = count > sizeof(time_t) ? sizeof(time_t) : count;

        time_t timer = cast(time_t, get_host_time() + hdl->offset);
        memcpy(dst, &timer, count);

        *rdcnt = count;
        *fpos = 0;

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
API_MOD_IOCTL(RTC, void *device_handle, int request, void *arg)
{
        UNUSED_ARG1(device_handle);

        int err = EBADRQC;

        switch (request) {
        case IOCTL_RTC__SET_ALARM:
                /* alarm (wake up) is not supported by simulator, request accepted */
                err = arg ? ESUCC : EINVAL;
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
API_MOD_FLUSH(RTC, void *device_handle)
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
API_MOD_STAT(RTC, void *device_handle, struct vfs_dev_stat *device_stat)
{
        UNUSED_ARG1(device_handle);

        device_stat->st_size = sizeof(time_t);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Function returns host real time in seconds.
 *
 * @return Host UNIX time in seconds.
 */
//==============================================================================
static i64_t get_host_time(void)
{
        return _host_get_realtime_us() / 1000000;
}

/*==============================================================================
  End of file
==============================================================================*/
